#!/usr/bin/env python3
"""esphook release kit: flash, USB input, PC apps and environment diagnostics."""
import argparse
import json
from pathlib import Path
import runpy
import sys
ROOT=Path(__file__).resolve().parent

def main(argv=None):
    argv=sys.argv[1:] if argv is None else argv
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("command",choices=("flash","input","keys","bridge","demo","stock","hook","key-report","monitor","hardware","doctor"))
    if not argv or argv[0] in ("--help","-h"):
        p.print_help();return 0
    args=p.parse_args(argv[:1]);rest=argv[1:]
    if args.command=="doctor":
        print(json.dumps(dict(python=sys.version.split()[0],version=(ROOT/"version.txt").read_text().strip() if (ROOT/"version.txt").exists() else "unknown",
            firmware=(ROOT/"firmware/flash-manifest.json").is_file() or (ROOT/"flash-manifest.json").is_file(),
            sdk=(ROOT/"framework/include/esphook/app.h").is_file()),ensure_ascii=False))
        return 0
    if args.command=="flash":
        from tools.flash import main as flash
        return flash(rest)
    if "--help" not in rest and "-h" not in rest and "--dry-run" not in rest:
        from tools.bootstrap import ensure
        ensure()
    if args.command=="input":
        from tools.input import main as input_main
        return input_main(rest)
    if args.command=="monitor":
        from tools.monitor import main as monitor
        return monitor(rest)
    if args.command=="hardware":
        from tools.hardware import main as hardware
        return hardware(rest)
    scripts={"keys":"apps/key_report/pc/controller.py","bridge":"apps/key_report/pc/bridge.py","demo":"apps/demo/pc/main.py","stock":"apps/stock/pc/stock_t.py",
             "hook":"apps/hook/pc/esphookd.py","key-report":"apps/key_report/pc/main.py"}
    script=ROOT/scripts[args.command]
    sys.path.insert(0,str(script.parent));sys.argv=[str(script),*rest]
    runpy.run_path(str(script),run_name="__main__")
    return 0

if __name__=="__main__":
    try:raise SystemExit(main())
    except (OSError,ValueError,RuntimeError,KeyError) as exc:
        print(f"esphook: {exc}",file=sys.stderr);raise SystemExit(1)
