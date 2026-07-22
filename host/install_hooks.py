#!/usr/bin/env python3
"""Install esphook lifecycle adapters for local coding agents.

The installer edits only the supported user-level hook files and preserves
existing entries.  Its own entries are identified by the hook script path, so
running it repeatedly is idempotent.
"""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import stat
import sys
from typing import Any, Callable


HOOK_SCRIPT = Path(__file__).with_name("hooks") / "esphook_notify_hook.py"
MANAGED_START = "# BEGIN ESPHOOK MANAGED HOOKS"
MANAGED_END = "# END ESPHOOK MANAGED HOOKS"

TOOL_ALIASES = {
    "claude": "claude",
    "claudecode": "claude",
    "codex": "codex",
    "kimi": "kimi",
    "kimicode": "kimi",
    "cursor": "cursor",
    "cursor-agent": "cursor",
}


def command_for(source: str) -> str:
    return shlex.join([
        sys.executable,
        str(HOOK_SCRIPT.resolve()),
        "--source",
        source,
    ])


def managed(value: Any) -> bool:
    try:
        text = json.dumps(value, ensure_ascii=False)
    except (TypeError, ValueError):
        return False
    return "esphook_notify_hook.py" in text


def read_json(path: Path) -> dict[str, Any]:
    if not path.exists():
        return {}
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, ValueError) as exc:
        raise RuntimeError(f"无法读取 JSON 配置 {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise RuntimeError(f"JSON 配置顶层必须是对象: {path}")
    return value


def atomic_write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    mode = stat.S_IMODE(path.stat().st_mode) if path.exists() else 0o600
    temporary = path.with_suffix(path.suffix + ".esphook.tmp")
    temporary.write_text(text, encoding="utf-8")
    os.chmod(temporary, mode)
    os.replace(temporary, path)


def backup_once(path: Path) -> Path | None:
    if not path.exists():
        return None
    backup = path.with_name(path.name + ".esphook.bak")
    if not backup.exists():
        shutil.copy2(path, backup)
    return backup


def update_json(path: Path, update: Callable[[dict[str, Any]], None], dry_run: bool) -> str:
    data = read_json(path)
    update(data)
    rendered = json.dumps(data, ensure_ascii=False, indent=2) + "\n"
    if not dry_run:
        backup_once(path)
        atomic_write(path, rendered)
    return rendered


def add_nested_hook(data: dict[str, Any], events: list[str], command: str) -> None:
    hooks = data.setdefault("hooks", {})
    if not isinstance(hooks, dict):
        raise RuntimeError("hooks 字段必须是对象")
    for event in events:
        entries = hooks.setdefault(event, [])
        if not isinstance(entries, list):
            raise RuntimeError(f"hooks.{event} 必须是数组")
        hooks[event] = [entry for entry in entries if not managed(entry)]
        hooks[event].append({
            "hooks": [{
                "type": "command",
                "command": command,
                "timeout": 5,
            }],
        })


def add_cursor_hook(data: dict[str, Any], events: list[str], command: str) -> None:
    hooks = data.setdefault("hooks", {})
    if not isinstance(hooks, dict):
        raise RuntimeError("hooks 字段必须是对象")
    data.setdefault("version", 1)
    for event in events:
        entries = hooks.setdefault(event, [])
        if not isinstance(entries, list):
            raise RuntimeError(f"hooks.{event} 必须是数组")
        hooks[event] = [entry for entry in entries if not managed(entry)]
        hooks[event].append({"command": command})


def kimi_block(command: str) -> str:
    # Kimi validates [[hooks]] entries strictly; keep this block limited to
    # documented fields and use a matcher only for notification events that
    # represent a pending user interaction.
    lines = [MANAGED_START]
    entries = [
        ("Stop", ""),
        ("StopFailure", ""),
        ("Interrupt", ""),
        ("PermissionRequest", ""),
        ("Notification", "permission.*|approval.*|elicitation.*|idle_prompt"),
    ]
    for event, matcher in entries:
        lines.extend([
            "[[hooks]]",
            f"event = {json.dumps(event, ensure_ascii=False)}",
        ])
        if matcher:
            lines.append(f"matcher = {json.dumps(matcher, ensure_ascii=False)}")
        lines.extend([
            f"command = {json.dumps(command, ensure_ascii=False)}",
            "timeout = 5",
            "",
        ])
    lines.append(MANAGED_END)
    return "\n".join(lines) + "\n"


def update_kimi(path: Path, command: str, dry_run: bool) -> str:
    try:
        current = path.read_text(encoding="utf-8") if path.exists() else ""
    except OSError as exc:
        raise RuntimeError(f"无法读取 Kimi 配置 {path}: {exc}") from exc
    pattern = re.compile(
        rf"(?ms)^\s*{re.escape(MANAGED_START)}\n.*?^\s*{re.escape(MANAGED_END)}\n?"
    )
    current = pattern.sub("", current).rstrip()
    rendered = (current + "\n\n" if current else "") + kimi_block(command)
    if not dry_run:
        backup_once(path)
        atomic_write(path, rendered)
    return rendered


def remove_nested_hooks(data: dict[str, Any]) -> bool:
    changed = False
    hooks = data.get("hooks")
    if not isinstance(hooks, dict):
        return False
    for event, entries in list(hooks.items()):
        if not isinstance(entries, list):
            continue
        filtered = [entry for entry in entries if not managed(entry)]
        if len(filtered) != len(entries):
            hooks[event] = filtered
            changed = True
    return changed


def remove_cursor_hooks(data: dict[str, Any]) -> bool:
    return remove_nested_hooks(data)


def remove_kimi(path: Path, dry_run: bool) -> bool:
    if not path.exists():
        return False
    current = path.read_text(encoding="utf-8")
    pattern = re.compile(
        rf"(?ms)^\s*{re.escape(MANAGED_START)}\n.*?^\s*{re.escape(MANAGED_END)}\n?"
    )
    rendered = pattern.sub("", current).rstrip() + "\n"
    if rendered == current:
        return False
    if not dry_run:
        backup_once(path)
        atomic_write(path, rendered)
    return True


def canonical_tools(value: str) -> list[str]:
    requested = [part.strip().lower() for part in value.split(",") if part.strip()]
    if not requested or "all" in requested:
        return ["claude", "codex", "kimi", "cursor"]
    result: list[str] = []
    for name in requested:
        if name not in TOOL_ALIASES:
            raise RuntimeError(f"不支持的工具: {name}（可选 claude,codex,kimi,cursor,all）")
        canonical = TOOL_ALIASES[name]
        if canonical not in result:
            result.append(canonical)
    return result


def install_tool(tool: str, home: Path, dry_run: bool) -> Path:
    command = command_for(tool)
    if tool == "claude":
        path = home / ".claude" / "settings.json"
        update_json(path, lambda data: add_nested_hook(
            data, ["Notification", "Stop", "StopFailure"], command
        ), dry_run)
    elif tool == "codex":
        path = home / ".codex" / "hooks.json"
        update_json(path, lambda data: add_nested_hook(
            data, ["Stop", "PermissionRequest", "PostToolUse"], command
        ), dry_run)
    elif tool == "kimi":
        path = home / ".kimi-code" / "config.toml"
        update_kimi(path, command, dry_run)
    elif tool == "cursor":
        path = home / ".cursor" / "hooks.json"
        update_json(path, lambda data: add_cursor_hook(data, ["stop"], command), dry_run)
    else:
        raise RuntimeError(f"internal error: {tool}")
    return path


def uninstall_tool(tool: str, home: Path, dry_run: bool) -> Path:
    if tool == "kimi":
        path = home / ".kimi-code" / "config.toml"
        remove_kimi(path, dry_run)
        return path
    path = (home / ".claude" / "settings.json" if tool == "claude"
            else home / ".codex" / "hooks.json" if tool == "codex"
            else home / ".cursor" / "hooks.json")
    if not path.exists():
        return path
    data = read_json(path)
    changed = remove_nested_hooks(data)
    if changed and not dry_run:
        backup_once(path)
        atomic_write(path, json.dumps(data, ensure_ascii=False, indent=2) + "\n")
    return path


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="安装/卸载 esphook Agent Hooks")
    parser.add_argument("--tools", default="all", help="all 或逗号分隔的 claude,codex,kimi,cursor")
    parser.add_argument("--home", type=Path, default=Path.home(), help=argparse.SUPPRESS)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--uninstall", action="store_true")
    args = parser.parse_args(argv)
    try:
        tools = canonical_tools(args.tools)
        home = args.home.expanduser().resolve()
        for tool in tools:
            path = uninstall_tool(tool, home, args.dry_run) if args.uninstall else install_tool(tool, home, args.dry_run)
            action = "remove" if args.uninstall else "install"
            suffix = " (dry-run)" if args.dry_run else ""
            print(f"esphook: {action} {tool} hooks -> {path}{suffix}")
        if not args.uninstall:
            print("esphook: 请重启对应 Agent；Codex 可能还需要在 /hooks 中信任新 Hook")
        return 0
    except (OSError, RuntimeError, ValueError) as exc:
        print(f"esphook: hook 安装失败: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
