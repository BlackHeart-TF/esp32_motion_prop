#!/usr/bin/env bash
# Convert audio in a folder to 44.1 kHz, 16-bit, mono wavs in that same folder.
# The second argument is the filename prefix: scream -> scream01.wav
#   tools/convert_clips.sh raw/screams scream
set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "usage: convert_clips.sh FOLDER PREFIX" >&2
    exit 1
fi

folder=$1
prefix=$2

command -v ffmpeg >/dev/null 2>&1 || { echo "ffmpeg not found" >&2; exit 1; }
[[ -d "$folder" ]] || { echo "not a directory: $folder" >&2; exit 1; }

mapfile -d '' files < <(find "$folder" -maxdepth 1 -type f \
    \( -iname '*.wav' -o -iname '*.mp3' -o -iname '*.flac' -o -iname '*.ogg' \
       -o -iname '*.m4a' -o -iname '*.aac' -o -iname '*.aiff' -o -iname '*.aif' \
       -o -iname '*.wma' -o -iname '*.opus' \) -print0 | sort -z)

i=1
for src in "${files[@]}"; do
    [[ -n "$src" ]] || continue
    printf -v name '%s%02d.wav' "$prefix" "$i"
    echo "$name  <=  $(basename "$src")"
    ffmpeg -y -hide_banner -loglevel error -i "$src" -ac 1 -ar 44100 -c:a pcm_s16le "$folder/$name"
    i=$((i + 1))
done

if [[ $i -eq 1 ]]; then
    echo "no audio files in $folder" >&2
    exit 1
fi
if [[ $((i - 1)) -gt 64 ]]; then
    echo "warning: the firmware uses only 64 clips from each folder" >&2
fi
echo "wrote $((i - 1)) files to $folder"
