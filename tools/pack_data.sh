#!/usr/bin/env bash
# Zip the contents of data/ into the preset pack the app downloads from a release.
#   tools/pack_data.sh
#   tools/pack_data.sh dist/preset_pack.zip
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
data=$root/data
out=${1:-$root/build/preset_pack.zip}

[[ -d "$data" ]] || { echo "not a directory: $data" >&2; exit 1; }

mkdir -p "$(dirname "$out")"
out=$(cd "$(dirname "$out")" && pwd)/$(basename "$out")
rm -f "$out"

cd "$data"
if command -v zip >/dev/null 2>&1; then
    zip -r -X -q "$out" . -x '.*' -x '*/.*'
elif command -v bsdtar >/dev/null 2>&1; then
    bsdtar --format zip --exclude '.*' -cf "$out" -- *
else
    echo "zip or bsdtar not found" >&2
    exit 1
fi
echo "wrote $out"
