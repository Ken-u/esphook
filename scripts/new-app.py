#!/usr/bin/env python3
"""Create a manifest-discovered native app and optional PC companion."""
import argparse
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def create(root, app_id, title, pc=False):
    if not re.fullmatch(r"[a-z][a-z0-9_]{0,22}", app_id):
        raise ValueError("id: 1..23 lowercase letters/digits/underscore, starting with a letter")
    if not title or any(ord(c) < 32 for c in title) or sum(6 if c.isascii() else 12 for c in title) > 48:
        raise ValueError("title must fit 48px: up to four Chinese or eight ASCII characters")
    if not (root / "framework/include/esphook/app.h").is_file():
        raise ValueError("--root must point to an esphook project")
    target = root / "apps" / app_id
    if target.exists():
        raise ValueError(f"app already exists: {app_id}")
    template = ROOT / ".agents/skills/esphook-app/assets/app.c.in"
    native = template.read_text().replace("@APP_ID@", app_id).replace("@TITLE_C@", json.dumps(title, ensure_ascii=False))
    companion = (ROOT / ".agents/skills/esphook-pc/assets/main.py.in").read_text().replace("@APP_ID@", app_id) if pc else None
    (target / "firmware").mkdir(parents=True)
    (target / "firmware/app.c").write_text(native)
    (target / "manifest.json").write_text(json.dumps(dict(id=app_id, order="500", required=False, sources=["firmware/app.c"]), indent=2) + "\n")
    if companion is not None:
        (target / "pc").mkdir()
        (target / "pc/main.py").write_text(companion)
    return target

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("id")
    p.add_argument("--title", required=True)
    p.add_argument("--pc", action="store_true")
    p.add_argument("--root", type=Path, default=ROOT)
    args = p.parse_args()
    try:
        print(create(args.root.resolve(), args.id, args.title, args.pc))
    except (ValueError, OSError) as exc:
        p.error(str(exc))

if __name__ == "__main__":
    main()
