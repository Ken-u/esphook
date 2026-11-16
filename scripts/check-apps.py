#!/usr/bin/env python3
"""Check app manifests, layering and repository-local skill contracts."""
import argparse
import json
from pathlib import Path
import re

def check(root):
    errors, ids = [], set()
    for manifest in sorted((root / "apps").glob("*/manifest.json")):
        try:
            m = json.loads(manifest.read_text())
            app_id = m["id"]
            if not re.fullmatch(r"[a-z][a-z0-9_]{0,22}", app_id) or app_id != manifest.parent.name or app_id in ids:
                raise ValueError("id must be unique and match its directory")
            ids.add(app_id)
            if not re.fullmatch(r"[0-9]{3}", m["order"]) or not isinstance(m["required"], bool):
                raise ValueError("order must have three digits; required must be bool")
            if not m["sources"] or len(set(m["sources"])) != len(m["sources"]):
                raise ValueError("sources must be a non-empty unique list")
            texts = []
            for source in m["sources"]:
                p = Path(source)
                if p.is_absolute() or ".." in p.parts or p.suffix != ".c" or not (manifest.parent / p).is_file():
                    raise ValueError(f"invalid source {source}")
                texts.append((manifest.parent / p).read_text())
            combined = "\n".join(texts)
            if not re.search(r"\bconst\s+app_descriptor_t\s+esphook_app_" + app_id + r"\s*=", combined):
                raise ValueError("descriptor symbol missing")
            if not re.search(r'\.id\s*=\s*"' + app_id + '"', combined):
                raise ValueError("descriptor id mismatch")
            if re.search(r'#\s*include\s*[<"](?:driver/|esp_lcd|esp_http_client|usb_cdc\.h|(?:esphook/)?(?:robot_runtime|motion_ports)\.h)', combined):
                raise ValueError("apps must use framework device/transport APIs")
        except (KeyError, ValueError, TypeError, OSError) as exc:
            errors.append(f"{manifest.relative_to(root)}: {exc}")
    for required in ("launcher", "face", "settings"):
        if required not in ids:
            errors.append(f"missing required app {required}")
    for skill in sorted((root / ".agents/skills").glob("*/SKILL.md")):
        text = skill.read_text()
        if not text.startswith("---\n") or text.count("\n---\n") < 1 or "TODO" in text:
            errors.append(f"{skill}: incomplete frontmatter or placeholder")
        name = re.search(r"^name: ([a-z0-9-]+)$", text, re.M)
        if not name or name[1] != skill.parent.name or not re.search(r"^description: .+", text, re.M):
            errors.append(f"{skill}: invalid name/description")
        metadata = skill.parent / "agents/openai.yaml"
        if not metadata.is_file() or f"${skill.parent.name}" not in metadata.read_text():
            errors.append(f"{skill}: missing or stale agents metadata")
    return errors, len(ids)

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    args = p.parse_args()
    errors, count = check(args.root.resolve())
    if errors:
        p.exit(1, "\n".join(errors) + "\n")
    print(f"app contracts: {count} descriptors, manifests, layering and repository skills passed")

if __name__ == "__main__":
    main()
