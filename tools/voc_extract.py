#!/usr/bin/env python3
"""Split the game's STORYn_V voice files into WAV files.

Each STORYn_V file is several Creative Voice File (VOC) clips concatenated
back to back. A clip starts with the 19-byte signature "Creative Voice File".
Observed layout (see docs/formats.md):
  - 26-byte VOC header, then a type-1 sound-data block (8-bit unsigned PCM,
    mono, codec 0), then a type-0 terminator byte.
  - The sample rate is 1,000,000 / (256 - divisor byte of the type-1 block).
    The clips checked so far use divisor 180 (0xB4), which is 13,158 Hz. The
    tool reads it from each clip rather than assuming it.

Usage: tools/voc_extract.py IN_FILE OUT_DIR
Example: tools/voc_extract.py original_bin/STORY1_V work/audio/STORY1
Standard library only. Output is derived from game data: keep it under work/.
"""
import struct
import sys
import wave
from pathlib import Path

SIG = b"Creative Voice File"


def split_clips(data: bytes):
    offs, i = [], 0
    while True:
        i = data.find(SIG, i)
        if i < 0:
            break
        offs.append(i)
        i += 1
    ends = offs[1:] + [len(data)]
    return list(zip(offs, ends))


def decode_clip(clip: bytes):
    """Return (rate_hz, pcm_bytes) from one VOC clip (type 1/2 blocks only)."""
    hdr_len = struct.unpack("<H", clip[20:22])[0]
    p, rate, pcm = hdr_len, None, bytearray()
    while p < len(clip):
        t = clip[p]
        if t == 0:
            break
        size = clip[p + 1] | clip[p + 2] << 8 | clip[p + 3] << 16
        body = clip[p + 4:p + 4 + size]
        if t == 1:
            divisor, codec = body[0], body[1]
            if codec != 0:
                raise ValueError(f"unsupported VOC codec {codec}")
            rate = round(1_000_000 / (256 - divisor))
            pcm += body[2:]
        elif t == 2:
            pcm += body
        else:
            raise ValueError(f"unhandled VOC block type {t}")
        p += 4 + size
    if rate is None:
        raise ValueError("no sound-data block")
    return rate, bytes(pcm)


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    src, out = Path(sys.argv[1]), Path(sys.argv[2])
    out.mkdir(parents=True, exist_ok=True)
    data = src.read_bytes()
    clips = split_clips(data)
    total = 0.0
    for n, (a, b) in enumerate(clips, 1):
        try:
            rate, pcm = decode_clip(data[a:b])
        except ValueError as e:
            print(f"{src.name} clip {n}: skipped ({e})")
            continue
        # WAV 8-bit PCM is unsigned, the same as VOC, so no conversion needed.
        path = out / f"{src.name}_{n:02d}.wav"
        with wave.open(str(path), "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(1)
            w.setframerate(rate)
            w.writeframes(pcm)
        secs = len(pcm) / rate
        total += secs
        print(f"{path.name}: {rate} Hz, {len(pcm)} samples, {secs:.2f} s")
    print(f"{len(clips)} clips, {total:.1f} s total")
    return 0


if __name__ == "__main__":
    sys.exit(main())
