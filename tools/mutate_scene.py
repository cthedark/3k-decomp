#!/usr/bin/env python3
"""Negative controls for tools/walk_scene.py: apply one deliberate bug at a time to a COPY of the model and check that
`scenes` or `fuzz` reports a difference.

  work/venv/bin/python tools/mutate_scene.py [FUZZ_N] [SEED]

The copy is tools/_walk_scene_mut.py (removed at the end).  A mutation that is not caught is printed as MISSED.
"""
import subprocess
import sys
from pathlib import Path

SRC = Path(__file__).parent / "walk_scene.py"
COPY = Path(__file__).parent / "_walk_scene_mut.py"
PY = sys.executable

MUTATIONS = [
    ("tile-set index uses 9 not 10", "ent = 0x109 + 10 * h[0]", "ent = 0x109 + 9 * h[0]"),
    ("bank number threshold 0x14", "n = h[1] - 5 if h[1] > 0x13 else h[1]", "n = h[1] - 5 if h[1] > 0x14 else h[1]"),
    ("bank number not reduced", "n = h[1] - 5 if h[1] > 0x13 else h[1]", "n = h[1]"),
    ("object map plane offset", "d[25700:25700 + 12800]", "d[25701:25701 + 12800]"),
    ("kind 2 object write is one byte", "put16(s.omap, (yy << 7) + (xx >> 1), R(7) | (R(8) << 8))",
     "put8(s.omap, (yy << 7) + (xx >> 1), R(7))"),
    ("kind 2 omap x not halved", "(yy << 7) + (xx >> 1)", "(yy << 7) + xx"),
    ("kind 2 ground second word dropped", "                put16(s.gmap, (yy << 8) + xx + 2, R(9) | (R(10) << 8))\n", ""),
    ("kind 2 ignores r[13]", "if kind == 0x02 and R(13) == 0:", "if kind == 0x02:"),
    ("kind 2 tests r[5] not r[6]", "            if R(6) != 0:\n                put16(s.omap", "            if R(5) != 0:\n                put16(s.omap"),
    ("kind 0xa second flag is r[11]", "if flags(R(13), R(14)) and W(9) != 0:", "if flags(R(11), R(12)) and W(9) != 0:"),
    ("kind 0xa second target r[11]", "put16(s.gmap, W(9), W(11))", "put16(s.gmap, W(9), W(13))"),
    ("kind 0xc x/y swapped", "put8(s.omap, (R(4) << 7) + R(3), R(5))", "put8(s.omap, (R(3) << 7) + R(4), R(5))"),
    ("kind 0xc first flag r[6]", "if flags(R(6), R(7)):\n                put8", "if flags(R(7), R(6)):\n                put8"),
    ("kind 0xc second entry skipped", "if flags(R(11), R(12)) and W(8) != 0:\n                put8(s.omap, (R(9) << 7) + R(8), R(10))",
     "if False:\n                put8(s.omap, (R(9) << 7) + R(8), R(10))"),
    ("kind 0xd target r[5] swapped with r[7]", "put16(ta, W(5), W(7))", "put16(ta, W(7), W(5))"),
    ("kind 0xb member status limit 11", "if tb[di + 3] > 10:", "if tb[di + 3] > 11:"),
    ("kind 0xb continues past a dead member", "break                       # 0x11430", "continue                    # 0x11430"),
    ("kind 0xb item range starts at +0x13", "range(di + 0x14, di + 0x28)", "range(di + 0x13, di + 0x28)"),
    ("kind 0xb item range stops at +0x27", "range(di + 0x14, di + 0x28)", "range(di + 0x14, di + 0x27)"),
    ("kind 0xf stops at > 0x226 -> >= 0x226", "if v > 0x226:\n                        break", "if v >= 0x226:\n                        break"),
    ("kind 0xf list limit", "wv == v or wv > 0x226", "wv == v or wv > 0x225"),
    ("kind 0xf ignores flag", "if kind == 0x0F:\n            if flags(R(3), R(4)):", "if kind == 0x0F:\n            if True:"),
    ("flag bit order", "return (tb[200 + idx] >> (n - 1)) & 1 if n <= 8 else 0", "return (tb[200 + idx] >> (8 - n)) & 1 if n <= 8 else 0"),
    ("flag byte base 201", "(tb[200 + idx] >> (n - 1)) & 1", "(tb[201 + idx] >> (n - 1)) & 1"),
    ("set-flag ignores bit", "tb[200 + idx] |= (1 << (bit & 31)) & 0xFF if bit != 0 else 1", "tb[200 + idx] |= 1"),
    ("hero x from byte 502 -> 500", "x, y = tb[512], (tb[513] << 1) & 0xFF", "x, y = tb[500], (tb[513] << 1) & 0xFF"),
    ("hero y not doubled", "(tb[513] << 1) & 0xFF", "tb[513]"),
    ("trail count", "for _ in range(0, 0x27, 3):", "for _ in range(0, 0x24, 3):"),
    ("villager speed rule threshold", "if s.m < 2:", "if s.m < 1:"),
    ("villager byte 9 kept", "            ta[o + 9] = 0\n", ""),
    ("villager clamp floor", "ta[o + 8] - k if ta[o + 8] > k else 1", "ta[o + 8] - k if ta[o + 8] > k else 0"),
    ("villager copy index", "ta[STATE + i] = ta[8 + VILL * i]", "ta[STATE + i] = ta[9 + VILL * i]"),
    ("scene block start", "now_file[s.scene * STATE:(s.scene + 1) * STATE]", "now_file[s.scene * STATE + 1:(s.scene + 1) * STATE + 1]"),
    ("scene byte 510 not copied from 500", "tb[510:514] = tb[500:504]", "tb[510:514] = tb[501:505]"),
    ("ground plane order", "gm[0::2], gm[1::2] = d[100:100 + 12800], d[100 + 12800:100 + 25600]",
     "gm[1::2], gm[0::2] = d[100:100 + 12800], d[100 + 12800:100 + 25600]"),
    ("list tail offset", "d[38740:38840]", "d[38742:38842]"),
]


def run(args):
    r = subprocess.run([PY, str(COPY)] + args, capture_output=True, text=True, cwd=Path(__file__).parent.parent)
    return r.returncode, r.stdout + r.stderr


def main():
    n = sys.argv[1] if len(sys.argv) > 1 else "60"
    seed = sys.argv[2] if len(sys.argv) > 2 else "3"
    only = sys.argv[3:]           # optional: only mutations whose name contains one of these words
    base = SRC.read_text()
    missed = 0
    try:
        for name, old, new in MUTATIONS:
            if only and not any(w in name for w in only):
                continue
            if base.count(old) != 1:
                print(f"BAD PATTERN ({base.count(old)} matches): {name}")
                missed += 1
                continue
            COPY.write_text(base.replace(old, new))
            rc1, _ = run(["scenes"])
            if rc1 == 0:
                rc2, _ = run(["fuzz", n, seed])
                caught = "fuzz" if rc2 != 0 else None
            else:
                caught = "scenes"
            if caught:
                print(f"caught by {caught:6s}: {name}")
            else:
                print(f"MISSED: {name}")
                missed += 1
    finally:
        if COPY.exists():
            COPY.unlink()
    total = len([m for m in MUTATIONS if not only or any(w in m[0] for w in only)])
    print(f"{total - missed} of {total} caught")
    return 1 if missed else 0


if __name__ == "__main__":
    sys.exit(main())
