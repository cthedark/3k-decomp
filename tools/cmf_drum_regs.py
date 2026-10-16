#!/usr/bin/env python3
"""Show the original FM driver's OPL writes for the rhythm channels of a CMF song (see the usage below)."""
import sys, collections
sys.path.insert(0, "tools")
from cmf_survey import parse, events
# usage: tools/cmf_drum_regs.py MUC.ALL OFFSET LENGTH OPL_LOG [COUNT]
# For the first COUNT note events on the rhythm channels (MIDI 11, 13, 15) of the song at OFFSET, print the OPL
# writes the original's driver made at the same song tick (OPL_LOG from `dosemu.py --opl-log`): registers 0xbd and
# the ones of voices 6..8. The log's interrupt count is the song tick: the driver's timer runs at 96 Hz.
path, off, length, logf = sys.argv[1], int(sys.argv[2], 0), int(sys.argv[3], 0), sys.argv[4]
count = int(sys.argv[5]) if len(sys.argv) > 5 else 26
d = open(path, "rb").read()[off:off + length]
s = parse(d)
ev = list(events(s["body"]))
w = [tuple(int(x, 16) if i else int(x) for i, x in enumerate(l.split())) for l in open(logf)]
first = next(t for t, r, v in w if 0xb0 <= r <= 0xb8 and v & 0x20)
by_tick = collections.defaultdict(list)
for t, r, v in w:
    by_tick[t - first].append((r, v))
def voice_of(r):
    if r in (0xa6, 0xb6, 0xc6): return 6
    if r in (0xa7, 0xb7, 0xc7): return 7
    if r in (0xa8, 0xb8, 0xc8): return 8
    if 0x20 <= r < 0x100 and r not in (0xa0,):
        o = r & 0x1f
        return {0x10: 6, 0x13: 6, 0x11: 7, 0x14: 7, 0x12: 8, 0x15: 8}.get(o)
    return None
drum = [(t, a) for t, k, a in ev if k in ("on", "off") and a[0] in (11, 12, 13, 14, 15)]
print("drum events:", len(drum), collections.Counter((a[0], k) for t, k, a in [(t, k, a) for t, k, a in ev if k in ("on", "off") and a[0] in (11, 12, 13, 14, 15)]))
n = 0
for t, k, a in ev:
    if k in ("on", "off") and a[0] in (11, 13, 15):
        ws = [f"{r:02x}={v:02x}" for r, v in by_tick.get(t, []) if r == 0xbd or voice_of(r)]
        print(f"tick {t:5d} {k:3s} ch{a[0]} note {a[1]:3d} vel {a[2]:3d}: {' '.join(ws)}")
        n += 1
        if n >= count: break
