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
from typing import Any


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
    return _string(payload.get("hook_event_name") or payload.get("event")).lower()


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
        "permissionrequest",
        "permission_request",
        "permissionresult",
        "interrupt",
        "elicitation",
    }
    done_events = {"stop", "sessionend", "session_end"}

    if event in error_events or agent_status in {"error", "failed", "failure"}:
        status = "error"
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
        title = f"{label} 出错"
        body = _compact(
            payload.get("error")
            or payload.get("message")
            or payload.get("last_assistant_message")
            or reason
            or "本轮任务执行失败"
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
    if not classified:
        return
    session_id = _session_id(payload)
    if not session_id:
        # Never invent a new id per event: that would make dismiss/routing
        # unreliable.  The vendor hook should provide one; manual callers can
        # use ESPHOOK_SESSION_ID or the CLI --session-id option.
        return

    status, title, body = classified
    client_id = _string(os.environ.get("ESPHOOK_CLIENT_ID"))
    if not client_id:
        source_id = source.lower().replace("-", "") or "agent"
        client_id = f"{socket.gethostname()}:{source_id}"
    environment = os.environ.copy()
    environment["ESPHOOK_SESSION_ID"] = session_id
    environment["ESPHOOK_CLIENT_ID"] = client_id
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
