#!/usr/bin/env python3
"""Mouse scripts for the battle (tools/battle_check.py --mouse ...).

Usage: tools/battle_mouse.py SEED N [--from PASS] [--right R] [--world]
Prints --mouse PASS:X:Y:BUTTONS arguments for N passes, and --key 1:0x81 (a key release, as the keyboard port holds after any key,
so that the lists' key latches are set). The pointer goes to random places on the battle's buttons (the four command tabs, the three
front monsters, the rows and icons of the magic and item lists, the member picker's boxes, the music and sound buttons, the left-hand
buttons' corner) or anywhere, now and then moving a few pixels; it presses the left button (or, one time in R, the right one) for 1 to
4 passes and lets go, with 1 to 10 passes between. --world: only over the play window (walking towards the pointer, shooting with the
right button; the on-screen buttons open actions that are not built), presses of 1 to 30 passes, the right button one time in 3.
"""
import argparse
import random

PLACES = [   # (x0, x1, y0, y1), inclusive
    (0x40, 0xFF, 0x3C, 0x49),   # the command tabs
    (0x41, 0x7C, 0x76, 0xB1), (0x83, 0xBE, 0x76, 0xB1), (0xC3, 0xFE, 0x76, 0xB1),   # the front monsters
    (0x40, 0xFF, 0x66, 0xB0),   # the list rows
    (0x54, 0x63, 0x4C, 0x5B), (0x54, 0x83, 0x4C, 0x5B),   # the magic icon, the item icons
    (0x86, 0xDF, 0x4A, 0x5B),   # the picker's boxes
    (0x10D, 0x13B, 0x63, 0x89),   # music and sound
    (0x05, 0x31, 0x67, 0x78),   # the left-hand buttons' corner (leaves the item list)
    (0, 0x13F, 0, 0xC7),   # anywhere
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('seed', type=int)
    ap.add_argument('n', type=int)
    ap.add_argument('--from', dest='start', type=int, default=2)
    ap.add_argument('--right', type=int, default=8, help='one press in R is the right button (Esc)')
    ap.add_argument('--world', action='store_true', help='presses over the play window only, long ones (walking) and right ones')
    a = ap.parse_args()
    rng = random.Random(a.seed)
    out = ['--key 1:0x81']
    x, y = 160, 100
    out.append(f'--mouse 0:{x}:{y}:0')
    p = a.start
    while p < a.n:
        if rng.random() < 0.3:
            x = min(0x13F, max(0, x + rng.randint(-6, 6)))
            y = min(0xC7, max(0, y + rng.randint(-6, 6)))
        else:
            x0, x1, y0, y1 = (0x40, 0xFF, 0x10, 0xB7) if a.world else rng.choice(PLACES)
            x, y = rng.randint(x0, x1), rng.randint(y0, y1)
        out.append(f'--mouse {p}:{x}:{y}:0')
        p += rng.randint(1, 3)
        if rng.random() < 0.7 and p < a.n:
            b = 2 if rng.randrange(3 if a.world else a.right) == 0 else 1
            out.append(f'--mouse {p}:{x}:{y}:{b}')
            p += rng.randint(1, 30 if a.world and b == 1 else 4)
            if p < a.n:
                out.append(f'--mouse {p}:{x}:{y}:0')
        p += rng.randint(1, 10)
    print(' '.join(out))


if __name__ == '__main__':
    main()
