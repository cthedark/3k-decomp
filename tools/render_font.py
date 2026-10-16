#!/usr/bin/env python3
"""Render Chinese text using the game's own bitmap font, NUMBER.LCZ.

Format (reverse-engineered, see docs/findings.md):
  - 16-byte header (contents unknown), then 30-byte glyphs.
  - Each glyph is 16 pixels wide x 15 rows, 2 bytes per row, MSB = leftmost.
  - Glyph index for a Big5 character (lead, trail):
        index = (lead - 0xA1) * 157 + trail_index + 33
    where trail_index = trail - 0x40 for 0x40..0x7E, and
          trail_index = trail - 0xA1 + 63 for 0xA1..0xFE.

Usage: tools/render_font.py "交談觀看" out.png [--scale N] [--font PATH]
Needs only the Python standard library. The font file is NOT in this repo;
it is read from original_bin/NUMBER.LCZ by default.
"""
import argparse
import struct
import sys
import zlib
from pathlib import Path

HEADER = 16
GLYPH_BYTES = 30
GLYPH_W, GLYPH_H = 16, 15
INDEX_OFFSET = 33  # empirical: constant across all characters checked


def glyph_index(ch: str) -> int:
    b = ch.encode("big5")
    if len(b) != 2:
        raise ValueError(f"{ch!r} is not a double-byte Big5 character")
    lead, trail = b
    t = trail - 0x40 if trail <= 0x7E else trail - 0xA1 + 63
    return (lead - 0xA1) * 157 + t + INDEX_OFFSET


def glyph_rows(font: bytes, index: int):
    off = HEADER + index * GLYPH_BYTES
    if off < 0 or off + GLYPH_BYTES > len(font):
        raise IndexError(f"glyph index {index} is outside the font file")
    return [(font[off + 2 * y] << 8) | font[off + 2 * y + 1] for y in range(GLYPH_H)]


def write_png(path: Path, width: int, height: int, gray: bytes) -> None:
    def chunk(tag: bytes, data: bytes) -> bytes:
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))

    raw = b"".join(b"\x00" + gray[y * width:(y + 1) * width] for y in range(height))
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 0, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9))
    png += chunk(b"IEND", b"")
    path.write_bytes(png)


def main() -> int:
    root = Path(__file__).resolve().parent.parent
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("text")
    ap.add_argument("out")
    ap.add_argument("--scale", type=int, default=4)
    ap.add_argument("--font", default=str(root / "original_bin" / "NUMBER.LCZ"))
    args = ap.parse_args()

    font = Path(args.font).read_bytes()
    chars = [c for c in args.text if c != "\n"]
    cell_w, cell_h, s = GLYPH_W + 1, GLYPH_H + 1, args.scale
    width, height = cell_w * len(chars) * s, cell_h * s
    gray = bytearray([255]) * (width * height)
    for i, ch in enumerate(chars):
        if ch == " ":
            continue
        try:
            rows = glyph_rows(font, glyph_index(ch))
        except (ValueError, IndexError, UnicodeEncodeError) as e:
            print(f"skip {ch!r}: {e}", file=sys.stderr)
            continue
        for y, bits in enumerate(rows):
            for x in range(GLYPH_W):
                if bits & (0x8000 >> x):
                    for dy in range(s):
                        base = (y * s + dy) * width + (i * cell_w + x) * s
                        for dx in range(s):
                            gray[base + dx] = 0
    write_png(Path(args.out), width, height, bytes(gray))
    print(f"wrote {args.out} ({width}x{height}, {len(chars)} characters)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
