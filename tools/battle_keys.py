#!/usr/bin/env python3
"""Key scripts for the battle's command and target choice (tools/battle_check.py --key ...).

Usage: tools/battle_keys.py SEED N [--from PASS]
Prints --key PASS:CODE arguments for N passes: presses of Left, Right, Space, Enter, Esc, 2 and 3 (--all: Up, Down too), each held for 1 to 6 passes and
released (make code + 0x80), with 1 to 8 passes between. The script follows the command tab and whether a target is being chosen, the way
the battle does (findings.md "WALK.SL step 14"), so it never confirms an attack or commands 1 to 3 (--attack, --flee and --all do).
Nothing is pressed before pass PASS (default 2); the first event is a release, because the battle takes a key only after one.
"""
import argparse
import random

LEFT, RIGHT, SPACE, ENTER, ESC = 0x4B, 0x4D, 0x39, 0x1C, 0x01
UP, DOWN, KEY1, KEY2 = 0x48, 0x50, 0x02, 0x03   # in the lists key 1 casts or uses, key 2 turns the item page


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('seed', type=int)
    ap.add_argument('n', type=int)
    ap.add_argument('--from', dest='start', type=int, default=2)
    ap.add_argument('--attack', action='store_true',
                    help='only Space, Enter and Esc on the first tab: targets are chosen and attacks confirmed (stage 3)')
    ap.add_argument('--flee', action='store_true',
                    help='Left once (the fourth tab, flee), then only Space and Enter: the party tries to flee each turn')
    ap.add_argument('--all', action='store_true',
                    help='any key the battle and its lists use (arrows, Space, Enter, Esc, 1, 2): every command, the magic and item '
                         'lists and the member picker')
    a = ap.parse_args()
    rng = random.Random(a.seed)
    tab, choosing = 0, False
    out = [f'{a.start}:0x81']
    p = a.start + 1 + rng.randint(1, 4)
    while True:
        hold, gap = rng.randint(1, 6), rng.randint(1, 8)
        if p + hold + 1 >= a.n:
            break
        if a.all:
            k = rng.choice([LEFT, RIGHT, UP, DOWN, SPACE, SPACE, ENTER, ENTER, ESC, KEY1, KEY1, KEY2])
        elif a.flee:
            k = LEFT if tab == 0 else rng.choice([SPACE, ENTER])
            tab = 3
        elif a.attack:
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
