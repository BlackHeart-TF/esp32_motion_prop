#!/usr/bin/env bash
# Flash a merged image from tools/prepare_image.sh. esptool finds the port when it is left out.
#   tools/flash_board.sh build/coffin.bin
#   tools/flash_board.sh build/coffin.bin /dev/ttyACM1
set -euo pipefail

if [[ $# -lt 1 || $# -gt 2 ]]; then
    echo "usage: flash_board.sh FILE [PORT]" >&2
    exit 1
fi

file=$1
[[ -f "$file" ]] || { echo "no such file: $file" >&2; exit 1; }

port=()
[[ $# -eq 2 ]] && port=(-p "$2")

if python3 -m esptool version >/dev/null 2>&1; then
    py=python3
elif python -m esptool version >/dev/null 2>&1; then
    py=python
else
    echo "esptool not found. Install it, or source ESP-IDF: . ~/esp/esp-idf/export.sh" >&2
    exit 1
fi

exec "$py" -m esptool --chip esp32s3 "${port[@]}" write_flash 0x0 "$file"
