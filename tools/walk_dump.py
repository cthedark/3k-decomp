#!/usr/bin/env python3
"""Run WALK.SL in the oracle and (a) log every DOS file read as (tick, file, file offset, destination, count),
(b) after N ticks dump linear memory ranges to files.

Usage: work/venv/bin/python tools/walk_dump.py OUTDIR TICKS [--dump NAME:LINEAR:LEN ...] [--key TICK:SCAN[:REL]]
       [--data DIR] [--start work/check_credits/start.raw]

The reads are written to OUTDIR/reads.txt. Dumps go to OUTDIR/NAME.bin.
Each --call line shows the caller (ret=LINEAR, the return address) and the first NWORDS stack words (the arguments, first one first).
The oracle is the one in tools/dosemu.py (WALK.SL is started with the menu's screen and palette, as exec would).
"""
import argparse
import shutil
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import dosemu  # noqa: E402
from dosemu import Emu, VGA  # noqa: E402
from unicorn.x86_const import UC_X86_REG_BX, UC_X86_REG_CX, UC_X86_REG_DS, UC_X86_REG_DX  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("ticks", type=int)
    ap.add_argument("--dump", action="append", default=[])
    ap.add_argument("--dump-at", action="append", default=[],
                    help="CODEADDR:NTH:NAME:LINEAR:LEN - at the NTH (0-based) arrival at CODEADDR dump LEN bytes at LINEAR")
    ap.add_argument("--key", action="append", default=[])
    ap.add_argument("--call", action="append", default=[],
                    help="LINEAR:NAME:NWORDS - log each call to this far-function entry, interleaved with the reads")
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

    reads = []
    orig = emu.dos

    def dos(ax, ah):
        if ah == 0x3F:
            bx = emu.r(UC_X86_REG_BX)
            f = emu.files.get(bx)
            pos = f[0].tell() if f else -1
            name = f[1] if f else "?"
            cx = emu.r(UC_X86_REG_CX)
            dest = emu.r(UC_X86_REG_DS) * 16 + emu.r(UC_X86_REG_DX)
            reads.append((emu.ticks, name, pos, dest, cx, "", []))
        return orig(ax, ah)

    emu.dos = dos

    from unicorn import UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_SP, UC_X86_REG_SS

    def mkcall(name, nw):
        def cb(uc, address, size, user):
            sp = uc.reg_read(UC_X86_REG_SP)
            ss = uc.reg_read(UC_X86_REG_SS)
            w = [int.from_bytes(bytes(uc.mem_read(ss * 16 + sp + 2 * i, 2)), "little") for i in range(nw + 2)]
            args = f"ret={w[1] * 16 + w[0]:#x} " + " ".join(f"{x:04x}" for x in w[2:])
            lin = lambda seg, off: (seg * 16 + off) & 0xFFFFF
            reads.append((emu.ticks, "CALL " + name, -1, 0, 0, args, w))
        return cb

    for dspec in a.dump_at:
        ca, nth, nm, lin, ln = dspec.split(":")
        cnt = [0]

        def mkd(nth=int(nth), nm=nm, lin=int(lin, 16), ln=int(ln, 0), cnt=cnt):
            def cb(uc, address, size, user):
                if cnt[0] == nth:
                    (out / f"{nm}.bin").write_bytes(bytes(uc.mem_read(lin, ln)))
                cnt[0] += 1
            return cb
        emu.uc.hook_add(UC_HOOK_CODE, mkd(), begin=int(ca, 16), end=int(ca, 16))

    for c in a.call:
        lin, name, nw = c.split(":")
        emu.uc.hook_add(UC_HOOK_CODE, mkcall(name, int(nw)), begin=int(lin, 16), end=int(lin, 16))
    hooks = {}
    for k in a.key:
        parts = k.split(":")
        t, sc = int(parts[0]), int(parts[1], 0)
        hooks.setdefault(t, []).append(lambda e, sc=sc: setattr(e, "key", sc))
        if len(parts) > 2:
            hooks.setdefault(int(parts[2]), []).append(lambda e: setattr(e, "key", 0))
    emu.run(a.ticks, hooks)
    print("stopped:", emu.stop_reason)
    with open(out / "reads.txt", "w") as f:
        for t, name, pos, dest, cx, args, w in reads:
            if name.startswith("CALL"):
                f.write(f"t{t} {name} {args}\n")
            else:
                f.write(f"t{t} {name} off={pos:#x} -> {dest:#x} count={cx:#x}\n")
    for d in a.dump:
        name, lin, n = d.split(":")
        (out / f"{name}.bin").write_bytes(bytes(emu.uc.mem_read(int(lin, 16), int(n, 0))))
    pix, pal = emu.snapshot()
    (out / "screen.raw").write_bytes(pix + pal)


if __name__ == "__main__":
    main()
