#!/usr/bin/env python3
"""Compare ENDING.SL in the C port with the original, pass by pass (docs/findings.md, "ENDING.SL step 2" on).

Usage: work/venv/bin/python tools/ending_check.py [--n N] [--cache FILE] [--port build/ending_dump] [--keep DIR] [--probe]

The original runs in tools/dosemu.py (no sound card, no mouse). Each arrival at one of the loop tops in TOPS is recorded with the
screen and the palette as they are then, that is after the pass before it; the end of a loop (the first instruction after it) is
recorded the same way. The port (tests/ending_dump.c) writes the same records, one per pass, with the same loop numbers. The run
compares the first N records: loop, screen and palette.
"""
import argparse
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
TOPS = {0x1042D: 1, 0x10603: 2, 0x106D1: 3, 0x108D0: 4}
NAMES = {1: 'scene 1', 2: 'scene 1 ended', 3: 'scene 2', 4: 'scene 2 ended'}
FRAME = 1 + 64000 + 768


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
        rec.append((TOPS[address], bytes(uc.mem_read(VGA, 64000)), bytes(emu.pal)))

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
    a = ap.parse_args()
    Path(a.keep).mkdir(parents=True, exist_ok=True)
    rec = None
    if a.cache and Path(a.cache).exists():
        rec = pickle.loads(zlib.decompress(Path(a.cache).read_bytes()))
        if len(rec) < a.n and rec[-1][0] != max(TOPS.values()):
            rec = None
    if rec is None:
        rec = oracle(a.n, a.keep, a.probe)
        if a.cache:
            Path(a.cache).write_bytes(zlib.compress(pickle.dumps(rec), 1))
    import subprocess
    out = Path(a.keep) / 'port.bin'
    r = subprocess.run([a.port, 'original_bin', str(len(rec)), str(out)], capture_output=True, text=True)
    if r.returncode != 0:
        print('port run failed:', r.returncode, r.stderr.strip()[:300])
        return 2
    pb = out.read_bytes()
    prec = [(pb[i], pb[i + 1:i + 64001], pb[i + 64001:i + FRAME]) for i in range(0, len(pb) - FRAME + 1, FRAME)]
    bad = 0
    for i, (k, scr, pal) in enumerate(rec):
        if i >= len(prec):
            print(f'record {i}: the port stopped after {len(prec)} records ({NAMES.get(k)} in the original)')
            return 1
        pk, pscr, ppal = prec[i]
        diffs = []
        if pk != k:
            diffs.append(f'loop: original {NAMES.get(k)}, port {NAMES.get(pk)}')
        if pscr != scr:
            px = [j for j in range(64000) if pscr[j] != scr[j]]
            diffs.append(f'screen differs in {len(px)} pixels, ({px[0] % 320}, {px[0] // 320}) .. ({px[-1] % 320}, {px[-1] // 320})')
            (Path(a.keep) / f'diff_{i}_orig.raw').write_bytes(scr + pal)
            (Path(a.keep) / f'diff_{i}_port.raw').write_bytes(pscr + ppal)
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
    for k, _, _ in rec:
        counts[NAMES.get(k)] = counts.get(NAMES.get(k), 0) + 1
    print(f'all {len(rec)} records identical (' + ', '.join(f'{v} {k}' for k, v in counts.items()) + ')')
    return 0


if __name__ == '__main__':
    sys.exit(main())
