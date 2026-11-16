#!/usr/bin/env python3
"""Keep pre-migration Agent hook registrations working after updating the repo."""
import runpy
import sys
from pathlib import Path

script = Path(__file__).resolve().parents[2] / "apps/hook/pc/hooks/esphook_notify_hook.py"
sys.path.insert(0, str(script.parent))
if __name__ == "__main__":
    runpy.run_path(str(script), run_name="__main__")
