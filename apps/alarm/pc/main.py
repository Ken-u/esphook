#!/usr/bin/env python3
"""Set (or read) the esphook clock from this computer.

The board has no battery clock: the alarm clock and the hourly chime need the time again after every power-up
unless Wi-Fi fetches it. This sends this computer's time and UTC offset over USB."""
import argparse
import json
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "framework" / "pc"))
from esphook_sdk import AppLink


def describe(clock):
    if not clock.get("valid"):
        return "设备时钟还没有设置"
    zone = clock.get("tz_min", 0)
    local = time.gmtime(clock["epoch"] + zone * 60)
    sign = "+" if zone >= 0 else "-"
    return "设备时间 %s（UTC%s%02d:%02d）" % (time.strftime("%Y-%m-%d %H:%M:%S", local), sign, abs(zone) // 60, abs(zone) % 60)


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--port", help="USB serial device (auto-detected when one ESP32 is connected)")
    p.add_argument("--show", action="store_true", help="only read the device clock")
    p.add_argument("--epoch", type=int, help="UTC seconds to set instead of this computer's time")
    p.add_argument("--tz-min", type=int, help="local offset from UTC in minutes instead of this computer's")
    p.add_argument("--dry-run", action="store_true", help="print the request without a device")
    args = p.parse_args(argv)
    epoch = int(time.time()) if args.epoch is None else args.epoch
    zone = (time.localtime(epoch).tm_gmtoff or 0) // 60 if args.tz_min is None else args.tz_min
    if args.dry_run:
        print(json.dumps(dict(op="time", epoch=epoch, tz_min=zone)))
        return 0
    link = AppLink(args.port)
    try:
        print("设备原来：" + describe(link.clock()))
        if args.show:
            return 0
        print("已设置：" + describe(link.set_time(epoch, zone)))
    finally:
        link.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
