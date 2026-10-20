#!/usr/bin/env python3
"""Negative controls for the battle, stage 1 (src/battle.c and the battle parts of src/walk.c).

Applies one deliberate bug at a time to a copy of the sources, builds walk_dump from it and runs tools/check_battle.sh with that
build (the original's recordings are reused from work/check_battle, so run check_battle.sh once first). A mutation the check does
not notice is printed as MISSED. ONLY=text runs the mutations whose name contains the text.

  work/venv/bin/python tools/mutate_battle.py
"""
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WORK = ROOT / "work" / "mut_battle"
SOURCES = ["src/walk.c", "src/battle.c", "src/msgbox.c", "src/gameover.c", "src/walkscene.c", "src/assets.c", "src/lcz.c", "src/screen.c"]

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
m("battle.c", "MP not drawn", "0xB0);   /* MP */\n        put(scr, at + 0x10, num, 20, 5);", "0xB0);   /* MP */")
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
m("battle.c", "sway count: no change", "b->v28 = b->v82;\n    }\n    if (b->dir) {\n        if (++b->count == 3)", "b->v28 = b->v82;\n    }\n    if (b->dir) {\n        if (b->count == 3)")
m("battle.c", "rewards: sway count no change", "if (b->dir) {\n        if (++b->count == 3)\n            b->dir = 0;\n    } else if (--b->count == 0) {\n        b->dir = 1;\n    }\n    battle_sway(b->back, b->field, b->count, b->dir);\n    walk_palette_step",
  "if (b->dir) {\n        if (b->count == 3)\n            b->dir = 0;\n    } else if (--b->count == 0) {\n        b->dir = 1;\n    }\n    battle_sway(b->back, b->field, b->count, b->dir);\n    walk_palette_step")
m("battle.c", "jitter: one side only", "if (r > 0x7447)\n            at = (at + 1) & 0xFFFF;", "")
m("battle.c", "frame change: up threshold", "if (r > 0x7D00 && t < 2)", "if (r > 0x7C00 && t < 2)")
m("battle.c", "back row: shaded once", "small[i] = b->table[b->table[small[i]]];", "small[i] = b->table[small[i]];")
m("battle.c", "back row: full size", "if (sl < 2) {", "if (sl < 0) {")
m("battle.c", "shrink: step", "xint = sw / dw,", "xint = sw / dw + 1,")
m("battle.c", "monsters' turn: no pass over empty slots", "b->v5e++;   /* 0x1dbf3: an empty slot is passed over */", ";")
m("battle.c", "palette cycle not run", "walk_palette_step(b->w, scr);\n    if (b->v28", "if (b->v28")
m("battle.c", "field one row lower", "b->shout_h);\n    put(scr, FIELD_AT, b->field,", "b->shout_h);\n    put(scr, FIELD_AT + 320, b->field,")
m("battle.c", "rewards: field one row lower", "b->field[b->bar_at - 4 + i] = b->bar[i];\n    put(scr, FIELD_AT, b->field,",
  "b->field[b->bar_at - 4 + i] = b->bar[i];\n    put(scr, FIELD_AT + 320, b->field,")
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
m("battle.c", "kill: no experience", "hp = 0;\n        unsigned x = (w16(party, m + 4) + b->mon[0x2C]) & 0xFFFF;", "hp = 0;\n        unsigned x = w16(party, m + 4);")
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

# what follows a win and a loss (step 16)
m("walk.c", "resume: frame not from byte 0x2f", "w->hero_frame = w->battle_boss & 0xFF;", "")
m("walk.c", "resume: counter not the result", "w->ctr = result & 0xFF;", "")
m("walk.c", "resume: the new pass's rain stride", "return pass_tail(w, ticks, port60, w->stride);",
  "return pass_tail(w, ticks, port60, walk_rain_stride(w->scene->weather, ticks));")
m("battle.c", "second battle: [bp-0x84] from 0", "b->v84 = *walk_battle_left(w);", "b->v84 = 0;")
m("battle.c", "kills: frame 5 not dead", "if (b->mon[i] > 2 && b->mon[i] < 9)", "if (b->mon[i] > 2 && b->mon[i] < 5)")
m("battle.c", "bar: kills glyph", "bar_glyph(b, 3, 2 + kills);", "bar_glyph(b, 3, 1 + kills);")
m("battle.c", "bar: x one right", "bar_glyph(b, 0x16, 0);", "bar_glyph(b, 0x17, 0);")
m("battle.c", "bar: per from the 4th digit", "for (int i = 2; i < 5; i++)", "for (int i = 3; i < 5; i++)")
m("battle.c", "bar: total digits closer", "bar_glyph(b, 0x75 + 0x13 *", "bar_glyph(b, 0x75 + 0x12 *")
m("battle.c", "bar: per from the next stat", "unsigned per = b->mon[0x2B]", "unsigned per = b->mon[0x2C]")
m("battle.c", "coins not shown", "walk_show_gold(b->w, scr);", "(void)scr;")
m("battle.c", "coins: cap", "return g > 0xC350 ? 0xC350 : g;", "return g > 0xEA60 ? 0xEA60 : g;")
m("battle.c", "bar starts a row lower", "b->bar_at = 0x1744;", "b->bar_at = 0x1804;")
m("battle.c", "bar: step kept at the bottom", "if (b->bar_step > 0x180)\n            b->bar_step = 0;", "if (b->bar_step > 0x300)\n            b->bar_step = 0;")
m("battle.c", "bar: lower bound", "if (b->bar_at > 0x2944 || b->bar_at < 0x1804)", "if (b->bar_at > 0x2A04 || b->bar_at < 0x1804)")
m("battle.c", "bar: opaque", "if (b->bar[i] && b->bar_at - 4 + i", "if (b->bar_at - 4 + i")
m("battle.c", "rewards: Esc does not end", "int leave = k == 0x39 || k == 1 || k == 0x1C;", "int leave = k == 0x39 || k == 0x1C;")
m("battle.c", "rewards: no palette step", "walk_palette_step(b->w, scr);\n    b->bar_at", "b->bar_at")
m("battle.c", "rewards: no sway", "battle_sway(b->back, b->field, b->count, b->dir);\n    walk_palette_step(b->w, scr);\n    b->bar_at",
  "walk_palette_step(b->w, scr);\n    b->bar_at")
m("msgbox.c", "message: short text a row higher", "if (n < 0x28) {\n        row += 15;", "if (n < 0x28) {\n        row += 14;")
m("msgbox.c", "message: padding", "x += 8 * ((0x28 - n) / 2);", "x += 8 * ((0x29 - n) / 2);")
m("msgbox.c", "message: last 4 pixels kept", "    memcpy(box + sizeof box - 4, tail, 4);\n", "")
m("msgbox.c", "message: shaded once less", "for (int t = 0; t < 2; t++) {\n        for (unsigned i = 0; i < len", "for (int t = 0; t < 1; t++) {\n        for (unsigned i = 0; i < len")
m("msgbox.c", "message: inner rows from row 0", "for (unsigned i = SCREEN_W; i < SCREEN_W * 45 && i < len; i++)", "for (unsigned i = 0; i < SCREEN_W * 45 && i < len; i++)")
m("msgbox.c", "message: shadow colour", "(uint8_t)(colour + 6);", "(uint8_t)(colour + 5);")
m("msgbox.c", "message: one shadow", "shadow[2] = {SCREEN_W + 1, SCREEN_W};", "shadow[2] = {SCREEN_W, SCREEN_W};")
m("msgbox.c", "message: ASCII recolour later", "unsigned from = w == 8 ? 0x26 : 0x50", "unsigned from = w == 8 ? 0x27 : 0x50")
m("msgbox.c", "message: Big5 recolour later", "unsigned from = w == 8 ? 0x26 : 0x50", "unsigned from = w == 8 ? 0x26 : 0x51")
m("msgbox.c", "message: ASCII 9 wide", "x += 8;\n        } else {", "x += 9;\n        } else {")
m("msgbox.c", "message: palette every 40th", "if (++m->count > 0x28) {", "if (++m->count > 0x27) {")
m("msgbox.c", "message: no release needed", "if (m->wait)\n        k = 0x80;", "")
m("msgbox.c", "message: not taken away", "memcpy(scr->pix + 0xA780, m->under, sizeof m->under);   /* 0x27328", "(void)0;   /* 0x27328")
m("battle.c", "level-up: next message", "unsigned msg = m[3] + 0x2E2u;", "unsigned msg = m[3] + 0x2E3u;")
m("battle.c", "level-up: first member only", "for (; b->lu < 5; b->lu++) {", "for (; b->lu < 1; b->lu++) {")
m("battle.c", "level-up: HP not full", "m[0xA] = m[8] = (uint8_t)hp;\n    m[0xB] = m[9] = (uint8_t)(hp >> 8);", "m[0xA] = (uint8_t)hp;\n    m[0xB] = (uint8_t)(hp >> 8);")
m("gameover.c", "game over: shaded three times", "for (int t = 0; t < 4; t++)", "for (int t = 0; t < 3; t++)")
# "game over: tiles not shrunk the same way" (every 4th pixel instead of battle_shrink) is left out: 320 / 80 and 200 / 50 are whole,
# so battle_shrink's fractions are 0 and it takes exactly every 4th pixel too; the two cannot differ.
m("gameover.c", "game over: no roll up", "memmove(g->pic, g->pic + W, (size_t)W * (H - 1));\n    memcpy(g->pic + (size_t)W * (H - 1), row, W);", "(void)row;")
m("gameover.c", "game over: roll right", "memmove(p, p + 1, W - 1);\n        p[W - 1] = first;", "(void)first;\n        uint8_t last = p[W - 1];\n        memmove(p + 1, p, W - 1);\n        p[0] = last;")
m("gameover.c", "game over: lettering a pixel right", "WORDS_AT = 0x6D36 - 4 }", "WORDS_AT = 0x6D36 - 3 }")
m("gameover.c", "game over: lettering opaque", "if (v && WORDS_AT + r * W + x", "if (WORDS_AT + r * W + x")

# the other three commands (step 17): fleeing
m("battle.c", "flee: always away", "if (walk_rand(walk_rng(b->w)) < 0x4000)\n            start_rewards", "if (walk_rand(walk_rng(b->w)) < 0x8000)\n            start_rewards")
m("battle.c", "flee: mark stays", "mark(b, scr, box_at(b->v44), 0x0F, 0x1F);\n        b->v44 = 0xC8;", "b->v44 = 0xC8;")
m("battle.c", "flee: the next member's turn", "        b->v44 = 0xC8;\n        b->choosing = 0;", "        next_member(b, scr);\n        b->choosing = 0;")
# the magic list
m("battle.c", "magic: no icon", "put(scr, 0x5F54, icon, 16, 16);", "(void)icon;")
m("battle.c", "magic: rows from the other column", "at = k == 4 ? 0x8020 : at + 0x12C0;", "at = k == 4 ? 0x8021 : at + 0x12C0;")
m("battle.c", "magic: level compare strict", "if (level >= t[0]) {", "if (level > t[0]) {")
m("battle.c", "magic: cost = level", "if (mp >= t[0] * 2u) {", "if (mp >= t[0]) {")
m("battle.c", "magic: shadow one pixel", "silhouette(scr, at + 0x141, px, 96, 14, 0xFA);\n", "")
m("battle.c", "text: no colour bands", "g[j] = (uint8_t)(colour + 1 + j / split);", "g[j] = colour;")
# "text: an ASCII character 16 wide" is left out: no spell or item name in MAGICT.LCC or OBJECT.LCC has an ASCII byte other than
# trailing spaces, so the width of an ASCII character never shows.
m("battle.c", "magic: palette counter from 0", "b->mg_count = 3;", "b->mg_count = 0;")
m("battle.c", "magic: row not lit", "list_row(scr, b->mg_cur, 1);\n        b->mg_prev", "b->mg_prev")
m("battle.c", "magic: Down wraps to column 2 early", "if (b->mg_idx == 0x32)\n            b->mg_cur = 0x8161;", "if (b->mg_idx == 0x28)\n            b->mg_cur = 0x8161;")
m("battle.c", "magic: Space casts", "    if (k != 2)\n        return;\n    const uint8_t *t = b->mg_tab", "    if (k != 2 && k != 0x39)\n        return;\n    const uint8_t *t = b->mg_tab")
m("battle.c", "magic: attack spell costs nothing", "    party[b->v44 + 0xD] = (uint8_t)mp;\n    party[b->v44 + 0xE] = (uint8_t)(mp >> 8);\n    magic_end(b, scr, t[7]);", "    (void)mp;\n    magic_end(b, scr, t[7]);")
m("battle.c", "magic: heal a fallen one", "        if (hp != 0) {\n            hp = (hp + v) & 0xFFFF;", "        if (1) {\n            hp = (hp + v) & 0xFFFF;")
m("battle.c", "magic: heal not capped", "            if (hp > w16(party, m + 0xA))\n                hp = w16(party, m + 0xA);\n            party[m + 8] = (uint8_t)hp;\n            party[m + 9] = (uint8_t)(hp >> 8);\n            res = 0x2F;", "            party[m + 8] = (uint8_t)hp;\n            party[m + 9] = (uint8_t)(hp >> 8);\n            res = 0x2F;")
m("battle.c", "magic: revive face not drawn", "    if (res == 0x1F)\n        put(scr, FACE_AT", "    if (res == 0)\n        put(scr, FACE_AT")
m("battle.c", "magic: flash colour", "(uint8_t)(res - 15));   /* 0x25c18 */", "(uint8_t)(res - 16));   /* 0x25c18 */")
m("battle.c", "magic: latch not the result", "b->latch = r & 0xFF;", "b->latch = 0;")
m("battle.c", "magic: spell hits a dead slot", "            if (hp == 0)\n                continue;\n            if (hp > r) {", "            if (hp > r) {")
m("battle.c", "magic: kill gives no experience", "party[m + 4] = (uint8_t)x;\n                party[m + 5] = (uint8_t)(x >> 8);\n            }\n            b->mon[0x34 + k]", "(void)x;\n            }\n            b->mon[0x34 + k]")
m("battle.c", "magic: numbers swapped", "add_number(b, pos, r, 0x32, 0xF0);", "add_number(b, pos, hp, 0x32, 0xF0);")
m("battle.c", "magic: shout from the party's", "set_shout(b, walk_rand(walk_rng(b->w)) / 0x4000, 0x30", "set_shout(b, walk_rand(walk_rng(b->w)) / 0x4000 + 2, 0x30")
m("battle.c", "magic: flashes 18 passes", "b->v8a = 0x13;\n        b->v8c = 1;", "b->v8a = 0x12;\n        b->v8c = 1;")
m("battle.c", "magic: no flash", "memset(b->field, 0x10, sizeof b->field);", "(void)0;")
# "magic: hit place not moved" (the 0x241 a pass) is left out: [bp-0x32] is only read by the hit pictures of an attack, which set
# it afresh first, so its motion during a spell cannot show.
m("battle.c", "magic: attack picture x", "b->v92 = ((0x6E - ph) * 0xC0 + 0x47) & 0xFFFF;", "b->v92 = ((0x6E - ph) * 0xC0 + 0x48) & 0xFFFF;")
m("battle.c", "magic: strip MP not redrawn", "        battle_number(w16(party, o + 0xD), b->font, num, 0xB0);\n        put(scr, at + 0x10, num, 20, 5);\n    }\n}\n\nstatic void magic_end", "    }\n}\n\nstatic void magic_end")
# the member picker
m("battle.c", "picker: counter from 0", "b->pk_count = b->last_gw;", "b->pk_count = 0;")
m("battle.c", "picker: fallen face not crossed", "                pick_cross(scr, pos + 0x141, 0x10, 0);", "                (void)0;")
m("battle.c", "picker: Left does not skip", "        while (party[b->pk_m + 3] > 4) {", "        while (0) {")
m("battle.c", "picker: chosen face not crossed", "        pick_cross(scr, b->pk_sel + 0x141, 7, 1);", "        (void)0;")
m("battle.c", "picker: Esc chooses", "    } else if (k == 1) {\n        b->pk_m = 0xC8;", "    } else if (k == 1) {\n        b->pk_m = b->pk_m;")
# the item list
m("battle.c", "items: not packed", "                party[i] = 0;\n                party[j] = (uint8_t)v;", "                (void)v;")
m("battle.c", "items: one icon", "memcpy(icon + 32 * r, b->mg_bar + 0x5A8 + 0x30 * r, 32);", "memcpy(icon + 32 * r, b->mg_bar + 0x5A8 + 0x30 * r, 16);")
m("battle.c", "items: counter from 0", "b->it_count = 0x98;", "b->it_count = 0;")
# "items: the lit row recoloured to its end" is left out: the two rows the original leaves out (the row picture's last and the next
# row's first, or the field under the last row) had none of the colours 0x74 .. 0x77 in any start tried, so nothing showed.
m("battle.c", "items: shadow colour", "silhouette(scr, at + 0x141, px, 96, 14, 0x16);", "silhouette(scr, at + 0x141, px, 96, 14, 0x17);")
m("battle.c", "items: worn record not swapped", "            t[i] = w[i];\n            w[i] = x;", "            w[i] = x;")
m("battle.c", "items: worn not halved", "            if (r[0x16] != n) {\n                ra >>= 1;", "            if (0) {\n                ra >>= 1;")
m("battle.c", "items: defence not / 5", "rd = r[0xF] / 5;", "rd = r[0xF] / 4;")
m("battle.c", "items: food kept", "    if ((v = w16(t, 0xC)) != 0) {\n        party[slot] = 0;", "    if ((v = w16(t, 0xC)) != 0) {")
m("battle.c", "items: heal not capped", "        if (hp > top)\n            hp = top;", "        (void)top;")
m("battle.c", "items: page not turned", "        b->it_page += 10;", "        b->it_page += 0;")
m("battle.c", "items: Esc keeps the strip", "    b->phase = BATTLE_KIND_FIGHT;\n    strip_numbers(b, scr);\n    b->choosing = 0;\n}", "    b->phase = BATTLE_KIND_FIGHT;\n    b->choosing = 0;\n}")
m("battle.c", "items: ends the turn", "    b->phase = BATTLE_KIND_FIGHT;\n    strip_numbers(b, scr);\n    b->choosing = 0;\n}", "    b->phase = BATTLE_KIND_FIGHT;\n    strip_numbers(b, scr);\n    next_member(b, scr);\n    b->choosing = 0;\n}")
# the mouse (step 18)
m("walk.c", "world pointer: not drawn on the screen", "        walk_mouse_put(scr, pos, w->mpic, 1);\n        w->mprev = pos;", "        w->mprev = pos;")
m("walk.c", "world pointer: not into the buffer", "    if (x > 0x3A && y > 6 && x < 0x100 && y < 0xB8)   /* 0x26ad2", "    if (0)   /* 0x26ad2")
m("walk.c", "world pointer: buffer a pixel right", "size_t o = shift + (size_t)(x - 0x30) + (size_t)y * WALK_W", "size_t o = shift + (size_t)(x - 0x2F) + (size_t)y * WALK_W")
m("walk.c", "world pointer: x not clamped", "*x = w->mouse_x > 0x13A ? 0x13A :", "*x = w->mouse_x > 0x13F ? 0x13F :")
m("walk.c", "touch: pointer left on the screen", "    if (w->mouse_on)\n        walk_mouse_put(scr, w->mprev, w->mbg, 0);\n}", "    (void)scr;\n}")
m("walk.c", "world pointer: no corner at the start", "    w->mprev = 0;\n    walk_mouse_grab(scr, 0, w->mbg);\n    walk_show_gold", "    w->mprev = 0;\n    walk_show_gold")
m("battle.c", "fight pointer: not into the field", "if (mx > 0x3A && my > 0x40 && mx < 0x100 && my < 0xB3) {", "if (0) {")
m("battle.c", "fight pointer: field rows from 0x49", "                frow = (unsigned)my - 0x4A;", "                frow = (unsigned)my - 0x49;")
m("battle.c", "fight pointer: cut at the top wrong", "                r0 = 0x4A - (unsigned)my;", "                r0 = 0x49 - (unsigned)my;")
m("battle.c", "fight pointer: not cut at the bottom", "                r1 = 0xB2 - (unsigned)my;", "                r1 = 9;")
m("battle.c", "fight pointer: not cut at the right", "                c1 = 0xFF - (unsigned)mx;", "                c1 = 5;")
m("battle.c", "fight pointer: tabs drawn over it", "        draw_tabs(scr, b->v82);\n        ptr_show(b, scr, b->mptr);", "        draw_tabs(scr, b->v82);")
m("battle.c", "fight mouse: right button not Esc", "        b->latch = 1;\n        k = 1;\n    }", "        b->latch = 1;\n    }")
m("battle.c", "fight mouse: tab not hovered", "        b->v82 = (x - 0x40) / 0x30;", "        (void)0;")
m("battle.c", "fight mouse: a held press counts", "        if (mb == 1 && b->mlatch) {\n            b->mlatch = 0;\n            k = 0x39;\n            b->latch = 1;\n        }\n    }\n    if (b->choosing", "        if (mb == 1) {\n            b->mlatch = 0;\n            k = 0x39;\n            b->latch = 1;\n        }\n    }\n    if (b->choosing")
m("battle.c", "fight mouse: target not hovered", "            b->mon[o + 3] = 0x96;\n            if (mb == 1", "            if (mb == 1")
m("battle.c", "fight mouse: old target kept", "                if (b->mon[i] == 0x96)\n                    b->mon[i] = 0;", "                (void)0;")
m("battle.c", "fight mouse: dead monster targeted", "b->mon[o + 2] < 3 && (b->mon[o + 3] == 0", "b->mon[o + 2] < 9 && (b->mon[o + 3] == 0")
m("battle.c", "fight mouse: keys only without", "    if (walk_mouse(b->w, &mx, &my, &mb))   /* 0x1e472", "    if (0)   /* 0x1e472")
m("battle.c", "lists: fight pointer left on", "        ptr_hide(b, scr, b->mptr);\n        magic_begin(b, scr);", "        magic_begin(b, scr);")
m("battle.c", "lists: fight pointer not from the corner", "    ptr_corner(b, scr, &b->mptr);   /* 0x1f24a", "    (void)0;   /* 0x1f24a")
m("battle.c", "magic mouse: pointer not drawn", "        if (pos != b->mg_ptr) {   /* the lit row lit again; its loop leaves [bp-0x1c]", "        if (0) {   /* the lit row lit again; its loop leaves [bp-0x1c]")
m("battle.c", "magic mouse: palette counter kept", "            b->mg_count = 0x78;", "            (void)0;")
m("battle.c", "magic mouse: row from y - 0x65", "((y - 0x66) / 0xF) * 10) & 0xFF, i = 0;", "((y - 0x65) / 0xF) * 10) & 0xFF, i = 0;")
# "magic mouse: buttons kept after a row" is left out: the row number that replaces the buttons is a multiple of 10, so the icon (1) and
# right button (2) tests that follow fail either way.
m("battle.c", "magic mouse: icon does not cast", "        if (bt == 1 && x > 0x53 && x < 0x64 && y > 0x4B && y < 0x5C)\n            k = 2;", "        (void)0;")
m("battle.c", "magic mouse: right button stays", "        if (bt == 2) {\n            magic_end(b, scr, 0);", "        if (bt == 99) {\n            magic_end(b, scr, 0);")
m("battle.c", "magic: list pointer left on at the end", "    ptr_hide(b, scr, b->mg_ptr);    /* 0x2219c", "    (void)0;    /* 0x2219c")
m("battle.c", "press: held buttons count", "    if (*down)\n        bt = 0;", "    (void)0;")
m("battle.c", "picker mouse: hover does not choose", "                    b->pk_m = 0x28 * i;\n                    b->pk_sel = 0x5D06 + 0x12 * i;", "                    b->pk_m = b->pk_m;")
m("battle.c", "picker mouse: boxes redraw keeps prev", "            b->pk_prev = p;", "            b->pk_prev = b->pk_sel;")
m("battle.c", "picker mouse: right button chooses", "            b->pk_m = 0xC8;\n            done = 1;\n        }\n    }", "            done = 1;\n        }\n    }")
m("battle.c", "picker: pointer left on at the end", "    ptr_hide(b, scr, b->pk_ptr);   /* 0x1ae98 */", "    (void)0;   /* 0x1ae98 */")
m("battle.c", "items mouse: palette counter kept", "            b->it_count = 0x78;", "            (void)0;")
m("battle.c", "items mouse: row click keeps the counter", "            b->it_count = i;", "            (void)i;")
m("battle.c", "items mouse: icons all key 1", "            k = ((x - 0x54) >> 4) + 2;", "            k = 2;")
m("battle.c", "items mouse: corner does not leave", "            k = 0x40;", "            (void)0;")
m("battle.c", "rewards: a button does not leave", "if (!leave && walk_mouse(b->w, &mx, &my, &mb) && mb != 0)", "if (0)")
m("battle.c", "rewards: pointer left on", "        ptr_hide(b, scr, b->mptr);   /* 0x1fcd7 */", "        (void)0;   /* 0x1fcd7 */")
m("msgbox.c", "message box: left button closes it", "        if (m->mlatch || buttons != 2)", "        if (m->mlatch || buttons == 0)")
m("msgbox.c", "message box: no latch at the start", "    m->mlatch = 1;", "    m->mlatch = 0;")
# the world's mouse buttons (step 19)
m("walk.c", "world mouse: left press ignored", "        if (mb == 1) {\n            k = walk_mouse_key(", "        if (mb == 99) {\n            k = walk_mouse_key(")
m("walk.c", "world mouse: right press not Space", "        if (mb == 2)\n            k = 0x39;", "        if (mb == 2)\n            k = 0x1C;")
m("walk.c", "world mouse: walk flag not set", "                w->vc0 = 1;", "                (void)0;")
m("walk.c", "world mouse: held arrow kept after a walk", "        w->vc0 = 0;\n        w->c2 = 0;", "        w->vc0 = 0;")
m("walk.c", "world mouse: last key not kept", "    w->v30 = k;   /* 0x137d6", "    (void)0;   /* 0x137d6")
# "world mouse: last key before the suppression" is left out: it differs only for keys 2, 3 and A (kept as 0x82), and neither is an
# arrow, which is all the next press asks of it.
m("walk.c", "world mouse: the pass's own place", "k = walk_mouse_key(w->mx, w->my, w->v30);", "k = walk_mouse_key((unsigned)mx, (unsigned)my, w->v30);")
m("walk.c", "world mouse: pointer not moved with the shake", "size_t o = shift + (size_t)(x - 0x30)", "size_t o = (size_t)(x - 0x30)")
m("walk.c", "mouse key: up and down swapped", "        dir = y > 0x64 ? 0x50 : 0x48;", "        dir = y > 0x64 ? 0x48 : 0x50;")
m("walk.c", "mouse key: ties go up or down", "    if (dy <= dx)\n        dir = x < 0xA0", "    if (dy < dx)\n        dir = x < 0xA0")
m("walk.c", "mouse key: buttons after an arrow", "    if (last >= 0x48 && last < 0x51)\n        return dir;", "    if (last >= 0x48 && last < 0x50)\n        return dir;")
m("walk.c", "mouse key: F-key rows off by one", "static const unsigned top[7] = {0x15, 0x29", "static const unsigned top[7] = {0x16, 0x29")
# F1, talking (step 20)
m("walk.c", "talk: frame without the party", "    walk_draw_entities(w->buf, s, x, y, 1);\n    walk_draw_objects(w->buf, s, x, y);\n    if (s->header[5])", "    walk_draw_objects(w->buf, s, x, y);\n    if (s->header[5])")
m("walk.c", "talk: frame with the walking frame", "    walk_draw_entities(w->buf, s, x, y, 1);\n    walk_draw_objects(w->buf, s, x, y);\n    if (s->header[5])", "    walk_draw_entities(w->buf, s, x, y, w->hero_frame);\n    walk_draw_objects(w->buf, s, x, y);\n    if (s->header[5])")
m("walk.c", "talk: darkened once", "    for (int t = 0; t < 2; t++) {\n        fkey_shade(w);", "    for (int t = 0; t < 1; t++) {\n        fkey_shade(w);")
m("walk.c", "talk: palette not dimmed", "        w->dimpal[i] = w->dimpal[i] > 0x10 ? (uint8_t)(w->dimpal[i] - 0x10) : 0;", "        (void)0;")
m("walk.c", "talk: button not pressed", "    button_toggle(scr, 0x284, 0x2F, 0x14);   /* 0x1c3ba: F1's button pressed */", "    (void)0;")
m("walk.c", "button: inside turned too", "        memcpy(outer + (r + 2) * w + 2, inner + r * (w - 4), w - 4);", "        (void)inner;")
m("walk.c", "talk: cells beside not looked at", "        px[1] = (uint8_t)(px[1] - 2);\n        px[2] = (uint8_t)(px[2] + 2);", "        px[1] = (uint8_t)(px[1] - 0);\n        px[2] = (uint8_t)(px[2] + 0);")
m("walk.c", "talk: two cells only", "    for (int step = 0; step < 3; step++) {", "    for (int step = 0; step < 2; step++) {")
m("walk.c", "talk: villager not turned", "                if (dir < 4)\n                    r[2] = facing[dir];", "                (void)facing;")
m("walk.c", "talk: walk counter kept", "            w->ctr = vx;   /* [bp-0x3c]", "            (void)vx;   /* [bp-0x3c]")
m("walk.c", "talk: no zoom", "            d[0] = p;\n            d[1] = q;\n            d[SCREEN_W] = q;\n            d[SCREEN_W + 1] = w->table[q];", "            (void)d; (void)q;")
m("walk.c", "talk: the first entry always", "    while (k < 0x2F && w16(r, k + 5) != 0 && walk_flag_test(party, r[k], r[k + 1]))\n        k += 7;", "    (void)0;")
m("walk.c", "talk: the entry after the last set flag", "    if (k > 0xC)\n        k -= 7;\n    unsigned e", "    unsigned e")
# "talk: flag not set" is left out: every talk entry in scene 0 sets flag 0 bit 0, which is set from the start (a cleared one is set
# again by the scene's records before any talk), so it cannot differ in the starts the comparison can run.
m("walk.c", "talk: no next message", "    if (w->box.last == 0x7F) {", "    if (w->box.last == 0x17F) {")
m("walk.c", "talk: palette steps on the real palette", "    walk_t *w = ctx;\n    palette_cycle_on(w, scr, w->dimpal);", "    walk_t *w = ctx;\n    palette_cycle_on(w, scr, w->palbuf);")
# "talk: gold not drawn again" is left out: the coins cannot change in a talk until selling is built, and drawing the same number again
# changes nothing.
m("walk.c", "talk: button stays pressed", "    button_toggle(scr, 0x284, 0x2F, 0x14);\n    if (w->mouse_on)   /* only kept", "    if (w->mouse_on)   /* only kept")
m("walk.c", "talk: walk counters after F1", "    if (walk_keys(w, port60))\n        return w->unbuilt_new ? WALK_PASS_UNBUILT : 0;", "    walk_keys(w, port60);")
m("msgbox.c", "box: tail left as it was", "        for (unsigned i = 0; i < 4; i++)\n            tail[i] = table[tail[i]];", "        (void)0;")
# selling and asking talks (step 21)
m("msgbox.c", "box: no-wait box full width", "        bw -= 16 * ((0x28 - n) / 2);", "        (void)0;")
m("msgbox.c", "box: no-wait box at the text's row", "    unsigned at = SCREEN_W * row + x - SCREEN_W;", "    unsigned at = SCREEN_W * row + x;")
m("msgbox.c", "box: short box 15 high", "        bh = 0x10;", "        bh = 0xF;")
m("msgbox.c", "box: two-row box 46 high", "        bh = 0x1F;", "        (void)0;")
m("msgbox.c", "box: second shading by rows", "        for (unsigned i = SCREEN_W; i < SCREEN_W * 45 && i < len; i++)", "        for (unsigned i = bw; i < bw * (bh - 1); i++)")
m("walk.c", "sell: boxes wait", "    if (r[0x30] == 0 && r[k + 4] != 0xFF) {   /* the wait flag", "    if (r[k + 4] != 0xFF) {   /* the wait flag")
m("walk.c", "sell: bottom not put back", "    put(scr, 0xA780, w->talk_under, SCREEN_W, 46);\n    talk_done", "    talk_done")
m("walk.c", "yes/no: right half not pressed", "    button_toggle(scr, 0x30A5, 0x17, 0x14);", "    (void)0;")
m("walk.c", "yes/no: starts on no", "    w->yn_sel = 1;\n    w->talk_count", "    w->yn_sel = 0;\n    w->talk_count")
# "yes/no: count from 0" is left out: the count starts from the high word of the text's file offset, 0 for every message below 513,
# and every shipped seller's message is below 513.
m("walk.c", "yes/no: palette steps on the real palette", "        w->talk_count = 0;\n        palette_cycle_on(w, scr, w->dimpal);\n    }\n    if (w->yn_shown", "        w->talk_count = 0;\n        palette_cycle_on(w, scr, w->palbuf);\n    }\n    if (w->yn_shown")
m("walk.c", "yes/no: palette every 25th", "    if (w->talk_count > 0x19) {", "    if (w->talk_count > 0x18) {")
m("walk.c", "yes/no: Right says yes", "        if (k == 0x4D)\n            w->yn_sel = 0;", "        if (k == 0x4D)\n            w->yn_sel = 1;")
m("walk.c", "yes/no: Space does not take it", "        if (k != 0x1C && k != 0x39)", "        if (k != 0x1C)")
m("walk.c", "yes/no: buttons not put back", "    put(scr, 0x308D, w->yn_pic[0], 47, 20);", "    (void)0;")
m("walk.c", "yes/no: mouse halves the other way", "            w->yn_sel = mx < 0x124 ? 1 : 0;", "            w->yn_sel = mx < 0x124 ? 0 : 1;")
m("walk.c", "yes/no: any button takes it", "            done = mb == 1;", "            done = mb != 0;")
m("walk.c", "buy: free", "                gold -= price;", "                (void)0;")
m("walk.c", "buy: in the last place", "        for (unsigned j = i + 0x14; j < i + 0x28; j++)\n            if (party[j] == 0) {", "        for (unsigned j = i + 0x27; j >= i + 0x14; j--)\n            if (party[j] == 0) {")
m("walk.c", "buy: on credit", "    if (gold < price)\n        return;", "    if (gold < price)\n        gold = price;")
m("walk.c", "ask: box not drawn", "    put(scr, 0x71, w->ask_art + 0x284, 94, 16);", "    (void)0;")
m("walk.c", "ask: marks one pixel lower", "    fill(scr, 0x46E, 8, 1, 9);", "    fill(scr, 0x46E + 320, 8, 1, 9);")
m("walk.c", "ask: count from 0", "    w->talk_count = 0x16;", "    w->talk_count = 0;")
m("walk.c", "ask: no latch at the start", "    w->ask_latch = 1;\n    w->talking = 3;", "    w->ask_latch = 0;\n    w->talking = 3;")
m("walk.c", "ask: keys repeat", "        if (key >= 0x7F || !w->ask_latch)", "        if (key >= 0x7F)")
m("walk.c", "ask: digits from the right", "        put(scr, 0x58A + 9 * i, w->ask_art", "        put(scr, 0x5B7 - 9 * i, w->ask_art")
m("walk.c", "ask: no cap", "            if (w->ask_value > 50000)", "            if (w->ask_value > 99999)")
m("walk.c", "ask: Down below 0", "        if (key == 0x50 && w->ask_value >= unit[w->ask_pos])", "        if (key == 0x50 && w->ask_value >= 1)")
m("walk.c", "ask: Left does not wrap", "            w->ask_pos = w->ask_pos >= 5 ? 1 : w->ask_pos + 1;", "            w->ask_pos = w->ask_pos >= 5 ? 5 : w->ask_pos + 1;")
m("walk.c", "ask: old marks kept", "    fill(scr, 0x44A, 0x2C, 1, 0);", "    (void)0;")
m("walk.c", "ask: right button not Left", "        if (!done && mb == 2)\n            key = 0x4B;", "        (void)0;")
m("walk.c", "ask: plus and minus swapped", "            if (mx > 0x71 && mx < 0x83)\n                key = 0x48;", "            if (mx > 0x71 && mx < 0x83)\n                key = 0x50;")
m("walk.c", "ask: answer of the first entry", "    if (w->ask_value == w16(r, 0x28 + (w->talk_k - 0xC) / 7 * 2))", "    if (w->ask_value == w16(r, 0x28))")
m("walk.c", "ask: box not put back", "    put(scr, 0x71, w->ask_under, 94, 16);", "    (void)0;")
m("walk.c", "gold not drawn at the start", "    walk_show_gold(w, scr);   /* 0x10bc9 */", "    (void)0;   /* 0x10bc9 */")


def main():
    if not (ROOT / "work/check_battle").exists():
        print("run tools/check_battle.sh first")
        return 2
    only = os.environ.get("ONLY", "")
    skip = int(os.environ.get("SKIP", "0"))   # SKIP=n leaves out the first n mutations of the list
    missed = bad = ran = 0
    WORK.mkdir(parents=True, exist_ok=True)
    try:
        for f, name, old, new in M[skip:]:
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
            if f == "walk.c":   # the world's own unit tests (tests/test_walk.c) pin parts of the resumed pass
                core = re.search(r"^CORE_SRC\s*:=\s*(.*)$", (ROOT / "Makefile").read_text(), re.M).group(1).split()
                opl = sorted(str(p) for p in (ROOT / "build/opl").glob("*.o"))
                r = subprocess.run(["cc", "-std=c99", "-O1", "-w", "-I", str(src)] + [str(WORK / s) for s in core] +
                                   [str(ROOT / "tests/test_walk.c")] + opl + ["-o", str(WORK / "test_walk"), "-lc++" if sys.platform == "darwin" else "-lstdc++"],
                                   capture_output=True, text=True)
                if r.returncode != 0:
                    print(f"DOES NOT BUILD: {name}: {r.stderr.strip()[:200]}")
                    bad += 1
                    continue
                if subprocess.run([str(WORK / "test_walk"), str(ROOT / "original_bin")], capture_output=True).returncode != 0:
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
