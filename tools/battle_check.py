#!/usr/bin/env python3
"""Compare WALK.SL's battle in the C port with the original, pass by pass (docs/findings.md, "WALK.SL step 13").

Usage: work/venv/bin/python tools/battle_check.py DATA N [--tick-base B] [--tick-div D] [--key PASS:CODE ...] [--keep DIR]
                                                 [--cache FILE] [--port BIN]

DATA is a data directory that starts the game on an enemy (tools/walk_scene_data.py DIR 1 106 36). The oracle runs WALK.SL with
the world loop (0x11d36) and the battle loop (0x1d418) counted as one sequence of passes; at each arrival it records the screen,
the palette, the rand state and, in the battle, the monster table ([bp-0x7c], 0x3c bytes), the sway count and direction
([bp-0x52], [bp-0x70]), who acts ([bp-0x44]) and the turn counter ([bp-0x5e]). build/walk_dump DATA battle runs the port for
the same passes. Prints one line per kind of difference and "all N passes identical" when there is none; exit status 1 on a
difference, 2 if a run fails.
"""
import argparse
import pickle
import shutil
import struct
import subprocess
import sys
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from dosemu import Emu, VGA  # noqa: E402
from unicorn import UC_HOOK_CODE  # noqa: E402
from unicorn.x86_const import UC_X86_REG_BP, UC_X86_REG_DS, UC_X86_REG_SS  # noqa: E402

WORLD_TOP, BATTLE_TOP = 0x11D36, 0x1D418
MON = 0x3C
STATE = 4 + MON + 4
FRAME = 64000 + 768 + 1 + STATE


def oracle(a, n, keys):
    work = Path(a.keep) / 'emu'
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
    emu.watch_pass([WORLD_TOP, BATTLE_TOP], a.tick_div, {n + 1: str(work / '_last.raw')}, keys)
    rec = []

    def far(uc, ss, bp, off):
        o, s = struct.unpack('<HH', bytes(uc.mem_read(ss * 16 + ((bp - off) & 0xFFFF), 4)))
        return s * 16 + o

    def cb(uc, address, size, user):
        k = emu.pass_count - 1
        if k == 0 and a.tick_base is not None:
            emu.tick_base = a.tick_base
            emu.set_ticks(a.tick_base)
        if k > n + 1:
            return
        ss, bp, ds = emu.r(UC_X86_REG_SS), emu.r(UC_X86_REG_BP), emu.r(UC_X86_REG_DS)
        st = bytearray(STATE)
        st[0:4] = bytes(uc.mem_read(ds * 16 + 0x2D4, 4))
        inb = address == BATTLE_TOP
        if inb:
            st[4:4 + MON] = bytes(uc.mem_read(far(uc, ss, bp, 0x7C), MON))
            loc = lambda off: uc.mem_read(ss * 16 + ((bp - off) & 0xFFFF), 1)[0]  # noqa: E731
            st[4 + MON:8 + MON] = bytes([loc(0x52), loc(0x70), loc(0x44), loc(0x5E)])
        rec.append((inb, bytes(uc.mem_read(VGA, 64000)), bytes(emu.pal), bytes(st)))

    emu.uc.hook_add(UC_HOOK_CODE, cb, None, WORLD_TOP, WORLD_TOP)
    emu.uc.hook_add(UC_HOOK_CODE, cb, None, BATTLE_TOP, BATTLE_TOP)
    emu.run(100000, {})
    return rec


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('data')
    ap.add_argument('n', type=int)
    ap.add_argument('--tick-base', type=int, default=20)
    ap.add_argument('--tick-div', type=int, default=3)
    ap.add_argument('--key', action='append', default=[])
    ap.add_argument('--keep', default='work/battle_check')
    ap.add_argument('--start', default='work/check_credits/start.raw')
    ap.add_argument('--exe', default='work/unpacked/WALK.SL.exe')
    ap.add_argument('--port', default='build/walk_dump')
    ap.add_argument('--cache', help='keep the original\'s recording in this file and reuse it (same DATA, N, clock and keys)')
    a = ap.parse_args()
    Path(a.keep).mkdir(parents=True, exist_ok=True)
    keys = {int(k.split(':')[0]): int(k.split(':')[1], 0) for k in a.key}

    rec = None
    if a.cache and Path(a.cache).exists():
        rec = pickle.loads(zlib.decompress(Path(a.cache).read_bytes()))
    if rec is None or len(rec) < a.n + 1:
        rec = oracle(a, a.n, keys)
        if a.cache:
            Path(a.cache).write_bytes(zlib.compress(pickle.dumps(rec), 1))
    if len(rec) < a.n + 1:
        print(f'oracle: only {len(rec)} arrivals for {a.n} passes')
        return 2
    out = Path(a.keep) / 'port.bin'
    r = subprocess.run([a.port, a.data, 'battle', str(out), str(a.n), str(a.tick_base), str(a.tick_div), a.start] + a.key,
                       capture_output=True, text=True)
    sys.stderr.write(r.stderr)
    if r.returncode != 0:
        print('port run failed:', r.returncode)
        return 2
    port = out.read_bytes()
    if len(port) != FRAME * a.n:
        print(f'port: {len(port)} bytes, want {FRAME * a.n}')
        return 2
    first = {}
    for i in range(a.n):
        inb, scr, pal, st = rec[i + 1]
        f = port[FRAME * i:FRAME * (i + 1)]
        pscr, ppal, pinb, pst = f[:64000], f[64000:64768], f[64768], f[64769:]
        checks = {'world/battle': bool(pinb) == inb, 'screen': pscr == scr, 'palette': ppal == pal, 'rand': pst[:4] == st[:4]}
        if inb:
            checks['monsters'] = pst[4:4 + MON] == st[4:4 + MON]
            checks['sway'] = pst[4 + MON:6 + MON] == st[4 + MON:6 + MON]
            checks['turn'] = pst[6 + MON:8 + MON] == st[6 + MON:8 + MON]
        for k, ok in checks.items():
            if not ok and k not in first:
                first[k] = i
                if k == 'screen':
                    d = [j for j in range(64000) if pscr[j] != scr[j]]
                    xs, ys = [j % 320 for j in d], [j // 320 for j in d]
                    print(f'pass {i}: screen differs in {len(d)} pixels, ({min(xs)}, {min(ys)}) .. ({max(xs)}, {max(ys)})')
                    Path(a.keep, f'diff_{i}_orig.raw').write_bytes(scr + pal)
                    Path(a.keep, f'diff_{i}_port.raw').write_bytes(pscr + ppal)
                elif k in ('monsters', 'sway', 'turn', 'rand'):
                    print(f'pass {i}: {k} differs:\n  original {st.hex()}\n  port     {bytes(pst).hex()}')
                else:
                    print(f'pass {i}: {k} differs')
    nb = sum(1 for x in rec[1:a.n + 1] if x[0])
    if first:
        return 1
    print(f'all {a.n} passes identical ({a.n - nb} world, {nb} battle)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
