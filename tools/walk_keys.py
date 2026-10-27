#!/usr/bin/env python3
"""Key scripts for tools/walk_idle_trace.py: prints '--key PASS:CODE ...' arguments.

Usage: tools/walk_keys.py fuzz SEED NPASSES    random presses and releases of the four arrow keys (also the break code of a key that is not
                                               the held one, the 0xe0 prefix and break codes of other keys, and bumps into walls)
       tools/walk_keys.py shoot SEED NPASSES   fuzz plus the Space bar (0x39) held or tapped now and then: the party shoots
       tools/walk_keys.py volley SEED NPASSES  Space tapped every few passes with short turns and steps between: from above enemies, hits and respawns
       tools/walk_keys.py door SEED NPASSES    (SEED and NPASSES are ignored) right to x = 38, up through the house door to the top of the
                                               corridor and down again: the roof comes off and goes back (needs about 420 passes)
       tools/walk_keys.py route SEED NPASSES   (SEED and NPASSES are ignored) a tour of the streets planned with the model (tools/walk_route.py):
                                               to (60, 46), to (86, 73) and back to (40, 46); about 470 passes
       tools/walk_keys.py yoyo SEED NPASSES    right and left in turn with short holds and now and then a vertical nudge; it keeps the hero
                                               around the house door (x 34 .. 44), so scene records are stepped on again and again
       tools/walk_keys.py talk SEED NPASSES    short walks with the arrows, each followed by F1 (talk) one time in two, then Space or Enter
                                               every 30 to 90 passes (a message box takes a key after a release)
       tools/walk_keys.py look SEED NPASSES    the same with F2 (look) three times in four and F1 the fourth
       tools/walk_keys.py take SEED NPASSES    the same with F3 (take) three times in four and F2 the fourth
       tools/walk_keys.py cast SEED NPASSES    short walks, and F4 (cast) with keys for its picker and list, then Esc
       tools/walk_keys.py items SEED NPASSES   the same with F5 (items): use, turn the page, give
       tools/walk_keys.py equip SEED NPASSES   the same with F6 or F5, and the equipment screen
       tools/walk_keys.py use SEED NPASSES     the same with F7: coins or an item, and what follows
       tools/walk_keys.py map SEED NPASSES     the talk walks with F8 (the map) three times in four and F3 the fourth
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


def shoot(seed, n):
    """fuzz with the Space bar: held for a few passes or tapped, now and then while walking."""
    rng = random.Random(seed)
    out = fuzz(seed, n)
    t = 6
    while t < n - 20:
        out.append(f"--key {t}:0x39")
        out.append(f"--key {t + rng.randint(1, 12)}:0xb9")
        t += rng.randint(14, 60)
    by = {}
    for a in out:     # one code per pass; Space wins where both land on the same pass
        p, c = a.split()[1].split(":")
        if int(p) not in by or c in ("0x39", "0xb9"):
            by[int(p)] = c
    return [f"--key {p}:{c}" for p, c in sorted(by.items())]


def volley(seed, n):
    """Space tapped every few passes, with short turns or steps between (mostly down, then left and right): from a start above enemies
    the shots hit, destroy and respawn them."""
    rng = random.Random(seed)
    out, t = [], 6
    while t < n - 20:
        out.append(f"--key {t}:0x39")
        t += rng.randint(1, 3)
        out.append(f"--key {t}:0xb9")
        t += rng.randint(2, 8)
        if rng.random() < 0.35:
            k = rng.choice([0x50, 0x50, 0x4B, 0x4D, 0x48])
            out.append(f"--key {t}:{k:#x}")
            t += rng.randint(1, 9)
            out.append(f"--key {t}:{k + 0x80:#x}")
            t += rng.randint(1, 4)
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


def talk(seed, n, fkeys=(0x3B,)):
    rng = random.Random(seed)
    make = [0x48, 0x50, 0x4B, 0x4D]
    t, out = 8, []
    while t < n - 120:
        k = rng.choice(make)
        hold = rng.randint(1, 12)
        out += [f"--key {t}:{k:#x}", f"--key {t + hold}:{k + 0x80:#x}"]
        t += hold + rng.randint(1, 6)
        if rng.random() < 0.5:
            f = rng.choice(fkeys) if len(fkeys) > 1 else fkeys[0]   # one key: no draw, so the F1 scripts stay as they were
            out += [f"--key {t}:{f:#x}", f"--key {t + 2}:{f + 0x80:#x}"]
            t += 4
            for _ in range(rng.randint(1, 3)):
                t += rng.randint(30, 90)
                c = rng.choice([0x39, 0x1C])
                out += [f"--key {t}:{c:#x}", f"--key {t + 2}:{c + 0x80:#x}"]
            t += 4
    return out


def look(seed, n):
    return talk(seed, n, (0x3C, 0x3C, 0x3C, 0x3B))


def take(seed, n):
    return talk(seed, n, (0x3D, 0x3D, 0x3D, 0x3C))


def map_(seed, n):
    return talk(seed, n, (0x42, 0x42, 0x42, 0x3D))


def cast(seed, n, fkey=0x3E, inner=(0x48, 0x50, 0x4B, 0x4D, 0x02, 0x02, 0x4D, 0x1C), guard=False):
    """Short walks, each followed now and then by F4: the picker gets an arrow or two and Enter or Esc, the list arrows, key 1 (cast)
    and Enter for a helped member's picker, and at last Esc. With `guard` no list keys follow an Esc in the picker (they would be
    the world's)."""
    rng = random.Random(seed)
    make = [0x48, 0x50, 0x4B, 0x4D]
    t, out = 8, []

    def tap(c):
        nonlocal t
        out.extend([f"--key {t}:{c:#x}", f"--key {t + 2}:{c + 0x80:#x}"])
        t += rng.randint(4, 30)

    while t < n - 300:
        k = rng.choice(make)
        hold = rng.randint(1, 12)
        out += [f"--key {t}:{k:#x}", f"--key {t + hold}:{k + 0x80:#x}"]
        t += hold + rng.randint(1, 6)
        if rng.random() < 0.6:
            tap(fkey)
            for _ in range(rng.randint(0, 2)):
                tap(rng.choice([0x4B, 0x4D]))
            first = rng.choice([0x1C, 0x1C, 0x1C, 0x01])
            tap(first)
            if guard and first == 0x01:
                continue
            for _ in range(rng.randint(1, 6)):
                tap(rng.choice(inner))
            tap(0x01)
            tap(0x01)
    return out


def items(seed, n):
    """cast with F5: in the item list arrows, key 1 (use), key 2 (page), key 3 (give: its picker gets an arrow or Enter)"""
    return cast(seed, n, 0x3F, (0x48, 0x50, 0x4B, 0x4D, 0x02, 0x03, 0x04, 0x4D, 0x1C), True)


def equip(seed, n):
    """cast with F6 or F5: the equipment screen's Up, Down, Enter and Space, the item list's keys, F5 and F6 between them"""
    rng = random.Random(seed + 1000)
    return cast(seed, n, rng.choice([0x40, 0x3F]), (0x48, 0x50, 0x1C, 0x39, 0x50, 0x02, 0x03, 0x4D, 0x3F, 0x40), True)


def use(seed, n):
    """cast with F7: the prompt's Left or Right and Enter or Esc; then the picker, the item list (key 1 gives the item to F7), the
    number (Up, Down, Left, Right, Enter), the yes / no buttons or a message box"""
    return cast(seed, n, 0x41, (0x48, 0x50, 0x4B, 0x4D, 0x02, 0x1C, 0x39, 0x1C, 0x40), True)


def route(seed, n):
    import walk_route
    events, end, _m = walk_route.plan([(60, 46), (86, 73), (40, 46)])
    events[end] = events[max(events)] + 0x80
    return [f"--key {p}:{c:#x}" for p, c in sorted(events.items())]


if __name__ == "__main__":
    if len(sys.argv) != 4 or sys.argv[1] not in ("fuzz", "shoot", "volley", "yoyo", "door", "route", "talk", "look", "take", "cast",
                                                 "items", "equip", "use", "map"):
        raise SystemExit(__doc__)
    print(" ".join({"fuzz": fuzz, "shoot": shoot, "volley": volley, "yoyo": yoyo, "door": door, "route": route, "talk": talk,
                    "look": look, "take": take, "cast": cast, "items": items, "equip": equip, "use": use,
                    "map": map_}[sys.argv[1]](int(sys.argv[2]), int(sys.argv[3]))))
