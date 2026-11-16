#!/usr/bin/env bash
set -u

PROJECT_DIR="${ESPHOOK_PROJECT_DIR:-$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)}"
exec python3 "$PROJECT_DIR/apps/hook/pc/install_hooks.py" "$@"
