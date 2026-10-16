#!/usr/bin/env python3
"""Record the OPL register writes of the ORIGINAL's FM driver for any song of MUC.ALL.

CAST.SL always reads 20,000 bytes at offset 2 of MUC.ALL and plays them (song 0). This tool builds a small private
copy of the data in which that range holds song N instead (MUC.ALL's other bytes and length are unchanged), runs CAST.SL
unpacked in the oracle emulator (tools/dosemu.py --opl-log) on it, and writes the driver's register writes to OUT.
The data copy is made under work/opl/ and the original files are never modified.

Usage: tools/capture_song_opl.py SONG_INDEX OUT.txt [BIOS_TICKS]
  SONG_INDEX   0..24, in file order (the offsets of the "CTMF" signatures, see docs/formats.md)
  BIOS_TICKS   how long to run the program (default 1560: CAST.SL starts the menu again after about 1,576, and
               one BIOS tick is 1/18.2 s, so this is the first 85 seconds of the song)
OUT has lines "interrupt register value" (hex register and value; the interrupt count is the song tick plus a constant
offset, see tools/compare_opl.py). Needs work/venv with unicorn and work/unpacked/CAST.SL.exe (docs/environment.md).
"""
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "original_bin"
NEED = ["CAST.SL", "CAST1.LCZ", "CAST2.LCZ", "ANI.LCP", "MUC.ALL", "WALK0.SL"]


def song_offsets(data):
    offs, i = [], 0
    while True:
        i = data.find(b"CTMF", i)
        if i < 0:
            break
        offs.append(i)
        i += 4
    offs.append(len(data))
    return offs


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    n, out = int(argv[1]), Path(argv[2])
    ticks = int(argv[3]) if len(argv) > 3 else 1560
    muc = (SRC / "MUC.ALL").read_bytes()
    offs = song_offsets(muc)
    if not 0 <= n < len(offs) - 1:
        print(f"song index must be 0..{len(offs) - 2}")
        return 2
    song = muc[offs[n]:offs[n + 1]]
    if len(song) > 20000:
        print(f"song {n} is {len(song)} bytes, more than the 20,000 CAST.SL reads")
        return 2
    d = ROOT / "work" / "opl" / f"data_{n}"
    d.mkdir(parents=True, exist_ok=True)
    for name in NEED:
        if name != "MUC.ALL":
            shutil.copy(SRC / name, d / name)
    new = bytearray(muc)
    new[2:2 + 20000] = song + bytes(20000 - len(song))
    (d / "MUC.ALL").write_bytes(bytes(new))
    work = ROOT / "work" / "opl" / f"emu_{n}"
    cmd = [str(ROOT / "work/venv/bin/python"), str(ROOT / "tools/dosemu.py"), str(ROOT / "work/unpacked/CAST.SL.exe"),
           "--data", str(d), "--work", str(work), "--ticks", str(ticks), "--instr-per-tick", "500000",
           "--opl-log", str(out)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    lines = [l for l in r.stdout.splitlines() if l.startswith("opl:") or l.startswith("stopped")]
    print(f"song {n}: " + " | ".join(l[:110] for l in lines))
    shutil.rmtree(work, ignore_errors=True)
    return 0 if r.returncode == 0 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
