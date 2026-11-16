#!/usr/bin/env python3
"""Produce verified flash, PC, developer and all-in-one release bundles."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import zipfile
ROOT=Path(__file__).resolve().parents[1]
SOURCE_PATHS=("main","framework","apps","cmake",".agents","scripts","tests","docs","bin","host","tools",
    "AGENTS.md","README.md","CMakeLists.txt","sdkconfig.defaults","partitions_aihook.csv","dependencies.lock",
    "version.txt","install.sh","run.py","run.sh","run.cmd",".gitignore")

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def copy_source(destination):
    for name in SOURCE_PATHS:
        source=ROOT/name;target=destination/name
        if source.is_dir():shutil.copytree(source,target,ignore=shutil.ignore_patterns("__pycache__","*.pyc",".esphook-venv"))
        else:target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,target)
    shutil.copy2(ROOT/"docs/release-kit.md",destination/"START_HERE.md")

def zip_tree(source,target):
    with zipfile.ZipFile(target,"w",zipfile.ZIP_DEFLATED,compresslevel=9) as archive:
        for path in sorted(source.rglob("*")):
            if path.is_file():archive.write(path,str(Path(source.name)/path.relative_to(source)))

def prepare_flash(inputs,destination,metadata):
    firmware=inputs/"esphook-firmware";resources=inputs/"esphook-full-flash-resources"
    build=json.loads((firmware/"flasher_args.json").read_text())
    files={"bootloader.bin":firmware/"bootloader/bootloader.bin","partition-table.bin":firmware/"partition_table/partition-table.bin",
           "ota_data_initial.bin":firmware/"ota_data_initial.bin","supermini-aihook.bin":firmware/"supermini-aihook.bin",
           "fontdata.bin":resources/"main/fontdata.bin"}
    destination.mkdir(parents=True)
    entries=[]
    for offset,source in sorted(build["flash_files"].items(),key=lambda item:int(item[0],0)):
        name=Path(source).name
        if name not in files:raise ValueError(f"unsupported flash image in build manifest: {source}")
        source_path=files[name]
        target_name="esphook-firmware.bin" if name=="supermini-aihook.bin" else name
        shutil.copy2(source_path,destination/target_name)
        entries.append(dict(offset=hex(int(offset,0)),file=target_name,sha256=sha(source_path),size=source_path.stat().st_size))
    if {entry["file"] for entry in entries}!={"bootloader.bin","partition-table.bin","ota_data_initial.bin","esphook-firmware.bin","fontdata.bin"}:
        raise ValueError("full flash bundle must contain bootloader, partitions, OTA metadata, app and fonts")
    for source,name in ((resources/"partitions_aihook.csv","partitions_aihook.csv"),(resources/"dependencies.lock","dependencies.lock")):
        if source.read_bytes()!=(ROOT/name).read_bytes():raise ValueError(f"resource does not match source checkout: {name}")
        shutil.copy2(source,destination/name)
    if (resources/"main/fontdata.bin").read_bytes()!=(ROOT/"main/fontdata.bin").read_bytes():raise ValueError("font resource mismatch")
    settings=build["flash_settings"]
    manifest=dict(schema=1,chip="esp32c3",flash_size=settings["flash_size"],flash_mode=settings["flash_mode"],
                  flash_freq=settings["flash_freq"],files=entries,**metadata)
    (destination/"flash-manifest.json").write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+"\n")
    from tools.flash import verified
    verified(destination)
    (destination/"FLASH_LAYOUT.txt").write_text("ESP32-C3 full flash. Run python flash.py --port <device>.\n"
        "Default preserves NVS/settings; --erase-all explicitly resets all flash.\n\n"+
        "\n".join(f"{e['offset']:>10}  {e['file']}" for e in entries)+"\n\nApp-only .bin is for OTA. The complete bundle includes fixed fontdata.\n")

def package(inputs,dist):
    inputs=Path(inputs).resolve();dist=Path(dist).resolve();dist.mkdir(parents=True,exist_ok=True)
    try:commit=subprocess.check_output(["git","rev-parse","HEAD"],cwd=ROOT,text=True,stderr=subprocess.DEVNULL).strip()
    except (OSError,subprocess.CalledProcessError):commit="unknown"
    metadata=dict(version=(ROOT/"version.txt").read_text().strip(),source_commit=os.environ.get("GITHUB_SHA",commit),
                  profile="universal",idf="5.5.4",python="3.10+",dependencies=dict(esptool="5.1.0",pyserial="3.5"))
    with tempfile.TemporaryDirectory(prefix="esphook-release-") as temporary:
        work=Path(temporary);sdk=work/"esphook-sdk";sdk.mkdir();copy_source(sdk)
        (sdk/"release.json").write_text(json.dumps(metadata,indent=2)+"\n")
        flash=work/"full-flash";prepare_flash(inputs,flash,metadata)
        (flash/"tools").mkdir()
        for name in ("bootstrap.py","flash.py","requirements-flash.txt"):shutil.copy2(ROOT/"tools"/name,flash/"tools"/name)
        (flash/"flash.py").write_text("from tools.flash import main\nif __name__ == '__main__':\n    raise SystemExit(main())\n")
        (flash/"flash.sh").write_text('#!/usr/bin/env bash\nset -euo pipefail\ncd "$(dirname "$0")"\nexec python3 flash.py "$@"\n')
        (flash/"flash.sh").chmod(0o755)
        (flash/"flash.cmd").write_text('@echo off\ncd /d "%~dp0"\npy -3 flash.py %*\n')
        (flash/"START_HERE.md").write_text("# USB flashing\n\nPython 3.10+ is required. Run `python flash.py --dry-run` to verify images, then `python flash.py --port COM4` (Windows) or `/dev/cu.usbmodem...` (macOS).\n"
            "First actual use prepares a small isolated esptool/pyserial environment using pip and Internet. ESP-IDF is not needed.\n"
            "Do not run a serial monitor or PC game on the same port during flashing. Full kit and SDK are separate release assets.\n")
        kit=work/"esphook-kit";shutil.copytree(sdk,kit);shutil.copytree(flash,kit/"firmware")
        zip_tree(sdk,dist/"esphook-sdk.zip");zip_tree(kit,dist/"esphook-kit.zip");zip_tree(flash,dist/"esphook-full-flash.zip")
        with tarfile.open(dist/"esphook-host.tar.gz","w:gz") as archive:
            for path in sorted(sdk.iterdir()):archive.add(path,arcname=path.name)
        shutil.copy2(flash/"esphook-firmware.bin",dist/"esphook-firmware.bin")
    (dist/"release.json").write_text(json.dumps(metadata,indent=2)+"\n")
    names=("esphook-kit.zip","esphook-sdk.zip","esphook-full-flash.zip","esphook-host.tar.gz","esphook-firmware.bin","release.json")
    (dist/"SHA256SUMS").write_text("".join(f"{sha(dist/name)}  {name}\n" for name in names))
    (dist/"RELEASE_NOTES.md").write_text(f"esphook universal {metadata['version']}\n\nCommit: `{metadata['source_commit']}`\n\n"
        "- **esphook-kit.zip**: full firmware, one-entry USB flasher, PC apps, SDK source and project skills. Start with START_HERE.md.\n"
        "- **esphook-full-flash.zip**: bootloader/partitions/OTA data/app/fonts and a small USB flashing environment.\n"
        "- **esphook-sdk.zip**: minimal development source, app scaffolding, shared interfaces, tests and skills; no bundled ESP-IDF toolchain.\n"
        "- **esphook-host.tar.gz**: compatible PC/install archive. **esphook-firmware.bin**: app-only OTA image.\n"
        "- Python 3.10+; first real USB use installs pinned esptool/pyserial into an isolated environment and needs Internet. Building apps needs ESP-IDF 5.5.4 or its container.\n"
        "- SDK USB virtual keys share the same gestures and support persistent physical/USB/reply-word mappings. Default auto profile preserves legacy keys and never initializes servo PWM; select robot and reboot explicitly.\n"
        "- Settings report detected radar, unknown screen/servo connections and saved hardware profiles; optional peripherals can be absent.\n"
        "- CI verifies both board builds, app lifecycle/rendering, input and packaging; actual screen frame rate and hardware behavior still need device testing.\n")
    return [dist/name for name in (*names,"SHA256SUMS")]

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument("inputs",type=Path);p.add_argument("dist",type=Path);a=p.parse_args()
    for path in package(a.inputs,a.dist):print(path)
if __name__=="__main__":main()
