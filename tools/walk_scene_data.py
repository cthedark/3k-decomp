#!/usr/bin/env python3
"""Make a data directory that starts WALK.SL in another scene: a copy of the data with MAN_INFO.NOW byte 500 = SCENE and bytes 502, 503 =
the hero's x and y / 2 (the units of the villager records: a hero at (104, 72) stands beside a record at (106, 72)).

Usage: tools/walk_scene_data.py OUT_DIR SCENE X Y_HALF [--data original_bin] [--join K ...] [--hp K:HP ...] [--word K:OFF:VALUE ...]
    [--byte K:OFF:VALUE ...] [--talk V:SCENE:W ...]
--join K puts party member K (1 .. 4, with his own stats from the file) in the party; --hp K:HP sets a member's HP (to make a battle
that is lost, for example).
--talk V:SCENE:W gives villager V of the start scene the talk of villager W of scene SCENE (bytes 0xc .. 0x31 of its record in
__1.NOW: entries, item, price, flags, answers): a selling or asking talk in a lit scene, for example.
The source directory is only read; OUT_DIR is replaced."""
import argparse
import shutil
from pathlib import Path

ap = argparse.ArgumentParser()
ap.add_argument("out")
ap.add_argument("scene", type=int)
ap.add_argument("x", type=int)
ap.add_argument("y_half", type=int)
ap.add_argument("--data", default="original_bin")
ap.add_argument("--join", type=int, action="append", default=[], metavar="K", help="put party member K (1 .. 4) in the party")
ap.add_argument("--hp", action="append", default=[], metavar="K:HP", help="set party member K's HP")
ap.add_argument("--byte", action="append", default=[], metavar="K:OFF:VALUE", help="set byte OFF of party member K's record")
ap.add_argument("--word", action="append", default=[], metavar="K:OFF:VALUE",
                help="set the word at byte OFF of party member K's record (4 experience, 8 HP, 0x10 what a blow taken raises)")
ap.add_argument("--talk", action="append", default=[], metavar="V:SCENE:W",
                help="give villager V of the start scene the talk of villager W of scene SCENE")
a = ap.parse_args()
for v, name in ((a.scene, "SCENE"), (a.x, "X"), (a.y_half, "Y_HALF")):
    if not 0 <= v <= 255:
        raise SystemExit(f"{name} must be 0 .. 255")
out = Path(a.out)
if out.exists():
    shutil.rmtree(out)
out.mkdir(parents=True)
for p in Path(a.data).iterdir():
    if p.is_file():
        shutil.copy2(p, out / p.name)
mi = bytearray((out / "MAN_INFO.NOW").read_bytes())
mi[500], mi[502], mi[503] = a.scene, a.x, a.y_half
for k in a.join:   # member k (1 .. 4) in the party with his own sprite set: byte 3 of his record = k (shipped: 0xc8 + k, not joined)
    mi[40 * k + 3] = k
for spec in a.hp:
    k, v = (int(x, 0) for x in spec.split(":"))
    mi[40 * k + 8], mi[40 * k + 9] = v & 0xFF, v >> 8
for spec in a.byte:   # e.g. 0xc, the level a spell needs (MAGICT.LCC)
    k, o, v = (int(x, 0) for x in spec.split(":"))
    mi[40 * k + o] = v & 0xFF
for spec in a.word:
    k, o, v = (int(x, 0) for x in spec.split(":"))
    mi[40 * k + o], mi[40 * k + o + 1] = v & 0xFF, v >> 8
(out / "MAN_INFO.NOW").write_bytes(bytes(mi))
if a.talk:
    now = bytearray((out / "__1.NOW").read_bytes())
    for spec in a.talk:   # 1750 bytes a scene, 50 a villager record
        v, sc, w = (int(x, 0) for x in spec.split(":"))
        dst, src = a.scene * 1750 + 50 * v, sc * 1750 + 50 * w
        now[dst + 0xC:dst + 0x32] = now[src + 0xC:src + 0x32]
    (out / "__1.NOW").write_bytes(bytes(now))
