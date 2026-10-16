#!/usr/bin/env python3
"""Contact sheet of raw 8-bit buffers (320 pixels wide), rendered with the interface palette.

Usage: tools/buf_sheet.py OUT.png BUF1.bin [BUF2.bin ...] [--width 320] [--rows 200] [--cols 2]
                          [--palette FILE:OFFSET]

Each BUF is shown as a WIDTH x ROWS picture (first WIDTH*ROWS bytes), labelled by its position in the
list (a small block of the index in the top-left corner is NOT drawn: the order on the sheet is
left-to-right, top-to-bottom, as given). The palette is the first 768 bytes of RPG.LCP unless
--palette FILE:OFFSET is given (a 6-bit palette block, e.g. ANI.LCP:0 or a dump). Reads the files;
writes only the PNG. Output is derived from game data: keep it under work/.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import lcz_decode as lz  # noqa: E402


def main(argv):
    args = [a for a in argv[1:] if not a.startswith("--")]
    opt = {}
    for k in ("--width", "--rows", "--cols", "--palette"):
        if k in argv:
            opt[k] = argv[argv.index(k) + 1]
            args.remove(opt[k])
    if len(args) < 2:
        raise SystemExit(__doc__)
    w, rows, cols = int(opt.get("--width", 320)), int(opt.get("--rows", 200)), int(opt.get("--cols", 2))
    pal = lz.load_palette(opt.get("--palette"))
    bufs = [Path(p).read_bytes()[:w * rows] for p in args[1:]]
    bufs = [b + bytes(w * rows - len(b)) for b in bufs]
    n = len(bufs)
    nrows = (n + cols - 1) // cols
    gap = 4
    W, H = cols * w + (cols - 1) * gap, nrows * rows + (nrows - 1) * gap
    rgb = bytearray(b"\x40\x40\x40" * (W * H))
    for i, b in enumerate(bufs):
        ox, oy = (i % cols) * (w + gap), (i // cols) * (rows + gap)
        for y in range(rows):
            base = ((oy + y) * W + ox) * 3
            row = b[y * w:(y + 1) * w]
            rgb[base:base + w * 3] = b"".join(bytes(pal[v]) for v in row)
    lz.write_png(Path(args[0]), W, H, bytes(rgb))
    print("wrote %s: %d buffer(s), %dx%d" % (args[0], n, W, H))


if __name__ == "__main__":
    main(sys.argv)
