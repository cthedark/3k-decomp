#!/usr/bin/env python3
"""Decode the game's .LCZ picture containers and render images to PNG.

Container format (reverse-engineered, see docs/formats.md):
  A file is a sequence of chunks:
      [esc : 1 byte][size : 2 bytes little-endian][data : size - 3 bytes]
  `size` counts the 3 header bytes. `esc` is also the run marker inside the
  chunk's data. Data is run-length encoded:
      esc, count, value  -> `count` copies of `value`
      any other byte     -> that byte, literal
  Decoded chunks of exactly 64,000 bytes are full 320x200 8-bit screens.

Palette: the first 768 bytes of RPG.LCP (offset 0, no header) are 256 RGB
triplets in VGA 6-bit range (0-63). It is the interface palette: the frame and
the "Ready" screen (GMAIN.LCZ) render correctly with it. ANI.LCP also starts
with a palette at offset 0, probably for the animated slides. Use --palette
FILE:OFFSET to choose another 768-byte block (offset in decimal).

Usage:
  tools/lcz_decode.py list FILE
  tools/lcz_decode.py render FILE OUTPREFIX [--palette FILE:OFFSET]
Standard library only. Output is derived from game data: keep it under work/.
"""
import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_PALETTE = (ROOT / "original_bin" / "RPG.LCP", 0)


def decode_chunks(data: bytes):
    """Return (chunks, status). chunks = list of (file_offset, esc, size, bytes)."""
    pos, chunks = 0, []
    while pos < len(data):
        if pos + 3 > len(data):
            return chunks, f"short header at {pos}"
        esc = data[pos]
        size = struct.unpack("<H", data[pos + 1:pos + 3])[0]
        if size < 3 or pos + size > len(data):
            return chunks, f"bad chunk size {size} at {pos}"
        body = data[pos + 3:pos + size]
        out, i = bytearray(), 0
        while i < len(body):
            b = body[i]
            if b == esc:
                if i + 2 >= len(body):
                    return chunks, f"truncated escape in chunk at {pos}"
                out += bytes([body[i + 2]]) * body[i + 1]
                i += 3
            else:
                out.append(b)
                i += 1
        chunks.append((pos, esc, size, bytes(out)))
        pos += size
    return chunks, "ok"


def load_palette(spec=None):
    path, off = DEFAULT_PALETTE
    if spec:
        p, o = spec.rsplit(":", 1)
        path, off = Path(p), int(o)
    raw = Path(path).read_bytes()[off:off + 768]
    if len(raw) != 768 or max(raw) > 63:
        raise ValueError("palette block is not 768 bytes of 6-bit values")
    # Scale 6-bit (0-63) to 8-bit.
    return [tuple(min(255, raw[i * 3 + c] * 255 // 63) for c in range(3)) for i in range(256)]


def write_png(path: Path, width: int, height: int, rgb: bytes) -> None:
    def chunk(tag: bytes, data: bytes) -> bytes:
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))

    stride = width * 3
    raw = b"".join(b"\x00" + rgb[y * stride:(y + 1) * stride] for y in range(height))
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9))
    png += chunk(b"IEND", b"")
    path.write_bytes(png)


def cmd_list(path: Path) -> int:
    chunks, status = decode_chunks(path.read_bytes())
    print(f"{path.name}: {len(chunks)} chunks, status: {status}")
    for n, (off, esc, size, out) in enumerate(chunks):
        note = "  (full 320x200 screen)" if len(out) == 64000 else ""
        print(f"  chunk {n}: file offset {off}, esc 0x{esc:02x}, packed {size}, decoded {len(out)}{note}")
    return 0


def cmd_render(path: Path, prefix: str, palette_spec) -> int:
    pal = load_palette(palette_spec)
    chunks, status = decode_chunks(path.read_bytes())
    print(f"{path.name}: {len(chunks)} chunks, status: {status}")
    made = 0
    for n, (off, esc, size, out) in enumerate(chunks):
        if len(out) < 64000:
            print(f"  chunk {n}: {len(out)} bytes, not a full screen, skipped")
            continue
        screen = out[:64000]
        rgb = bytearray()
        for px in screen:
            rgb += bytes(pal[px])
        dest = Path(f"{prefix}_{n}.png")
        dest.parent.mkdir(parents=True, exist_ok=True)
        write_png(dest, 320, 200, bytes(rgb))
        extra = f" (+{len(out) - 64000} extra bytes ignored)" if len(out) > 64000 else ""
        print(f"  chunk {n}: wrote {dest}{extra}")
        made += 1
    return 0 if made else 1


def main(argv) -> int:
    if len(argv) >= 3 and argv[1] == "list":
        return cmd_list(Path(argv[2]))
    if len(argv) >= 4 and argv[1] == "render":
        spec = None
        if "--palette" in argv:
            spec = argv[argv.index("--palette") + 1]
        return cmd_render(Path(argv[2]), argv[3], spec)
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
