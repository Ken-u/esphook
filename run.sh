#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
if command -v python3 >/dev/null 2>&1 && python3 -c 'import sys; sys.exit(sys.version_info < (3,10))'; then
    exec python3 run.py "$@"
elif command -v uv >/dev/null 2>&1; then
    exec uv run --python 3.11 --with-requirements tools/requirements-flash.txt python run.py "$@"
fi
echo "Install Python 3.10+ or uv to use the release kit." >&2
exit 1
