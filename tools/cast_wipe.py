#!/usr/bin/env python3
"""Check a model of CAST.SL's start-up wipe (0x101b0..0x10240) against screens saved by the oracle.

The wipe: the old screen (what the previous program left) is saved, then 200 steps, each:
    rem -= 320; off += 320; si -= 320; di += 320
    screen[0:rem] = saved[off:off + rem]                 (0x11712: the old picture rolls up one row)
    if si < 0x7d00: si = di = 0x7d00; screen[rem:rem + 320] = 0      (0x11630)
    screen[si:si + di] = background[0:di]                (0x11712: the new picture rolls in from below)

Needs work/cast/buf/W_old.bin (the saved screen), G_0.bin (the decoded cast1.lcz) and W_<k>.raw (screen after k
steps), made with the oracle (see findings.md). Usage: tools/cast_wipe.py [dir]"""
import sys
from pathlib import Path

D = Path(sys.argv[1] if len(sys.argv) > 1 else "work/cast/buf")
KS = [0, 1, 2, 3, 50, 99, 100, 101, 102, 150, 199, 200]

saved = (D / "W_old.bin").read_bytes()
bg = (D / "G_0.bin").read_bytes()
screen = bytearray(saved)       # the screen still holds the old picture when the wipe starts
rem, off, si, di = 64000, 0, 64000, 0
states = {0: bytes(screen)}
for k in range(1, 201):
    rem -= 320
    off += 320
    si -= 320
    di += 320
    screen[0:rem] = saved[off:off + rem]
    if si < 0x7d00:
        si = di = 0x7d00
        screen[rem:rem + 320] = bytes(320)
    screen[si:si + di] = bg[0:di]
    states[k] = bytes(screen)

good = 0
for k in KS:
    ref = (D / f"W_{k}.raw").read_bytes()[:64000]
    if states[k] == ref:
        good += 1
    else:
        d = [i for i in range(64000) if states[k][i] != ref[i]]
        print(f"step {k}: {len(d)} pixels differ, rows {d[0] // 320}..{d[-1] // 320}")
print(f"wipe: {good} of {len(KS)} screens identical")
