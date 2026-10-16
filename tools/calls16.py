#!/usr/bin/env python3
"""Summarise a stretch of 16-bit C code as calls with their pushed arguments.

The programs were built with Microsoft C, whose far calls push arguments right to left,
so a call looks like  push arg2; push arg1; lcall seg:off; add sp, N.  This tool folds
the argument set-up into one line, tracks simple register contents (`mov ax, 0x42`,
`mov dx, ds`, `lea ax, [bp-0x30]`) and prints `DS:0x42` pointers together with the
string stored there. Other instructions are shown only when they affect control flow
or store to memory, so the shape of a long function stays visible.

It is a reading aid, not a decompiler. Register tracking is linear (no branches), so
after a label or jump the tracked values are cleared. Arguments are printed in
parameter order (first parameter first).

Usage: work/venv/bin/python tools/calls16.py MZFILE START END [--ds-off 0xHEX] [--all]
  START/END   linear addresses, load segment 0x1000 (first image byte = 0x10000)
  --ds-off    image offset of the data segment (default: taken from the MZ header:
              the segment that the entry code loads; override if wrong)
  --all       also print the instructions that are normally folded away
"""
import argparse
import struct
import sys
from pathlib import Path

try:
    import capstone
except ImportError:
    sys.exit("capstone is required: work/venv/bin/pip install capstone==5.0.3")

LOAD = 0x10000


def load_image(path):
    d = Path(path).read_bytes()
    if d[:2] != b"MZ":
        raise SystemExit("not an MZ file")
    hdr = struct.unpack("<H", d[8:10])[0] * 16
    return d[hdr:]


def cstring(img, off, limit=48):
    if not 0 <= off < len(img):
        return None
    end = img.find(b"\0", off, off + limit)
    if end <= off:
        return None
    s = img[off:end]
    if all(0x20 <= b < 0x7F for b in s):
        return s.decode("ascii")
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("file")
    ap.add_argument("start", type=lambda x: int(x, 0))
    ap.add_argument("end", type=lambda x: int(x, 0))
    ap.add_argument("--ds-off", type=lambda x: int(x, 0), default=None)
    ap.add_argument("--all", action="store_true")
    a = ap.parse_args()

    img = load_image(a.file)
    ds_off = a.ds_off
    if ds_off is None:
        # The C start-up code puts the data segment at SS = DS; the segment value is
        # the `mov di, imm` in the entry code. Fall back to the usual value.
        ds_off = 0x8080
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_16)
    md.detail = False

    code = img[a.start - LOAD:a.end - LOAD]
    regs = {}   # tracked register contents (strings), cleared at control-flow merges
    args = []   # pushed values, in push order
    pending_label = False

    def val(r):
        return regs.get(r, r)

    def fmt_ptr(seg, off):
        if seg == "ds" and isinstance(off, int):
            s = cstring(img, ds_off + off)
            return f"DS:{off:#x}" + (f" \"{s}\"" if s else "")
        return f"{seg}:{off if not isinstance(off, int) else hex(off)}"

    pos = 0
    for ins in md.disasm(code, a.start):
        mn, op = ins.mnemonic, ins.op_str
        addr = ins.address
        shown = False

        if mn == "push":
            args.append(val(op) if op in regs else op)
            if a.all:
                print(f"{addr:#07x}    push {op}")
            continue
        if mn == "mov" and "," in op:
            dst, src = [x.strip() for x in op.split(",", 1)]
            if dst in ("ax", "bx", "cx", "dx", "si", "di"):
                if src.startswith("0x") or src.lstrip("-").isdigit():
                    regs[dst] = src
                elif src in ("ds", "ss", "es", "cs"):
                    regs[dst] = src
                elif src in regs:
                    regs[dst] = regs[src]
                else:
                    regs[dst] = src
                if a.all:
                    print(f"{addr:#07x}    {mn} {op}")
                continue
            if dst in ("al", "ah", "bl", "bh", "cl", "ch", "dl", "dh"):
                regs[dst] = src
                if a.all:
                    print(f"{addr:#07x}    {mn} {op}")
                continue
        if mn == "lea":
            dst, src = [x.strip() for x in op.split(",", 1)]
            regs[dst] = "&" + src.replace("word ptr ", "")
            continue
        if mn == "xor" and len(set(x.strip() for x in op.split(","))) == 1:
            regs[op.split(",")[0].strip()] = "0"
            continue
        if mn == "add" and op.startswith("sp,"):
            continue
        if mn in ("lcall", "call"):
            parts = [x.strip() for x in op.split(",")]
            if mn == "lcall" and len(parts) == 2:
                seg, off = int(parts[0], 0), int(parts[1], 0)
                tgt = LOAD + seg * 16 + off
                name = f"{tgt:#x}"
            else:
                name = op
            # Arguments were pushed right to left, so reverse for parameter order.
            # A far pointer is pushed segment first, then offset; after reversing it
            # reads (offset, segment), which is combined into one "seg:off" argument.
            out = list(reversed(args))
            comb = []
            i = 0
            while i < len(out):
                if i + 1 < len(out) and out[i + 1] in ("ds", "ss", "es"):
                    off = out[i]
                    try:
                        off = int(off, 0)
                    except ValueError:
                        pass
                    comb.append(fmt_ptr(out[i + 1], off))
                    i += 2
                else:
                    comb.append(out[i])
                    i += 1
            print(f"{addr:#07x}  call {name}({', '.join(comb)})")
            args = []
            regs = {}   # a call may clobber every register
            continue
        if mn.startswith("j") or mn in ("loop", "loope", "loopne", "ret", "retf", "iret"):
            print(f"{addr:#07x}  {mn} {op}")
            if mn.startswith("j") or mn.startswith("loop"):
                pass
            continue
        # Targets of jumps: mark by clearing tracked registers conservatively
        # when an instruction writes memory or compares, print it.
        if mn in ("cmp", "test", "inc", "dec", "sub", "add", "sbb", "adc", "shl", "shr",
                  "mul", "imul", "div", "idiv", "and", "or", "xor", "neg", "cwd", "cbw",
                  "int", "in", "out", "stosb", "stosw", "rep stosb", "rep stosw",
                  "rep movsb", "rep movsw", "mov", "xchg", "les", "lds"):
            txt = f"{mn} {op}"
            # keep register tracking honest: arithmetic invalidates the target
            if "," in op:
                dst = op.split(",")[0].strip()
                regs.pop(dst, None)
            print(f"{addr:#07x}    {txt}")
            continue
        if a.all:
            print(f"{addr:#07x}    {mn} {op}")


if __name__ == "__main__":
    main()
