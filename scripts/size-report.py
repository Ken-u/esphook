#!/usr/bin/env python3
"""Firmware size budget: how full the OTA slot is, where the bytes go, what every app costs.

    python3 scripts/size-report.py --bin build/supermini-aihook.bin --map build/supermini-aihook.map \
        [--partitions partitions_aihook.csv] [--min-free-pct 8] [--summary "$GITHUB_STEP_SUMMARY"]

CI runs it after the firmware build. It fails when the free part of the smallest OTA slot drops under --min-free-pct,
so a game that eats the headroom is noticed while it is one commit, not when the image stops fitting.

The per-app table needs the link order: ESP-IDF names every app object "app.c.obj", so the apps are told apart by
walking the objects in the order cmake/apps.cmake links them (manifest "order"). If that does not add up the table
is left out and said so.
"""
import argparse
import glob
import json
import os
import re
import sys

NETWORK = ("libnet80211", "libpp", "libwpa_supplicant", "libphy", "libesp_wifi", "libesp_coex", "libmesh", "libespnow",
           "liblwip", "libesp_netif", "libmbedcrypto", "libmbedtls", "libmbedx509", "libesp-tls", "libtcp_transport",
           "libesp_http_client", "libesp_http_server", "libhttp_parser", "libesp_event")
# SRAM the application can use on the ESP32-C3 after the ROM's reservations (what esp-idf-size reports as "Total").
SRAM_TOTAL = 321296


def parse_size(text):
    text = text.strip()
    if text.lower().startswith("0x"):
        return int(text, 16)
    unit = {"K": 1024, "M": 1024 * 1024}.get(text[-1:].upper(), 1)
    return int(text.rstrip("kKmM")) * unit


def ota_slot_bytes(csv_path):
    sizes = []
    with open(csv_path, encoding="utf-8") as handle:
        for line in handle:
            if line.lstrip().startswith("#") or not line.strip():
                continue
            cols = [c.strip() for c in line.split(",")]
            if len(cols) >= 5 and cols[1] == "app" and cols[0].startswith("ota_"):
                sizes.append(parse_size(cols[4]))
    if not sizes:
        raise SystemExit("no ota_N app partition in " + csv_path)
    return min(sizes)


def parse_map(path):
    """Returns (output sections {name: size}, input sections [(output, name, size, object path)]).

    String literals are merged by the linker: the first object's entry carries the whole merged lump and the later
    objects list their share again at addresses inside it. Entries that start inside an earlier one are dropped so
    nothing is counted twice.
    """
    with open(path, encoding="utf-8", errors="replace") as handle:
        lines = handle.read().split("\n")
    outputs, items, out, end = {}, [], None, 0

    def add(name, address, size, obj):
        nonlocal end
        if address < end:
            return
        end = max(end, address + size)
        items.append((out, name, size, obj.strip()))

    for i, line in enumerate(lines):
        if line.startswith(".") and line.split():
            head = line.split()
            out, end = head[0], 0
            if len(head) >= 3 and head[2].startswith("0x"):
                outputs[out] = int(head[2], 16)
            continue
        one = re.match(r"^ (\.\S+)\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s+(\S.*)$", line)
        if one:
            add(one.group(1), int(one.group(2), 16), int(one.group(3), 16), one.group(4))
            continue
        name = re.match(r"^ (\.\S+)$", line)
        if name and i + 1 < len(lines):
            two = re.match(r"^\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s+(\S.*)$", lines[i + 1])
            if two:
                add(name.group(1), int(two.group(1), 16), int(two.group(2), 16), two.group(3))
    return outputs, items


def archive_of(path):
    match = re.search(r"/(lib[^/()]+)\.a\(([^)]+)\)", path)
    return (match.group(1), match.group(2)) if match else (None, None)


def app_order(root):
    """App ids in the order cmake/apps.cmake links them."""
    entries = []
    for manifest in glob.glob(os.path.join(root, "apps", "*", "manifest.json")):
        with open(manifest, encoding="utf-8") as handle:
            data = json.load(handle)
        entries.append(("%s:%s" % (data.get("order", "999"), manifest), data["id"]))
    return [app for _, app in sorted(entries)]


def extra_objects(root):
    """Object name -> app id for the sources that are not called app.c (cores, models, codecs)."""
    names = {}
    for source in glob.glob(os.path.join(root, "apps", "*", "firmware", "*.c")):
        base = os.path.basename(source)
        if base != "app.c":
            names[base + ".obj"] = source.split(os.sep)[-3]
    return names


def per_app(items, root):
    """{app: bytes of flash} or None when the link order does not add up."""
    order = app_order(root)
    segments, current = [], None
    for out, name, size, path in items:
        lib, obj = archive_of(path)
        if out != ".flash.text" or lib != "libmain" or obj != "app.c.obj":
            continue
        if current is None or name in current["names"]:
            current = {"names": set(), "size": 0}
            segments.append(current)
        current["names"].add(name)
        current["size"] += size
    if not segments or len(segments) != len(order):
        return None
    cost = {app: seg["size"] for app, seg in zip(order, segments)}
    extra = extra_objects(root)
    for out, name, size, path in items:
        lib, obj = archive_of(path)
        if out in (".flash.text", ".flash.rodata") and lib == "libmain" and obj in extra:
            cost[extra[obj]] = cost.get(extra[obj], 0) + size
    return cost


def categories(items, root):
    extra = extra_objects(root)
    cats = {"apps + app cores/models": 0, "framework + main": 0, "LVGL": 0,
            "Wi-Fi, lwip, TLS, HTTP": 0, "string literals (merged, all objects)": 0, "ESP-IDF core, libc, drivers": 0}
    for out, name, size, path in items:
        if out not in (".flash.text", ".flash.rodata", ".iram0.text", ".dram0.data"):
            continue
        lib, obj = archive_of(path)
        if re.search(r"\.str1\.[14]$", name) and size > 50000:
            cats["string literals (merged, all objects)"] += size
        elif lib == "libmain":
            cats["apps + app cores/models" if obj == "app.c.obj" or obj in extra else "framework + main"] += size
        elif lib == "liblvgl__lvgl":
            cats["LVGL"] += size
        elif lib in NETWORK:
            cats["Wi-Fi, lwip, TLS, HTTP"] += size
        else:
            cats["ESP-IDF core, libc, drivers"] += size
    return cats


def kib(value):
    return "%.1f KiB" % (value / 1024)


def build_report(args):
    image = os.path.getsize(args.bin)
    slot = ota_slot_bytes(args.partitions)
    free = slot - image
    free_pct = 100.0 * free / slot
    outputs, items = parse_map(args.map)
    root = args.root
    lines = ["Firmware size budget",
             "  image            %9d B  %s" % (image, kib(image)),
             "  smallest OTA slot %8d B  %s" % (slot, kib(slot)),
             "  free             %9d B  %s  (%.1f%%)" % (free, kib(free), free_pct)]
    sram = outputs.get(".iram0.text", 0) + outputs.get(".dram0.data", 0) + outputs.get(".dram0.bss", 0)
    if sram:
        lines.append("  static SRAM      %9d B  of %d (%.1f%%); what is left is the heap for Wi-Fi, stacks and LVGL"
                     % (sram, SRAM_TOTAL, 100.0 * sram / SRAM_TOTAL))
    lines += ["", "Where the image goes (flash text + rodata + IRAM + data)"]
    cats = categories(items, root)
    total = sum(cats.values()) or 1
    for name, size in sorted(cats.items(), key=lambda kv: -kv[1]):
        lines.append("  %-40s %9d B  %5.1f%%" % (name, size, 100.0 * size / total))
    apps = per_app(items, root)
    lines.append("")
    if apps is None:
        lines.append("Per-app cost: not available (the link order did not match the manifests)")
    else:
        lines.append("Per-app flash cost (code + own rodata, string literals not included)")
        for app, size in sorted(apps.items(), key=lambda kv: -kv[1]):
            lines.append("  %-14s %8d B  %s" % (app, size, kib(size)))
        lines.append("  %-14s %8d B" % ("total", sum(apps.values())))
    return lines, free_pct, free, slot


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--bin", required=True)
    parser.add_argument("--map", required=True)
    parser.add_argument("--partitions", default=os.path.join(os.path.dirname(__file__), "..", "partitions_aihook.csv"))
    parser.add_argument("--min-free-pct", type=float, default=8.0, help="fail when the free part of the OTA slot is smaller")
    parser.add_argument("--root", default=os.path.dirname(os.path.dirname(os.path.abspath(__file__))), help=argparse.SUPPRESS)
    parser.add_argument("--summary", help="also append the report as Markdown to this file (GITHUB_STEP_SUMMARY)")
    args = parser.parse_args(argv)
    lines, free_pct, free, slot = build_report(args)
    print("\n".join(lines))
    if args.summary:
        with open(args.summary, "a", encoding="utf-8") as handle:
            handle.write("### Firmware size budget\n\n```text\n" + "\n".join(lines) + "\n```\n")
    if free_pct < args.min_free_pct:
        print("\nFAIL: only %.1f%% of the OTA slot is free, the budget is %.1f%%" % (free_pct, args.min_free_pct), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
