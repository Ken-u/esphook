#!/usr/bin/env python3
"""Print key-report app events without injecting keystrokes into other programs."""
import argparse
import json
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "framework" / "pc"))
from esphook_sdk import AppLink

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--port")
    p.add_argument("--dry-run", action="store_true")
    args = p.parse_args()
    if args.dry_run:
        print(json.dumps(dict(op="open", app="key_report")))
        return
    link = AppLink(args.port)
    try:
        link.open("key_report")
        link.wait_active("key_report")
        while True:
            for event in link.poll():
                if event.get("app") == "key_report":
                    print(json.dumps(event, ensure_ascii=False))
    except KeyboardInterrupt:
        link.menu()
    finally:
        link.close()

if __name__ == "__main__":
    main()
