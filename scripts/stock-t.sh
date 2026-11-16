#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
# Metadata checks never open USB and need no dependency installation.
for stock_arg in "$@"; do
    if [[ "$stock_arg" == --dry-run || "$stock_arg" == --help || "$stock_arg" == -h ]]; then
        exec python3 apps/stock/pc/stock_t.py "$@"
    fi
done
# Existing uv users can run the game with its only optional USB dependency.
if command -v uv >/dev/null 2>&1; then
    exec uv run --with pyserial python apps/stock/pc/stock_t.py "$@"
fi
exec python3 apps/stock/pc/stock_t.py "$@"
