#!/usr/bin/env python3
"""Render every picture chunk of an .LCZ file onto one contact-sheet PNG.

For each decoded chunk the tool picks an interpretation, in this order:
  1. exactly 64,000 bytes            -> 320x200, 8 bits per pixel
  2. a sequence of [w:2][h:2][w*h]   -> one image per record (8 bpp)
  3. headerless 8 bpp                -> width guessed from row similarity
     (needs --guess; a guess is only a guess, check the picture)
Chunks it cannot place are listed and skipped.

--dims CHUNK:WxH overrides the choice for one chunk: the chunk is treated as a
vertical stack of frames, each W x H pixels (8 bpp, no header). Repeat the
option for several chunks. Frame sizes of headerless chunks are not stored in
the .LCZ file (the program that loads it knows them), so these are hypotheses
to check by eye.

Usage: tools/lcz_sheet.py FILE OUT.png [--palette FILE:OFFSET] [--guess]
                          [--dims CHUNK:WxH ...]
Standard library only. Output is derived from game data: keep it under work/.
Reference: docs/formats.md (*.LCZ section).
"""
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import lcz_decode as L  # noqa: E402

SHEET_W = 960
GAP = 4
BACKGROUND = (255, 0, 255)  # magenta, so palette index 0 is not confused with the gap


def parse_sprites(buf):
    out, pos = [], 0
    while pos < len(buf):
        if pos + 4 > len(buf):
            return None
        w, h = struct.unpack("<HH", buf[pos:pos + 4])
        if w == 0 or h == 0 or w > 640 or h > 480 or pos + 4 + w * h > len(buf):
            return None
        out.append((w, h, buf[pos + 4:pos + 4 + w * h]))
        pos += 4 + w * h
    return out


def row_score(buf, w):
    n = len(buf) // w
    if n < 8:
        return 0.0
    same = 0
    for y in range(1, n):
        a, b = buf[(y - 1) * w:y * w], buf[y * w:(y + 1) * w]
        same += sum(1 for p, q in zip(a, b) if p == q)
    return same / (w * (n - 1))


def guess_width(buf):
    best = (0.0, 0)
    for w in range(16, 331):
        if len(buf) % w == 0:
            s = row_score(buf, w)
            if s > best[0]:
                best = (s, w)
    return best


def collect(path, allow_guess, dims=None):
    dims = dims or {}
    chunks, status = L.decode_chunks(path.read_bytes())
    items, notes = [], [f"{path.name}: {len(chunks)} chunks, status {status}"]
    for i, (_, _, _, buf) in enumerate(chunks):
        if i in dims:
            w, h = dims[i]
            n = len(buf) // (w * h)
            if n == 0:
                notes.append(f"  chunk {i}: {len(buf)} bytes is smaller than one {w}x{h} frame")
                continue
            extra = len(buf) - n * w * h
            kind = f"--dims {w}x{h}, frame {{k}} of {n}" + (f", {extra} bytes left over" if extra else "")
            for k in range(n):
                items.append((f"chunk {i}.{k}", w, h, buf[k * w * h:(k + 1) * w * h], kind.format(k=k + 1)))
            continue
        if len(buf) == 64000:
            items.append((f"chunk {i}", 320, 200, buf, "full screen"))
            continue
        sp = parse_sprites(buf)
        if sp:
            for k, (w, h, px) in enumerate(sp):
                items.append((f"chunk {i}.{k}", w, h, px, "sprite with header"))
            continue
        if allow_guess:
            score, w = guess_width(buf)
            if w and score >= 0.5:
                h = len(buf) // w
                items.append((f"chunk {i}", w, h, buf[:w * h], f"GUESSED width {w} (match {score:.0%})"))
                continue
        notes.append(f"  chunk {i}: {len(buf)} bytes, not placed")
    return items, notes


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    src, out = Path(argv[1]), Path(argv[2])
    spec = argv[argv.index("--palette") + 1] if "--palette" in argv else None
    pal = L.load_palette(spec)
    dims = {}
    for j, a in enumerate(argv):
        if a == "--dims":
            chunk_s, size_s = argv[j + 1].split(":")
            w_s, h_s = size_s.lower().split("x")
            dims[int(chunk_s)] = (int(w_s), int(h_s))
    items, notes = collect(src, "--guess" in argv, dims)
    if not items:
        print("\n".join(notes))
        return 1

    # layout: left to right, wrap at SHEET_W
    x = y = row_h = 0
    placed = []
    for label, w, h, px, kind in items:
        if x and x + w > SHEET_W:
            x, y, row_h = 0, y + row_h + GAP, 0
        placed.append((x, y, w, h, px))
        x += w + GAP
        row_h = max(row_h, h)
    height = y + row_h
    canvas = bytearray(bytes(BACKGROUND) * (SHEET_W * height))
    for (x0, y0, w, h, px) in placed:
        for yy in range(h):
            base = ((y0 + yy) * SHEET_W + x0) * 3
            for xx in range(w):
                canvas[base + xx * 3:base + xx * 3 + 3] = bytes(pal[px[yy * w + xx]])
    out.parent.mkdir(parents=True, exist_ok=True)
    L.write_png(out, SHEET_W, height, bytes(canvas))
    print("\n".join(notes))
    for n, (label, w, h, px, kind) in enumerate(items):
        print(f"  item {n}: {label}: {w}x{h} [{kind}] at ({placed[n][0]},{placed[n][1]})")
    print(f"wrote {out} ({SHEET_W}x{height})")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
