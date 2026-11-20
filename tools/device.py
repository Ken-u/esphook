"""Look at and drive a connected board over USB: what is running, open an app, send one text command.

This is the one-shot command line for people (and AI agents) who set a board up or check it after a change; every
action is a single short USB session. For scripts that keep a session open use esphook_sdk.AppLink.

  status              the running app, the standby app, the hardware profile and heap, the clock (JSON)
  apps                the app catalog (JSON)
  open <id>           open an app (and wait until it is on screen)
  menu                back to the menu
  cmd "<line>"        send one USB text command (`sound`, `robot status`, `status`, `help`, ...) and print the reply lines

`cmd` refuses the two kinds of line that cannot be undone or that move hardware - the factory reset and the servo's
arm/follow/center/angle - unless --force is given. Only add it when the person who owns the board said so.
"""
import argparse
import json
from pathlib import Path
import re
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "framework/pc"))
from esphook_sdk import AppLink

RISKY = (
    (re.compile(r"^reset$"), "it is the factory reset: Wi-Fi, calibration, screen setup, high scores and every other saved setting are wiped"),
    (re.compile(r"^robot\s+(?:(?:arm|follow|center)$|angle:)"), "it powers or moves the servo, which only the person who owns the board may confirm"),
)
LOG_LINE = re.compile(r"^(?:\x1b\[[0-9;]*m)?[IWEDV] \(\d+\) ")  # an ESP-IDF log line: `I (1234) tag: text`


def text_command(link, line, wait=1.0, expect=None, logs=False):
    """Sends one text command and returns the complete reply lines seen within `wait` seconds (or up to the first line
    that starts with `expect`). JSON frames and, unless `logs`, ESP-IDF log lines are left out; a line still arriving
    when the time is up is dropped rather than returned half read."""
    link.serial.reset_input_buffer()
    link.send_line(line)
    deadline = time.monotonic() + wait
    pending = bytearray()
    lines = []
    while time.monotonic() < deadline:
        pending.extend(link.serial.read(link.serial.in_waiting or 1))
        while b"\n" in pending:
            raw, _, rest = bytes(pending).partition(b"\n")
            pending = bytearray(rest)
            text = raw.decode("utf-8", "replace").strip()
            if not text or text.startswith("{") or (not logs and LOG_LINE.match(text)):
                continue
            lines.append(text)
            if expect and text.startswith(expect):
                return lines
    return lines


def risk(line):
    """Why `line` should not be sent without the owner's say-so, or None."""
    text = " ".join(line.lower().split())
    return next((why for pattern, why in RISKY if pattern.match(text)), None)


def parser():
    common = argparse.ArgumentParser(add_help=False)  # accepted before or after the action
    common.add_argument("--port", default=argparse.SUPPRESS, help="USB serial port (found by itself when one ESP32 is connected)")
    common.add_argument("--dry-run", action="store_true", default=argparse.SUPPRESS,
                        help="print what would be sent, without opening the port")
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter, parents=[common])
    sub = p.add_subparsers(dest="action", required=True)
    for name in ("status", "apps", "menu"):
        sub.add_parser(name, parents=[common])
    open_app = sub.add_parser("open", parents=[common])
    open_app.add_argument("app")
    cmd = sub.add_parser("cmd", parents=[common])
    cmd.add_argument("line", help="the text command, one line")
    cmd.add_argument("--wait", type=float, default=1.0, help="seconds to listen for the reply (default 1)")
    cmd.add_argument("--expect", help="stop at the first reply line that starts with this")
    cmd.add_argument("--logs", action="store_true", help="keep ESP-IDF log lines in the output")
    cmd.add_argument("--force", action="store_true", help="send a line that wipes settings or moves the servo anyway")
    return p


def main(argv=None):
    p = parser()
    a = p.parse_args(argv)
    if a.action == "cmd" and (not a.line.strip() or "\n" in a.line or a.wait <= 0):
        p.error("cmd needs one non-empty line and a positive --wait")
    if a.action == "cmd" and not a.force and risk(a.line):
        print(f"esphook: not sending {a.line!r}: {risk(a.line)}. Ask the owner first; --force sends it.", file=sys.stderr)
        return 4
    if getattr(a, "dry_run", False):
        print(json.dumps(dict(action=a.action, **{k: v for k, v in vars(a).items() if k in ("app", "line", "wait", "expect")}),
                         ensure_ascii=False))
        return 0
    link = AppLink(getattr(a, "port", None))
    try:
        if a.action == "status":
            print(json.dumps(dict(app=link.status(), hardware=link.hardware(), clock=link.clock()), ensure_ascii=False, indent=2))
        elif a.action == "apps":
            print(json.dumps(link.catalog(), ensure_ascii=False, indent=2))
        elif a.action == "menu":
            print(json.dumps(link.menu(), ensure_ascii=False))
        elif a.action == "open":
            link.open(a.app)
            link.wait_active(a.app)
            print(json.dumps(link.status(), ensure_ascii=False))
        else:
            lines = text_command(link, a.line, a.wait, a.expect, a.logs)
            if not lines:
                print(f"esphook: no reply to {a.line!r} within {a.wait:g} s (try a larger --wait)", file=sys.stderr)
                return 3
            print("\n".join(lines))
    finally:
        link.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
