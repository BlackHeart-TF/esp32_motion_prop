#!/usr/bin/env bash
# Merge a built firmware image into one file another machine can flash.
# Run after idf.py build:
#   tools/prepare_image.sh
#   tools/prepare_image.sh dist/coffin.bin
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
build=$root/build
args=$build/flasher_args.json
out=${1:-$build/coffin.bin}

[[ -f "$args" ]] || { echo "no build yet. From the project folder: idf.py build" >&2; exit 1; }

if python3 -m esptool version >/dev/null 2>&1; then
    py=(python3)
elif python -m esptool version >/dev/null 2>&1; then
    py=(python)
else
    echo "esptool not found. Install it, or source ESP-IDF: . ~/esp/esp-idf/export.sh" >&2
    exit 1
fi

mapfile -t fields < <("${py[@]}" - "$args" <<'PY'
import json, sys
data = json.load(open(sys.argv[1]))
settings = data["flash_settings"]
print(data["extra_esptool_args"]["chip"])
print(settings["flash_mode"])
print(settings["flash_freq"])
print(settings["flash_size"])
files = sorted(data["flash_files"].items(), key=lambda item: int(item[0], 16))
for offset, name in files:
    print(offset)
    print(name)
PY
)

chip=${fields[0]}
mode=${fields[1]}
freq=${fields[2]}
size=${fields[3]}

pairs=()
i=4
while [[ $i -lt ${#fields[@]} ]]; do
    offset=${fields[$i]}
    name=${fields[$((i + 1))]}
    [[ -f "$build/$name" ]] || { echo "missing $build/$name" >&2; exit 1; }
    pairs+=("$offset" "$build/$name")
    i=$((i + 2))
done

mkdir -p "$(dirname "$out")"

help=$("${py[@]}" -m esptool merge-bin --help 2>&1 || true)
if [[ "$help" == *"--flash-mode"* ]]; then
    "${py[@]}" -m esptool --chip "$chip" merge-bin -o "$out" \
        --flash-mode "$mode" --flash-freq "$freq" --flash-size "$size" \
        "${pairs[@]}"
else
    "${py[@]}" -m esptool --chip "$chip" merge_bin -o "$out" \
        --flash_mode "$mode" --flash_freq "$freq" --flash_size "$size" \
        "${pairs[@]}"
fi

echo "wrote $out"
echo "flash it with:"
echo "  tools/flash_board.sh $out"
