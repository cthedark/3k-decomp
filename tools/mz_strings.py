#!/usr/bin/env python3
"""List printable ASCII strings in an unpacked MZ image with Ghidra-style linear addresses.

Ghidra defines very few strings in these 16-bit programs, so this reads the
bytes directly. Address = 0x10000 + offset into the load image.

Usage: tools/mz_strings.py FILE [MINLEN]
"""
import re
import sys
from pathlib import Path

d = Path(sys.argv[1]).read_bytes()
minlen = int(sys.argv[2]) if len(sys.argv) > 2 else 4
hdr = int.from_bytes(d[8:10], "little") * 16
img = d[hdr:]
for m in re.finditer(rb"[\x20-\x7e]{%d,}" % minlen, img):
    print(f"0x{0x10000 + m.start():05x}  {m.group().decode('ascii')!r}")
