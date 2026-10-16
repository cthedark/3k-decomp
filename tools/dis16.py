#!/usr/bin/env python3
"""Disassemble 16-bit x86 code from an unpacked DOS MZ image.

Addresses are Ghidra-style linear addresses (load segment 0x1000, so the first
byte of the load image is 0x10000). Output marks bytes that the MZ relocation
table patches (segment fixups) with `<seg>`, because those 16-bit values are
segment numbers, not ordinary constants.

Usage: tools/dis16.py MZFILE START [END]      (hex, e.g. 0x10e1a 0x11126)
Needs the `capstone` package; this project keeps it in work/venv:
    work/venv/bin/python tools/dis16.py work/unpacked/MARK.unpacked.exe 0x10e1a 0x10e60
"""
import struct
import sys
from pathlib import Path

try:
    import capstone
except ImportError:
    sys.exit("capstone is required: work/venv/bin/pip install capstone==5.0.3")

LOAD = 0x10000


def load(path):
    d = Path(path).read_bytes()
    if d[:2] != b"MZ":
        raise SystemExit("not an MZ file")
    nreloc = struct.unpack("<H", d[6:8])[0]
    hdr = struct.unpack("<H", d[8:10])[0] * 16
    reloc_off = struct.unpack("<H", d[24:26])[0]
    relocs = set()
    for i in range(nreloc):
        off, seg = struct.unpack("<HH", d[reloc_off + 4 * i:reloc_off + 4 * i + 4])
        relocs.add(LOAD + seg * 16 + off)  # linear address of the patched word
    return d[hdr:], relocs


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    img, relocs = load(argv[1])
    start = int(argv[2], 16)
    end = int(argv[3], 16) if len(argv) > 3 else start + 0x80
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_16)
    md.detail = False
    off = start - LOAD
    code = img[off:end - LOAD]
    for ins in md.disasm(code, start):
        raw = bytes(ins.bytes)
        flag = ""
        for k in range(len(raw) - 1):
            if ins.address + k in relocs:
                flag = "   ; <seg> fixup at +%d" % k
        print(f"0x{ins.address:05x}  {raw.hex(' '):<20s} {ins.mnemonic} {ins.op_str}{flag}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
