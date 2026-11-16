#!/usr/bin/env python3
"""Upload demo text and receive tap/reset events using the shared SDK."""
import argparse
import json
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "framework" / "pc"))
from esphook_sdk import AppLink

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--port")
    p.add_argument("--text", default="Hello from PC")
    p.add_argument("--dry-run", action="store_true")
    args = p.parse_args()
    if len(args.text.encode()) > 48:
        p.error("text must fit in 48 UTF-8 bytes")
    if args.dry_run:
        print(json.dumps(dict(op="message", app="demo", data=dict(text=args.text))))
        return
    link = AppLink(args.port)
    try:
        print(link.catalog())
        link.open("demo")
        link.wait_active("demo")
        link.message("demo", dict(text=args.text))
        while True:
            for event in link.poll():
                print(json.dumps(event, ensure_ascii=False))
    except KeyboardInterrupt:
        link.menu()
    finally:
        link.close()

if __name__ == "__main__":
    main()
