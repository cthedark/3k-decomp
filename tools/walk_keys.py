#!/usr/bin/env python3
"""Key scripts for tools/walk_idle_trace.py: prints '--key PASS:CODE ...' arguments.

Usage: tools/walk_keys.py fuzz SEED NPASSES    random presses and releases of the four arrow keys (also the break code of a key that is not
                                               the held one, the 0xe0 prefix and break codes of other keys, and bumps into walls)
       tools/walk_keys.py door SEED NPASSES    (SEED and NPASSES are ignored) right to x = 38, up through the house door to the top of the
                                               corridor and down again: the roof comes off and goes back (needs about 420 passes)
       tools/walk_keys.py route SEED NPASSES   (SEED and NPASSES are ignored) a tour of the streets planned with the model (tools/walk_route.py):
                                               to (60, 46), to (86, 73) and back to (40, 46); about 470 passes
       tools/walk_keys.py yoyo SEED NPASSES    right and left in turn with short holds and now and then a vertical nudge; it keeps the hero
                                               around the house door (x 34 .. 44), so scene records are stepped on again and again
Codes: 0x48 up, 0x50 down, 0x4b left, 0x4d right; the break code is the make code + 0x80.
"""
import random
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))


def fuzz(seed, n):
    rng = random.Random(seed)
    make = [0x48, 0x50, 0x4B, 0x4D]
    t, held, out = 8, None, []
    while t < n - 40:
        r = rng.random()
        if r < 0.55:
            held = rng.choice(make)
            out.append(f"--key {t}:{held:#x}")
        elif r < 0.9 and held is not None:
            k = held if rng.random() < 0.7 else rng.choice(make)
            out.append(f"--key {t}:{k + 0x80:#x}")
            if k == held:
                held = None
        if rng.random() < 0.06:        # the extended-key prefix, or the break code of a key that is not an arrow
            out.append(f"--key {t + 1}:{rng.choice([0xE0, 0x9C, 0xB9, 0x81]):#x}")
        t += rng.randint(2, 40)
    return out


def yoyo(seed, n):
    rng = random.Random(seed)
    t, side, out = 8, 0x4D, []
    while t < n - 40:
        hold = rng.randint(2, 14)
        out.append(f"--key {t}:{side:#x}")
        out.append(f"--key {t + hold}:{side + 0x80:#x}")
        side = 0x4B if side == 0x4D else 0x4D
        if rng.random() < 0.25:
            v = rng.choice([0x48, 0x50])
            t2 = t + hold + 2
            out.append(f"--key {t2}:{v:#x}")
            out.append(f"--key {t2 + rng.randint(1, 5)}:{v + 0x80:#x}")
            t = t2 + 8
        t += hold + rng.randint(2, 12)
    return out


def door(seed, n):
    return [f"--key {p}:{c:#x}" for p, c in ((10, 0x4D), (16, 0xCD), (30, 0x48), (130, 0xC8), (150, 0x50), (260, 0xD0))]


def route(seed, n):
    import walk_route
    events, end, _m = walk_route.plan([(60, 46), (86, 73), (40, 46)])
    events[end] = events[max(events)] + 0x80
    return [f"--key {p}:{c:#x}" for p, c in sorted(events.items())]


if __name__ == "__main__":
    if len(sys.argv) != 4 or sys.argv[1] not in ("fuzz", "yoyo", "door", "route"):
        raise SystemExit(__doc__)
    print(" ".join({"fuzz": fuzz, "yoyo": yoyo, "door": door, "route": route}[sys.argv[1]](int(sys.argv[2]), int(sys.argv[3]))))
