"""Trace the idle main loop of WALK.SL in the oracle: at each arrival at the loop top (0x11d36) record
the main frame's locals, the data-segment counters, the villager and party tables, the screen and the palette.
Usage: walk_idle_trace.py OUT.pkl NPASSES [--tick-base N] [--tick-div D] [--key PASS:CODE ...]   (CODE: 0x4d Right, 0x4b Left, 0x48 Up, 0x50 Down; the break code is the make code + 0x80)
Pass n of the record = state at the n-th arrival (n completed passes). No input unless --key is given."""
import argparse, pickle, shutil, struct, sys, zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from dosemu import Emu, VGA  # noqa: E402
from unicorn.x86_const import UC_X86_REG_BP, UC_X86_REG_DS, UC_X86_REG_SS  # noqa: E402

ap = argparse.ArgumentParser()
ap.add_argument('out')
ap.add_argument('n', type=int)
ap.add_argument('--tick-div', type=int, default=3)
ap.add_argument('--key', action='append', default=[], help='PASS:CODE - keyboard port 0x60 holds CODE from that pass on (a make code, later 0x80+ for the break)')
ap.add_argument('--tick-base', type=int, default=None, help='BIOS tick counter at pass 0 (default: 0)')
ap.add_argument('--data', default='original_bin')
ap.add_argument('--start', default='work/check_credits/start.raw')
ap.add_argument('--work', default='work/walk6/emu_trace')
ap.add_argument('--exe', default='work/unpacked/WALK.SL.exe')
a = ap.parse_args()

work = Path(a.work)
if work.exists():
    shutil.rmtree(work)
work.mkdir(parents=True)
for p in Path(a.data).iterdir():
    if p.is_file():
        shutil.copy2(p, work / p.name)
emu = Emu(a.exe, work, 250_000, False)
raw = Path(a.start).read_bytes()
emu.pal[:] = raw[-768:]
emu.uc.mem_write(VGA, raw[:64000])
keys = {int(k.split(':')[0]): int(k.split(':')[1], 0) for k in a.key}
emu.watch_pass([0x11d36], a.tick_div, {a.n: str(work / '_last.raw')}, keys)
rec = []


def far(uc, ss, bp, off):
    a_ = ss * 16 + ((bp - off) & 0xFFFF)
    o, s = struct.unpack('<HH', bytes(uc.mem_read(a_, 4)))
    return s * 16 + o


def cb(uc, address, size, user):
    n = emu.pass_count - 1
    if n > a.n:
        return
    if n == 0 and a.tick_base is not None:
        emu.tick_base = a.tick_base
        emu.set_ticks(a.tick_base)
    ss = emu.r(UC_X86_REG_SS)
    bp = emu.r(UC_X86_REG_BP)
    ds = emu.r(UC_X86_REG_DS)
    locs = bytes(uc.mem_read(ss * 16 + ((bp - 0xe2) & 0xFFFF), 0xe2))      # bp-0xe2 .. bp-1
    d = dict(n=n, keys=keys, ticks=emu.tick_base, locs=locs, bp=bp, ss=ss, ds=ds,
             dsb=bytes(uc.mem_read(ds * 16 + 0x500, 0x60)),
             vill=bytes(uc.mem_read(far(uc, ss, bp, 8), 1000)),
             party=bytes(uc.mem_read(far(uc, ss, bp, 0xa8), 200)),
             drops=bytes(uc.mem_read(far(uc, ss, bp, 0x7c), 150)),
             anim=bytes(uc.mem_read(far(uc, ss, bp, 0xcc), 40)),
             abank=bytes(uc.mem_read(far(uc, ss, bp, 0xd4), 320)),
             blkx=bytes(uc.mem_read(far(uc, ss, bp, 0x88), 240)),
             blky=bytes(uc.mem_read(far(uc, ss, bp, 0xd0), 240)),
             other=bytes(uc.mem_read(far(uc, ss, bp, 0xdc), 600)),
             trail=bytes(uc.mem_read(far(uc, ss, bp, 0xc8), 39)),
             rnd=bytes(uc.mem_read(ds * 16 + 0x2d4, 4)),
             palbuf=bytes(uc.mem_read(far(uc, ss, bp, 0xac), 768)),
             screen=bytes(uc.mem_read(VGA, 64000)), pal=bytes(emu.pal))
    rec.append(d)


emu.uc.hook_add(__import__('unicorn').UC_HOOK_CODE, cb, None, 0x11d36, 0x11d36)
emu.run(100000, {})
Path(a.out).write_bytes(zlib.compress(pickle.dumps(rec), 1))
print('recorded', len(rec), 'passes; stop:', emu.stop_reason)
