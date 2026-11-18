import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from tools.package_release import package
from tools.flash import verified

def fixture(folder):
    firmware=folder/"esphook-firmware";resources=folder/"esphook-full-flash-resources"
    for name in ("bootloader/bootloader.bin","partition_table/partition-table.bin","ota_data_initial.bin","supermini-aihook.bin"):
        path=firmware/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(b'\xe9'+b'\0'*1023)
    (resources/"main").mkdir(parents=True)
    for source,dest in (("main/fontdata.bin","main/fontdata.bin"),("partitions_aihook.csv","partitions_aihook.csv"),("dependencies.lock","dependencies.lock")):
        shutil.copy2(ROOT/source,resources/dest)
    (firmware/"flasher_args.json").write_text(json.dumps(dict(flash_files={"0x0":"bootloader/bootloader.bin","0x8000":"partition_table/partition-table.bin",
        "0xf000":"ota_data_initial.bin","0x20000":"supermini-aihook.bin","0x340000":"../main/fontdata.bin"},flash_settings=dict(flash_mode="dio",flash_size="4MB",flash_freq="80m"))))

class PackageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory();cls.root=Path(cls.temp.name);fixture(cls.root/"input")
        cls.dist=cls.root/"dist";package(cls.root/"input",cls.dist)
        for name in ("esphook-kit","esphook-sdk","esphook-full-flash"):
            with zipfile.ZipFile(cls.dist/f"{name}.zip") as archive:archive.extractall(cls.root)
    @classmethod
    def tearDownClass(cls):cls.temp.cleanup()

    def run_python(self,root,*args):
        return subprocess.run([sys.executable,*args],cwd=root,text=True,check=True,capture_output=True).stdout

    def test_all_in_one_and_developer_source(self):
        kit=self.root/"esphook-kit";sdk=self.root/"esphook-sdk"
        info=json.loads(self.run_python(kit,"run.py","doctor"));self.assertTrue(info["sdk"] and info["firmware"])
        command=json.loads(self.run_python(kit,"run.py","flash","--port","COM_TEST","--dry-run"))
        self.assertIn("esp32c3",command);self.assertIn("0x340000",command);self.assertNotIn("--erase-all",command)
        self.run_python(kit,"run.py","input","tap","--dry-run")
        self.assertEqual(json.loads(self.run_python(kit,"run.py","bridge","--dry-run"))["bindings"]["space"],0)
        self.run_python(kit,"run.py","stock","--demo","--dry-run")
        self.run_python(sdk,"scripts/new-app.py","counter","--title","计数","--pc")
        self.run_python(sdk,"scripts/check-apps.py")
        self.run_python(sdk,"apps/counter/pc/main.py","--dry-run")
        self.assertTrue((sdk/"main/CMakeLists.txt").is_file())
        self.assertTrue((sdk/".agents/skills/esphook-app/SKILL.md").is_file())
        self.assertFalse((sdk/".git").exists() or (sdk/"managed_components").exists() or (sdk/"build").exists())

    def test_flash_integrity_and_checksums(self):
        flash=self.root/"full-flash"
        self.run_python(flash,"flash.py","--dry-run")
        for line in (self.dist/"SHA256SUMS").read_text().splitlines():
            digest,name=line.split("  ",1);self.assertEqual(digest,hashlib.sha256((self.dist/name).read_bytes()).hexdigest())
        with tempfile.TemporaryDirectory() as tmp:
            copy=Path(tmp)/"flash";shutil.copytree(flash,copy)
            (copy/"bootloader.bin").write_bytes(b"corrupted")
            with self.assertRaises(ValueError):verified(copy)
        with tempfile.TemporaryDirectory() as tmp:
            copy=Path(tmp)/"flash";shutil.copytree(flash,copy)
            path=copy/"flash-manifest.json";manifest=json.loads(path.read_text());manifest["files"][1]["offset"]="0x0";path.write_text(json.dumps(manifest))
            with self.assertRaises(ValueError):verified(copy)

if __name__=="__main__":unittest.main()
