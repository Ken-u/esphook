"""USB key actions and persistent device mapping through the shared SDK."""
import argparse
import json
from pathlib import Path
import sys
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"framework/pc"))
from esphook_sdk import AppLink

def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("action",choices=("tap","hold","triple","release","keymap"))
    p.add_argument("--port")
    p.add_argument("--key",default="space")
    p.add_argument("--duration",type=int,default=550)
    p.add_argument("--config",type=Path,help="PC bindings JSON")
    p.add_argument("--set",type=Path,dest="mapping",help="device physical/usb/words mapping JSON")
    p.add_argument("--dry-run",action="store_true")
    a=p.parse_args(argv)
    if a.mapping and a.action!="keymap":p.error("--set applies only to keymap")
    if a.dry_run:
        print(json.dumps(dict(action=a.action,key=a.key,config=str(a.config) if a.config else None)))
        return 0
    link=AppLink(a.port)
    try:
        if a.config:link.set_bindings(a.config)
        key=int(a.key) if a.key in ("0","1","2") else a.key
        if a.action=="keymap":
            if a.mapping:
                mapping=json.loads(a.mapping.read_text());print(link.set_keymap(**mapping))
            else:print(link.keymap())
        elif a.action=="release":link.release_keys()
        elif a.action=="hold":link.hold(key,a.duration)
        else:getattr(link,a.action)(key)
    finally:link.close()
    return 0

if __name__=="__main__":raise SystemExit(main())
