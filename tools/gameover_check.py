#!/usr/bin/env python3
"""Compare MARK.EXE's game-over screen in the C port (src/gameover.c) with the original, pass by pass (docs/findings.md,
"WALK.SL step 16").

Usage: work/venv/bin/python tools/gameover_check.py START.raw N [--data original_bin] [--keep DIR] [--port build/walk_dump]

START is the screen WALK.SL leaves when the party has lost (64,000 pixels then the 768-byte palette). The oracle runs MARK.EXE with
ANI.LCP byte 0x2ff set to 0x3e (WALK.SL's message) and START on the screen and in the DAC, counting the game-over loop's top
(0x1040a) as passes; build/walk_dump DATA gameover runs the port for the same passes. Prints "all N passes identical" or the first
difference; exit status 1 on a difference, 2 if a run fails.
"""
import argparse
import shutil
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from dosemu import Emu, VGA  # noqa: E402
from unicorn import UC_HOOK_CODE  # noqa: E402

TOP = 0x1040A


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('start')
    ap.add_argument('n', type=int)
    ap.add_argument('--data', default='original_bin')
    ap.add_argument('--keep', default='work/gameover_check')
    ap.add_argument('--port', default='build/walk_dump')
    ap.add_argument('--exe', default='work/unpacked/MARK.unpacked.exe')
    ap.add_argument('--key', action='append', default=[], metavar='PASS:CODE', help='the keyboard port from that pass on')
    ap.add_argument('--mouse', action='append', default=[], metavar='PASS:X:Y:BUTTONS',
                    help='a mouse driver is there; from pass PASS the pointer is at (X, Y) with BUTTONS held')
    a = ap.parse_args()
    keys = {int(k.split(':')[0]): int(k.split(':')[1], 0) for k in a.key}
    mice = {int(m.split(':')[0]): [int(v, 0) for v in m.split(':')[1:]] for m in a.mouse}
    keep = Path(a.keep)
    work = keep / 'emu'
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)
    for p in Path(a.data).iterdir():
        if p.is_file():
            shutil.copy2(p, work / p.name)
    ani = next(p for p in work.iterdir() if p.name.lower() == 'ani.lcp')
    b = bytearray(ani.read_bytes())
    b[0x2FF] = 0x3E
    ani.write_bytes(bytes(b))
    raw = Path(a.start).read_bytes()
    emu = Emu(a.exe, work, 250_000, False)
    emu.pal[:] = raw[-768:]
    emu.uc.mem_write(VGA, raw[:64000])
    if mice:
        emu.mouse = list(mice[min(mice)])
    emu.watch_pass(TOP, 1, {}, keys, mice=mice or None)
    rec = []

    def cb(uc, address, size, user):
        rec.append((bytes(uc.mem_read(VGA, 64000)), bytes(emu.pal)))
        if len(rec) > a.n:
            uc.emu_stop()
    emu.uc.hook_add(UC_HOOK_CODE, cb, None, TOP, TOP)
    emu.run(a.n + 400, {})
    ends = len(rec) < a.n + 1   # a key or a button left the loop: its last pass is len(rec) - 1
    if ends and not (keys or mice):
        print(f'oracle: only {len(rec)} arrivals')
        return 2
    out = keep / 'port.bin'
    r = subprocess_run([a.port, a.data, 'gameover', str(out), str(a.n), a.start] + a.key + ['m:' + m for m in a.mouse])
    if r != 0:
        print('port run failed:', r)
        return 2
    port = out.read_bytes()
    F = 64768
    np = len(port) // F
    want = len(rec) if ends else a.n
    for i in range(min(np, want, len(rec) - 1)):
        p, (s, pal) = port[F * i:F * (i + 1)], rec[i + 1]
        if p[:64000] != s or p[64000:] != pal:
            d = [j for j in range(64000) if p[j] != s[j]]
            print(f'pass {i}: differs ({len(d)} pixels' + (f', first at ({d[0] % 320}, {d[0] // 320})' if d else ', palette') + ')')
            (keep / f'diff_{i}_orig.raw').write_bytes(s + pal)
            (keep / f'diff_{i}_port.raw').write_bytes(p)
            return 1
    if np != want:
        print(f'the original ends after {want} passes, the port after {np}')
        return 1
    print(f'all {want} passes identical' + (f'; both end in pass {want - 1}' if ends else ''))
    return 0


def subprocess_run(cmd):
    import subprocess
    r = subprocess.run(cmd, capture_output=True, text=True)
    sys.stderr.write(r.stderr)
    return r.returncode


if __name__ == '__main__':
    sys.exit(main())
