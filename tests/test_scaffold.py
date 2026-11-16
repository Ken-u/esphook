import importlib.util
from pathlib import Path
import subprocess
import tempfile
import shutil
import unittest
ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("new_app", ROOT / "scripts/new-app.py")
scaffold = importlib.util.module_from_spec(spec)
spec.loader.exec_module(scaffold)

class ScaffoldTests(unittest.TestCase):
    def test_create_and_reject_overwrite_and_traversal(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "framework/include/esphook").mkdir(parents=True)
            (root / "framework/include/esphook/app.h").touch()
            shutil.copytree(ROOT / "framework/pc", root / "framework/pc", ignore=shutil.ignore_patterns("__pycache__"))
            app = scaffold.create(root, "counter", "计数", pc=True)
            self.assertTrue((app / "pc/main.py").is_file())
            self.assertIn('esphook_app_counter', (app / "firmware/app.c").read_text())
            subprocess.run(["python3", "-m", "py_compile", str(app / "pc/main.py")], check=True)
            subprocess.run(["python3", str(app / "pc/main.py"), "--dry-run"], check=True, capture_output=True)
            with self.assertRaises(ValueError): scaffold.create(root, "counter", "计数")
            for invalid in ("../escape", "UPPER", "a-b", "a"*24):
                with self.assertRaises(ValueError): scaffold.create(root, invalid, "Test")
            with self.assertRaises(ValueError): scaffold.create(root, "long", "标题超出四个汉字")

if __name__ == "__main__": unittest.main()
