"""Install only the small flashing/USB environment, never the ESP-IDF toolchain."""
import importlib.metadata
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]

def ensure():
    requirements = dict(esptool="5.1.0", pyserial="3.5")
    try:
        if all(importlib.metadata.version(name) == version for name, version in requirements.items()):
            return
    except importlib.metadata.PackageNotFoundError:
        pass
    if os.environ.get("ESPHOOK_BOOTSTRAPPED") == "1":
        raise RuntimeError("flashing dependencies are unavailable in the prepared environment")
    if sys.version_info < (3, 10):
        raise RuntimeError("Use Python 3.10+ or run.sh/run.cmd with uv installed")
    folder = Path(os.environ.get("ESPHOOK_ENV_DIR", str(ROOT / ".esphook-venv")))
    python = folder / ("Scripts/python.exe" if os.name == "nt" else "bin/python")
    if not python.exists():
        subprocess.run([sys.executable, "-m", "venv", str(folder)], check=True)
    subprocess.run([str(python), "-m", "pip", "install", "--disable-pip-version-check", "-r",
                    str(ROOT / "tools/requirements-flash.txt")], check=True)
    environment = dict(os.environ, ESPHOOK_BOOTSTRAPPED="1")
    os.execve(str(python), [str(python), *sys.argv], environment)
