#!/usr/bin/env python3
"""Compare WALK.SL's battle in the C port with the original, pass by pass (docs/findings.md, "WALK.SL steps 13 to 16").

Usage: work/venv/bin/python tools/battle_check.py DATA N [--tick-base B] [--tick-div D] [--key PASS:CODE ...] [--keep DIR]
                                                 [--cache FILE] [--port BIN]

DATA is a data directory that starts the game on an enemy (tools/walk_scene_data.py DIR 1 106 36). The oracle runs WALK.SL with
nine loop tops counted as one sequence of passes: the world (0x11d36), the battle (0x1d418), the rewards bar after a win (0x1fb7d),
a message box's wait (0x1b506), the magic list (0x217a6), the member picker (0x1aa30) and the item list (0x207b2; in these three only
the screen, palette and rand state are compared), a talk's yes / no buttons (0x18d23) and its number (0x19166; the same). At each arrival it records which loop, the screen, the palette, the rand state and, in the battle
and the rewards bar, the monster table ([bp-0x7c], 0x3c bytes), the sway count and direction ([bp-0x52], [bp-0x70]), the party table
and the floating numbers; in the battle also who acts ([bp-0x44]), the turn counter ([bp-0x5e]), the chosen command ([bp-0x82]), the
choosing flag ([bp-0x72]), the key latch ([bp-0x38]) and more locals; in the world the villager table and the party table up to the
coins. build/walk_dump DATA battle runs the port for the same passes. A lost battle ends the comparison (the original then leaves for
MARK.EXE). Prints one line per kind of difference and "all N passes identical" when there is none; exit status 1 on a difference, 2 if
a run fails.
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
from sb_stub import SoundBlaster  # noqa: E402
from unicorn import UC_HOOK_CODE  # noqa: E402
from unicorn.x86_const import UC_X86_REG_BP, UC_X86_REG_DS, UC_X86_REG_SS  # noqa: E402

WORLD_TOP, BATTLE_TOP, REWARDS_TOP, DIALOG_TOP = 0x11D36, 0x1D418, 0x1FB7D, 0x1B506
# the magic list's, the member picker's and the item list's loops (screen, palette and rand are compared there)
MAGIC_TOP, PICK_TOP, ITEMS_TOP = 0x217A6, 0x1AA30, 0x207B2
# a talk's yes / no buttons (0x18c36) and its number to set (0x18fdc): screen, palette and rand
YESNO_TOP, NUMBER_TOP = 0x18D23, 0x19166
# F3's wait after something is taken (an item, coins): screen, palette and rand
TAKE_TOPS = (0x160B0, 0x164AD)
# F5's and F6's equipment screen (0x221d2): screen, palette, rand and the party
EQUIP_TOP = 0x2256A
KIND = {WORLD_TOP: 0, BATTLE_TOP: 1, REWARDS_TOP: 3, DIALOG_TOP: 4, MAGIC_TOP: 5, PICK_TOP: 6, ITEMS_TOP: 7, YESNO_TOP: 8,
        NUMBER_TOP: 9, TAKE_TOPS[0]: 10, TAKE_TOPS[1]: 10, EQUIP_TOP: 11}
KIND_NAME = {0: 'world', 1: 'battle', 2: 'lost', 3: 'rewards', 4: 'message box', 5: 'magic list', 6: 'member picker',
             7: 'item list', 8: 'yes / no', 9: 'number', 10: 'take', 11: 'equipment', 'flash': 'face flash steps', 'sounds': 'sounds'}
MON = 0x3C
LOCALS = (0x52, 0x70, 0x44, 0x5E, 0x82, 0x72, 0x38, 0x42, 0x8C, 0x0C, 0x58, 0x14, 0x84, 0x36, 0x10, 0x78)
BSTATE = 4 + MON + len(LOCALS) + 200 + 60
STATE = 4 + 1000 + 0x124   # a world pass: rand, the villager table, the party table up to the coins
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
    if a.opl:   # an AdLib: the game finds it and its music flag (0x2cc5:5) is 0x81
        emu.opl_log = []
    raw = Path(a.start).read_bytes()
    emu.pal[:] = raw[-768:]
    emu.uc.mem_write(VGA, raw[:64000])
    mice = {int(m.split(':')[0]): [int(v, 0) for v in m.split(':')[1:]] for m in a.mouse}
    if mice:   # a mouse driver is there from the start; its first place and buttons are the earliest --mouse
        emu.mouse = list(mice[min(mice)])
    emu.watch_pass(list(KIND), a.tick_div, {n + 1: str(work / '_last.raw')}, keys, mice=mice or None)
    sb = SoundBlaster(emu, work) if a.sb else None
    a.sb_log = sb
    rec = []
    flash = []   # the screen after each step of the picker face's flash (0x22087), with the pass it came in

    def flash_cb(uc, address, size, user):
        if emu.pass_count - 1 <= n:
            flash.append((emu.pass_count - 1, bytes(uc.mem_read(VGA, 64000))))

    emu.uc.hook_add(UC_HOOK_CODE, flash_cb, None, 0x22087, 0x22087)

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
        kind = KIND[address]
        if kind == 0:
            st[4:1004] = bytes(uc.mem_read(far(uc, ss, bp, 8), 1000))
            st[1004:1004 + 0x124] = bytes(uc.mem_read(far(uc, ss, bp, 0xA8), 0x124))
        if kind in (1, 3):
            st[4:4 + MON] = bytes(uc.mem_read(far(uc, ss, bp, 0x7C), MON))
            loc = lambda off: uc.mem_read(ss * 16 + ((bp - off) & 0xFFFF), 1)[0]  # noqa: E731
            st[4 + MON:4 + MON + len(LOCALS)] = bytes(loc(o) for o in LOCALS)
            q = 4 + MON + len(LOCALS)
            st[q:q + 200] = bytes(uc.mem_read(far(uc, ss, bp, -0xE), 200))
            st[q + 200:q + 260] = bytes(uc.mem_read(far(uc, ss, bp, -0xA) + 0xD4E2, 60))
        if kind in (5, 6, 7, 11):   # the lists and the picker get the party table as their first argument
            q = 4 + MON + len(LOCALS)
            st[q:q + 200] = bytes(uc.mem_read(far(uc, ss, bp, -6), 200))
        rec.append((kind, bytes(uc.mem_read(VGA, 64000)), bytes(emu.pal), bytes(st)))

    for top in KIND:
        emu.uc.hook_add(UC_HOOK_CODE, cb, None, top, top)
    # The loop tops set the clock to base + pass / tick_div. The run's own limit is in emulated time, which a battle's passes use more
    # of than the clock they are given, so it is generous: it only matters when the original waits somewhere that is not a loop top.
    emu.run((a.tick_base or 0) + n + 400, {})
    return rec, flash, ([e for e in sb.events if e[0] <= n] if sb else None)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('data')
    ap.add_argument('n', type=int)
    ap.add_argument('--tick-base', type=int, default=20)
    ap.add_argument('--tick-div', type=int, default=3)
    ap.add_argument('--key', action='append', default=[])
    ap.add_argument('--mouse', action='append', default=[], metavar='PASS:X:Y:BUTTONS',
                    help='a mouse driver is there; from pass PASS the pointer is at (X, Y) with BUTTONS held (bit 0 left)')
    ap.add_argument('--world-passes', type=int, metavar='K',
                    help='stop comparing K world passes after the battle (the world\'s own mouse buttons are not built)')
    ap.add_argument('--opl', action='store_true', help='an FM card is there (music on), in the original and the port')
    ap.add_argument('--sb', action='store_true',
                    help='a Sound Blaster is there (sounds on): the sounds started, cut and waited for are compared (tools/sb_stub.py)')
    ap.add_argument('--keep', default='work/battle_check')
    ap.add_argument('--start', default='work/check_credits/start.raw')
    ap.add_argument('--exe', default='work/unpacked/WALK.SL.exe')
    ap.add_argument('--port', default='build/walk_dump')
    ap.add_argument('--cache', help='keep the original\'s recording in this file and reuse it (same DATA, N, clock, keys and mouse)')
    a = ap.parse_args()
    Path(a.keep).mkdir(parents=True, exist_ok=True)
    keys = {int(k.split(':')[0]): int(k.split(':')[1], 0) for k in a.key}

    rec = None
    flash = None
    sounds = None
    fcache = Path(a.cache + '.flash') if a.cache else None
    scache = Path(a.cache + '.sb') if a.cache else None
    if a.cache and Path(a.cache).exists():
        rec = pickle.loads(zlib.decompress(Path(a.cache).read_bytes()))
        if rec and (len(rec[0][3]) != STATE or isinstance(rec[0][0], bool)):
            rec = None   # recorded by an older version of this tool
        q = 4 + MON + len(LOCALS)
        if rec and any(r[0] in (5, 6, 7) and not any(r[3][q:q + 200]) for r in rec):
            rec = None   # recorded before the lists' party table was
        if rec and fcache.exists():
            flash = pickle.loads(zlib.decompress(fcache.read_bytes()))
        elif rec and any(r[0] == 6 for r in rec):
            rec = None   # recorded before the picker face's flash steps were
        if rec and a.sb:
            if scache.exists():
                sounds = pickle.loads(zlib.decompress(scache.read_bytes()))
            else:
                rec = None
    if rec is None or len(rec) < a.n + 1:
        rec, flash, sounds = oracle(a, a.n, keys)
        if a.cache:
            Path(a.cache).write_bytes(zlib.compress(pickle.dumps(rec), 1))
            fcache.write_bytes(zlib.compress(pickle.dumps(flash), 1))
            if a.sb:
                scache.write_bytes(zlib.compress(pickle.dumps(sounds), 1))
    out = Path(a.keep) / 'port.bin'
    fout = Path(a.keep) / 'port_flash.bin'
    if fout.exists():
        fout.unlink()
    sout = Path(a.keep) / 'port_sounds.txt'
    if sout.exists():
        sout.unlink()
    start = a.start
    if a.mouse:
        # With a mouse the pointer keeps what was on the screen before the world loop's first pass, which the original has drawn
        # (the world window included) and the port's start does not: the port starts from the original's screen there.
        start = str(Path(a.keep) / 'start_loop.raw')
        Path(start).write_bytes(rec[0][1] + rec[0][2])
    r = subprocess.run([a.port, a.data, 'battle', str(out), str(a.n), str(a.tick_base), str(a.tick_div), start] + a.key +
                       ['m:' + m for m in a.mouse] + ([('fm-shown' if a.mouse else 'fm')] if a.opl else []) + ['f:' + str(fout)] +
                       ([('sb-shown' if a.mouse else 'sb'), 's:' + str(sout)] if a.sb else []),
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
    kinds = {}
    in_battle, back, stopped = False, 0, None   # --world-passes: how many world passes after a battle are compared
    for i in range(a.n):
        f = port[FRAME * i:FRAME * (i + 1)]
        pscr, ppal, pkind, pst = f[:64000], f[64000:64768], f[64768], f[64769:]
        if pkind == 2:   # the party has lost in this pass: the original must have left the battle loop in it (for MARK.EXE)
            ended = i
            # (after it the oracle, whose exec is not taken, runs on into the world's level-ups and world loop: not compared)
            if i + 1 < len(rec) and rec[i + 1][0] in (1, 3):
                print(f'pass {i}: the port lost the battle, the original did not leave the battle')
                first['end'] = i
            break
        if i + 1 >= len(rec):
            print(f'oracle: only {len(rec)} arrivals for {a.n} passes')
            return 2
        kind, scr, pal, st = rec[i + 1]
        if kind != 0:
            in_battle = True
        elif in_battle and a.world_passes is not None:
            back += 1
            if back > a.world_passes:
                stopped = i
                break
        kinds[kind] = kinds.get(kind, 0) + 1
        checks = {'loop': pkind == kind, 'screen': pscr == scr, 'palette': ppal == pal, 'rand': pst[:4] == st[:4]}
        if pkind != kind and 'loop' not in first:
            print(f'pass {i}: the original is in the {KIND_NAME.get(kind)} loop, the port in the {KIND_NAME.get(pkind)} loop')
        if kind == 0 and pkind == 0:
            checks['villagers'] = pst[4:1004] == st[4:1004]
            checks['party'] = pst[1004:1004 + 0x124] == st[1004:1004 + 0x124]
        if kind in (1, 3) and pkind == kind:
            checks['monsters'] = pst[4:4 + MON] == st[4:4 + MON]
            checks['sway'] = pst[4 + MON:6 + MON] == st[4 + MON:6 + MON]
            q = 4 + MON + len(LOCALS)
            checks['party'] = pst[q:q + 200] == st[q:q + 200]
            checks['numbers'] = pst[q + 200:q + 260] == st[q + 200:q + 260]
        if kind in (5, 6, 7, 11) and pkind == kind:
            q = 4 + MON + len(LOCALS)
            checks['party'] = pst[q:q + 200] == st[q:q + 200]
        if kind == 1 and pkind == 1:   # the rewards loop uses some of these locals for other things
            checks['turn'] = pst[6 + MON:8 + MON] == st[6 + MON:8 + MON]
            checks['command'] = pst[8 + MON:10 + MON] == st[8 + MON:10 + MON]
            checks['key latch'] = pst[10 + MON] == st[10 + MON]
            pa, oa = bytearray(pst[11 + MON:q]), bytearray(st[11 + MON:q])
            if a.opl:   # [bp-0x58] starts as what the FM driver's timer interrupt pushed there (1 .. 0xfe all act as 1)
                j = LOCALS.index(0x58) - 7
                pa[j], oa[j] = min(pa[j], 1), min(oa[j], 1)
            checks['attack locals'] = pa == oa
        for k, ok in checks.items():
            if not ok and k not in first:
                first[k] = i
                if k == 'screen':
                    d = [j for j in range(64000) if pscr[j] != scr[j]]
                    xs, ys = [j % 320 for j in d], [j // 320 for j in d]
                    print(f'pass {i}: screen differs in {len(d)} pixels, ({min(xs)}, {min(ys)}) .. ({max(xs)}, {max(ys)})')
                    Path(a.keep, f'diff_{i}_orig.raw').write_bytes(scr + pal)
                    Path(a.keep, f'diff_{i}_port.raw').write_bytes(pscr + ppal)
                elif k in ('monsters', 'sway', 'turn', 'rand', 'command', 'key latch', 'attack locals', 'party', 'numbers', 'villagers'):
                    d = [j for j in range(STATE) if st[j] != pst[j]]
                    if kind == 0:
                        names = ['rand'] * 4 + [f'villager {j // 50}+{j % 50:#x}' for j in range(1000)] + [f'party+{j:#x}' for j in range(0x124)]
                    else:
                        names = ['rand'] * 4 + [f'mon+{j:#x}' for j in range(MON)] + [f'bp-{o:#x}' for o in LOCALS] + \
                            [f'party+{j:#x}' for j in range(200)] + [f'number {j // 3}.{j % 3}' for j in range(60)]
                        names += ['?'] * (STATE - len(names))
                    print(f'pass {i}: {k} differs: ' + ', '.join(f'{names[j]} original {st[j]:#04x} port {pst[j]:#04x}' for j in d[:12]))
                else:
                    print(f'pass {i}: {k} differs')
    n = a.n if ended is None else ended
    n = n if stopped is None else stopped
    # the picker face's flash: the screen after each of its steps, in the passes compared
    pf = []
    if fout.exists():
        fb = fout.read_bytes()
        pf = [(struct.unpack_from('<i', fb, k)[0], fb[k + 4:k + 64004]) for k in range(0, len(fb), 64004)]
    of = [(p, s) for p, s in (flash or []) if p < n]
    pf = [(p, s) for p, s in pf if p < n]
    if len(of) != len(pf):
        print(f'the picker face\'s flash: {len(of)} steps in the original, {len(pf)} in the port')
        first['flash'] = 0
    else:
        for j, ((op, os_), (pp, ps)) in enumerate(zip(of, pf)):
            if op != pp or os_ != ps:
                print(f'the picker face\'s flash, step {j}: original pass {op}, port pass {pp}, '
                      f'{sum(x != y for x, y in zip(os_, ps))} pixels differ')
                first['flash'] = j
                break
    if a.sb:   # the sounds: started (which block of which file), cut and waited for, by pass
        ps = []
        if sout.exists():
            for line in sout.read_text().split('\n'):
                f = line.split()
                if f:
                    ps.append((int(f[0]), f[1]) + ((f[2], int(f[3], 0)) if f[1] == 'play' else ()))
        os2 = [e for e in sounds if e[0] < n]
        ps = [e for e in ps if e[0] < n]
        if os2 != ps:
            j = next((j for j, (x, y) in enumerate(zip(os2, ps)) if x != y), min(len(os2), len(ps)))
            show = lambda e: ' '.join(str(v) if not isinstance(v, int) or k < 3 else hex(v) for k, v in enumerate(e))  # noqa: E731
            print(f'sounds: {len(os2)} in the original, {len(ps)} in the port; event {j}: original '
                  f'{show(os2[j]) if j < len(os2) else "none"}, port {show(ps[j]) if j < len(ps) else "none"}')
            first['sounds'] = j
        else:
            kinds['sounds'] = len(os2)
    if first:
        return 1
    if of:
        kinds['flash'] = len(of)
    end = '' if ended is None else f'; the party lost at pass {ended} in both (not compared after)'
    if stopped is not None:
        end = f'; back in the world at pass {stopped - a.world_passes}, compared {a.world_passes} world passes more'
    print(f'all {n} passes identical (' + ', '.join(f'{v} {KIND_NAME[k]}' for k, v in sorted(kinds.items(), key=str)) + f'){end}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
