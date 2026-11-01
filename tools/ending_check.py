#!/usr/bin/env python3
"""Compare ENDING.SL in the C port with the original, pass by pass (docs/findings.md, "ENDING.SL step 2" on).

Usage: work/venv/bin/python tools/ending_check.py [--n N] [--cache FILE] [--port build/ending_dump] [--keep DIR] [--probe]

The original runs in tools/dosemu.py (no sound card, no mouse). Each arrival at one of the loop tops in TOPS is recorded with the
screen and the palette as they are then, that is after the pass before it; the end of a loop (the first instruction after it) is
recorded the same way. The port (tests/ending_dump.c) writes the same records, one per pass, with the same loop numbers. The run
compares the first N records: loop, screen and palette.

A record keeps the screen's CRC-32 (zlib's), and the whole screen only when it differs from the record before (the long loops of the
fifth scene repeat one picture for hundreds of passes). The port's dump has the same CRCs; on a difference the port is run again with
ENDING_FULL=i, which makes it write record i's whole screen too, and both screens are kept for a look.
"""
import argparse
import os
import subprocess
import pickle
import shutil
import struct
import sys
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from dosemu import Emu, VGA  # noqa: E402
from unicorn import UC_HOOK_CODE  # noqa: E402
from unicorn.x86_const import UC_X86_REG_BP, UC_X86_REG_SS  # noqa: E402

# loop top -> its number (the port's record carries the same number); 0x10603 is the first scene's end
TOPS = {0x1042D: 1, 0x10603: 2, 0x106D1: 3, 0x108D0: 4, 0x10968: 5, 0x10BA1: 6, 0x10CD1: 7, 0x10D2A: 8, 0x10D58: 9,
        0x10EA0: 10, 0x10FD6: 11, 0x1101A: 12, 0x11078: 13, 0x110C8: 14,
        0x11129: 15, 0x1119F: 16, 0x11218: 17, 0x112C1: 18, 0x11384: 19}
NAMES = {1: 'scene 1', 2: 'scene 1 ended', 3: 'scene 2', 4: 'scene 2 ended', 5: 'scene 3', 6: 'scene 3 ended',
         7: 'scene 4 shake', 8: 'scene 4 shake ended', 9: 'scene 4 strobe', 10: 'scene 4 ended',
         11: 'scene 5 face', 12: 'scene 5 face ended', 13: 'scene 5 eyes', 14: 'scene 5 ended',
         15: 'scene 6 wipe', 16: 'scene 6 fade', 17: 'scene 6 ended',
         18: 'credits', 19: 'credits ended'}
FRAME = 1 + 4 + 768


def oracle(n, keep, probe):
    work = Path(keep) / 'emu'
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)
    for p in Path('original_bin').iterdir():
        if p.is_file():
            shutil.copy2(p, work / p.name)
    emu = Emu('work/unpacked/ENDING.SL.unpacked.exe', work, 250_000, False)
    rec = []

    def cb(uc, address, size, user):
        if len(rec) >= n:
            emu.stop_reason = f'{n} records'
            uc.emu_stop()
            return
        if probe and not rec:
            ss, bp = emu.r(UC_X86_REG_SS), emu.r(UC_X86_REG_BP)
            loc = lambda o: struct.unpack('<H', bytes(uc.mem_read(ss * 16 + ((bp - o) & 0xFFFF), 2)))[0]  # noqa: E731
            print('probe at the first loop top: ' + ' '.join(f'[bp-{o:#x}] {loc(o):#06x}' for o in (0x48, 0x3e, 0x40, 0x42)))
        scr = bytes(uc.mem_read(VGA, 64000))
        crc = zlib.crc32(scr)
        full = scr if not rec or rec[-1][1] != crc else None
        rec.append((TOPS[address], crc, bytes(emu.pal), emu.ticks, full))

    for top in TOPS:
        emu.uc.hook_add(UC_HOOK_CODE, cb, None, top, top)
    emu.run(20000, {})
    return rec


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--n', type=int, default=400)
    ap.add_argument('--cache')
    ap.add_argument('--port', default='build/ending_dump')
    ap.add_argument('--keep', default='work/ending_check')
    ap.add_argument('--probe', action='store_true')
    ap.add_argument('--pace', action='store_true', help='print each loop\'s passes and BIOS ticks in the original')
    a = ap.parse_args()
    Path(a.keep).mkdir(parents=True, exist_ok=True)
    rec = None
    if a.cache and Path(a.cache).exists():
        rec = pickle.loads(zlib.decompress(Path(a.cache).read_bytes()))
        if (len(rec) < a.n and rec[-1][0] != max(TOPS.values())) or isinstance(rec[0][1], bytes):
            rec = None
    if rec is None:
        rec = oracle(a.n, a.keep, a.probe)
        if a.cache:
            Path(a.cache).write_bytes(zlib.compress(pickle.dumps(rec), 1))
    if a.pace and len(rec[0]) > 3:
        spans = {}
        for (k, _, _, t0, *_), (_, _, _, t1, *_) in zip(rec, rec[1:]):
            n, t = spans.get(k, (0, 0))
            spans[k] = (n + 1, t + t1 - t0)
        for k, (n, t) in spans.items():
            print(f'{NAMES.get(k)}: {n} passes in {t} ticks, {t * 54.925 / n:.1f} ms a pass')
    out = Path(a.keep) / 'port.bin'
    r = subprocess.run([a.port, 'original_bin', str(len(rec)), str(out)], capture_output=True, text=True)
    if r.returncode != 0:
        print('port run failed:', r.returncode, r.stderr.strip()[:300])
        return 2
    pb = out.read_bytes()
    prec = [(pb[i], struct.unpack('<I', pb[i + 1:i + 5])[0], pb[i + 5:i + FRAME]) for i in range(0, len(pb) - FRAME + 1, FRAME)]

    def screen_of(i):   # the original's whole screen at record i: the last one kept at or before it
        while rec[i][4] is None:
            i -= 1
        return rec[i][4]

    bad = 0
    for i, (k, scr, pal, *_) in enumerate(rec):
        if i >= len(prec):
            print(f'record {i}: the port stopped after {len(prec)} records ({NAMES.get(k)} in the original)')
            return 1
        pk, pscr, ppal = prec[i]
        diffs = []
        if pk != k:
            diffs.append(f'loop: original {NAMES.get(k)}, port {NAMES.get(pk)}')
        if pscr != scr:
            full = Path(a.keep) / f'diff_{i}_port.raw'
            subprocess.run([a.port, 'original_bin', str(i + 1), str(Path(a.keep) / 'port_one.bin')], capture_output=True,
                           env={**os.environ, 'ENDING_FULL': str(i), 'ENDING_FULL_OUT': str(full)})
            oscr = screen_of(i)
            pfull = full.read_bytes()[:64000] if full.exists() else bytes(64000)
            px = [j for j in range(64000) if pfull[j] != oscr[j]] or [0]
            diffs.append(f'screen differs in {len(px)} pixels, ({px[0] % 320}, {px[0] // 320}) .. ({px[-1] % 320}, {px[-1] // 320})')
            (Path(a.keep) / f'diff_{i}_orig.raw').write_bytes(oscr + pal)
        if ppal != pal:
            diffs.append('palette differs')
        if diffs:
            print(f'record {i}: ' + '; '.join(diffs))
            bad += 1
            if bad >= 3:
                return 1
    if bad:
        return 1
    counts = {}
    for k, *_ in rec:
        counts[NAMES.get(k)] = counts.get(NAMES.get(k), 0) + 1
    print(f'all {len(rec)} records identical (' + ', '.join(f'{v} {k}' for k, v in counts.items()) + ')')
    return 0


if __name__ == '__main__':
    sys.exit(main())
