#!/usr/bin/env python3
"""Compare the world's start zoom (docs/findings.md, "WALK.SL step 23") with the original.

Usage: work/venv/bin/python tools/zoom_check.py DATA [--start RAW] [--keep DIR] [--port BIN]

DATA is a data directory (tools/walk_scene_data.py). The oracle runs WALK.SL up to its main loop's first arrival (0x11d36) and keeps
the screen each time the start's zoom (0x19ab2) has shown a step (after its call of 0x27328 at 0x19b75), then the screen at the loop's
first arrival; build/walk_dump DATA zoom draws the same steps over the same start screen. Prints the first difference of each step,
or "all 13 steps identical", exit status 1 on a difference, 2 if a run fails.
"""
import argparse
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from dosemu import Emu, VGA  # noqa: E402
from unicorn import UC_HOOK_CODE  # noqa: E402

STEP_SHOWN, LOOP_TOP, STEPS = 0x19B75, 0x11D36, 13


def oracle(a):
    work = Path(a.keep) / 'emu'
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)
    for p in Path(a.data).iterdir():
        if p.is_file():
            shutil.copy2(p, work / p.name)
    emu = Emu('work/unpacked/WALK.SL.exe', work, 250_000, False)
    raw = Path(a.start).read_bytes()
    emu.pal[:] = raw[-768:]
    emu.uc.mem_write(VGA, raw[:64000])
    shots, done = [], []

    def shown(uc, address, size, user):
        if not done:
            shots.append(bytes(uc.mem_read(VGA, 64000)) + bytes(emu.pal))

    def top(uc, address, size, user):
        if not done:
            shots.append(bytes(uc.mem_read(VGA, 64000)) + bytes(emu.pal))
            done.append(1)
        uc.emu_stop()

    emu.uc.hook_add(UC_HOOK_CODE, shown, None, STEP_SHOWN, STEP_SHOWN)
    emu.uc.hook_add(UC_HOOK_CODE, top, None, LOOP_TOP, LOOP_TOP)
    emu.run(400, {})
    return shots


def first_diff(x, y):
    d = [i for i in range(64000) if x[i] != y[i]]
    if not d:
        return 'palette' if x[64000:] != y[64000:] else None
    xs, ys = [i % 320 for i in d], [i // 320 for i in d]
    return f'{len(d)} pixels, ({min(xs)}, {min(ys)}) .. ({max(xs)}, {max(ys)})'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('data')
    ap.add_argument('--start', default='work/check_credits/start.raw')
    ap.add_argument('--keep', default='work/zoom_check')
    ap.add_argument('--port', default='build/walk_dump')
    a = ap.parse_args()
    Path(a.keep).mkdir(parents=True, exist_ok=True)
    shots = oracle(a)
    if len(shots) != STEPS + 1:
        print(f'oracle: {len(shots)} pictures, want {STEPS} steps and the loop top')
        return 2
    out = Path(a.keep) / 'port.bin'
    r = subprocess.run([a.port, a.data, 'zoom', str(out), a.start], capture_output=True, text=True)
    sys.stderr.write(r.stderr)
    if r.returncode != 0:
        print('port run failed:', r.returncode)
        return 2
    port = out.read_bytes()
    if len(port) != 64768 * STEPS:
        print(f'port: {len(port)} bytes, want {64768 * STEPS}')
        return 2
    bad = 0
    for k in range(STEPS):
        p = port[64768 * k:64768 * (k + 1)]
        d = first_diff(shots[k], p)
        if d:
            print(f'step {k}: differs: {d}')
            (Path(a.keep) / f'step{k}_orig.raw').write_bytes(shots[k])
            (Path(a.keep) / f'step{k}_port.raw').write_bytes(p)
            bad += 1
    if shots[STEPS] != shots[STEPS - 1]:   # nothing is drawn between the last step and the loop
        print('the screen changed between the last step and the main loop')
        bad += 1
    if bad:
        return 1
    print(f'all {STEPS} steps identical')
    return 0


if __name__ == '__main__':
    sys.exit(main())
