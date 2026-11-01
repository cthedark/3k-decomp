#!/usr/bin/env python3
"""Compare WALK.SL's battle in the C port with the original, pass by pass (docs/findings.md, "WALK.SL steps 13 to 16").

Usage: work/venv/bin/python tools/battle_check.py DATA N [--tick-base B] [--tick-div D] [--key PASS:CODE ...] [--keep DIR]
                                                 [--cache FILE] [--port BIN]

DATA is a data directory that starts the game on an enemy (tools/walk_scene_data.py DIR 1 106 36). The oracle runs WALK.SL with
nine loop tops counted as one sequence of passes: the world (0x11d36), the battle (0x1d418), the rewards bar after a win (0x1fb7d),
a message box's wait (0x1b506), the magic list (0x217a6), the member picker (0x1aa30) and the item list (0x207b2; in these three only
the screen, palette and rand state are compared), a talk's yes / no buttons (0x18d23) and its number (0x19166; the same); then the
F-keys' own loops (F3's waits, F6's equipment screen, F7's prompt, F8's map 0x1ca82, F9's order 0x1b834, F10's data 0x1a641; the same). At each arrival it records which loop, the screen, the palette, the rand state and, in the battle
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
# F7's prompt, coins or an item (0x166a8): screen, palette and rand
USE_TOP = 0x166A8
# F8's map (0x1c6e4): screen, palette and rand
MAP_TOP = 0x1CA82
# F9's party order (0x1b748): screen, palette, rand and the party
ORDER_TOP = 0x1B834
# F10's party data (0x1a122): screen, palette and rand
DATA_TOP = 0x1A641
SHOP_TOP, INN_TOP = 0x23386, 0x240EC
# the card game of a kind 0xe record (0x24962): its betting loop and its wait after a round
CARDS_TOP, CARDS_WAIT = 0x25088, 0x258AD
KIND = {WORLD_TOP: 0, BATTLE_TOP: 1, REWARDS_TOP: 3, DIALOG_TOP: 4, MAGIC_TOP: 5, PICK_TOP: 6, ITEMS_TOP: 7, YESNO_TOP: 8,
        NUMBER_TOP: 9, TAKE_TOPS[0]: 10, TAKE_TOPS[1]: 10, EQUIP_TOP: 11, USE_TOP: 12, MAP_TOP: 13, ORDER_TOP: 14, DATA_TOP: 15, SHOP_TOP: 17, INN_TOP: 18,
        CARDS_TOP: 19, CARDS_WAIT: 20}
KIND_NAME = {0: 'world', 1: 'battle', 2: 'lost', 3: 'rewards', 4: 'message box', 5: 'magic list', 6: 'member picker',
             7: 'item list', 8: 'yes / no', 9: 'number', 10: 'take', 11: 'equipment', 12: 'use prompt', 13: 'map', 14: 'order', 15: 'data',
             'flash': 'face flash and zoom steps', 'sounds': 'sounds', 'files': 'files written', 16: 'left', 17: 'shop', 18: 'inn', 19: 'cards', 20: 'cards end'}
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

    def zoom_cb(uc, address, size, user):   # a door's steps: its picture (0x17a00), the zoom out (0x19a8e) and in (0x19b75), not the start's
        if 1 <= emu.pass_count and emu.pass_count - 1 <= n:
            flash.append((emu.pass_count - 1, bytes(uc.mem_read(VGA, 64000))))

    # and the boat ride's steps (0x1c21a, after its window copy), a warp's turning and sliding rounds (0x17441, 0x17563)
    # and the card game's steps: the window shaded (0x24a17) and a card dealt or taken back (0x256c6, 0x2576d, 0x25a66, 0x25b14)
    for top in (0x17A00, 0x19A8E, 0x19B75, 0x1C21A, 0x17441, 0x17563, 0x24A17, 0x256C6, 0x2576D, 0x25A66, 0x25B14):
        emu.uc.hook_add(UC_HOOK_CODE, zoom_cb, None, top, top)

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
        if kind in (5, 6, 7, 11, 14):   # the lists and the picker get the party table as their first argument
            q = 4 + MON + len(LOCALS)
            st[q:q + 200] = bytes(uc.mem_read(far(uc, ss, bp, -6), 200))
        rec.append((kind, bytes(uc.mem_read(VGA, 64000)), bytes(emu.pal), bytes(st)))

    for top in KIND:
        emu.uc.hook_add(UC_HOOK_CODE, cb, None, top, top)
    # 0x18c29: the end of WALK.SL (key 4's yes) calls execl("mark.exe"); the emulator's C runtime returns from it without the exec
    # reaching DOS (the run then ends with exit code 255), so the call itself is what is recorded
    execd = []
    emu.uc.hook_add(UC_HOOK_CODE, lambda uc, address, size, user: execd.append(emu.pass_count - 1), None, 0x18C29, 0x18C29)
    # 0x18634: a kind 0x13 record's execl("ending.sl"); here the C runtime's execl returns and WALK.SL would go on with its
    # buffers freed, so the run stops at the call
    ended = []

    def end_cb(uc, address, size, user):
        ended.append(emu.pass_count - 1)
        emu.stop_reason = 'exec ending.sl'
        uc.emu_stop()

    emu.uc.hook_add(UC_HOOK_CODE, end_cb, None, 0x18634, 0x18634)
    # The loop tops set the clock to base + pass / tick_div. The run's own limit is in emulated time, which a battle's passes use more
    # of than the clock they are given, so it is generous: it only matters when the original waits somewhere that is not a loop top.
    emu.run((a.tick_base or 0) + n + 400, {})
    # the files the run created or changed (key 3's saves, __1.NOW), and whether it ended by starting another program (key 4)
    files = {}
    for p in work.iterdir():
        src = Path(a.data) / p.name
        if p.name.startswith('_last') or not p.is_file():
            continue
        if not src.exists() or src.read_bytes() != p.read_bytes():
            files[p.name.upper()] = p.read_bytes()
    files['__stop__'] = (f'exec mark.exe at pass {execd[0]}' if execd else f'exec ending.sl at pass {ended[0]}' if ended
                         else emu.stop_reason or '').encode()
    return rec, flash, ([e for e in sb.events if e[0] <= n] if sb else None), files


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
    files = None
    fcache = Path(a.cache + '.flash') if a.cache else None
    scache = Path(a.cache + '.sb') if a.cache else None
    wcache = Path(a.cache + '.files') if a.cache else None
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
        if rec and wcache.exists():   # recordings made before the files were kept have none (they never save)
            files = pickle.loads(zlib.decompress(wcache.read_bytes()))
    exec_end = files is not None and files.get('__stop__', b'').startswith(b'exec')
    if rec is None or (len(rec) < a.n + 1 and not exec_end):
        rec, flash, sounds, files = oracle(a, a.n, keys)
        if a.cache:
            Path(a.cache).write_bytes(zlib.compress(pickle.dumps(rec), 1))
            fcache.write_bytes(zlib.compress(pickle.dumps(flash), 1))
            wcache.write_bytes(zlib.compress(pickle.dumps(files), 1))
            if a.sb:
                scache.write_bytes(zlib.compress(pickle.dumps(sounds), 1))
    exec_end = files is not None and files.get('__stop__', b'').startswith(b'exec')
    pdir = Path(a.keep) / 'port_files'
    if pdir.exists():
        shutil.rmtree(pdir)
    pdir.mkdir(parents=True)
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
                       ([('sb-shown' if a.mouse else 'sb'), 's:' + str(sout)] if a.sb else []) + ['w:' + str(pdir)],
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
    left, left_prog = None, 'MARK.EXE'
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
        if pkind in (16, 21):   # key 4's yes or a kind 0x13 record: WALK.SL has ended in this pass; the original must have
            left = i            # started MARK.EXE (16) or ENDING.SL (21) there
            stop = files.get('__stop__', b'').decode() if files else ''
            prog = 'mark.exe' if pkind == 16 else 'ending.sl'
            left_prog = prog.upper()
            if i + 1 < len(rec) or not exec_end or stop != f'exec {prog} at pass {i}':
                print(f'pass {i}: the port left WALK.SL; the original: {stop or "did not"}')
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
        if kind in (5, 6, 7, 11, 14) and pkind == kind:
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
    n = n if left is None else left
    n = n if stopped is None else stopped
    # the files written (key 3's saves, __1.NOW): the same names and bytes; skipped for recordings made before they were kept
    if files is not None:
        ofiles = {k: v for k, v in files.items() if k != '__stop__'}
        if ended is not None:   # a lost battle: the original writes 0x3e into ANI.LCP for MARK.EXE's game-over (not walk_dump's part)
            ofiles.pop('ANI.LCP', None)
        pfiles = {p.name.upper(): p.read_bytes() for p in pdir.iterdir() if p.is_file()}
        for k in list(pfiles):   # written with the bytes the data directory has already: unchanged, as the oracle's list counts it
            src = next((q for q in Path(a.data).iterdir() if q.name.upper() == k), None)
            if src is not None and src.read_bytes() == pfiles[k]:
                del pfiles[k]
        if set(ofiles) != set(pfiles):
            print(f'files written: original {sorted(ofiles)}, port {sorted(pfiles)}')
            first['files'] = 0
        else:
            for k in sorted(ofiles):
                if ofiles[k] != pfiles[k]:
                    o, p = ofiles[k], pfiles[k]
                    j = next((j for j in range(min(len(o), len(p))) if o[j] != p[j]), min(len(o), len(p)))
                    print(f'file {k}: {len(o)} bytes in the original, {len(p)} in the port; first difference at {j}')
                    first['files'] = 0
                    break
            if ofiles and 'files' not in first:
                kinds['files'] = len(ofiles)
    # the picker face's flash and a door's zoom steps: the screen after each step, in the passes compared
    pf = []
    if fout.exists():
        fb = fout.read_bytes()
        pf = [(struct.unpack_from('<i', fb, k)[0], fb[k + 4:k + 64004]) for k in range(0, len(fb), 64004)]
    of = [(p, s) for p, s in (flash or []) if p < n]
    pf = [(p, s) for p, s in pf if p < n]
    if len(of) != len(pf):
        print(f'the flash and zoom steps: {len(of)} steps in the original, {len(pf)} in the port')
        first['flash'] = 0
    else:
        for j, ((op, os_), (pp, ps)) in enumerate(zip(of, pf)):
            if op != pp or os_ != ps:
                print(f'the flash and zoom steps, step {j}: original pass {op}, port pass {pp}, '
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
    if left is not None:
        end = f'; WALK.SL left for {left_prog} at pass {left} in both'
    if stopped is not None:
        end = f'; back in the world at pass {stopped - a.world_passes}, compared {a.world_passes} world passes more'
    print(f'all {n} passes identical (' + ', '.join(f'{v} {KIND_NAME[k]}' for k, v in sorted(kinds.items(), key=str)) + f'){end}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
