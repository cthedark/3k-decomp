#!/usr/bin/env python3
"""Split the emulated event times of a program into a clock-bound and a CPU-bound part.

Input: logs written by `dosemu.py --log-files` at several `--instr-per-tick` values, named
`PREFIX_<ipt>.log`. The emulator's BIOS clock advances only with executed instructions, so an
event at tick t satisfies  t = a + W / ipt,  where
  a  = ticks spent waiting on the (18.2 Hz) clock, a real-time quantity, and
  W  = instructions executed by CPU-bound work (busy loops, blits, decoding).
The tool fits a and W per event by least squares and prints the residual, so a poor fit
(an event that is neither) is visible.

Usage: tools/fit_timeline.py PREFIX IPT [IPT ...]
"""
import re
import sys


def read(path):
    ev = []
    for line in open(path):
        m = re.match(r"tick (\d+): open (\S+)", line)
        if m:
            ev.append((int(m.group(1)), m.group(2)))
    return ev


def fit(xs, ys):
    n = len(xs)
    mx, my = sum(xs) / n, sum(ys) / n
    sxx = sum((x - mx) ** 2 for x in xs)
    b = sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / sxx
    a = my - b * mx
    res = max(abs(y - (a + b * x)) for x, y in zip(xs, ys))
    return a, b, res


def main():
    prefix, ipts = sys.argv[1], [int(x) for x in sys.argv[2:]]
    runs = [read(f"{prefix}_{i}.log") for i in ipts]
    n = min(len(r) for r in runs)
    if len({tuple(name for _, name in r[:n]) for r in runs}) != 1:
        sys.exit("event sequences differ between runs")
    print(f"{'event':<4} {'file':<14} {'clock ticks a':>14} {'cpu Minstr W':>13} {'max resid':>10}")
    for k in range(n):
        xs = [1.0 / i for i in ipts]
        ys = [r[k][0] for r in runs]
        a, w, res = fit(xs, ys)
        print(f"{k:<4} {runs[0][k][1]:<14} {a:>14.1f} {w / 1e6:>13.2f} {res:>10.2f}")


main()
