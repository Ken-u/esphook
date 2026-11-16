#!/usr/bin/env python3
"""Forward lifecycle events from coding agents to the local esphook CLI.

Each supported agent sends a JSON event on stdin.  This adapter deliberately
does not print anything and always exits successfully: a reminder device being
offline must never block the agent's work.
"""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import time
from typing import Any, Callable

try:
    import fcntl
except ImportError:  # pragma: no cover - Windows does not have fcntl.
    fcntl = None


SOURCE_LABELS = {
    "claude": "Claude",
    "claudecode": "Claude",
    "codex": "Codex",
    "kimi": "Kimi",
    "kimicode": "Kimi",
    "cursor": "Cursor",
    "cursor-agent": "Cursor",
}


def _string(value: Any) -> str:
    if value is None:
        return ""
    if isinstance(value, str):
        return value.strip()
    return str(value).strip()


def _compact(value: Any, limit: int = 220) -> str:
    text = _string(value).replace("\x00", " ")
    text = " ".join(text.split())
    if len(text) > limit:
        return text[: limit - 1].rstrip() + "…"
    return text


def _event(payload: dict[str, Any]) -> str:
    return _string(payload.get("hook_event_name") or payload.get("event")).lower().replace("-", "_")


PERMISSION_REQUEST_EVENTS = {
    "permissionrequest",
    "permission_request",
}
PERMISSION_RESULT_EVENTS = {
    "permissionresult",
    "permission_result",
    "permissionresponse",
    "permission_response",
    "approvalresult",
    "approval_result",
    "approvalresponse",
    "approval_response",
}
APPROVED_DECISIONS = {
    "accept",
    "accepted",
    "allow",
    "allowed",
    "approve",
    "approved",
    "grant",
    "granted",
    "success",
    "succeeded",
    "true",
    "yes",
}
DENIED_DECISIONS = {
    "cancel",
    "cancelled",
    "canceled",
    "decline",
    "declined",
    "deny",
    "denied",
    "false",
    "reject",
    "rejected",
    "no",
}
PENDING_TTL_SECONDS = 24 * 60 * 60


def _decision_name(value: Any) -> str:
    if isinstance(value, bool):
        return "true" if value else "false"
    text = _string(value).lower().replace("-", "_").replace(" ", "_")
    if "." in text:
        text = text.rsplit(".", 1)[-1]
    if ":" in text:
        text = text.rsplit(":", 1)[-1]
    return text


def _permission_decision(payload: dict[str, Any], include_status: bool = False) -> str:
    """Return approved/denied when the payload carries an explicit decision."""
    fields = (
        "permission_result",
        "permissionResult",
        "permission_response",
        "permissionResponse",
        "approval",
        "approval_result",
        "approvalResult",
        "approval_response",
        "approvalResponse",
        "decision",
        "behavior",
        "action",
    )
    if include_status:
        fields += ("status", "subtype", "result", "response")
    containers = ("permission", "approval", "result", "response", "decision", "hookSpecificOutput")
    stack: list[dict[str, Any]] = [payload]
    seen: set[int] = set()
    while stack:
        current = stack.pop()
        marker = id(current)
        if marker in seen:
            continue
        seen.add(marker)
        for field in fields:
            value = current.get(field)
            if isinstance(value, dict):
                stack.append(value)
                continue
            decision = _decision_name(value)
            if decision in APPROVED_DECISIONS or decision.startswith(("allow_", "approved_")):
                return "approved"
            if decision in DENIED_DECISIONS or decision.startswith(("deny_", "denied_", "reject_", "rejected_")):
                return "denied"
        for container in containers:
            value = current.get(container)
            if isinstance(value, dict):
                stack.append(value)
    return ""


def _pending_state_path() -> Path:
    configured = os.environ.get("ESPHOOK_PENDING_FILE", "").strip()
    if configured:
        return Path(configured).expanduser()
    state_home = os.environ.get("XDG_STATE_HOME", "").strip()
    if state_home:
        return Path(state_home) / "esphook" / "pending-confirms.json"
    return Path.home() / ".local" / "state" / "esphook" / "pending-confirms.json"


def _pending_key(source: str, client_id: str, session_id: str) -> str:
    return json.dumps([source.lower(), client_id, session_id], ensure_ascii=False, separators=(",", ":"))


def _mutate_pending_state(mutator: Callable[[dict[str, Any]], Any]) -> Any:
    """Mutate the small host-local pending confirmation registry safely."""
    path = _pending_state_path()
    lock_path = path.with_name(path.name + ".lock")
    try:
        path.parent.mkdir(parents=True, exist_ok=True)
        with lock_path.open("a+", encoding="utf-8") as lock:
            if fcntl is not None:
                fcntl.flock(lock.fileno(), fcntl.LOCK_EX)
            try:
                try:
                    value = json.loads(path.read_text(encoding="utf-8")) if path.exists() else {}
                except (OSError, ValueError, TypeError):
                    value = {}
                state = value if isinstance(value, dict) else {}
                now = time.time()
                state = {
                    key: record
                    for key, record in state.items()
                    if isinstance(record, dict)
                    and now - float(record.get("created_at", 0)) <= PENDING_TTL_SECONDS
                }
                result = mutator(state)
                if state:
                    temporary = path.with_name(f".{path.name}.{os.getpid()}.tmp")
                    temporary.write_text(json.dumps(state, ensure_ascii=False), encoding="utf-8")
                    temporary.replace(path)
                elif path.exists():
                    path.unlink()
                return result
            finally:
                if fcntl is not None:
                    fcntl.flock(lock.fileno(), fcntl.LOCK_UN)
    except (OSError, ValueError, TypeError):
        return None


def _remember_pending_confirmation(source: str, client_id: str, session_id: str, payload: dict[str, Any]) -> None:
    key = _pending_key(source, client_id, session_id)
    record = {
        "created_at": time.time(),
        "turn_id": _string(payload.get("turn_id")),
        "tool_name": _string(payload.get("tool_name")),
    }
    _mutate_pending_state(lambda state: state.__setitem__(key, record))


def _clear_pending_confirmation(source: str, client_id: str, session_id: str) -> None:
    key = _pending_key(source, client_id, session_id)
    _mutate_pending_state(lambda state: state.pop(key, None))


def _take_pending_for_post_tool(
    source: str,
    client_id: str,
    session_id: str,
    payload: dict[str, Any],
) -> bool:
    key = _pending_key(source, client_id, session_id)
    turn_id = _string(payload.get("turn_id"))

    def take(state: dict[str, Any]) -> bool:
        record = state.get(key)
        if not isinstance(record, dict):
            return False
        pending_turn_id = _string(record.get("turn_id"))
        if pending_turn_id and turn_id and pending_turn_id != turn_id:
            return False
        state.pop(key, None)
        return True

    return bool(_mutate_pending_state(take))


def classify(payload: dict[str, Any], source: str) -> tuple[str, str, str] | None:
    """Return (status, title, body), or None for an uninteresting event."""
    event = _event(payload)
    notification_type = _string(
        payload.get("notification_type")
        or payload.get("notificationType")
        or payload.get("type")
    ).lower()
    agent_status = _string(payload.get("status") or payload.get("subtype")).lower()
    reason = _string(
        payload.get("reason")
        or payload.get("stop_reason")
        or payload.get("stopReason")
    )
    event_text = " ".join((event, notification_type, agent_status, reason)).lower()

    error_events = {
        "stopfailure",
        "posttoolusefailure",
        "post_tool_use_failure",
        "permissiondenied",
        "sessionendfailure",
    }
    confirm_events = {
        *PERMISSION_REQUEST_EVENTS,
        "interrupt",
        "elicitation",
    }
    done_events = {"stop", "sessionend", "session_end"}
    permission_decision = _permission_decision(
        payload,
        include_status=event in PERMISSION_RESULT_EVENTS or event in PERMISSION_REQUEST_EVENTS,
    )
    permission_denied = False

    if event in error_events or agent_status in {"error", "failed", "failure"}:
        status = "error"
    elif event in PERMISSION_RESULT_EVENTS or (event in PERMISSION_REQUEST_EVENTS and permission_decision):
        if permission_decision == "approved":
            return "dismiss", "", ""
        if permission_decision == "denied":
            status = "error"
            permission_denied = True
        else:
            status = "confirm"
    elif event in confirm_events:
        status = "confirm"
    elif event == "notification":
        # Notification hooks are intentionally filtered here rather than in
        # every vendor config, because notification type names vary by tool
        # version.  Unknown notifications are ignored.
        if any(word in event_text for word in ("permission", "approval", "elicitation", "idle_prompt")):
            status = "confirm"
        elif any(word in event_text for word in ("failed", "failure", "error")):
            status = "error"
        elif any(word in event_text for word in ("completed", "complete", "success", "finished")):
            status = "done"
        else:
            return None
    elif event in done_events or event == "afteragentresponse":
        if agent_status in {"error", "failed", "failure", "aborted"}:
            status = "error" if agent_status != "aborted" else "confirm"
        else:
            status = "done"
    elif event == "stop" and agent_status == "aborted":
        status = "confirm"
    else:
        return None

    label = SOURCE_LABELS.get(source.lower(), source or "Agent")
    if status == "done":
        title = f"{label} 完成"
        body = _compact(
            payload.get("last_assistant_message")
            or payload.get("message")
            or payload.get("response")
            or "本轮任务已完成"
        )
    elif status == "error":
        title = f"{label} 权限被拒绝" if permission_denied else f"{label} 出错"
        body = _compact(
            payload.get("error")
            or payload.get("message")
            or payload.get("last_assistant_message")
            or reason
            or ("权限请求已被拒绝" if permission_denied else "本轮任务执行失败")
        )
    else:
        title = f"{label} 需要确认"
        body = _compact(
            payload.get("message")
            or payload.get("reason")
            or payload.get("agent_message")
            or payload.get("tool_name")
            or "等待你的确认"
        )
    return status, title, body


def _session_id(payload: dict[str, Any]) -> str:
    return _string(
        payload.get("session_id")
        or payload.get("conversation_id")
        or payload.get("thread_id")
        or os.environ.get("ESPHOOK_SESSION_ID")
        or os.environ.get("CODEX_THREAD_ID")
    )


def _esphook_command() -> list[str]:
    configured = os.environ.get("ESPHOOK_BIN", "").strip()
    if configured:
        return [configured]
    found = shutil.which("esphook")
    if found:
        return [found]
    host_cli = Path(__file__).resolve().parents[1] / "esphookd.py"
    return [sys.executable, str(host_cli)]


def forward(payload: dict[str, Any], source: str) -> None:
    classified = classify(payload, source)
    session_id = _session_id(payload)
    if not session_id:
        # Never invent a new id per event: that would make dismiss/routing
        # unreliable.  The vendor hook should provide one; manual callers can
        # use ESPHOOK_SESSION_ID or the CLI --session-id option.
        return

    client_id = _string(os.environ.get("ESPHOOK_CLIENT_ID"))
    if not client_id:
        source_id = source.lower().replace("-", "") or "agent"
        client_id = f"{socket.gethostname()}:{source_id}"

    event = _event(payload)
    if not classified and source.lower() == "codex" and event == "posttooluse":
        if _take_pending_for_post_tool(source, client_id, session_id, payload):
            classified = ("dismiss", "", "")
    if not classified:
        return

    status, title, body = classified
    if status == "confirm" and source.lower() == "codex" and event in PERMISSION_REQUEST_EVENTS:
        _remember_pending_confirmation(source, client_id, session_id, payload)
    elif status == "dismiss" or event in {"stop", "sessionend", "session_end"}:
        _clear_pending_confirmation(source, client_id, session_id)

    environment = os.environ.copy()
    environment["ESPHOOK_SESSION_ID"] = session_id
    environment["ESPHOOK_CLIENT_ID"] = client_id
    if status == "dismiss":
        command = _esphook_command() + [
            "dismiss",
            "--session-id",
            session_id,
            "--client-id",
            client_id,
        ]
    else:
        command = _esphook_command() + [
            "notify",
            "--session-id",
            session_id,
            "--client-id",
            client_id,
            status,
            title,
            body,
        ]
    try:
        subprocess.run(
            command,
            env=environment,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            timeout=3.0,
            check=False,
        )
    except (OSError, subprocess.TimeoutExpired):
        pass


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True, choices=sorted(SOURCE_LABELS))
    args = parser.parse_args(argv)
    try:
        raw = sys.stdin.read()
        payload = json.loads(raw) if raw.strip() else {}
        if isinstance(payload, dict):
            forward(payload, args.source)
    except (OSError, UnicodeDecodeError, ValueError, TypeError):
        pass
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
