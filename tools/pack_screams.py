#!/usr/bin/env python3
"""Pack screams/*.wav into one flash image.

Each clip must be 44100 Hz, 16-bit, mono PCM. The image starts with a
small table the firmware uses to pick a clip.
"""

import os
import struct
import sys
import wave

PARTITION_BYTES = 0x200000


def main() -> None:
    if len(sys.argv) != 3:
        sys.exit("usage: pack_screams.py <screams-dir> <output.bin>")

    source_dir, dest = sys.argv[1], sys.argv[2]
    names = sorted(
        name for name in os.listdir(source_dir) if name.lower().endswith(".wav")
    )
    if not names:
        sys.exit(f"no .wav files in {source_dir}")

    clips = []
    for name in names:
        path = os.path.join(source_dir, name)
        with wave.open(path, "rb") as wav:
            if wav.getnchannels() != 1 or wav.getframerate() != 44100 or wav.getsampwidth() != 2:
                sys.exit(f"{name} must be 44100 Hz, 16-bit, mono PCM")
            if wav.getcomptype() != "NONE":
                sys.exit(f"{name} must be uncompressed PCM")
            clips.append((name, wav.readframes(wav.getnframes())))

    header_bytes = 8 + len(clips) * 8
    offset = header_bytes
    entries = []
    pcm = bytearray()
    for name, data in clips:
        entries.append(struct.pack("<II", offset, len(data)))
        pcm.extend(data)
        seconds = len(data) / 2 / 44100
        print(f"  {name}: {seconds:.2f}s")
        offset += len(data)

    blob = struct.pack("<4sI", b"SCRM", len(clips)) + b"".join(entries) + pcm
    if len(blob) > PARTITION_BYTES:
        sys.exit(
            f"packed audio is {len(blob)} bytes and the screams partition is {PARTITION_BYTES}"
        )

    os.makedirs(os.path.dirname(os.path.abspath(dest)), exist_ok=True)
    with open(dest, "wb") as out:
        out.write(blob)
    print(f"packed {len(clips)} screams, {len(blob)} bytes")


if __name__ == "__main__":
    main()
