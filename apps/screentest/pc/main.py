"""Tune the panel of a 0.96" ST7735 module over USB: `main.py status`, `plain`, `plugin`, `invert 0`, `gap 0 24`, `save`.

The two common module flavours need different colour polarity and RAM offsets (see docs/hardware.md). Every word is
one `robot screentest ...` command: the change shows at once and is only stored by `save`.
"""
import argparse
import json
import sys
import time
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "framework" / "pc"))
from esphook_sdk import AppLink

WORDS = "status plugin plain invert gap save mhz raw start stop"

def command_line(words):
    return " ".join(["robot screentest"] + list(words)).strip()

def ask(link, line, wait=2.0):
    """First plain-text (non-JSON) reply line, which is how apps answer `robot <app> ...`."""
    link.serial.reset_input_buffer()
    link.send_line(line)
    end = time.monotonic() + wait
    buf = b""
    while time.monotonic() < end:
        buf += link.serial.read(link.serial.in_waiting or 1)
        for raw in buf.split(b"\n")[:-1]:
            text = raw.strip()
            if text and not text.startswith(b"{"):
                return text.decode(errors="replace")
    return None

def send(link, line, wait=2.0):
    """ask(), repeated while the screen is still busy with the previous change (a few milliseconds)."""
    reply = ask(link, line, wait)
    for _ in range(10):
        if not (reply or "").startswith("ERR busy"):
            break
        time.sleep(0.05)
        reply = ask(link, line, wait)
    return reply

def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--port")
    p.add_argument("--dry-run", action="store_true", help="print the command line instead of sending it")
    p.add_argument("words", nargs="*", help=WORDS)
    args = p.parse_args(argv)
    line = command_line(args.words or ["status"])
    if args.dry_run:
        print(json.dumps(dict(command=line)))
        return 0
    link = AppLink(args.port)
    try:
        reply = send(link, line)
    finally:
        link.close()
    if reply is None:
        print("no reply: is this firmware new enough to have the screentest app?", file=sys.stderr)
        return 1
    print(reply)
    return 0 if reply.startswith(("OK", "invert=")) else 1

if __name__ == "__main__":
    raise SystemExit(main())
