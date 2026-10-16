#!/usr/bin/env python3
"""Check, against buffers dumped from the original (work/cast/buf, see findings.md), the model of the
text sprites of CAST.SL's main loop (0x103fa..0x106af). Starting from the buffers at the top of pass 0, the
model is run pass by pass and compared with the dumps at the other passes.

Model of one pass (SEEN in the code, then checked here):
  B[last row] = A[row 0]                  (0x1173b, 60 words = one 120-byte row)
  A scrolls up one row, B scrolls up one row   (0x10b6f)
  A[ROW_NEW] = 1-bit row of the text bitmap at byte offset s, expanded to colour 0x10 (0x10748)
  s += 15, clamped to 0x6144

Usage: tools/cast_buffers.py [dir]"""
import struct
import sys
from pathlib import Path

D = Path(sys.argv[1] if len(sys.argv) > 1 else "work/cast/buf")
PASSES = [0, 1, 2, 3, 300, 301, 302, 700, 701, 702, 1000, 1001, 1002, 1650, 1651, 1659, 1660, 1661, 1662, 1663,
          1700, 1701]
W = 120
OFF_A, OFF_B = 0, 0xfa0
HA, HB = 30, 100


def rd(kind, n):
    return (D / f"{kind}_{n}.bin").read_bytes()


SCREEN_PASSES = [1, 2, 3, 300, 301, 1000, 1001, 1660, 1661, 1700, 1701]


def rd_screen(n):
    """Screen pixels (64,000 bytes) of the oracle's snapshot at the top of pass n."""
    return (D / f"S_{n}.raw").read_bytes()[:64000]


def expand_row(bitmap, off):
    out = bytearray()
    for b in bitmap[off:off + 15]:
        for k in range(8):
            out.append(0x10 if (b >> (7 - k)) & 1 else 0)
    return bytes(out)


def sprite_rows(buf, off, h):
    return [bytearray(buf[off + 4 + r * W: off + 4 + (r + 1) * W]) for r in range(h)]


def resize_line(src, dstlen, srclen=120):
    """0x114e2: nearest-neighbour line resize, 16.16 fixed point as in the original."""
    step = (0xffff * srclen) // dstlen
    acc, pos, out = 1, 0, bytearray()
    for _ in range(dstlen):
        out.append(src[pos] if pos < len(src) else 0)
        acc += step
        if acc > 0xffff:
            acc &= 0xffff
            pos += 1
    return bytes(out)


def recolour(buf, off, count, frm, to):
    for i in range(off, off + count):
        if buf[i] == frm:
            buf[i] = to


def floor_rows():
    """The source-row choice of the floor loop (0x105b2..0x1063c): yields (row start in L, dstlen, dst offset)."""
    row, cnt, rep, sub, si = 0, 0, 0, 0, 0x68
    for di in range(0x78, 0x142, 2):
        yield row, di, si
        si += 0x13f
        cnt += 1
        if cnt > rep:
            row += 120
            cnt = 0
            sub += 1
            if sub > 3:
                sub = 0
                rep += 1


def run():
    T = rd("T", 0)
    a0 = rd("A", 0)
    A, B = sprite_rows(a0, OFF_A, HA), sprite_rows(a0, OFF_B, HB)
    F = bytearray(rd("F", 0))
    C = bytearray(12004)
    L = bytearray(0x2000)
    s = 0
    scr = bytearray(rd_screen(0))
    bg = rd("G", 0)[:32000]
    state = {}
    for p in range(0, 1702):
        # pass p
        B[-1] = bytearray(A[0])
        A[:] = A[1:] + [A[-1]]
        B[:] = B[1:] + [B[-1]]
        A[26] = bytearray(expand_row(T, s))
        s = min(s + 15, 0x6144)
        # C: copy of B (header + 11996 bytes: 6000 words = 12000 bytes), then the two colour ramps
        Bflat = struct.pack("<HH", W, HB) + b"".join(bytes(r) for r in B)
        C[:12000] = Bflat[:12000]
        off, col = 4, 0xdf
        while col > 0xd0:
            recolour(C, off, 0xf0, 0x10, col)
            off += 0xf0
            col -= 1
        off, col = 0x20d4, 0xd1
        while col < 0xe0:
            recolour(C, off, 0xf0, 0x10, col)
            off += 0xf0
            col += 1
        # L: rows 0..26 of A (3240 bytes), rows 0..15 recoloured 0xdf .. 0xd0
        L[:3240] = b"".join(bytes(r) for r in A[:27])
        off, col = 0, 0xdf
        while col > 0xcf:
            recolour(L, off, 0x78, 0x10, col)
            off += 0x78
            col -= 1
        # the floor
        for row, dstlen, dst in floor_rows():
            F[dst:dst + dstlen] = resize_line(bytes(L[row:row + 120]), dstlen)
        # the screen: C blitted opaque at x = 100, y = 0 (0x11560); the lower half is the background with the
        # floor's non-zero pixels over it (0x1173b, 0x111d2), copied to A000:0x7d00
        for y in range(HB):
            scr[y * 320 + 100: y * 320 + 100 + W] = C[4 + y * W: 4 + (y + 1) * W]
        low = bytearray(bg)
        for i in range(32000):
            if F[4 + i]:
                low[i] = F[4 + i]
        scr[32000:64000] = low
        state[p + 1] = (bytes(Bflat), bytes(C), bytes(L), bytes(F),
                        [bytes(r) for r in A], [bytes(r) for r in B], bytes(scr))
    return state


st = run()
res = {"A": [], "B": [], "C": [], "L": [], "F": []}
for n in PASSES:
    if n == 0:
        continue
    _, C, L, F, A, B, _scr = st[n]
    a, l, f = rd("A", n), rd("L", n), rd("F", n)
    if [bytes(r) for r in sprite_rows(a, OFF_A, HA)] != A:
        res["A"].append(n)
    if [bytes(r) for r in sprite_rows(a, OFF_B, HB)] != B:
        res["B"].append(n)
    if a[0x4268:0x4268 + 12000] != C[:12000]:
        res["C"].append(n)
    if l != L:
        res["L"].append(n)
    if f != F:
        res["F"].append(n)
for k, v in res.items():
    print(f"{k}: {len(PASSES) - 1 - len(v)} of {len(PASSES) - 1} dumped passes identical" + (f"; differ at {v}" if v else ""))

bad = [n for n in SCREEN_PASSES if st[n][6] != rd_screen(n)]
print(f"screen: {len(SCREEN_PASSES) - len(bad)} of {len(SCREEN_PASSES)} snapshots identical" + (f"; differ at {bad}" if bad else ""))
for n in bad[:2]:
    a, b = st[n][6], rd_screen(n)
    d = [i for i in range(64000) if a[i] != b[i]]
    print(f"  pass {n}: {len(d)} pixels differ, first at (x={d[0] % 320}, y={d[0] // 320}), rows {min(i // 320 for i in d)}..{max(i // 320 for i in d)}")
