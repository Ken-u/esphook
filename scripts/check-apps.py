#!/usr/bin/env python3
"""Check app manifests, layering and repository-local skill contracts."""
import argparse
import json
from pathlib import Path
import re

# An app may only reach the device through the public framework API, so its includes are an allowlist: the C
# library, cJSON, its own local headers and `esphook/app*.h` (+ the radar view and the display protocol the
# services hand out, which the include path also exposes without the prefix). Anything else - FreeRTOS, ESP-IDF,
# drivers, the runtime internals - is a layering break; the framework exposes what an app needs instead
# (app_sync.h for task hand-off, app_command.h for USB commands, app_services.h for hardware).
STD_HEADERS = {"assert.h", "ctype.h", "errno.h", "float.h", "inttypes.h", "limits.h", "math.h", "stdarg.h", "stdbool.h",
               "stddef.h", "stdint.h", "stdio.h", "stdlib.h", "string.h", "time.h"}
PUBLIC_HEADER = re.compile(r"(?:esphook/)?(?:app(?!_runtime\b)(?:_[a-z0-9]+)*|radar_view|robot_ui_protocol)\.h")

def include_errors(text, local_headers=()):
    """Includes in `text` outside the app allowlist; `local_headers` are the file names of the app's own headers."""
    bad = []
    for quote, name in re.findall(r'^\s*#\s*include\s*([<"])([^>"]+)[>"]', text, re.M):
        if not ((quote == '"' and name in local_headers) or name in STD_HEADERS or name == "cJSON.h" or PUBLIC_HEADER.fullmatch(name)):
            bad.append(name)
    return bad

def descriptor_text(combined, app_id):
    """The `esphook_app_<id> = {...};` initializer, without unrelated structs in the same sources."""
    start = re.search(r"\bconst\s+app_descriptor_t\s+esphook_app_" + app_id + r"\s*=", combined)
    end = combined.find("};", start.end())
    return combined[start.end():end if end >= 0 else None]

def list_ack_frame(root, catalog):
    """Worst-case `app list` ack as app_runtime.c builds it with cJSON_PrintUnformatted."""
    header = (root / "framework/include/esphook/app.h").read_text()
    api = re.search(r"#define\s+APP_API_VERSION\s+(\d+)", header)
    apps = [dict(id=a["id"], title=a["title"], standby=a["standby"]) for a in catalog if not a["hidden"]]
    ack = dict(v=1, type="app_ack", seq=2147483647, ok=True, active="x" * 23, standby="x" * 23,
               api=int(api[1]) if api else 1, apps=apps)
    return json.dumps(ack, ensure_ascii=False, separators=(",", ":"))

def tx_budget(root):
    header = (root / "main/usb_cdc.h").read_text()
    match = re.search(r"#define\s+CDC_TX_BUFFER_BYTES\s+(\d+)", header)
    if not match:
        raise ValueError("main/usb_cdc.h must define CDC_TX_BUFFER_BYTES")
    return int(match[1])

def check(root, catalog=None):
    """Returns (errors, app count); `catalog`, when given, is filled with each app's title and flags."""
    errors, ids = [], set()
    catalog = [] if catalog is None else catalog
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
            descriptor = descriptor_text(combined, app_id)
            title = re.search(r'\.title\s*=\s*"((?:[^"\\]|\\.)*)"', descriptor)
            flags = re.search(r"\.flags\s*=\s*([^,}]+)", descriptor)
            if not title:
                raise ValueError("descriptor needs a literal .title")
            def has(flag):
                return bool(flags and re.search(r"\b" + flag + r"\b", flags[1]))
            if has("APP_DEFAULT_STANDBY") and not has("APP_STANDBY"):
                raise ValueError("APP_DEFAULT_STANDBY needs APP_STANDBY as well")
            catalog.append(dict(id=app_id, title=title[1], hidden=has("APP_HIDDEN"), standby=has("APP_STANDBY"),
                                default=has("APP_DEFAULT_STANDBY")))
            sources = sorted(f for pattern in ("*.c", "*.h") for f in (manifest.parent / "firmware").rglob(pattern))
            local = {f.name for f in sources}
            bad = sorted({name for f in sources for name in include_errors(f.read_text(), local)})
            if bad:
                raise ValueError(f"apps must use the public framework API (esphook/app*.h), not {', '.join(bad)}")
        except (KeyError, ValueError, TypeError, OSError) as exc:
            errors.append(f"{manifest.relative_to(root)}: {exc}")
    try:
        # The ack must fit the USB TX ring in one piece (+1 for the newline); see test-usb-cdc and usb_cdc.c.
        size, budget = len(list_ack_frame(root, catalog).encode()) + 1, tx_budget(root)
        if size > budget:
            errors.append(f"worst-case `list` ack is {size} bytes, above the {budget}-byte CDC_TX_BUFFER_BYTES budget; "
                          "shorten titles, reduce visible apps or enlarge main/usb_cdc.h")
    except (OSError, ValueError) as exc:
        errors.append(f"cannot verify the `list` ack size: {exc}")
    defaults = [a["id"] for a in catalog if a.get("default")]
    if len(defaults) > 1:
        errors.append(f"only one app may set APP_DEFAULT_STANDBY, found {', '.join(defaults)}")
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
