#!/usr/bin/env python3
"""Keys for tools/battle_check.py that answer a talk's number box (0x18fdc; docs/findings.md "WALK.SL step 21"): Escape released and F1
pressed just before pass START (default 12), then a Down at 0 (does nothing), Right (from the units round to the top digit) and Left back,
the digits of NUMBER set from the units up with Up and Left, one Up held (one step only: a key counts after a release), a Down, and
Enter. Prints '--key PASS:CODE ...'; the pass after the Enter goes to stderr.
Usage: tools/number_keys.py NUMBER [START]"""
import sys

digits = sys.argv[1]
t = int(sys.argv[2]) if len(sys.argv) > 2 else 12
out = [f'--key {t - 11}:0x81', f'--key {t - 9}:0x3b', f'--key {t - 7}:0xbb']


def press(code, hold=2, gap=2):
    global t
    out.append(f'--key {t}:{code:#x}')
    out.append(f'--key {t + hold}:{code + 0x80:#x}')
    t += hold + gap


# a Down at 0 (nothing), Right from the units wraps to the top digit and Left back
press(0x50)
press(0x4D)
press(0x4B)
for i, d in enumerate(reversed(digits)):   # units first
    for _ in range(int(d)):
        press(0x48)
    if i < len(digits) - 1:
        press(0x4B)
press(0x48, hold=6)   # held: one step only (a release must come first)
press(0x50)
t += 10
press(0x1C)
print(' '.join(out), end='')
print(f' # end {t}', file=sys.stderr)
