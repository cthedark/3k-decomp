#!/usr/bin/env python3
"""Run WALK.SL in the oracle and log every write into a linear memory range, grouped by the code that did it.

Usage: work/venv/bin/python tools/walk_wtrace.py OUTDIR TICKS LINEAR:LEN [LINEAR:LEN ...] [--data DIR]

For each range writes OUTDIR/w_<LINEAR>.txt, one line per write: tick, writer (linear address of the
instruction), offset in the range, size, value.  Prints a summary per writer: how many writes, how many distinct
offsets, the first and last tick.  Like walk_dump.py the program starts with the menu's screen and palette.
"""
import argparse
import shutil
import sys
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from dosemu import Emu, VGA  # noqa: E402
from unicorn import UC_HOOK_MEM_WRITE  # noqa: E402
from unicorn.x86_const import UC_X86_REG_CS, UC_X86_REG_IP  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("ticks", type=int)
    ap.add_argument("ranges", nargs="+")
    ap.add_argument("--data", default="original_bin")
    ap.add_argument("--start", default="work/check_credits/start.raw")
    ap.add_argument("--exe", default="work/unpacked/WALK.SL.exe")
    a = ap.parse_args()

    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    work = out / "emu"
    work.mkdir(exist_ok=True)
    for p in Path(a.data).iterdir():
        if p.is_file():
            shutil.copy2(p, work / p.name)

    emu = Emu(a.exe, work, 250_000, False)
    raw = Path(a.start).read_bytes()
    emu.pal[:] = raw[-768:]
    emu.uc.mem_write(VGA, raw[:64000])

    logs = {}
    for r in a.ranges:
        lin, ln = r.split(":")
        lin, ln = int(lin, 16), int(ln, 0)
        log = []
        logs[lin] = (ln, log)

        def cb(uc, access, address, size, value, user, lin=lin, ln=ln, log=log):
            pc = uc.reg_read(UC_X86_REG_CS) * 16 + uc.reg_read(UC_X86_REG_IP)
            log.append((emu.ticks, pc, address - lin, size, value & ((1 << (8 * size)) - 1)))

        emu.uc.hook_add(UC_HOOK_MEM_WRITE, cb, begin=lin, end=lin + ln - 1)

    emu.run(a.ticks, {})
    print("stopped:", emu.stop_reason)
    for lin, (ln, log) in logs.items():
        with open(out / f"w_{lin:x}.txt", "w") as f:
            for t, pc, off, size, v in log:
                f.write(f"{t} {pc:#x} {off:#x} {size} {v:#x}\n")
        by = defaultdict(list)
        for t, pc, off, size, v in log:
            by[pc].append((t, off))
        print(f"range {lin:#x}+{ln:#x}: {len(log)} writes")
        for pc, l in sorted(by.items(), key=lambda kv: kv[1][0][0]):
            print(f"  pc {pc:#x}: {len(l)} writes, {len(set(o for _, o in l))} offsets, ticks {l[0][0]}..{l[-1][0]}")


if __name__ == "__main__":
    main()
