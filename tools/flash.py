"""Verified manifest-based USB flashing, no ESP-IDF installation required."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.bootstrap import ensure

def verified(bundle):
    bundle = Path(bundle).resolve()
    manifest = json.loads((bundle / "flash-manifest.json").read_text(encoding="utf-8"))
    if manifest.get("schema") != 1 or manifest.get("chip") != "esp32c3" or manifest.get("flash_size") != "4MB":
        raise ValueError("unsupported flash manifest")
    if manifest.get("flash_mode") not in ("dio", "qio", "dout", "qout") or manifest.get("flash_freq") not in ("80m", "40m"):
        raise ValueError("unsupported flash settings")
    files = manifest.get("files")
    if not isinstance(files, list) or not files:
        raise ValueError("flash manifest has no images")
    result, spans = [], []
    for entry in files:
        name = entry["file"]
        if not isinstance(name, str) or not re.fullmatch(r"[A-Za-z0-9_.-]+", name):
            raise ValueError("unsafe firmware path")
        path = (bundle / name).resolve()
        if path.parent != bundle or not path.is_file():
            raise ValueError("firmware image not found inside bundle")
        data = path.read_bytes()
        if not data or type(entry.get("size")) is not int or len(data) != entry["size"] or hashlib.sha256(data).hexdigest() != entry.get("sha256"):
            raise ValueError(f"firmware checksum/size mismatch: {name}")
        offset = int(entry["offset"], 0)
        if offset < 0 or offset % 4096 or offset + len(data) > 4*1024*1024:
            raise ValueError("invalid flash address or image exceeds flash")
        if any(offset < end and offset+len(data) > start for start,end in spans):
            raise ValueError("overlapping flash images")
        spans.append((offset, offset+len(data)));result.append((hex(offset),str(path)))
    return manifest, result

def detect_port():
    from serial.tools import list_ports
    ports = [p.device for p in list_ports.comports() if p.vid == 0x303A]
    if len(ports) != 1:
        raise RuntimeError("Use --port to choose an ESP32 serial device; candidates: " + ", ".join(ports))
    return ports[0]

def command(bundle, port, baud=460800, erase_all=False):
    manifest,files = verified(bundle)
    args = [sys.executable,"-m","esptool","--chip","esp32c3","--port",port,"--baud",str(baud),
            "--before","default-reset","--after","hard-reset","write-flash",
            "--flash-mode",manifest["flash_mode"],"--flash-size","4MB","--flash-freq",manifest["flash_freq"]]
    if erase_all:
        args.append("--erase-all")
    for offset,path in files:
        args.extend((offset,path))
    return args

def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bundle", type=Path, default=ROOT/"firmware" if (ROOT/"firmware").is_dir() else ROOT)
    parser.add_argument("--port")
    parser.add_argument("--baud", type=int, choices=(115200,230400,460800,921600), default=460800)
    parser.add_argument("--erase-all", action="store_true", help="explicitly erase all flash, including saved settings")
    parser.add_argument("--dry-run", action="store_true", help="verify all images and print arguments without opening USB")
    args = parser.parse_args(argv)
    verified(args.bundle)
    if args.dry_run:
        print(json.dumps(command(args.bundle,args.port or "<auto-detect>",args.baud,args.erase_all),ensure_ascii=False))
        return 0
    ensure()
    return subprocess.call(command(args.bundle,args.port or detect_port(),args.baud,args.erase_all))

if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError,ValueError,RuntimeError,KeyError) as exc:
        print(f"flash: {exc}",file=sys.stderr)
        raise SystemExit(1)
