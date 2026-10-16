#!/usr/bin/env python3
"""Decode one LCZ chunk found inside another file (e.g. RPG.LCP) and show it.

Usage: tools/lcz_at.py FILE OFFSET LENGTH [--width W] [--png OUT.png] [--skip N]
                       [--palette FILE:OFFSET]

Reads LENGTH bytes at OFFSET (decimal or 0x hex), decodes them as the chunks of an .LCZ container
(tools/lcz_decode.py: [esc][size16][RLE data]) and prints, per chunk, the decoded size and the
first bytes. With --width the first chunk (after skipping SKIP leading bytes) is written as a PNG
of that width using the interface palette (first 768 bytes of RPG.LCP unless --palette is given).
Reads the files; writes only the PNG. Output is derived from game data: keep it under work/.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import lcz_decode as lz  # noqa: E402


def main(argv):
    args = [a for a in argv[1:] if not a.startswith("--")]
    opt = {}
    for k in ("--width", "--png", "--skip", "--palette"):
        if k in argv:
            opt[k] = argv[argv.index(k) + 1]
            args.remove(opt[k])
    if len(args) != 3:
        raise SystemExit(__doc__)
    data = Path(args[0]).read_bytes()
    off, length = int(args[1], 0), int(args[2], 0)
    blob = data[off:off + length]
    found, status = lz.decode_chunks(blob)
    chunks = [c[3] for c in found]
    print("%s @0x%x len %d: %d chunk(s), status: %s" % (args[0], off, length, len(chunks), status))
    for (pos, esc, size, body) in found:
        print("  chunk at +%d: escape 0x%02x, size %d -> %d bytes decoded; first 24: %s"
              % (pos, esc, size, len(body), body[:24].hex(" ")))
    if "--png" in opt and chunks:
        w = int(opt.get("--width", 320))
        px = chunks[0][int(opt.get("--skip", 0)):]
        h = len(px) // w
        pal = lz.load_palette(opt.get("--palette"))
        rgb = bytearray()
        for v in px[:w * h]:
            r, g, b = pal[v]
            rgb += bytes((r, g, b))
        lz.write_png(Path(opt["--png"]), w, h, bytes(rgb))
        print("wrote %s (%dx%d)" % (opt["--png"], w, h))


if __name__ == "__main__":
    main(sys.argv)
