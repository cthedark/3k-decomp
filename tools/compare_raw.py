#!/usr/bin/env python3
"""Compare raw screen dumps (64,000 pixels + 768 palette bytes), e.g. the port's against the original's.

Usage: tools/compare_raw.py A.raw B.raw [--box X0,Y0,X1,Y1]
Prints the number of differing pixels and palette bytes, and the bounding box of the differences.
Exit status 0 if identical, 1 otherwise. Standard library only.
"""
import sys
from pathlib import Path


def main(argv):
    args = [a for a in argv[1:] if not a.startswith("--")]
    box = None
    if "--box" in argv:
        v = argv[argv.index("--box") + 1]
        args.remove(v)
        box = tuple(int(x) for x in v.split(","))
    if len(args) != 2:
        raise SystemExit(__doc__)
    a, b = (Path(p).read_bytes() for p in args)
    if len(a) != 64768 or len(b) != 64768:
        print("size mismatch: %d and %d bytes (want 64768)" % (len(a), len(b)))
        return 2
    diff = [i for i in range(64000) if a[i] != b[i] and (box is None or (box[0] <= i % 320 <= box[2] and box[1] <= i // 320 <= box[3]))]
    pal = sum(1 for i in range(64000, 64768) if a[i] != b[i])
    if not diff and not pal:
        print("identical")
        return 0
    if diff:
        xs, ys = [i % 320 for i in diff], [i // 320 for i in diff]
        print("%d pixels differ, bounding box x %d..%d y %d..%d; %d palette bytes differ"
              % (len(diff), min(xs), max(xs), min(ys), max(ys), pal))
    else:
        print("0 pixels differ; %d palette bytes differ" % pal)
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
