#!/usr/bin/env python3
"""Compare many pairs of raw frame dumps (64000 pixel bytes + 768 palette bytes) at once.

Usage: tools/compare_set.py REF_DIR REF_PATTERN CAND_DIR CAND_PATTERN [NAME ...]
  The patterns contain one `{}` that stands for each NAME, e.g.
      tools/compare_set.py work/oracle1s s{}.raw work/cport1 s{}.raw 000 001 002
  With no NAMEs, every file that matches the reference pattern is compared.
Prints one line per pair that differs and a summary. Exit status 0 if all pairs are identical
in pixel indices and palette bytes (the two things the screen shows).
"""
import re
import sys
from pathlib import Path


def main(argv):
    if len(argv) < 5:
        print(__doc__)
        return 2
    rd, rp, cd, cp = Path(argv[1]), argv[2], Path(argv[3]), argv[4]
    names = argv[5:]
    if not names:
        rx = re.compile("^" + re.escape(rp).replace(r"\{\}", "(.+)") + "$")
        names = sorted(m.group(1) for f in rd.iterdir() if (m := rx.match(f.name)))
    same = diff = missing = 0
    for n in names:
        a, b = rd / rp.format(n), cd / cp.format(n)
        if not a.exists() or not b.exists():
            print(f"{n}: missing {'reference' if not a.exists() else 'candidate'}")
            missing += 1
            continue
        x, y = a.read_bytes(), b.read_bytes()
        if len(x) != len(y):
            print(f"{n}: sizes differ {len(x)} vs {len(y)}")
            diff += 1
            continue
        px = sum(1 for i in range(64000) if x[i] != y[i])
        pal = sum(1 for i in range(64000, len(x)) if x[i] != y[i])
        if px or pal:
            print(f"{n}: {px} pixels, {pal} palette bytes differ")
            diff += 1
        else:
            same += 1
    print(f"identical: {same}, different: {diff}, missing: {missing}")
    return 0 if diff == 0 and missing == 0 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
