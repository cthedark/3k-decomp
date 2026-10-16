#!/usr/bin/env python3
"""Compare two raw frame dumps (64000 pixel bytes + 768 palette bytes).

Used to check the C port against the reference run of the original program
(tools/dosemu.py). Compares as the viewer would see it: the RGB each pixel resolves to.
Pixel indices and palette entries are also reported separately.

Usage: tools/compare_frames.py REFERENCE.raw CANDIDATE.raw [--png OUT.png]
Exit status 0 if the two images are identical as seen, 1 otherwise.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import lcz_decode as L  # noqa: E402


def rgb_of(raw):
    pix, pal = raw[:64000], raw[64000:64000 + 768]
    lut = [v * 255 // 63 for v in range(64)]
    out = bytearray()
    for p in pix:
        out += bytes((lut[pal[p * 3] & 63], lut[pal[p * 3 + 1] & 63], lut[pal[p * 3 + 2] & 63]))
    return bytes(out)


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    a, b = Path(argv[1]).read_bytes(), Path(argv[2]).read_bytes()
    if len(a) != 64768 or len(b) != 64768:
        print(f"size mismatch: {len(a)} vs {len(b)} (expected 64768)")
        return 2
    ra, rb = rgb_of(a), rgb_of(b)
    diff_px = [i for i in range(64000) if ra[i * 3:i * 3 + 3] != rb[i * 3:i * 3 + 3]]
    idx_diff = sum(1 for i in range(64000) if a[i] != b[i])
    pal_diff = sum(1 for i in range(768) if a[64000 + i] != b[64000 + i])
    print(f"visible pixel differences: {len(diff_px)} of 64000; index differences: {idx_diff}; palette byte differences: {pal_diff}")
    if diff_px:
        rows = sorted({i // 320 for i in diff_px})
        cols = sorted({i % 320 for i in diff_px})
        print(f"   differing rows {rows[0]}..{rows[-1]}, columns {cols[0]}..{cols[-1]}")
        first = diff_px[0]
        print(f"   first at row {first // 320}, column {first % 320}: reference index {a[first]}, candidate index {b[first]}")
    if "--png" in argv:
        out = Path(argv[argv.index("--png") + 1])
        w = 320 * 3
        img = bytearray()
        for y in range(200):
            img += ra[y * 320 * 3:(y + 1) * 320 * 3]
            img += rb[y * 320 * 3:(y + 1) * 320 * 3]
            mark = bytearray()
            for x in range(320):
                i = y * 320 + x
                mark += b"\xff\x00\x00" if ra[i * 3:i * 3 + 3] != rb[i * 3:i * 3 + 3] else b"\x00\x00\x00"
            img += mark
        L.write_png(out, w, 200, bytes(img))
        print(f"wrote {out} (reference | candidate | differences in red)")
    return 0 if not diff_px else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
