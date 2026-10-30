#!/usr/bin/env python3
"""Show that tools/check_load.sh and tests/test_walk.c can fail on the save loading (src/menu.c menu_load_save, src/walk.c
walk_rle_unpack): apply one deliberate bug at a time to scratch copies of the sources, build the headless game and test_walk from
them in work/mut_load, and expect check_load.sh (LOADS_ONLY=1: the loads, not the world after them) or the unit test to fail.
Deletes nothing. Run tools/check_load.sh once first (it makes the port's own saves the loads use).

    work/venv/bin/python tools/mutate_load.py
"""
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
D = ROOT / "work" / "mut_load"
OBJ = ["build/opl/ymfm_opl.o", "build/opl/ymfm_adpcm.o", "build/opl/ymfm_pcm.o", "build/opl/opl.o"]

# Dropped: reading all of .000 instead of its first 504 bytes. It can only differ for a .000 longer than 504 bytes, and packing the
# 504-byte party table gives that only when nearly every byte value is used in it (the marker is the least-used value, 3 bytes a
# use); the shipped and port-made saves are 100 to 230 bytes.
MUTATIONS = [
    ("menu.c", "MAN_INFO.NOW 503 bytes", 'assets_write("man_info.now", party, 0x1F8)', 'assets_write("man_info.now", party, 0x1F7)'),
    ("menu.c", ".002 not written", "memcpy(now + l1, b, l2);", "memset(now + l1, 0, l2);"),
    ("menu.c", ".001 and .002 swapped", "memcpy(now, a, l1);\n            memcpy(now + l1, b, l2);",
     "memcpy(now, b, l2);\n            memcpy(now + l2, a, l1);"),
    ("menu.c", "slot one higher", 'snprintf(name, sizeof name, "save%02d.000", slot);',
     'snprintf(name, sizeof name, "save%02d.000", slot + 1);'),
    ("walk.c", "unpack: a run gives one more", "for (unsigned k = 0; k < in[si + 1]; k++, o++)",
     "for (unsigned k = 0; k <= in[si + 1]; k++, o++)"),
    ("walk.c", "unpack: header length ignored", "    if (end > n)\n        end = n;\n    while (si < end) {",
     "    end = n;\n    while (si < end) {"),
    ("walk.c", "unpack: run byte from the count", "                out[o] = in[si + 2];\n        si += 3;",
     "                out[o] = in[si + 1];\n        si += 3;"),
    ("walk.c", "unpack: marker not recognised", "        if (in[si] != in[0]) {", "        if (in[si] != in[0] || si == 3) {"),
    ("walk.c", "unpack: starts at byte 2", "    size_t end = (size_t)in[1] | (size_t)in[2] << 8, si = 3, o = 0;",
     "    size_t end = (size_t)in[1] | (size_t)in[2] << 8, si = 2, o = 0;"),
]


def core_sources():
    m = re.search(r"^CORE_SRC := (.*)$", (ROOT / "Makefile").read_text(), re.M)
    return m.group(1).split()


def build(src_over):
    srcs = [str(D / Path(s).name) if Path(s).name in src_over else s for s in core_sources()]
    cc = ["cc", "-std=c99", "-O1", "-Isrc"]
    for out, extra in (("3k_headless", ["tests/platform_null.c", "src/main.c"]), ("test_walk", ["tests/test_walk.c"])):
        r = subprocess.run(cc + srcs + extra + OBJ + ["-o", str(D / out), "-lc++"], cwd=ROOT, capture_output=True, text=True)
        if r.returncode != 0:
            return False
    return True


def main():
    D.mkdir(parents=True, exist_ok=True)
    missed = 0
    for fname, name, old, new in MUTATIONS:
        text = (ROOT / "src" / fname).read_text()
        if text.count(old) != 1:
            print(f"PATTERN MATCHES {text.count(old)} TIMES: {name}")
            missed += 1
            continue
        for f in ("menu.c", "walk.c"):
            (D / f).write_text((ROOT / "src" / f).read_text())
        (D / fname).write_text(text.replace(old, new))
        if not build({"menu.c", "walk.c"}):
            print(f"DOES NOT BUILD: {name}")
            missed += 1
            continue
        unit = subprocess.run(["perl", "-e", "alarm 120; exec @ARGV", "--", str(D / "test_walk"), "original_bin"], cwd=ROOT,
                              capture_output=True, text=True)
        env = dict(os.environ, MENU=str(D / "3k_headless"), LOADS_ONLY="1")
        chk = subprocess.run(["tools/check_load.sh"], cwd=ROOT, env=env, capture_output=True, text=True)
        how = [w for w, failed in (("unit test", unit.returncode != 0), ("load check", chk.returncode != 0)) if failed]
        if how:
            print(f"caught ({', '.join(how)}): {name}")
        else:
            print(f"MISSED: {name}")
            missed += 1
    print(f"{len(MUTATIONS) - missed} of {len(MUTATIONS)} mutations caught")
    return 1 if missed else 0


if __name__ == "__main__":
    sys.exit(main())
