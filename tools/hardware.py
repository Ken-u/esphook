"""Inspect detected/unknown hardware or select a saved runtime profile."""
import argparse
import json
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"framework/pc"))
from esphook_sdk import AppLink
def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__);p.add_argument("--port");p.add_argument("--set",choices=("auto","screen","robot"),dest="profile");p.add_argument("--reboot",action="store_true");p.add_argument("--dry-run",action="store_true");a=p.parse_args(argv)
    if a.reboot and not a.profile:p.error("--reboot requires --set")
    if a.dry_run:print(json.dumps(dict(op="hardware",profile=a.profile,reboot=a.reboot)));return 0
    link=AppLink(a.port)
    try:print(json.dumps(link.set_hardware_profile(a.profile,reboot=a.reboot) if a.profile else link.hardware(),ensure_ascii=False,indent=2))
    finally:link.close()
    return 0
if __name__=="__main__":raise SystemExit(main())
