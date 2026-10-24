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
SOURCES = ["src/walk.c", "src/battle.c", "src/msgbox.c", "src/gameover.c", "src/sfx.c", "src/voc.c", "src/walkscene.c", "src/assets.c", "src/lcz.c", "src/screen.c"]

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
m("battle.c", "a held key counts again", "    b->latch = 0;\n    unsigned *fm = walk_fm(b->w)", "    unsigned *fm = walk_fm(b->w)")
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
m("battle.c", "magic: palette counter from 0", "b->mg_count = b->world ? 0xC3 : 3;", "b->mg_count = b->world ? 0xC3 : 0;")
m("battle.c", "magic: row not lit", "list_row(scr, b->mg_cur, 1);\n        b->mg_prev", "b->mg_prev")
m("battle.c", "magic: Down wraps to column 2 early", "if (b->mg_idx == 0x32)\n            b->mg_cur = 0x8161;", "if (b->mg_idx == 0x28)\n            b->mg_cur = 0x8161;")
m("battle.c", "magic: Space casts", "    if (k != 2)\n        return;\n    const uint8_t *t = b->mg_tab", "    if (k != 2 && k != 0x39)\n        return;\n    const uint8_t *t = b->mg_tab")
m("battle.c", "magic: attack spell costs nothing", "    party[b->v44 + 0xD] = (uint8_t)mp;\n    party[b->v44 + 0xE] = (uint8_t)(mp >> 8);\n    magic_end(b, scr, t[7]);", "    (void)mp;\n    magic_end(b, scr, t[7]);")
m("battle.c", "magic: heal a fallen one", "        if (hp != 0) {\n            hp = (hp + v) & 0xFFFF;", "        if (1) {\n            hp = (hp + v) & 0xFFFF;")
m("battle.c", "magic: heal not capped", "            if (hp > w16(party, m + 0xA))\n                hp = w16(party, m + 0xA);\n            party[m + 8] = (uint8_t)hp;\n            party[m + 9] = (uint8_t)(hp >> 8);\n            res = 0x2F;", "            party[m + 8] = (uint8_t)hp;\n            party[m + 9] = (uint8_t)(hp >> 8);\n            res = 0x2F;")
m("battle.c", "magic: revive face not drawn", "    if (res == 0x1F && !b->world)   /* 0x220b3", "    if (0)   /* 0x220b3")
m("battle.c", "magic: flash colour", "        for (unsigned s = 0; s < 16; s++) {\n            silhouette(scr, k * 0x12 + 0x5E47",
  "        for (unsigned s = 0; s < 15; s++) {\n            silhouette(scr, k * 0x12 + 0x5E47")
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
m("walk.c", "talk: button not pressed", "    walk_button_toggle(scr, 0x284, 0x2F, 0x14);   /* 0x1c3ba: F1's button pressed */", "    (void)0;")
m("walk.c", "button: inside turned too", "        memcpy(outer + (r + 2) * w + 2, inner + r * (w - 4), w - 4);", "        (void)inner;")
m("walk.c", "talk: cells beside not looked at", "        px[1] = (uint8_t)(px[1] - 2);\n        px[2] = (uint8_t)(px[2] + 2);", "        px[1] = (uint8_t)(px[1] - 0);\n        px[2] = (uint8_t)(px[2] + 0);")
m("walk.c", "talk: two cells only", "    for (int step = 0; step < 3; step++) {\n        for (int v = 0;", "    for (int step = 0; step < 2; step++) {\n        for (int v = 0;")
m("walk.c", "talk: villager not turned", "                if (dir < 4)\n                    r[2] = facing[dir];", "                (void)facing;")
m("walk.c", "talk: walk counter kept", "            w->ctr = vx;   /* [bp-0x3c]", "            (void)vx;   /* [bp-0x3c]")
m("walk.c", "talk: no zoom", "            d[0] = p;\n            d[1] = q;\n            d[SCREEN_W] = q;\n            d[SCREEN_W + 1] = w->table[q];", "            (void)d; (void)q;")
m("walk.c", "talk: the first entry always", "    while (k < 0x2F && w16(r, k + 5) != 0 && walk_flag_test(party, r[k], r[k + 1]))\n        k += 7;", "    (void)0;")
m("walk.c", "talk: the entry after the last set flag", "    if (k > 0xC)\n        k -= 7;\n    unsigned e", "    unsigned e")
# "talk: flag not set" is left out: every talk entry in scene 0 sets flag 0 bit 0, which is set from the start (a cleared one is set
# again by the scene's records before any talk), so it cannot differ in the starts the comparison can run.
m("walk.c", "talk: no next message", "    if (w->box.last == 0x7F) {\n        w->talk_msg++;\n        if (talk_open", "    if (w->box.last == 0x17F) {\n        w->talk_msg++;\n        if (talk_open")
m("walk.c", "talk: palette steps on the real palette", "    walk_t *w = ctx;\n    palette_cycle_on(w, scr, w->dimpal);", "    walk_t *w = ctx;\n    palette_cycle_on(w, scr, w->palbuf);")
# "talk: gold not drawn again" is left out: the coins cannot change in a talk until selling is built, and drawing the same number again
# changes nothing.
m("walk.c", "talk: button stays pressed", "    walk_button_toggle(scr, 0x284, 0x2F, 0x14);\n    if (w->mouse_on)   /* only kept", "    if (w->mouse_on)   /* only kept")
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
m("walk.c", "yes/no: right half not pressed", "    walk_button_toggle(scr, 0x30A5, 0x17, 0x14);", "    (void)0;")
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
# the dark scenes (step 22; RUNS=dark runs only their starts)
m("walk.c", "dark: not doubled", "    if (s->header[5]) {   /* 0x125c6", "    if (0) {   /* 0x125c6")
m("walk.c", "dark: shake not in the source", "        walk_double_dark(w->dbuf, w->buf + sh, w->table, s->dark_mask);", "        walk_double_dark(w->dbuf, w->buf, w->table, s->dark_mask);")
m("walk.c", "dark: window copied with the shake", "        view = w->dbuf;\n        sh = 0;", "        view = w->dbuf;")
m("walk.c", "dark: pointer into the world buffer", "                    view[o] = w->mpic[r * 6 + c];", "                    w->buf[o] = w->mpic[r * 6 + c];")
m("walk.c", "dark: band starts a row late", "            if (r >= 34 && r < 34 + 27) {", "            if (r >= 35 && r < 34 + 27) {")
m("walk.c", "dark: band ends a row early", "            if (r >= 34 && r < 34 + 27) {", "            if (r >= 34 && r < 33 + 27) {")
m("walk.c", "dark: outside pixel kept", "            } else {\n                d[1] = 0;", "            } else {\n                d[1] = p;")
m("walk.c", "dark: lower-left not shaded", "                d[1] = q;\n                d[WALK_W] = q;\n                d[WALK_W + 1] = table[q];\n            } else {",
  "                d[1] = q;\n                d[WALK_W] = p;\n                d[WALK_W + 1] = table[q];\n            } else {")
m("walk.c", "dark: no mask", "        dst[DARK_MASK_AT + i] &= mask[i];", "        (void)0;")
m("walk.c", "dark: mask a row low", "enum { DOUBLE_SRC = 0x3220, DOUBLE_DST = 0xE10, DARK_MASK_AT = 0x48A0 };", "enum { DOUBLE_SRC = 0x3220, DOUBLE_DST = 0xE10, DARK_MASK_AT = 0x48A0 + 224 };")
m("walk.c", "dark: F-keys' world not doubled", "    if (s->header[5]) {   /* 0x19985", "    if (0) {   /* 0x19985")
m("walk.c", "dark: F-keys' doubling darkened", "        walk_double(w->dbuf, w->buf, w->table);\n        memcpy", "        walk_double_dark(w->dbuf, w->buf, w->table, s->dark_mask);\n        memcpy")
m("walk.c", "records: not again after a talk", "    records_again(w, 0x384 / WS_RECORD_SIZE);   /* 0x1729e", "    (void)0;   /* 0x1729e")
# Left out: the record loop running again with [bp-0xbe] 0, and [bp-0xbe] not taking the kind of a record stepped on: it is 0 only
# after a record of kind 0 (or an unbuilt key, 0x14cd6), and a kind-0 record is itself not built, so no start can reach it.
m("walk.c", "records: [bp-0xbe] 0 at the start", "    w->vbe = 1;\n", "    w->vbe = 0;\n")
m("walkscene.c", "dark: mask from another place", "decode_block(rp + 0x4350,", "decode_block(rp + 0x4351,")
# the start's zoom (step 23; RUNS=zoom runs only its checks)
m("walk.c", "zoom: across from 0", "        unsigned bx = 1;\n        uint8_t al", "        unsigned bx = 0;\n        uint8_t al")
m("walk.c", "zoom: down from 0", "    unsigned dy = 1;\n", "    unsigned dy = 0;\n")
m("walk.c", "zoom: gaps keep the pixel", "            di++;\n            al = 0;\n", "            di++;\n")
m("walk.c", "zoom: grows 16 across", "    unsigned cw = 24 + 14 * (unsigned)k,", "    unsigned cw = 24 + 16 * (unsigned)k,")
m("walk.c", "zoom: corner a pixel off", "off = 0x3654 - 0x487 * (unsigned)k;", "off = 0x3655 - 0x487 * (unsigned)k;")
m("walk.c", "zoom: grabbed a row low", "w->buf + 0xE10 + r * WALK_W, WALK_ZOOM_W);", "w->buf + 0xE10 + (r + 1) * WALK_W, WALK_ZOOM_W);")
m("walk.c", "zoom: party walking, not standing", "    walk_draw_entities(w->buf, s, x, y, 1);\n    walk_draw_objects(w->buf, s, x, y);\n    if (s->header[5]) {   /* 0x19985",
  "    walk_draw_entities(w->buf, s, x, y, 0);\n    walk_draw_objects(w->buf, s, x, y);\n    if (s->header[5]) {   /* 0x19985")
m("walk.c", "zoom: not cleared", "    memset(dst, 0, W * H);\n    unsigned xstep", "    unsigned xstep")
# story enemies (step 24; RUNS=boss). Left out: the touch leaving the member's offset in the hero's walk counter and byte 0x2f in his
# frame (0x1276c, 0x1277f): only the box's passes come between, which draw no world, and walk_resume sets both again.
m("walk.c", "boss: box colour", "msg, 0xC2, 1);", "msg, 0xC3, 1);")
m("walk.c", "boss: no box", "        if (msg != 0) {\n            int r = msgbox_open", "        if (0) {\n            int r = msgbox_open")
m("walk.c", "boss: battle with the box", "            return ret & ~WALK_PASS_BATTLE;", "            return ret;")
m("walk.c", "boss: box's palette not stepped", "    palette_cycle(ctx, scr);\n}\n\n/* 0x1b506 for a story", "    (void)ctx;\n}\n\n/* 0x1b506 for a story")
m("walk.c", "boss: won, enemy stays", "        rec[0] = rec[1] = 0;\n        walk_flag_set(s->tab_b, rec[0xC]", "        walk_flag_set(s->tab_b, rec[0xC]")
m("walk.c", "boss: won, flag not set", "        walk_flag_set(s->tab_b, rec[0xC], rec[0xD]);\n        return;", "        return;")
m("walk.c", "boss: four kills enough", "    if ((result & 0xFF) == 5) {   /* 0x12fd1", "    if ((result & 0xFF) >= 4) {   /* 0x12fd1")
m("walk.c", "boss: fled, followers not put back", "    for (int i = 0; i < 5; i++) {\n        s->tab_b[WS_PARTY_SIZE * i] = x;",
  "    for (int i = 0; i < 1; i++) {\n        s->tab_b[WS_PARTY_SIZE * i] = x;")
m("walk.c", "boss: fled, y not put back", "        s->tab_b[WS_PARTY_SIZE * i + 1] = y;\n", "")
m("walk.c", "boss: fled, trail not put back", "        s->trail[3 * i] = x;\n", "")
m("walk.c", "boss: won, the pass goes on", "    if (w->battle_boss && w->ctr == 5) {", "    if (0) {")
m("walk.c", "boss: won, records not again", "        records_again(w, 0xC8 / WS_RECORD_SIZE);", "        (void)0;")
# keys 1 and 2 in the fight with an FM card (RUNS=fm). Left out: the music flag itself not switched (only the caller's song and the
# button at a later start read it, neither compared).
m("battle.c", "fm: no music button", "        sound_button(b, scr, 0x7B8D);", "        (void)0;")
m("battle.c", "fm: sound key without a Sound Blaster", "    if (k == 3 && *sb) {", "    if (k == 3) {")
m("battle.c", "fm: pointer not taken off", "    if (mouse_on(b))\n        walk_mouse_put(scr, b->mptr, walk_mouse_under(b->w), 0);\n    walk_button_toggle",
  "    walk_button_toggle")
m("battle.c", "fm: pointer place not kept", "    if (mouse_on(b))\n        walk_mouse_grab(scr, b->mptr, walk_mouse_under(b->w));\n}", "}")
m("battle.c", "fm: [bp-0x58] 0", "    b->v58 = *walk_fm(w) ? 1 : 0;", "    b->v58 = 0;")
m("walk.c", "fm: music button not pressed at the start", "    if (w->fm == 0x81)\n        walk_button_toggle", "    if (0)\n        walk_button_toggle")
# the picker face's flash (RUNS=magic_heal)
m("battle.c", "flash: once, not three times", "    for (int rep = 0; rep < 3; rep++)", "    for (int rep = 0; rep < 1; rep++)")
m("battle.c", "flash: colours up", "(uint8_t)(res - s));", "(uint8_t)(res + s));")
# the game-over screen's mouse (RUNS=gameover)
m("gameover.c", "gameover: buttons ignored", "    return (buttons & 0xFF) != 0;", "    return 0;")
m("gameover.c", "gameover: right button ignored", "    return (buttons & 0xFF) != 0;", "    return (buttons & 1) != 0;")
# the Sound Blaster's sounds (step 25; RUNS=sb_). Left out: the cut at the rewards bar's end with the sounds off (nothing plays then).
m("walk.c", "sb: destroyed enemy silent", "            walk_sfx_play(w, SFX_EF, 0);\n            sh[2] = 0xA;", "            sh[2] = 0xA;")
m("walk.c", "sb: destroyed enemy, other sound", "            walk_sfx_play(w, SFX_EF, 0);\n            sh[2] = 0xA;", "            walk_sfx_play(w, SFX_EF, 0x5F80);\n            sh[2] = 0xA;")
m("walk.c", "sb: spark sound at frame 1", "if (f == 2 && w->sb == 0x81 && !walk_sfx_busy(w))", "if (f == 1 && w->sb == 0x81 && !walk_sfx_busy(w))")
m("walk.c", "sb: spark sound over a busy card", "if (f == 2 && w->sb == 0x81 && !walk_sfx_busy(w))", "if (f == 2 && w->sb == 0x81)")
m("walk.c", "sb: spark, other sound", "                walk_sfx_play(w, SFX_EF, 0x5F80);", "                walk_sfx_play(w, SFX_EF, 0x127D);")
m("walk.c", "sb: roof off silent", "            walk_sfx_play(w, SFX_EF, 0x127D);   /* 0x17b9e */", "")
m("walk.c", "sb: roof back silent", "            walk_sfx_play(w, SFX_EF, 0x127D);   /* 0x17c2a */", "")
m("walk.c", "sb: right answer silent", "        walk_sfx_play(w, SFX_EF, 0x1BF5);\n    }", "    }")
m("walk.c", "sb: right answer, other file", "        walk_sfx_play(w, SFX_EF, 0x1BF5);\n    }", "        walk_sfx_play(w, SFX_LOC, 0x1BF5);\n    }")
m("walk.c", "sb: plays with the sounds off", "    if (w->sb == 0x81 && w->sfx.play)", "    if (w->sb && w->sfx.play)")
m("walk.c", "sb: never busy", "    return w->sfx.busy ? w->sfx.busy(w->sfx.ctx) : 0;", "    return 0;")
m("walk.c", "sb: sound button not pressed at the start", "    if (w->sb == 0x81)\n        walk_button_toggle", "    if (0)\n        walk_button_toggle")
m("battle.c", "sb: touch silent", "    walk_sfx_play(w, SFX_EF, 0);   /* 0x12df4 */", "")
m("battle.c", "sb: death silent", "                        walk_sfx_play(b->w, SFX_LOC, 0);   /* 0x1ff7c */", "")
m("battle.c", "sb: monster's blow silent", "    walk_sfx_play(b->w, SFX_LOC, 0xBC69);   /* 0x1d8f1 */", "")
m("battle.c", "sb: hit silent", "            walk_sfx_play(b->w, SFX_LOC, 0x37A3);   /* 0x1dfb9 */", "")
m("battle.c", "sb: spell's end silent", "            walk_sfx_play(b->w, SFX_LOC, 0x37A3);   /* 0x1e0ba */", "")
m("battle.c", "sb: no shout at an attack", "    member_sound(b, n);   /* 0x1f083 */", "")
m("battle.c", "sb: no shout at a spell", "        member_sound(b, n);   /* 0x1f4f4 */", "")
m("battle.c", "sb: the next member's shout", "        walk_sfx_play(b->w, SFX_LOC, at[n]);", "        walk_sfx_play(b->w, SFX_LOC, at[(n + 1) % 5]);")
m("battle.c", "sb: third member's shout", "0x8849, 0x8E01, 0xA1BB};", "0x8E01, 0x8E01, 0xA1BB};")
m("battle.c", "sb: fifth member's shout", "0x8849, 0x8E01, 0xA1BB};", "0x8849, 0x8E01, 0xA1BC};")
m("battle.c", "sb: rewards bar leaves the sound on", "        walk_sfx_stop(b->w);   /* 0x1fcc8: the sound is cut */", "")
m("battle.c", "sb: key 2 does not switch", "        *sb = *sb == 0x81 ? 0x80 : 0x81;", "        (void)0;")
m("battle.c", "sb: no sound button in the fight", "        sound_button(b, scr, 0x948D);", "        (void)0;")
m("sfx.c", "sfx: passes rounded down", "TK_WALK_PASS_HZ * (256u - block[4]) + 999999u) / 1000000u;", "TK_WALK_PASS_HZ * (256u - block[4])) / 1000000u;")
m("sfx.c", "sfx: passes from the divisor", "(256u - block[4])", "(255u - block[4])")
m("voc.c", "voc: 2.6-bit step stops at 3", "top = codec == 2 ? 4 : 5", "top = codec == 2 ? 3 : 5")
m("voc.c", "voc: 2-bit step stops at 4", "top = codec == 2 ? 4 : 5", "top = codec == 2 ? 4 : 4")
m("voc.c", "voc: 2.6-bit step 4 as the others", "codec == 2 && *r == 4 ? (int)(10 * m + 5)", "codec == 2 && *r == 5 ? (int)(10 * m + 5)")
m("voc.c", "voc: 2.6-bit third code without its 0", "(b & 3) << 1, &level", "b & 3, &level")
m("voc.c", "voc: 2-bit low bits first", "for (int s = 6; s >= 0; s -= 2)", "for (int s = 0; s <= 6; s += 2)")
m("voc.c", "voc: step does not shrink", "    else if (m == 0 && *r > 0)\n        (*r)--;\n    return (uint8_t)*level;\n}\n\nsize_t voc_adpcm_decode",
  "    return (uint8_t)*level;\n}\n\nsize_t voc_adpcm_decode")
m("voc.c", "voc: frames of a 2-bit block", "    return codec == 0 ? 1 : codec + 1;", "    return codec == 0 ? 1 : codec == 3 ? 3 : codec + 1;")
# F2, looking (step 26; RUNS=look). Left out: the pointer's corner put back before the button (the pointer is not over F2 in any
# start but look_mouse's click, and the click's press already shows it); the records run again after a look (a look sets no flag, and
# with the shipped flags no scene's records change anything when run again, after up to 400 passes of animation: work/b8/rerun.c; a
# kind 0x0d record turned on by an earlier talk could reset a villager who moved since).
m("walk.c", "look: no prelude", "        fkey_begin(w, w->scr);\n        look_key(w, w->scr);", "        look_key(w, w->scr);")
m("walk.c", "look: button not pressed", "    walk_button_toggle(scr, 0x1B84, 0x2F, 0x14);   /* 0x1c3ba: F2's button pressed */", "    (void)0;")
m("walk.c", "look: button stays pressed", "static void look_done(walk_t *w, screen_t *scr)\n{\n    walk_button_toggle(scr, 0x1B84, 0x2F, 0x14);",
  "static void look_done(walk_t *w, screen_t *scr)\n{\n    (void)0;")
m("walk.c", "look: second cell to the left", "        x1 = (uint8_t)(x1 + 2);\n    else", "        x1 = (uint8_t)(x1 - 2);\n    else")
m("walk.c", "look: second cell above", "        y1 = (uint8_t)(y1 + 1);\n    unsigned si", "        y1 = (uint8_t)(y1 - 1);\n    unsigned si")
m("walk.c", "look: no second cell", "        if (si == 0x2EE)\n            si = walk_look_record(s->tab_a, x1, y1);", "        (void)x1; (void)y1;")
m("walk.c", "look: two steps only", "    unsigned si = 0x2EE;\n    for (int step = 0; step < 3; step++) {", "    unsigned si = 0x2EE;\n    for (int step = 0; step < 2; step++) {")
m("walk.c", "look: up goes down", "        if (dir == 0) {\n            y0--;\n            y1--;", "        if (dir == 0) {\n            y0++;\n            y1++;")
m("walk.c", "look: left goes right", "            x0 = (uint8_t)(x0 - 2);\n            x1 = (uint8_t)(x1 - 2);", "            x0 = (uint8_t)(x0 + 2);\n            x1 = (uint8_t)(x1 + 2);")
m("walk.c", "look: right one cell short", "            x0 = (uint8_t)(x0 + 2);\n            x1 = (uint8_t)(x1 + 2);\n        }\n    }", "            x0 = (uint8_t)(x0 + 1);\n            x1 = (uint8_t)(x1 + 1);\n        }\n    }")
m("walk.c", "look: walk counter kept", "    w->ctr = x0;\n    w->hero_frame = y0;", "    w->hero_frame = y0;")
m("walk.c", "look: hero frame kept", "    w->ctr = x0;\n    w->hero_frame = y0;", "    w->ctr = x0;")
m("walk.c", "look: any kind", "        if (r[0] == x && r[1] == y && r[2] == kind)", "        if (r[0] == x && r[1] == y)")
m("walk.c", "look: no picture", "    if (r[0xE] != 0)\n        look_picture(w, scr, r[0xE]);", "    (void)0;")
m("walk.c", "look: next picture", "(size_t)(pic - 1) * 7000", "(size_t)pic * 7000")
m("walk.c", "look: no frame", "    fill(scr, 0x38AD, 102, 1, 0x28);", "    (void)0;")
m("walk.c", "look: frame's right side missing", "    fill(scr, 0x38AD + SCREEN_W + 101, 1, 70, 0x28);", "    (void)0;")
m("walk.c", "look: frame colour", "    fill(scr, 0x38AD + 71 * SCREEN_W, 102, 1, 0x28);", "    fill(scr, 0x38AD + 71 * SCREEN_W, 102, 1, 0x29);")
m("walk.c", "look: picture a pixel left", "    put(scr, 0x39EE, w->buf, 100, 70);", "    put(scr, 0x39ED, w->buf, 100, 70);")
m("walk.c", "look: picture not into the world buffer", "assets_read_at(\"spic.lcz\", (size_t)(pic - 1) * 7000, w->buf, 7000)",
  "assets_read_at(\"spic.lcz\", (size_t)(pic - 1) * 7000, w->buf + 0x8000, 7000)")
m("walk.c", "look: message's own entry", "    w->talk_msg = w16(r, walk_look_entry(party, r) - 2);", "    w->talk_msg = w16(r, walk_look_entry(party, r) + 2);")
m("walk.c", "look: entries' flags not tested", "        if (!walk_flag_test(party, r[k], r[k + 1]))\n            break;\n        k += 4;", "        k += 4;")
m("walk.c", "look: entries go on past a 0", "        if (k == 15 || w16(r, k + 2) == 0)", "        if (k == 15)")
m("walk.c", "look: talk's colour", "w->talk_msg, 0xB0, 1);", "w->talk_msg, 0x50, 1);")
m("walk.c", "look: no chain", "    if (w->box.last == 0x7F) {\n        w->talk_msg++;\n        if (look_open(w, scr) == 0)", "    if (0) {\n        w->talk_msg++;\n        if (look_open(w, scr) == 0)")
m("walk.c", "look: palette steps on the palette", "    palette_cycle_on(ctx, scr, ((walk_t *)ctx)->dimpal);   /* look */", "    palette_cycle(ctx, scr);   /* look */")
m("walk.c", "look: pass not ended", "        look_key(w, w->scr);\n        return 1;", "        look_key(w, w->scr);\n        return 0;")
# F4, casting (step 29; RUNS=cast). Left out: the records run again after F4 (no flag changes; as for F2); a fallen caster let through
# the picker (its flag 0), or F4's own HP test after it (0x15570) taken away: the original refuses a fallen caster twice, so either alone
# is hidden by the other.
m("walk.c", "cast: no prelude", "        fkey_begin(w, w->scr);\n        cast_key(w, w->scr);", "        cast_key(w, w->scr);")
m("walk.c", "cast: button not pressed", "    walk_button_toggle(scr, 0x4D84, 0x2F, 0x14);   /* 0x1c3ba: F4's button pressed */", "    (void)0;")
m("walk.c", "cast: button stays pressed", "    w->cast = NULL;\n    walk_button_toggle(scr, 0x4D84, 0x2F, 0x14);", "    w->cast = NULL;")
m("walk.c", "cast: pointer not kept again", "    w->cast = NULL;\n    walk_button_toggle(scr, 0x4D84, 0x2F, 0x14);\n    if (w->mouse_on) {",
  "    w->cast = NULL;\n    walk_button_toggle(scr, 0x4D84, 0x2F, 0x14);\n    if (0) {")
m("walk.c", "cast: pass not ended", "        cast_key(w, w->scr);\n        return 1;", "        cast_key(w, w->scr);\n        return 0;")
m("walk.c", "cast: no flash steps shown", "    battle_set_show(w->cast, w->show, w->show_ctx);", "    (void)0;")
m("walk.c", "cast: palette on the palette", "void walk_palette_step_dim(walk_t *w, screen_t *scr)\n{\n    palette_cycle_on(w, scr, w->dimpal);",
  "void walk_palette_step_dim(walk_t *w, screen_t *scr)\n{\n    palette_cycle(w, scr);")
m("battle.c", "cast: no faces in the caster's picker", "    pick_begin(b, scr, 1, 0);   /* 0x15549", "    pick_begin(b, scr, 0, 0);   /* 0x15549")
m("battle.c", "cast: picker count", "    b->pk_count = 0x2D;", "    b->pk_count = 0x10;")
m("battle.c", "cast: list count", "    b->mg_count = b->world ? 0xC3 : 3;", "    b->mg_count = 3;")
m("battle.c", "cast: monsters' spell takes MP", "    if (b->world)   /* 0x22107: a spell on the monsters does nothing in the world; the list goes on */\n        return;",
  "    if (b->world) {   /* 0x22107 */\n        party_of(b)[b->v44 + 0xD] -= 2;\n        return;\n    }")
m("battle.c", "cast: monsters' spell ends the list", "    if (b->world)   /* 0x22107: a spell on the monsters does nothing in the world; the list goes on */\n        return;",
  "    if (b->world) {   /* 0x22107 */\n        b->over = BATTLE_BACK;\n        return;\n    }")
m("battle.c", "cast: revived face in the strip", "    if (res == 0x1F && !b->world)", "    if (res == 0x1F)")
m("battle.c", "cast: battle's step in the world", "    if (b->world)\n        walk_palette_step_dim(b->w, scr);", "    if (0)\n        walk_palette_step_dim(b->w, scr);")
# F3, taking (step 28; RUNS=take). Left out: the hero's frame from the first cell's y instead of the third's (they differ by one, and
# any frame of 3 or more draws the same sprite, walk_sprite_off_party, and is reset at the next step; both are below 3 only for
# y / 2 < 3, where no scene lets the hero stand).
m("walk.c", "take: no prelude", "        fkey_begin(w, w->scr);\n        take_key(w, w->scr);", "        take_key(w, w->scr);")
m("walk.c", "take: button not pressed", "    walk_button_toggle(scr, 0x3484, 0x2F, 0x14);   /* 0x1c3ba: F3's button pressed */", "    (void)0;")
m("walk.c", "take: button stays pressed", "static void take_done(walk_t *w, screen_t *scr)\n{\n    walk_button_toggle(scr, 0x3484, 0x2F, 0x14);",
  "static void take_done(walk_t *w, screen_t *scr)\n{\n    (void)0;")
m("walk.c", "take: no third cell", "        for (int j = 0; j < 3 && si == 0x2EE; j++)", "        for (int j = 0; j < 2 && si == 0x2EE; j++)")
m("walk.c", "take: third cell on the right", "        x[2] = (uint8_t)(x[2] - 2);\n    } else {", "        x[2] = (uint8_t)(x[2] + 4);\n    } else {")
m("walk.c", "take: two steps only", "    unsigned si = 0x2EE, ok = dir;\n    for (int step = 0; step < 3; step++) {", "    unsigned si = 0x2EE, ok = dir;\n    for (int step = 0; step < 2; step++) {")
m("walk.c", "take: flag not tested", "            ok = walk_flag_test(party, r[3], r[4]);", "            ok = 1;")
m("walk.c", "take: walk counter kept", "    w->ctr = x[2];\n    w->hero_frame = y[2];", "    w->hero_frame = y[2];")
m("walk.c", "take: item stays", "        party[slot] = (uint8_t)item;\n        r[0xD] = 0;", "        party[slot] = (uint8_t)item;")
m("walk.c", "take: item not given", "        party[slot] = (uint8_t)item;\n        r[0xD] = 0;", "        r[0xD] = 0;")
m("walk.c", "take: item's flag not set", "        r[0xD] = 0;\n        walk_flag_set(party, r[0xB], r[0xC]);", "        r[0xD] = 0;")
m("walk.c", "take: next item's name", "assets_read_at(\"OBJECT.LCC\", (size_t)(item - 1) * 30", "assets_read_at(\"OBJECT.LCC\", (size_t)item * 30")
m("walk.c", "take: dead member gets it", "        if (party[di + 3] >= 5 || w16(party, di + 8) == 0)", "        if (party[di + 3] >= 5)")
m("walk.c", "take: strip's shadow colour", "                scr->pix[shadow[s] + (i / 96) * SCREEN_W + i % 96] = 0x1E;", "                scr->pix[shadow[s] + (i / 96) * SCREEN_W + i % 96] = 0x1F;")
m("walk.c", "take: no shadow below", "    static const unsigned shadow[4] = {0x9530, 0x97B0, 0x966F, 0x9671};", "    static const unsigned shadow[4] = {0x9530, 0x9530, 0x966F, 0x9671};")
m("walk.c", "take: strip's colour", "    if (battle_text_pic(w->font8, text, 12, 0x10, px, &gw))", "    if (battle_text_pic(w->font8, text, 12, 0x20, px, &gw))")
m("walk.c", "take: silent", "    walk_sfx_play(w, SFX_EF, 0x1BF5);\n    if (w->sb == 0x81) {", "    if (w->sb == 0x81) {")
m("walk.c", "take: sound not waited for", "        walk_sfx_wait(w);\n        walk_sfx_stop(w);", "        walk_sfx_stop(w);")
m("walk.c", "take: item palette count 0", "        take_strip(w, scr, text, 1);", "        take_strip(w, scr, text, 0);")
m("walk.c", "take: coins palette count 0", "    take_strip(w, scr, text, y[0]);", "    take_strip(w, scr, text, 0);")
m("walk.c", "take: palette never steps", "    if (w->take_count > 0x28) {", "    if (w->take_count > 0x128) {")
m("walk.c", "take: Esc does not end", "    if (k == 1 || k == 0x39 || k == 0x1C ||", "    if (k == 0x39 || k == 0x1C ||")
m("walk.c", "take: Enter does not end", "    if (k == 1 || k == 0x39 || k == 0x1C ||", "    if (k == 1 || k == 0x39 ||")
m("walk.c", "take: any button ends", "(walk_mouse(w, &mx, &my, &mb) && (mb & 0xFF) == 2)", "(walk_mouse(w, &mx, &my, &mb) && (mb & 2))")
m("walk.c", "take: full box's colour", "0xB, 0x20, 1);", "0xB, 0x50, 1);")
m("walk.c", "take: full box steps the dimmed palette", "static void take_box_step(void *ctx, screen_t *scr)\n{\n    palette_cycle(ctx, scr);",
  "static void take_box_step(void *ctx, screen_t *scr)\n{\n    palette_cycle_on(ctx, scr, ((walk_t *)ctx)->dimpal);")
m("walk.c", "take: coins over 60,000", "    if (60000u - gold < coins) {", "    if (65535u - gold < coins) {")
m("walk.c", "take: coins stay", "    r[0xD] = r[0xE] = 0;\n    walk_show_gold(w, scr);", "    walk_show_gold(w, scr);")
m("walk.c", "take: coins not drawn", "    walk_show_gold(w, scr);   /* 0x1bd6e */\n    walk_flag_set(party, r[0xB], r[0xC]);", "    walk_flag_set(party, r[0xB], r[0xC]);")
m("walk.c", "take: leading zeros kept", "    for (int i = 0; i < 4 && text[i] <= '0'; i++)", "    for (int i = 0; i < 0 && text[i] <= '0'; i++)")
m("walk.c", "take: coins' unit word", "{' ', 0xAA, 0x54, 0xAA, 0xF7, 0xB9, 0xF4};", "{' ', 0xAA, 0x54, 0xAA, 0xF7, 0xB9, 0xF5};")
m("walk.c", "take: no records again", "    w->talking = 0;\n    records_again(w, 0x384 / WS_RECORD_SIZE);\n}\n\n/* 0x15fea",
  "    w->talking = 0;\n}\n\n/* 0x15fea")
m("walk.c", "take: pass not ended", "        take_key(w, w->scr);\n        return 1;", "        take_key(w, w->scr);\n        return 0;")
m("walk.c", "take: any kind", "            si = walk_record_of_kind(s->tab_a, x[j], y[j], 2);", "            si = walk_record_of_kind(s->tab_a, x[j], y[j], 9);")
# the view past the map's edges (step 27; RUNS=edge). Left out: the art segment's first bytes (only a tile number 0x806a .. 0x807f
# reads them; no start's view has one) and the villager table in the map segment (only the ground's last row with y / 2 = 127 and x
# above 228 reaches 0x8c16; no scene lets the hero stand there).
m("walk.c", "edge: ground column not a byte", "| (((x & 0xFE) - 0x16) & 0xFF);", "| (((x & 0xFE) - 0x16) & 0x1FF);")
m("walk.c", "edge: ground row not a byte", "    unsigned bx = ((((y >> 1) - 10) & 0xFF) << 8)", "    unsigned bx = ((((y >> 1) - 10) & 0x7F) << 8)")
m("walk.c", "edge: object row not a byte", "    unsigned bx = (((((y >> 1) - 10) & 0xFF) << 7)", "    unsigned bx = (((((y >> 1) - 10) & 0x7F) << 7)")
m("walk.c", "edge: object column not a byte", "+ ((((x & 0xFF) - 0x16) & 0xFF) >> 1)) & 0xFFFF;", "+ ((((x & 0xFF) - 0x16) & 0x1FF) >> 1)) & 0xFFFF;")
m("walk.c", "edge: no objects past the map", ": walk_seg_byte(s, off);", ": 0xFF;")
m("walk.c", "edge: tiles not wrapped", "            if (t + 64 <= 0xFFF2 - 0x7F96) {", "            if (1) {")
m("walk.c", "edge: sprites not in the art segment", "        return off - 0x16 < s->sprites_len ? s->sprites[off - 0x16] : 0;", "        return 0;")
m("walk.c", "edge: zoom's window not kept", "    w->scene->view_head = w->zoom_grab;", "    (void)0;")
m("walkscene.c", "edge: segment head zero", "    if (off < 0x16)\n        return seg_head[off];", "    if (off < 0x16)\n        return 0;")
m("walkscene.c", "edge: no bank below the map", "        return s->bank1 ? s->bank1[off - 0x6416] : 0;", "        return 0;")
m("walkscene.c", "edge: no NUMBER bytes", "        return s->number_tail[off - 0xC500];", "        return 0;")
m("walkscene.c", "edge: no oval in the segment", "        return s->dark_mask && off - 0xCA86 < s->dark_mask_len ? s->dark_mask[off - 0xCA86] : 0;", "        return 0;")
m("walkscene.c", "edge: next head zero", "        return seg_next[off - 0xFB86];", "        return 0;")
m("walkscene.c", "edge: no map buffer", "    return s->view_head ? s->view_head[off - 0xFBA6] : 0;", "    return 0;")
m("walkscene.c", "edge: raw block not kept", "    s->view_head = s->raw_head;", "    s->view_head = NULL;")


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
            # the world's own unit tests (tests/test_walk.c) pin parts of the resumed pass; tests/test_audio.c the sounds' decoders and
            # lengths
            unit = {"walk.c": "test_walk", "sfx.c": "test_audio", "voc.c": "test_audio"}.get(f)
            if unit:
                core = re.search(r"^CORE_SRC\s*:=\s*(.*)$", (ROOT / "Makefile").read_text(), re.M).group(1).split()
                opl = sorted(str(p) for p in (ROOT / "build/opl").glob("*.o"))
                r = subprocess.run(["cc", "-std=c99", "-O1", "-w", "-I", str(src)] + [str(WORK / s) for s in core] +
                                   [str(ROOT / f"tests/{unit}.c")] + opl + ["-o", str(WORK / unit), "-lc++" if sys.platform == "darwin" else "-lstdc++"],
                                   capture_output=True, text=True)
                if r.returncode != 0:
                    print(f"DOES NOT BUILD: {name}: {r.stderr.strip()[:200]}")
                    bad += 1
                    continue
                if subprocess.run([str(WORK / unit), str(ROOT / "original_bin")], capture_output=True).returncode != 0:
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
