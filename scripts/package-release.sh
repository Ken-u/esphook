#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
exec python3 -m tools.package_release "${1:-release-input}" "${2:-dist}"
