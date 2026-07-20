#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
PROJECT_DIR="$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)"
INPUT_DIR="${1:-$PROJECT_DIR/release-input}"
DIST_DIR="${2:-$PROJECT_DIR/dist}"

if [[ ! -d "$INPUT_DIR" ]]; then
  echo "esphook: release input directory not found: $INPUT_DIR" >&2
  exit 1
fi

INPUT_DIR="$(CDPATH= cd -- "$INPUT_DIR" && pwd)"
mkdir -p "$DIST_DIR"
DIST_DIR="$(CDPATH= cd -- "$DIST_DIR" && pwd)"

require_file() {
  if [[ ! -f "$1" ]]; then
    echo "esphook: release input file not found: $1" >&2
    exit 1
  fi
}

require_file "$INPUT_DIR/esphook-firmware/bootloader/bootloader.bin"
require_file "$INPUT_DIR/esphook-firmware/partition_table/partition-table.bin"
require_file "$INPUT_DIR/esphook-firmware/ota_data_initial.bin"
require_file "$INPUT_DIR/esphook-firmware/supermini-aihook.bin"
require_file "$INPUT_DIR/esphook-full-flash-resources/main/fontdata.bin"
require_file "$INPUT_DIR/esphook-full-flash-resources/partitions_aihook.csv"
require_file "$INPUT_DIR/esphook-full-flash-resources/dependencies.lock"

command -v sha256sum >/dev/null 2>&1 || {
  echo "esphook: sha256sum is required to package a release" >&2
  exit 1
}
command -v zip >/dev/null 2>&1 || {
  echo "esphook: zip is required to package a release" >&2
  exit 1
}

rm -f \
  "$DIST_DIR/esphook-host.tar.gz" \
  "$DIST_DIR/esphook-firmware.bin" \
  "$DIST_DIR/esphook-full-flash.zip" \
  "$DIST_DIR/SHA256SUMS"

staging_dir="$(mktemp -d "${TMPDIR:-/tmp}/esphook-release-package.XXXXXX")"
trap 'rm -rf "$staging_dir"' EXIT

full_flash_dir="$staging_dir/full-flash"
mkdir -p "$full_flash_dir"
cp "$INPUT_DIR/esphook-firmware/bootloader/bootloader.bin" \
  "$full_flash_dir/bootloader.bin"
cp "$INPUT_DIR/esphook-firmware/partition_table/partition-table.bin" \
  "$full_flash_dir/partition-table.bin"
cp "$INPUT_DIR/esphook-firmware/ota_data_initial.bin" \
  "$full_flash_dir/ota_data_initial.bin"
cp "$INPUT_DIR/esphook-firmware/supermini-aihook.bin" \
  "$full_flash_dir/esphook-firmware.bin"
cp "$INPUT_DIR/esphook-full-flash-resources/main/fontdata.bin" \
  "$full_flash_dir/fontdata.bin"
cp "$INPUT_DIR/esphook-full-flash-resources/partitions_aihook.csv" \
  "$full_flash_dir/partitions_aihook.csv"
cp "$INPUT_DIR/esphook-full-flash-resources/dependencies.lock" \
  "$full_flash_dir/dependencies.lock"

cat > "$full_flash_dir/FLASH_LAYOUT.txt" <<'EOF'
esphook ESP32-C3 SuperMini full flash layout

Use an ESP32-C3-compatible esptool to write:

  0x000000  bootloader.bin
  0x008000  partition-table.bin
  0x00f000  ota_data_initial.bin
  0x020000  esphook-firmware.bin
  0x379000  fontdata.bin

The app image is also published separately as esphook-firmware.bin for
web/curl OTA. Do not upload this zip as an OTA app image.
EOF

tar --exclude='__pycache__' --exclude='*.pyc' -czf "$DIST_DIR/esphook-host.tar.gz" \
  -C "$PROJECT_DIR" \
  bin \
  host \
  scripts \
  docs/agent-hooks.md \
  docs/connection-design.md \
  docs/hardware.md \
  docs/ota.md \
  docs/quick-install.md \
  README.md \
  install.sh

cp "$INPUT_DIR/esphook-firmware/supermini-aihook.bin" \
  "$DIST_DIR/esphook-firmware.bin"
(cd "$staging_dir" && zip -qr "$DIST_DIR/esphook-full-flash.zip" full-flash)

(cd "$DIST_DIR" && sha256sum \
  esphook-host.tar.gz \
  esphook-firmware.bin \
  esphook-full-flash.zip \
  > SHA256SUMS)

echo "esphook: release assets written to $DIST_DIR"
