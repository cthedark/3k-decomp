#!/usr/bin/env python3
"""Negative controls for the battle, stage 1 (src/battle.c and the battle parts of src/walk.c).

Applies one deliberate bug at a time to a copy of the sources, builds walk_dump from it and runs tools/check_battle.sh with that
build (the original's recordings are reused from work/check_battle, so run check_battle.sh once first). A mutation the check does
not notice is printed as MISSED. ONLY=text runs the mutations whose name contains the text.

  work/venv/bin/python tools/mutate_battle.py
"""
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WORK = ROOT / "work" / "mut_battle"
SOURCES = ["src/walk.c", "src/battle.c", "src/walkscene.c", "src/assets.c", "src/lcz.c", "src/screen.c"]

M = []


def m(f, name, old, new):
    M.append((f, name, old, new))


# the hand-over
m("walk.c", "doubling: lower-left pixel not shaded", "d[192] = q;", "d[192] = p;")
m("walk.c", "doubling: source row", "w->buf[0x3220 + r * WALK_W + c]", "w->buf[0x3220 + (r + 1) * WALK_W + c]")
m("walk.c", "entities drawn in the view", "walk_draw_objects(w->buf, s, x, y);   /* no entities",
  "walk_draw_entities(w->buf, s, x, y, 0);\n    walk_draw_objects(w->buf, s, x, y);   /* no entities")
m("walk.c", "touch: group without -1", "rec[0xB] - 1) & 0xFF;   /*", "rec[0xB]) & 0xFF;   /*")
m("battle.c", "perspective: 4 more pixels a row", "unsigned dw = BATTLE_BACK_W + 6 * k;", "unsigned dw = BATTLE_BACK_W + 4 * k;")
m("battle.c", "perspective: kept from the left edge", "memcpy(b->back + k * BATTLE_BACK_W, row + 3 * k,",
  "memcpy(b->back + k * BATTLE_BACK_W, row + 2 * k,")
m("battle.c", "stretch starts at 0", "acc = 1, si = 0;", "acc = 0, si = 0;")
m("battle.c", "cycle colours not replaced", "if (b->back[j] < 16)", "if (b->back[j] < 1)")
m("battle.c", "cycle colours in every byte", "for (unsigned j = 0; j < 0x5284; j += 2)", "for (unsigned j = 0; j < 0x5284; j += 1)")
m("battle.c", "shading: one pass fewer", "for (int i = 0; i < 4; i++)\n        for (unsigned j = from[i]", "for (int i = 0; i < 3; i++)\n        for (unsigned j = from[i]")
m("battle.c", "shading: second range from 0", "from[4] = {0, 1, 0, 0}", "from[4] = {0, 0, 0, 0}")
m("battle.c", "face: other frame", "s->bank2 + m[3] * 0xC00 + 0x400", "s->bank2 + m[3] * 0xC00 + 0x300")
m("battle.c", "HP colour", "num, 0xF0);     /* HP */", "num, 0xF1);     /* HP */")
m("battle.c", "MP not drawn", "put(scr, at + 0x10, num, 20, 5);", "(void)num;")
m("battle.c", "number: leading zeros shown", "unsigned d = (value / div) & 0xFFFF;\n        value -= d * div;\n        div /= 10;\n        if (div == 0)\n            shown = 1;\n        if (d != 0 || shown) {",
  "unsigned d = (value / div) & 0xFFFF;\n        value -= d * div;\n        div /= 10;\n        if (div == 0)\n            shown = 1;\n        if (1) {")
m("battle.c", "floating number: leading zeros shown", "unsigned d = value / div;\n        value -= d * div;\n        div /= 10;\n        if (div == 0)\n            shown = 1;\n        if (d != 0 || shown) {",
  "unsigned d = value / div;\n        value -= d * div;\n        div /= 10;\n        if (div == 0)\n            shown = 1;\n        if (1) {")
m("battle.c", "slots: empty from 2", "if (t > 2)\n                t = 9;", "if (t > 1)\n                t = 9;")
m("battle.c", "slots: no move to the front", "for (int sl = 2; sl < BATTLE_SLOTS; sl++) {\n        if (b->mon[4 * sl + 2] <= 2)",
  "for (int sl = 5; sl < BATTLE_SLOTS; sl++) {\n        if (b->mon[4 * sl + 2] <= 2)")
m("battle.c", "stats: other 10 bytes", "raw + n - 10, 10);", "raw + n - 11, 10);")
m("battle.c", "mark colour", "scr->pix[p + r * SCREEN_W + x] = 0x0F;", "scr->pix[p + r * SCREEN_W + x] = 0x0E;")
m("battle.c", "tabs: chosen tab fill", "fill(scr, q + 0x141, 0x2C, 2, 0x10);", "fill(scr, q + 0x141, 0x2C, 2, 0x11);")
# Not in the list, because no start in check_battle.sh can show them: the touched enemy's new place (seen only when the battle
# returns to the world, not built), the 0x3e80 threshold of who starts (no recorded rand value falls between 0x3000 and 0x3e80),
# the tabs' outer outline height (the party strip's picture already has those pixels).
m("battle.c", "sway: bounce at 2", "} else if (++ax == 3) {", "} else if (++ax == 2) {")
m("battle.c", "sway count: no change", "if (++b->count == 3)\n            b->dir = 0;", "if (b->count == 3)\n            b->dir = 0;")
m("battle.c", "jitter: one side only", "if (r > 0x7447)\n            at = (at + 1) & 0xFFFF;", "")
m("battle.c", "frame change: up threshold", "if (r > 0x7D00 && t < 2)", "if (r > 0x7C00 && t < 2)")
m("battle.c", "back row: shaded once", "small[i] = b->table[b->table[small[i]]];", "small[i] = b->table[small[i]];")
m("battle.c", "back row: full size", "if (sl < 2) {", "if (sl < 0) {")
m("battle.c", "shrink: step", "xint = sw / dw,", "xint = sw / dw + 1,")
m("battle.c", "monsters' turn: no pass over empty slots", "b->v5e++;   /* 0x1dbf3: an empty slot is passed over */", ";")
m("battle.c", "palette cycle not run", "walk_palette_step(b->w, scr);\n    if (b->v28", "if (b->v28")
m("battle.c", "field one row lower", "put(scr, FIELD_AT, b->field,", "put(scr, FIELD_AT + 320, b->field,")
# stage 2: the command and target choice
m("battle.c", "Left does not wrap to the last tab", "b->v82 = 4;\n            b->v82--;", "b->v82 = 1;\n            b->v82--;")
m("battle.c", "Right wraps after the third tab", "if (k == 0x4D && ++b->v82 > 3)", "if (k == 0x4D && ++b->v82 > 2)")
m("battle.c", "Enter not a confirm", "if (k == 0x39 || k == 0x1C) {    /* Space", "if (k == 0x39) {    /* Space")
m("battle.c", "first target from the second front slot", "unsigned i = 8;\n                while (i < 0x14 && !can_target(b, i))",
  "unsigned i = 12;\n                while (i < 0x14 && !can_target(b, i))")
m("battle.c", "target of a dead slot", "return b->mon[i + 2] < 3 && b->mon[i + 3] == 0;", "return b->mon[i + 3] == 0;")
m("battle.c", "Right past an empty last slot", "b->mon[(j < 0x11 ? j : i) + 3] = 0x96;", "b->mon[(j < 0x15 ? j : i) + 3] = 0x96;")
m("battle.c", "Left does not move", "if (i > 8) {\n            b->mon[i + 3] = 0;", "if (i > 16) {\n            b->mon[i + 3] = 0;")
m("battle.c", "Esc keeps the target", "for (unsigned i = 8; i < 0x14; i += 4)\n            b->mon[i + 3] = 0;", "")
m("battle.c", "Esc keeps choosing", "if (k == 1) {   /* Esc: back to the commands */\n        b->choosing = 0;", "if (k == 1) {   /* Esc: back to the commands */")
m("battle.c", "box: one outline fewer", "for (int i = 0; i < 3; i++)\n                    box_outline", "for (int i = 0; i < 2; i++)\n                    box_outline")
m("battle.c", "box: outer colour", "{0xF4, 0xFF, 0xF8}", "{0xF5, 0xFF, 0xF8}")
m("battle.c", "latch starts at 0", "b->latch = rec & 0xFF;", "b->latch = 0;")
m("battle.c", "a held key counts again", "    b->latch = 0;\n    if (k == 2 || k == 3)", "    if (k == 2 || k == 3)")
# stage 3: the attack and the monsters' turn
m("battle.c", "blow: experience divisor", "(exp / 0x4B0) & 0xFF;", "(exp / 0x4B1) & 0xFF;")
m("battle.c", "blow: back members full stat", "if (back)\n        s >>= 1;", "")
m("battle.c", "blow: no minimum of 1", "if (d == 0)\n        d = 1;", "")
m("battle.c", "blow: no reduction", "if (t < 0x11 && t != 0)\n        d = (d - d / t) & 0xFF;", "")
m("battle.c", "kill: no experience", "unsigned x = (w16(party, m + 4) + b->mon[0x2C]) & 0xFFFF;", "unsigned x = w16(party, m + 4);")
m("battle.c", "latch not the HP", "b->latch = hp;   /*", "b->latch = 0;   /*")
m("battle.c", "hit size", "b->v8a = d / 7 + 0x14;", "b->v8a = d / 7 + 0x13;")
m("battle.c", "next member skips the status test", "if (w16(party, b->v44 + 8) != 0 && party[b->v44 + 3] < 5) {", "if (w16(party, b->v44 + 8) != 0) {")
m("battle.c", "party shout from 3", "unsigned r = walk_rand(walk_rng(b->w)) / 0x199A + 2;", "unsigned r = walk_rand(walk_rng(b->w)) / 0x199A + 3;")
m("battle.c", "monster blow: back row full force", "if (back)\n        atk >>= 1;", "")
m("battle.c", "monster blow: no defence", "return atk > def ? atk - def : 0;", "return atk;")
m("battle.c", "monster blow: no field flash", "memset(b->field, 0x28, sizeof b->field);", "")
m("battle.c", "close-up: frames two passes each", "if (++b->v10 > 3) {", "if (++b->v10 > 1) {")
m("battle.c", "close-up: other place", "blit_masked(b->field, 0x24, b->closeup,", "blit_masked(b->field, 0x20, b->closeup,")
m("battle.c", "doubling: second pixel", "d[1] = s[1];", "d[1] = s[0];")
m("battle.c", "hit face: one pass each", "if (++b->v58 > 1) {", "if (++b->v58 > 0) {")
m("battle.c", "shake: no rows", "unsigned r = walk_rand(walk_rng(b->w)) / 0x667;\n        if (r != 0", "unsigned r = walk_rand(walk_rng(b->w)) / 0x667;\n        if (0")
m("battle.c", "numbers: not rising", "b->fxpos[i] = (uint16_t)(b->fxpos[i] - 0xC0);", "")
m("battle.c", "numbers: count from the pixel", "blit_hdr(b->field, b->fxpos[i],", "blit_masked(b->field, b->fxpos[i],")
m("battle.c", "flash colour", "b->field[d] = (uint8_t)(f + 0xF);", "b->field[d] = (uint8_t)(f + 0xE);")
m("battle.c", "dying: masks in steps of 2", "((f - 0x3C) >> 2) * MON_W * MON_H", "((f - 0x3C) >> 3) * MON_W * MON_H")
m("battle.c", "sparks: no new place", "if (fr > 4) {\n                        fr = 0;", "if (fr > 4 && 0) {\n                        fr = 0;")
# "step forward: back HP kept" (b->mon[bh] instead of the full HP b->mon[0x28]) is left out: only front monsters can be hit until
# commands 1 to 3 are built, so a back monster's HP is always the full HP and the two are the same.
m("battle.c", "hit lands on the wrong slot", "if (col > 0x60)\n        k = 4;", "if (col > 0x70)\n        k = 4;")
m("battle.c", "hit animation: frames two passes", "if (++b->v8c > 2) {", "if (++b->v8c > 1) {")
m("battle.c", "stretch: rows from 1", "unsigned si = row, acc = 1;", "unsigned si = row, acc = 0;")


def main():
    if not (ROOT / "work/check_battle").exists():
        print("run tools/check_battle.sh first")
        return 2
    only = os.environ.get("ONLY", "")
    missed = bad = ran = 0
    WORK.mkdir(parents=True, exist_ok=True)
    try:
        for f, name, old, new in M:
            if only and only not in name:
                continue
            ran += 1
            text = (ROOT / "src" / f).read_text()
            if text.count(old) != 1:
                print(f"BAD PATTERN ({text.count(old)} matches): {name}")
                bad += 1
                continue
            src = WORK / "src"
            if src.exists():
                shutil.rmtree(src)
            shutil.copytree(ROOT / "src", src)
            (src / f).write_text(text.replace(old, new))
            out = WORK / "walk_dump"
            r = subprocess.run(["cc", "-std=c99", "-O1", "-w", "-I", str(src)] + [str(WORK / s) for s in SOURCES] +
                               [str(ROOT / "tests/walk_dump.c"), "-o", str(out)], capture_output=True, text=True)
            if r.returncode == 0:
                r = subprocess.run(["cc", "-std=c99", "-O1", "-w", "-I", str(src)] + [str(WORK / s) for s in SOURCES] +
                                   [str(ROOT / "tests/test_battle.c"), "-o", str(WORK / "test_battle")], capture_output=True, text=True)
            if r.returncode != 0:
                print(f"DOES NOT BUILD: {name}: {r.stderr.strip()[:200]}")
                bad += 1
                continue
            if subprocess.run([str(WORK / "test_battle")], capture_output=True).returncode != 0:
                print("caught by unit: " + name)
                continue
            try:
                r = subprocess.run(["sh", "tools/check_battle.sh"], cwd=ROOT, env={**os.environ, "PORT": str(out), "FIRST_FAIL_EXIT": "1"},
                                   capture_output=True, text=True, timeout=600)
                caught = r.returncode != 0
            except subprocess.TimeoutExpired:
                caught = True
            print(("caught: " if caught else "MISSED: ") + name)
            missed += not caught
            sys.stdout.flush()
    finally:
        shutil.rmtree(WORK, ignore_errors=True)
    print(f"{ran - missed - bad} of {ran} caught, {missed} missed, {bad} bad")
    return 1 if missed or bad else 0


if __name__ == "__main__":
    sys.exit(main())
