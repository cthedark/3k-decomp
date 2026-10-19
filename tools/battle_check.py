#!/usr/bin/env python3
"""Compare WALK.SL's battle in the C port with the original, pass by pass (docs/findings.md, "WALK.SL step 13").

Usage: work/venv/bin/python tools/battle_check.py DATA N [--tick-base B] [--tick-div D] [--key PASS:CODE ...] [--keep DIR]
                                                 [--cache FILE] [--port BIN]

DATA is a data directory that starts the game on an enemy (tools/walk_scene_data.py DIR 1 106 36). The oracle runs WALK.SL with
the world loop (0x11d36) and the battle loop (0x1d418) counted as one sequence of passes; at each arrival it records the screen,
the palette, the rand state and, in the battle, the monster table ([bp-0x7c], 0x3c bytes), the sway count and direction
([bp-0x52], [bp-0x70]), who acts ([bp-0x44]), the turn counter ([bp-0x5e]), the chosen command ([bp-0x82]), the
choosing flag ([bp-0x72]) and the key latch ([bp-0x38]). build/walk_dump DATA battle runs the port for
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
LOCALS = (0x52, 0x70, 0x44, 0x5E, 0x82, 0x72, 0x38, 0x42, 0x8C, 0x0C, 0x58, 0x14, 0x84, 0x36, 0x10, 0x78)
STATE = 4 + MON + len(LOCALS) + 200 + 60
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
            st[4 + MON:4 + MON + len(LOCALS)] = bytes(loc(o) for o in LOCALS)
            q = 4 + MON + len(LOCALS)
            st[q:q + 200] = bytes(uc.mem_read(far(uc, ss, bp, -0xE), 200))
            st[q + 200:q + 260] = bytes(uc.mem_read(far(uc, ss, bp, -0xA) + 0xD4E2, 60))
        rec.append((inb, bytes(uc.mem_read(VGA, 64000)), bytes(emu.pal), bytes(st)))

    emu.uc.hook_add(UC_HOOK_CODE, cb, None, WORLD_TOP, WORLD_TOP)
    emu.uc.hook_add(UC_HOOK_CODE, cb, None, BATTLE_TOP, BATTLE_TOP)
    # The loop tops set the clock to base + pass / tick_div; once a battle is over the original stops arriving and the clock runs freely,
    # so the run ends a little after the last pass could have come.
    emu.run((a.tick_base or 0) + n // a.tick_div + 400, {})
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
    short = len(rec) < a.n + 1   # the original may stop arriving at a loop top once a battle is over (it waits on another screen)
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
    ended = None
    for i in range(a.n):
        f = port[FRAME * i:FRAME * (i + 1)]
        pscr, ppal, pinb, pst = f[:64000], f[64000:64768], f[64768], f[64769:]
        if pinb == 2:   # the port's battle is over (won or lost) in this pass: the original must have left the battle loop in it
            ended = i
            if i + 1 < len(rec) and rec[i + 1][0]:
                print(f'pass {i}: the port ended the battle, the original did not')
                first['end'] = i
            break
        if i + 1 >= len(rec):
            print(f'oracle: only {len(rec)} arrivals for {a.n} passes, and the port\'s battle had not ended')
            return 2
        inb, scr, pal, st = rec[i + 1]
        checks = {'world/battle': bool(pinb) == inb, 'screen': pscr == scr, 'palette': ppal == pal, 'rand': pst[:4] == st[:4]}
        if inb:
            checks['monsters'] = pst[4:4 + MON] == st[4:4 + MON]
            checks['sway'] = pst[4 + MON:6 + MON] == st[4 + MON:6 + MON]
            checks['turn'] = pst[6 + MON:8 + MON] == st[6 + MON:8 + MON]
            checks['command'] = pst[8 + MON:10 + MON] == st[8 + MON:10 + MON]
            checks['key latch'] = pst[10 + MON] == st[10 + MON]
            q = 4 + MON + len(LOCALS)
            checks['attack locals'] = pst[11 + MON:q] == st[11 + MON:q]
            checks['party'] = pst[q:q + 200] == st[q:q + 200]
            checks['numbers'] = pst[q + 200:q + 260] == st[q + 200:q + 260]
        for k, ok in checks.items():
            if not ok and k not in first:
                first[k] = i
                if k == 'screen':
                    d = [j for j in range(64000) if pscr[j] != scr[j]]
                    xs, ys = [j % 320 for j in d], [j // 320 for j in d]
                    print(f'pass {i}: screen differs in {len(d)} pixels, ({min(xs)}, {min(ys)}) .. ({max(xs)}, {max(ys)})')
                    Path(a.keep, f'diff_{i}_orig.raw').write_bytes(scr + pal)
                    Path(a.keep, f'diff_{i}_port.raw').write_bytes(pscr + ppal)
                elif k in ('monsters', 'sway', 'turn', 'rand', 'command', 'key latch', 'attack locals', 'party', 'numbers'):
                    d = [j for j in range(STATE) if st[j] != pst[j]]
                    names = ['rand'] * 4 + [f'mon+{j:#x}' for j in range(MON)] + [f'bp-{o:#x}' for o in LOCALS] + \
                        [f'party+{j:#x}' for j in range(200)] + [f'number {j // 3}.{j % 3}' for j in range(60)]
                    print(f'pass {i}: {k} differs: ' + ', '.join(f'{names[j]} original {st[j]:#04x} port {pst[j]:#04x}' for j in d[:12]))
                else:
                    print(f'pass {i}: {k} differs')
    n = a.n if ended is None else ended
    nb = sum(1 for x in rec[1:n + 1] if x[0])
    if first:
        return 1
    end = '' if ended is None else f'; both ended the battle at pass {ended} (not compared after)'
    print(f'all {n} passes identical ({n - nb} world, {nb} battle){end}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
