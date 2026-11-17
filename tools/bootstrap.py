"""Install only the small flashing/USB environment, never the ESP-IDF toolchain."""
import importlib.metadata
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]

def _installed(name, version=None):
    try:
        return importlib.metadata.version(name) == version if version else bool(importlib.metadata.version(name))
    except importlib.metadata.PackageNotFoundError:
        return False

def ensure(extra=(), optional=()):
    """`extra` and `optional` name more distributions a PC tool needs (Pillow for the screen mirror); an optional one
    that cannot be installed leaves the tool to explain what it is missing."""
    requirements = dict(esptool="5.1.0", pyserial="3.5")
    ready = all(_installed(name, version) for name, version in requirements.items()) and all(_installed(name) for name in extra)
    if ready and all(_installed(name) for name in optional):
        return
    if os.environ.get("ESPHOOK_BOOTSTRAPPED") == "1":
        if ready:  # Already tried to install the optional ones.
            return
        raise RuntimeError("flashing dependencies are unavailable in the prepared environment")
    if sys.version_info < (3, 10):
        raise RuntimeError("Use Python 3.10+ or run.sh/run.cmd with uv installed")
    folder = Path(os.environ.get("ESPHOOK_ENV_DIR", str(ROOT / ".esphook-venv")))
    python = folder / ("Scripts/python.exe" if os.name == "nt" else "bin/python")
    if not python.exists():
        subprocess.run([sys.executable, "-m", "venv", str(folder)], check=True)
    subprocess.run([str(python), "-m", "pip", "install", "--disable-pip-version-check", "-r",
                    str(ROOT / "tools/requirements-flash.txt"), *extra], check=True)
    for name in optional:
        subprocess.run([str(python), "-m", "pip", "install", "--disable-pip-version-check", name], check=False)
    environment = dict(os.environ, ESPHOOK_BOOTSTRAPPED="1")
    os.execve(str(python), [str(python), *sys.argv], environment)
