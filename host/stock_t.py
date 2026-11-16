#!/usr/bin/env python3
"""Compatibility entry point; canonical implementation lives in apps/."""
import runpy
import sys
from pathlib import Path
script = Path(__file__).resolve().parents[1] / "apps/stock/pc/stock_t.py"
sys.path.insert(0, str(script.parent))
if __name__ == "__main__":
    runpy.run_path(str(script), run_name="__main__")
