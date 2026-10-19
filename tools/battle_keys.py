#!/usr/bin/env python3
"""Key scripts for the battle's command and target choice (tools/battle_check.py --key ...).

Usage: tools/battle_keys.py SEED N [--from PASS]
Prints --key PASS:CODE arguments for N passes: presses of Left, Right, Space, Enter, Esc, 2 and 3, each held for 1 to 6 passes and
released (make code + 0x80), with 1 to 8 passes between. The script follows the command tab and whether a target is being chosen, the way
the battle does (findings.md "WALK.SL step 14"), so it never confirms an attack and never confirms commands 1 to 3, which are not built.
Nothing is pressed before pass PASS (default 2); the first event is a release, because the battle takes a key only after one.
"""
import argparse
import random

LEFT, RIGHT, SPACE, ENTER, ESC = 0x4B, 0x4D, 0x39, 0x1C, 0x01


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('seed', type=int)
    ap.add_argument('n', type=int)
    ap.add_argument('--from', dest='start', type=int, default=2)
    ap.add_argument('--attack', action='store_true',
                    help='only Space, Enter and Esc on the first tab: targets are chosen and attacks confirmed (stage 3)')
    a = ap.parse_args()
    rng = random.Random(a.seed)
    tab, choosing = 0, False
    out = [f'{a.start}:0x81']
    p = a.start + 1 + rng.randint(1, 4)
    while True:
        hold, gap = rng.randint(1, 6), rng.randint(1, 8)
        if p + hold + 1 >= a.n:
            break
        if a.attack:
            k = rng.choice([SPACE, SPACE, ENTER, ENTER, ESC])
        elif choosing:
            k = rng.choice([LEFT, RIGHT, LEFT, RIGHT, ESC, 2, 3])
            if k == ESC:
                choosing = False
        else:
            ks = [LEFT, RIGHT, LEFT, RIGHT, 2, 3]
            if tab == 0:
                ks += [SPACE, ENTER]
            k = rng.choice(ks)
            if k == LEFT:
                tab = (tab - 1) % 4
            elif k == RIGHT:
                tab = (tab + 1) % 4
            elif k in (SPACE, ENTER):
                choosing = True
        out.append(f'{p}:{k:#04x}')
        out.append(f'{p + hold}:{k + 0x80:#04x}')
        p += hold + gap
    print(' '.join(f'--key {x}' for x in out))


if __name__ == '__main__':
    main()
