#!/usr/bin/env python3
"""Negative controls for the C port of WALK.SL's first screen (src/walk.c, src/walkscene.c).

Applies one deliberate bug at a time to a copy of a source file, builds the unit tests and build/walk_dump from it, and
checks that at least one of three things notices:
  unit     tests/test_walk.c fails
  screen   the first screen differs from the original's (work/check_walk/oracle_first.raw)
  idle     the port's idle passes differ from the original's (work/check_walk/idle_*.pkl and walk_*.pkl, tools/walk_idle.py cross, all passes, state included)
  loader   the loader's state differs from the Python model (tools/walk_scene.py cross)

  tools/check_walk.sh                       first, to make work/check_walk/oracle_first.raw
  work/venv/bin/python tools/mutate_walk.py [N]     N = randomised state files for the loader check (default 100)

A mutation nothing notices is printed as MISSED. Changes that no input can show are not in the list (see the note at the
end of the list).
"""
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WORK = ROOT / "work" / "mut_walk"
SOURCES = ["src/walk.c", "src/walkscene.c", "src/assets.c", "src/lcz.c", "src/screen.c"]
PY = sys.executable

M = []   # (file, name, old, new)


def m(f, name, old, new):
    M.append((f, name, old, new))


# ---- drawing
m("walk.c", "tile number shift", "unsigned off = (word << 6) & 0xFFFF;", "unsigned off = (word << 5) & 0xFFFF;")
m("walk.c", "tile number not masked to 16 bits", "unsigned off = (word << 6) & 0xFFFF;", "unsigned off = (word << 6);")
m("walk.c", "ground map x uses the odd bit", "* 256 + (int)(x & 0xFE) - 0x16;", "* 256 + (int)(x & 0xFF) - 0x16;")
m("walk.c", "ground map row", "int bx = ((int)(y >> 1) - 10) * 256", "int bx = ((int)(y >> 1) - 11) * 256")
GROUND_DX = ("int dx = 0xE10 - ((x & 1) ? 4 : 0) - ((y & 1) ? 0x380 : 0);\n    for (int row = 0; row < 23; row++) {\n"
             "        int b = bx, d = dx;\n        for (int col = 0; col < 25; col++) {\n            unsigned word")
m("walk.c", "ground odd x shift", GROUND_DX, GROUND_DX.replace("? 4 : 0", "? 3 : 0"))
m("walk.c", "ground odd y shift", GROUND_DX, GROUND_DX.replace("0x380", "0x300"))
m("walk.c", "ground columns", "for (int col = 0; col < 25; col++) {\n            unsigned word",
  "for (int col = 0; col < 24; col++) {\n            unsigned word")
m("walk.c", "object map x", "half_floor((int)(x & 0xFF) - 0x16)", "half_floor((int)(x & 0xFF) - 0x15)")
m("walk.c", "object map row", "int bx = ((int)(y >> 1) - 10) * 128", "int bx = ((int)(y >> 1) - 9) * 128")
m("walk.c", "object mask", "*p = (uint8_t)((*p & spr[0x40 + k * 8 + j]) | spr[k * 8 + j]);",
  "*p = (uint8_t)((*p & spr[k * 8 + j]) | spr[0x40 + k * 8 + j]);")
m("walk.c", "object sprite size", "s->sprites + (id << 7)", "s->sprites + (id << 6)")
m("walk.c", "object negative cell floor", "return v >= 0 ? v / 2 : -((-v + 1) / 2);", "return v / 2;")
m("walk.c", "draw order passes", "for (int pass = 0; pass < 24; pass++)", "for (int pass = 0; pass < 23; pass++)")
m("walk.c", "draw order compares", "for (int j = 0; j < 24; j++)\n            if (ys[j]", "for (int j = 0; j < 23; j++)\n            if (ys[j]")
m("walk.c", "draw order is not stable", "if (ys[j] > ys[j + 1]) {", "if (ys[j] >= ys[j + 1]) {")
m("walk.c", "draw order uses villagers' x", "ys[i] = s->tab_a[i * WS_VILLAGER_SIZE + 1];", "ys[i] = s->tab_a[i * WS_VILLAGER_SIZE];")
m("walk.c", "villager box left edge", "if (dl < bl)\n            return WALK_HIDDEN;", "if (dl <= bl)\n            return WALK_HIDDEN;")
m("walk.c", "villager box right edge", "dl > ((bl + 0x34) & 0xFF)", "dl > ((bl + 0x35) & 0xFF)")
m("walk.c", "villager box right edge limit", "if (bl <= 0xCB &&", "if (bl <= 0xCC &&")
m("walk.c", "villager box top", "if (dh < bh)\n            return WALK_HIDDEN;", "if (dh <= bh)\n            return WALK_HIDDEN;")
m("walk.c", "villager box bottom", "if (dh > ((bh + 0x2E) & 0xFF))", "if (dh > ((bh + 0x2F) & 0xFF))")
m("walk.c", "screen position row", "return (row * 0x380 + al) & 0xFFFF;", "return (row * 0x381 + al) & 0xFFFF;")
m("walk.c", "screen position column", "unsigned al = ((dl - bl) << 2) & 0xFF;", "unsigned al = ((dl - bl) << 3) & 0xFF;")
m("walk.c", "screen position offset +4", "unsigned dl = (px + 4) & 0xFF, dh = (py + 4) & 0xFF;",
  "unsigned dl = (px + 3) & 0xFF, dh = (py + 4) & 0xFF;")
m("walk.c", "hidden when x is 0", "px == 0 || py == 0", "py == 0")
m("walk.c", "villager sprite set", "unsigned off = ((vis - 1) * 0x800) & 0xFFFF;", "unsigned off = ((vis - 1) * 0x400) & 0xFFFF;")
m("walk.c", "villager direction", "off = (off + d * 0x200) & 0xFFFF;", "off = (off + d * 0x100) & 0xFFFF;")
m("walk.c", "villager direction above 3", "unsigned d = direction > 3 ? direction - 4 : direction;", "unsigned d = direction;")
m("walk.c", "villager frame", "off = (off + 0x100) & 0xFFFF;\n    return off;", "off = (off + 0x200) & 0xFFFF;\n    return off;")
m("walk.c", "villager bank switch", "src = off < 0x2800 ?", "src = off < 0x2000 ?")
m("walk.c", "villager bank 2 base", "((off - 0x9470) & 0xFFFF)", "((off - 0x9400) & 0xFFFF)")
m("walk.c", "party sprite number", "number * 0xC00", "number * 0xB00")
m("walk.c", "party sprite direction", "direction * 0x300", "direction * 0x200")
m("walk.c", "party walk counter 3", "unsigned f = frame + 1 < 4 ? frame : 1;", "unsigned f = frame;")
m("walk.c", "party not in party limit", "if (r[3] >= 0x64)", "if (r[3] > 0x64)")
m("walk.c", "party no box", "pos = walk_screen_pos(r[0], r[1], x, y, 1, 1);", "pos = walk_screen_pos(r[0], r[1], x, y, 0, 1);")
m("walk.c", "entity transparency", "if (src[r * 16 + c] && p <", "if (p <")
m("walk.c", "rain generator constant", "state = state * 0x343FDu + 0x269EC3u;", "state = state * 0x343FDu + 0x269EC4u;")
m("walk.c", "rain seed", "uint32_t state = 0;   /* WALK.SL calls srand(0)", "uint32_t state = 1;   /* WALK.SL calls srand(0)")
m("walk.c", "rain short drops", "int is_short = i < 40;", "int is_short = i < 39;")
m("walk.c", "rain drop step", "drops[i] + row * 0xE1", "drops[i] + row * 0xE0")
m("walk.c", "rain short stride", "is_short ? stride >> 1 : stride", "is_short ? stride >> 2 : stride")
m("walk.c", "rain wrap", "p >= 0xA100 ? p - 0xA100 : p", "p >= 0xA100 ? p - 0xA000 : p")
m("walk.c", "rain colours", "{0xAE, 0xAD, 0xAC}", "{0xAE, 0xAD, 0xAB}")
m("walk.c", "rain list not filled", "        w->drops[i] = (uint16_t)walk_rand(&w->rng);", "        w->drops[i] = 0;")
m("walk.c", "window source", "memcpy(scr->pix + (16 + r) * SCREEN_W + 64, buf + shift + (16 + r) * WALK_W + 16, 192);",
  "memcpy(scr->pix + (16 + r) * SCREEN_W + 64, buf + shift + (16 + r) * WALK_W + 17, 192);")
m("walk.c", "window rows", "for (int r = 0; r < 168; r++)\n        memcpy", "for (int r = 0; r < 167; r++)\n        memcpy")
# ---- interface
m("walk.c", "glyph offset base", "return 0x3B20 + 30 *", "return 0x3B21 + 30 *")
m("walk.c", "glyph offset trail byte", "lo <= 0x7E ? lo - 0x40 : lo - 0x62", "lo <= 0x7E ? lo - 0x40 : lo - 0x61")
m("walk.c", "glyph bit order", "if (v >> (15 - x) & 1)", "if (v >> x & 1)")
m("walk.c", "label top band", "uint8_t colour = y < 5 ? 0x89 : y < 10 ? 0x8A : 0x88;", "uint8_t colour = y < 6 ? 0x89 : y < 10 ? 0x8A : 0x88;")
m("walk.c", "label middle band", "uint8_t colour = y < 5 ? 0x89 : y < 10 ? 0x8A : 0x88;", "uint8_t colour = y < 5 ? 0x89 : y < 11 ? 0x8A : 0x88;")
m("walk.c", "label recolour end", "{240, 390, 0x8A}", "{240, 389, 0x8A}")
m("walk.c", "label recolour end 2", "{240, 390, 0x8A}", "{240, 391, 0x8A}")
m("walk.c", "label recolour 3 start", "{360, 510, 0x8B}", "{391, 510, 0x8B}")
m("walk.c", "label recolour colour", "{360, 510, 0x8B}", "{360, 510, 0x8C}")
m("walk.c", "single character recoloured", "if (chars == 2) {", "if (chars >= 1) {")
m("walk.c", "label shadow colour", "sprites[i], widths[i], 14, 0, 0x8E);", "sprites[i], widths[i], 14, 0, 0x8D);")
m("walk.c", "label shadow 2 colour", "sprites[i], widths[i], 14, 0, 0x8F);", "sprites[i], widths[i], 14, 0, 0x8E);")
m("walk.c", "label shadow offset", "at[i].y + 2, sprites[i]", "at[i].y + 3, sprites[i]")
m("walk.c", "label position left column", "{17, 24}", "{17, 25}")
m("walk.c", "label position right column", "{282, 100}", "{283, 100}")
m("walk.c", "yes label position", "{273, 40}", "{273, 41}")
m("walk.c", "no label code", "{0xAC, 0x4F, 0xA7, 0x5F}", "{0xAC, 0x4F, 0xA7, 0x5E}")
m("walk.c", "label text offset", 'assets_read_at("MES_TXT.LCC", 0x386,', 'assets_read_at("MES_TXT.LCC", 0x387,')
m("walk.c", "bottom right box width", "memset(scr->pix + (185 + r) * SCREEN_W + 273, 0, 40);", "memset(scr->pix + (185 + r) * SCREEN_W + 273, 0, 39);")
m("walk.c", "bottom right box start", "memset(scr->pix + (185 + r) * SCREEN_W + 273, 0, 40);", "memset(scr->pix + (185 + r) * SCREEN_W + 274, 0, 40);")
m("walk.c", "bottom right piece offset", 'assets_read_at("RPG.LCP", 0x9B8,', 'assets_read_at("RPG.LCP", 0x9B9,')
m("walk.c", "bottom right piece position", "SCREEN_W + 289, piece + r * 7, 7);", "SCREEN_W + 290, piece + r * 7, 7);")
m("walk.c", "hero walk counter of the first frame", "    w->hero_frame = 0;\n    w->ctr = 0;", "    w->hero_frame = 1;\n    w->ctr = 0;")
# ---- the world in motion (checked by the unit tests and by the idle comparison with the oracle's traces)
m("walk.c", "rand multiplier", "*state = *state * 0x343FDu + 0x269EC3u;", "*state = *state * 0x343FEu + 0x269EC3u;")
m("walk.c", "rand mask", "return (*state >> 16) & 0x7FFF;", "return (*state >> 16) & 0xFFFF;")
m("walk.c", "specks offset", "(walk_rand(&w->rng) + 0xE10) & 0xFFFF", "(walk_rand(&w->rng) + 0xE11) & 0xFFFF")
m("walk.c", "specks are not distinct", "if (w->specks[j] == v)", "if (w->specks[j] == v + 1)")
m("walk.c", "specks count", "for (int i = 0; i < WALK_SPECKS; i++) {   /* distinct", "for (int i = 0; i < WALK_SPECKS - 1; i++) {   /* distinct")
m("walk.c", "rain list size", "for (int i = 0; i < WALK_RAIN_DROPS; i++)\n        w->drops[i] = (uint16_t)walk_rand", "for (int i = 0; i < WALK_RAIN_DROPS - 1; i++)\n        w->drops[i] = (uint16_t)walk_rand")
m("walk.c", "stride dry from", "|| t >= 0x5000)", "|| t > 0x5000)")
m("walk.c", "stride dry from 2", "|| t >= 0x5000)", "|| t >= 0x4C00)")
m("walk.c", "stride peak", "    if (n > 10)\n        n = 20 - n;", "    if (n > 11)\n        n = 20 - n;")
m("walk.c", "stride fall", "        n = 20 - n;", "        n = 21 - n;")
m("walk.c", "stride step", "return (0xE1 * n + 0x384) & 0xFFFF;", "return (0xE0 * n + 0x384) & 0xFFFF;")
m("walk.c", "stride base", "return (0xE1 * n + 0x384) & 0xFFFF;", "return (0xE1 * n + 0x380) & 0xFFFF;")
m("walk.c", "stride clock is 16 bits", "unsigned t = ticks & 0xFFFF;\n    if ((weather", "unsigned t = ticks;\n    if ((weather")
m("walk.c", "ground animation counter", "unsigned counter = t[3] + 1u;\n        if (counter <= t[2]) {", "unsigned counter = t[3] + 1u;\n        if (counter < t[2]) {")
m("walk.c", "ground animation first tile", "cell = cell == w16(t, 6) ? w16(t, 4) : (cell + 1) & 0xFFFF;", "cell = cell == w16(t, 6) ? w16(t, 4) + 1 : (cell + 1) & 0xFFFF;")
m("walk.c", "ground animation last tile", "cell = cell == w16(t, 6) ? w16(t, 4)", "cell = cell >= w16(t, 6) ? w16(t, 4)")
m("walk.c", "ground animation entry size", "uint8_t *t = table + e * 8;", "uint8_t *t = table + e * 6;")
m("walk.c", "ground animation list end", "        unsigned bx = w16(t, 0);\n        if (bx == 0)\n            return;\n        unsigned counter = t[3] + 1u;",
  "        unsigned bx = w16(t, 0);\n        if (bx == 0)\n            continue;\n        unsigned counter = t[3] + 1u;")
m("walk.c", "object animation exact hit", "if (counter != t[2]) {", "if (counter < t[2]) {")
m("walk.c", "object animation wrap", "omap[bx] = (uint8_t)(v > t[5] ? t[4] : v);", "omap[bx] = (uint8_t)(v >= t[5] ? t[4] : v);")
m("walk.c", "object animation entry size", "uint8_t *t = table + e * 6;", "uint8_t *t = table + e * 8;")
m("walk.c", "palette group 1 size", "walk_pal_rotate(w->palbuf, 1, 3, 1)", "walk_pal_rotate(w->palbuf, 1, 2, 1)")
m("walk.c", "palette group 2 start", "walk_pal_rotate(w->palbuf, 4, 3, 1)", "walk_pal_rotate(w->palbuf, 5, 3, 1)")
m("walk.c", "palette group 3 direction", "walk_pal_rotate(w->palbuf, 7, 4, 0)", "walk_pal_rotate(w->palbuf, 7, 4, 1)")
m("walk.c", "palette group 4 size", "walk_pal_rotate(w->palbuf, 11, 4, 0)", "walk_pal_rotate(w->palbuf, 11, 3, 0)")
m("walk.c", "palette limit 0x533", "static const int limit[5] = {3, 10, 6, 12, 8};", "static const int limit[5] = {3, 11, 6, 12, 8};")
m("walk.c", "palette limit 0x534", "static const int limit[5] = {3, 10, 6, 12, 8};", "static const int limit[5] = {3, 10, 7, 12, 8};")
m("walk.c", "palette limit 0x535", "static const int limit[5] = {3, 10, 6, 12, 8};", "static const int limit[5] = {3, 10, 6, 11, 8};")
m("walk.c", "palette limit 0x536", "static const int limit[5] = {3, 10, 6, 12, 8};", "static const int limit[5] = {3, 10, 6, 12, 9};")
m("walk.c", "palette limit 0x532", "static const int limit[5] = {3, 10, 6, 12, 8};", "static const int limit[5] = {2, 10, 6, 12, 8};")
m("walk.c", "palette DAC range", "for (int i = 0; i < 16; i++)   /* 0x2c85e", "for (int i = 0; i < 15; i++)   /* 0x2c85e")
m("walk.c", "palette DAC mask", "scr->pal[i][ch] = w->palbuf[3 * i + ch] & 0x3F;", "scr->pal[i][ch] = w->palbuf[3 * i + ch];")
m("walk.c", "ramp step", "green = (green + 7) & 0xFF;", "green = (green + 6) & 0xFF;")
m("walk.c", "ramp turn", "if (green == 0x3F)\n            red = 0;", "if (green >= 0x3F)\n            red = 0;")
m("walk.c", "ramp down", "green = (green - 7) & 0xFF;", "green = (green - 6) & 0xFF;")
m("walk.c", "ramp bottom", "if (green <= 1)\n            red = 1;", "if (green <= 8)\n            red = 1;")
m("walk.c", "specks darken", "buf[p] = (uint8_t)(buf[p] - 2);", "buf[p] = (uint8_t)(buf[p] - 1);")
m("walk.c", "specks first half", "        if (i < 150) {", "        if (i < 149) {")
m("walk.c", "specks step", "            p += 0xDF;", "            p += 0xE0;")
m("walk.c", "specks wrap 1", "if (p >= 0xA0F0)\n                p -= 0x92E0;", "if (p > 0xA0F0)\n                p -= 0x92E0;")
m("walk.c", "specks wrap 1 amount", "p -= 0x92E0;", "p -= 0x92E1;")
m("walk.c", "specks wrap 2 amount", "p += 0xA0F0;\n        }", "p += 0xA0F1;\n        }")
m("walk.c", "specks other way", "            p -= 0xDF;", "            p -= 0xE0;")
m("walk.c", "villager hidden bank", "if (rec[0xB] == 0 || rec[0xB] >= 6)\n        return -1;", "if (rec[0xB] == 0 || rec[0xB] >= 7)\n        return -1;")
m("walk.c", "villager frame flip", "if (rec[0xA] == 2)\n        rec[0xA] = 0;", "if (rec[0xA] == 3)\n        rec[0xA] = 0;")
m("walk.c", "villager up limit", "case 0: if (y > rec[4]) ny--; break;", "case 0: if (y >= rec[4]) ny--; break;")
m("walk.c", "villager down limit", "case 1: if (y < rec[6]) ny++; break;", "case 1: if (y <= rec[6]) ny++; break;")
m("walk.c", "villager left limit", "case 2: if (x > rec[3]) nx--; break;", "case 2: if (x >= rec[3]) nx--; break;")
m("walk.c", "villager right limit", "case 3: if (x < rec[5]) nx++; break;", "case 3: if (x <= rec[5]) nx++; break;")
m("walk.c", "villager limits swapped", "case 0: if (y > rec[4]) ny--; break;", "case 0: if (y > rec[3]) ny--; break;")
m("walk.c", "villager tile row", "unsigned idx = (((ny >> 1) + 1) << 8) | (nx & 0xFE);", "unsigned idx = ((ny >> 1) << 8) | (nx & 0xFE);")
m("walk.c", "villager tile column", "unsigned idx = (((ny >> 1) + 1) << 8) | (nx & 0xFE);", "unsigned idx = (((ny >> 1) + 1) << 8) | (nx & 0xFF);")
m("walk.c", "villager second cell", "tile_listed(gmap, idx, list) && tile_listed(gmap, idx + 2, list);", "tile_listed(gmap, idx, list) && tile_listed(gmap, idx + 4, list);")
m("walk.c", "villager odd x cell", "ok = tile_listed(gmap, idx + 4, list);\n    if (ok && (ny & 1)) {", "ok = tile_listed(gmap, idx + 6, list);\n    if (ok && (ny & 1)) {")
m("walk.c", "villager odd y cells", "ok = tile_listed(gmap, idx + 0x100, list) && tile_listed(gmap, idx + 0x102, list);", "ok = tile_listed(gmap, idx + 0x100, list) && tile_listed(gmap, idx + 0x104, list);")
m("walk.c", "villager odd x and y cell", "ok = tile_listed(gmap, idx + 0x104, list);", "ok = tile_listed(gmap, idx + 0x106, list);")
m("walk.c", "villager list length", "for (int j = 0; j < 99; j++)\n        if (w16(list", "for (int j = 0; j < 100; j++)\n        if (w16(list")
m("walk.c", "villager counter wrap", "if (rec[7] > 0xA)\n        rec[7] = 0;", "if (rec[7] >= 0xA)\n        rec[7] = 0;")
m("walk.c", "villager 0x61 timeout", "if (rec[0xA] >= 0x14) {", "if (rec[0xA] > 0x14) {")
m("walk.c", "villager 0x61 direction", "rec[2] = 1;\n            rec[0xA] = 1;", "rec[2] = 0;\n            rec[0xA] = 1;")
m("walk.c", "villager step gate", "    rec[9]++;\n    if (rec[9] != rec[8])", "    rec[9]++;\n    if (rec[9] < rec[8])")
m("walk.c", "decision clock bits", "rec[7] = (uint8_t)(ticks & 7);", "rec[7] = (uint8_t)(ticks & 3);")
m("walk.c", "decision x limit", "if (r < 0x2AAA)", "if (r < 0x2000)")
m("walk.c", "decision x direction", "d = rec[0] > hero_x ? 2 : 3;", "d = rec[0] > hero_x ? 3 : 2;")
m("walk.c", "decision y limit", "if (r > 0x5554)", "if (r > 0x5000)")
m("walk.c", "decision y direction", "d = rec[1] > hero_y ? 0 : 1;", "d = rec[1] > hero_y ? 1 : 0;")
m("walk.c", "decision random in rain", "if (d == 5 || stride != 0 || rec[0x31] == 0) {", "if (d == 5 || rec[0x31] == 0) {")
m("walk.c", "decision random without byte 0x31", "if (d == 5 || stride != 0 || rec[0x31] == 0) {", "if (d == 5 || stride != 0) {")
m("walk.c", "decision random range", "d = walk_rand(rng) / 0x1000;", "d = walk_rand(rng) / 0x2000;")
m("walk.c", "decision uses the hero's x", "walk_villager_decide(rec, &w->rng, ticks, s->tab_b[0], s->tab_b[1], stride);", "walk_villager_decide(rec, &w->rng, ticks, s->tab_b[1], s->tab_b[0], stride);")
m("walk.c", "hero counter limit", "if (++w->ctr > (unsigned)s->speed_class + w->v2e) {", "if (++w->ctr > (unsigned)s->speed_class + w->v2e + 1) {")
m("walk.c", "hero frame count", "w->hero_frame = (w->hero_frame + 1) & 3;", "w->hero_frame = (w->hero_frame + 1) & 1;")
m("walk.c", "animation before drawing", "    walk_animate_ground(s->block_x, s->gmap);\n    walk_animate_objects(s->block_y, s->omap);\n    palette_cycle(w, scr);\n",
  "    palette_cycle(w, scr);\n")
m("walk.c", "specks clock", "(ticks & 0xFFFF) > 0xA000", "(ticks & 0xFFFF) > 0x9000")
m("walk.c", "specks only in rain weather", "int rainy = s->weather == 1 || s->weather == 2;", "int rainy = s->weather == 2;")
m("walk.c", "rain is drawn when dry", "if (stride && s->weather == 1)", "if (s->weather == 1)")
m("walk.c", "villager loop skipped", "    walk_copy_window_at(scr, w->buf, shake(w));\n\n    for (int k = 0; k < WS_VILLAGERS; k++) {",
  "    walk_copy_window_at(scr, w->buf, shake(w));\n    for (int k = 0; k < 0; k++) {")
m("walk.c", "villager step reads the object map", "walk_villager_step(rec, s->gmap, s->list);", "walk_villager_step(rec, s->omap, s->list);")
m("walk.c", "dry weather specks list", "walk_draw_specks(w->buf, WALK_BUF_LEN, w->specks);", "walk_draw_specks(w->buf, WALK_BUF_LEN, w->drops);")
m("walk.c", "palette buffer source", 'assets_read_at("RPG.LCP", 0, rpg, sizeof rpg)', 'assets_read_at("RPG.LCP", 3, rpg, sizeof rpg)')
# ---- the hero walking (unit tests, and the idle / walking comparison with the oracle's traces)
m("walk.c", "foot: row", "unsigned idx = ((((y >> 1) + 1) & 0xFF) << 8) | (x & 0xFE);", "unsigned idx = ((((y >> 1) + 0) & 0xFF) << 8) | (x & 0xFE);")
m("walk.c", "foot: column", "| (x & 0xFE);\n#define LISTED", "| (x & 0xFF);\n#define LISTED")
m("walk.c", "foot: second cell", "if (!LISTED(idx) || !LISTED(idx + 2))", "if (!LISTED(idx) || !LISTED(idx + 4))")
m("walk.c", "foot: first cell", "if (!LISTED(idx) || !LISTED(idx + 2))", "if (!LISTED(idx + 2))")
m("walk.c", "foot: third cell for odd x", "if ((x & 1) && !LISTED(idx + 4))\n        return 0;\n    if (y & 1) {", "if ((x & 0) && !LISTED(idx + 4))\n        return 0;\n    if (y & 1) {")
m("walk.c", "foot: odd y rows", "if (!LISTED(idx + 0x100) || !LISTED(idx + 0x102))", "if (!LISTED(idx + 0x100) || !LISTED(idx + 0x104))")
m("walk.c", "foot: odd y first row cell", "if (!LISTED(idx + 0x100) || !LISTED(idx + 0x102))", "if (!LISTED(idx + 0x102))")
m("walk.c", "foot: corner cell", "if ((x & 1) && !LISTED(idx + 0x104))", "if ((x & 1) && !LISTED(idx + 0x106))")
m("walk.c", "foot: corner needs odd x", "if ((x & 1) && !LISTED(idx + 0x104))", "if (!LISTED(idx + 0x104))")
m("walk.c", "foot: off the map", "#define LISTED(i) ((i) + 2 <= WS_GMAP_BYTES && tile_listed", "#define LISTED(i) (1 || tile_listed")
m("walk.c", "trail: shift length", "memmove(trail + 3, trail, 12 * 3);", "memmove(trail + 3, trail, 11 * 3);")
m("walk.c", "trail: shift distance", "memmove(trail + 3, trail, 12 * 3);", "memmove(trail + 6, trail, 11 * 3);")
m("walk.c", "trail: new entry x", "trail[0] = (uint8_t)x;", "trail[0] = (uint8_t)(x + 1);")
m("walk.c", "trail: new entry direction", "trail[2] = party[2];", "trail[2] = 0;")
m("walk.c", "trail: hero record", "party[1] = (uint8_t)y;\n    static const int entry", "party[1] = (uint8_t)(y + 1);\n    static const int entry")
m("walk.c", "trail: followers' entries", "static const int entry[4] = {3, 6, 9, 12};", "static const int entry[4] = {3, 6, 9, 11};")
m("walk.c", "trail: followers' entries 2", "static const int entry[4] = {3, 6, 9, 12};", "static const int entry[4] = {2, 6, 9, 12};")
m("walk.c", "trail: follower record size", "memcpy(party + WS_PARTY_SIZE * i, trail + 3 * entry[i - 1], 3);", "memcpy(party + 39 * i, trail + 3 * entry[i - 1], 3);")
m("walk.c", "record: x", "if (r[0] == (x & 0xFE) && r[1] == (y >> 1)) {", "if (r[0] == (x & 0xFF) && r[1] == (y >> 1)) {")
m("walk.c", "record: y", "if (r[0] == (x & 0xFE) && r[1] == (y >> 1)) {", "if (r[0] == (x & 0xFE) && r[1] == (y & 0xFE)) {")
m("walk.c", "record: kind 5 odd", "if (r[2] == 5 && ((x & 1) || (y & 1)))", "if (r[2] == 5 && (x & 1))")
m("walk.c", "record: stride", "bx += WS_RECORD_SIZE) {\n        const uint8_t *r = tab_a + 1000 + bx;", "bx += WS_RECORD_SIZE + 1) {\n        const uint8_t *r = tab_a + 1000 + bx;")
m("walk.c", "record: skipped when stepped on", "    if (w->v2e == 1) {   /* the hero took a step", "    if (w->v2e == 7) {   /* the hero took a step")
m("walk.c", "record: v2e after the check", "        w->v2e = 5;\n        unsigned si = walk_find_record", "        w->v2e = 1;\n        unsigned si = walk_find_record")
m("walk.c", "record: pass goes on", "            return w->unbuilt_new ? WALK_PASS_UNBUILT : 0;   /* the rest", "            /* the rest")
m("walk.c", "record: no re-entry", "    if (walk_scene_apply_records(s) != 0)\n        note_unbuilt(w, \"the record loop failed\"", "    if (0)\n        note_unbuilt(w, \"the record loop failed\"")
m("walk.c", "record: kinds that are reported", "kind == 7 || kind == 8", "kind == 6 || kind == 8")
m("walk.c", "roof: going in needs facing up", "if (d == 0 && rec[3] == 0 && w->alt[0] == 0) {", "if (d == 1 && rec[3] == 0 && w->alt[0] == 0) {")
m("walk.c", "roof: going in needs record byte 3", "if (d == 0 && rec[3] == 0 && w->alt[0] == 0) {", "if (d == 0 && w->alt[0] == 0) {")
m("walk.c", "roof: going in only once", "if (d == 0 && rec[3] == 0 && w->alt[0] == 0) {", "if (d == 0 && rec[3] == 0) {")
m("walk.c", "roof: coming out needs facing down", "if (d == 1 && rec[3] == 1 && w->alt[0] != 0) {", "if (d == 0 && rec[3] == 1 && w->alt[0] != 0) {")
m("walk.c", "roof: coming out needs record byte 3", "if (d == 1 && rec[3] == 1 && w->alt[0] != 0) {", "if (d == 1 && w->alt[0] != 0) {")
m("walk.c", "roof: coming out only after going in", "if (d == 1 && rec[3] == 1 && w->alt[0] != 0) {", "if (d == 1 && rec[3] == 1) {")
m("walk.c", "roof: flag set", "            w->alt[0] = 1;\n            s->weather += 3;", "            s->weather += 3;")
m("walk.c", "roof: flag cleared", "            w->alt[0] = 0;\n            w->v48 = 2;", "            w->v48 = 2;")
m("walk.c", "roof: weather going in", "s->weather += 3;", "s->weather += 2;")
m("walk.c", "roof: weather coming out", "s->weather -= 3;", "s->weather -= 2;")
m("walk.c", "roof: no bump coming out", "            w->alt[0] = 0;\n            w->v48 = 2;", "            w->alt[0] = 0;")
m("walk.c", "roof: bump going in", "            w->alt[0] = 1;\n            s->weather += 3;", "            w->alt[0] = 1;\n            w->v48 = 2;\n            s->weather += 3;")
m("walk.c", "roof: rectangle", "unsigned x0 = rec[4], y0 = rec[5], x1 = rec[6], y1 = rec[7];", "unsigned x0 = rec[4], y0 = rec[5], x1 = rec[6], y1 = rec[6];")
m("walk.c", "roof: rectangle x", "unsigned x0 = rec[4], y0 = rec[5], x1 = rec[6], y1 = rec[7];", "unsigned x0 = rec[5], y0 = rec[4], x1 = rec[6], y1 = rec[7];")
m("walk.c", "roof: width", "int wd = (int)x1 - (int)x0 + 1;", "int wd = (int)x1 - (int)x0;")
m("walk.c", "roof: empty rectangle", "    if (y1 < y0)\n        return 0;", "    if (y1 <= y0)\n        return 0;")
m("walk.c", "roof: width check", "if (wd <= 0 || x1 >= WS_MAP_W", "if (wd < 0 || x1 >= WS_MAP_W")
m("walk.c", "roof: right edge", "x1 >= WS_MAP_W || y1 >= 100", "x1 > WS_MAP_W || y1 >= 100")
m("walk.c", "roof: bottom edge", "y1 >= 100 ||", "y1 > 100 ||")
m("walk.c", "roof: row stride", "unsigned off = x0 + (y0 << 7);", "unsigned off = x0 + (y0 << 8);")
m("walk.c", "roof: next row", "        off += 0x80;", "        off += 0x7F;")
m("walk.c", "roof: saved after the flag", "    size_t idx = 1;", "    size_t idx = 0;")
m("walk.c", "roof: saved row by row", "        idx += (size_t)wd;", "        idx += 1;")
m("walk.c", "roof: saved width", "memcpy(alt + idx, omap + off, (size_t)wd);", "memcpy(alt + idx, omap + off, (size_t)wd - 1);")
m("walk.c", "roof: cleared to empty", "memset(omap + off, 0xFF, (size_t)wd);", "memset(omap + off, 0xFE, (size_t)wd);")
m("walk.c", "roof: cleared width", "memset(omap + off, 0xFF, (size_t)wd);", "memset(omap + off, 0xFF, (size_t)wd - 1);")
m("walk.c", "roof: put back", "memcpy(omap + off, alt + idx, (size_t)wd);", "memcpy(omap + off, alt + idx + 1, (size_t)wd);")
m("walk.c", "roof: reset clears the flag", "    memset(w->alt, 0, WALK_ALT_LEN);\n", "")
m("walk.c", "shake: size", "unsigned t = (v48 * 4) & 0xFF;", "unsigned t = (v48 * 8) & 0xFF;")
m("walk.c", "shake: divisor", "unsigned d = (0x7FFFu / t + 1) & 0xFFFF;", "unsigned d = (0x7FFFu / t) & 0xFFFF;")
m("walk.c", "shake: rows", "a = ((a - v48) * 0xE0) & 0xFFFF;", "a = ((a - v48) * 0xE1) & 0xFFFF;")
m("walk.c", "shake: threshold", "if (a > v48)\n        a = ((a", "if (a >= v48)\n        a = ((a")
m("walk.c", "shake: decay", "        w->v48--;\n    }\n    return a;", "    }\n    return a;")
m("walk.c", "shake: length of a bump", "w->v48 = 2;   /* the shake", "w->v48 = 3;   /* the shake")
m("walk.c", "shake: window offset", "walk_copy_window_at(scr, w->buf, shake(w));", "walk_copy_window_at(scr, w->buf, 0);")
m("walk.c", "keys: held arrow not released by another break code", "if (c2 == 0x4D && k != 0xCD)\n            k = c2;", "if (c2 == 0x4D && k != 0xCC)\n            k = c2;")
m("walk.c", "keys: break code of up", "if (c2 == 0x48 && k != 0xC8)", "if (c2 == 0x48 && k != 0xC9)")
m("walk.c", "keys: break code of down", "if (c2 == 0x50 && k != 0xD0)", "if (c2 == 0x50 && k != 0xCF)")
m("walk.c", "keys: break code of left", "if (c2 == 0x4B && k != 0xCB)", "if (c2 == 0x4B && k != 0xCA)")
m("walk.c", "keys: break range", "if (k > 0xC7 && k < 0xD1 && c2 > 0x47 && c2 < 0x51) {", "if (k > 0xC7 && k < 0xD1 && c2 > 0x47 && c2 < 0x50) {")
m("walk.c", "keys: release clears the last key", "    if (k > 0x7F)\n        c2 = 0;", "    if (k > 0x7F)\n        c2 = c2;")
m("walk.c", "keys: acted on every pass", "    if (w->v98 != 0 || k == 0)\n        return;", "    if (k == 0)\n        return;")
m("walk.c", "keys: counter wrap", "if (++w->v98 > (unsigned)s->speed_class)\n        w->v98 = 0;", "if (++w->v98 >= (unsigned)s->speed_class)\n        w->v98 = 0;")
m("walk.c", "keys: up direction", "party[2] = k == 0x48 ? 0 : k == 0x50 ? 1 : k == 0x4B ? 2 : 3;", "party[2] = k == 0x48 ? 1 : k == 0x50 ? 1 : k == 0x4B ? 2 : 3;")
m("walk.c", "keys: left direction", "party[2] = k == 0x48 ? 0 : k == 0x50 ? 1 : k == 0x4B ? 2 : 3;", "party[2] = k == 0x48 ? 0 : k == 0x50 ? 1 : k == 0x4B ? 3 : 3;")
m("walk.c", "keys: up step", "ny = (y - 1) & 0xFF;", "ny = (y - 2) & 0xFF;")
m("walk.c", "keys: down step", "ny = (y + 1) & 0xFF;", "ny = (y + 2) & 0xFF;")
m("walk.c", "keys: left step", "nx = (x - 1) & 0xFF;", "nx = (x - 2) & 0xFF;")
m("walk.c", "keys: right step", "nx = (x + 1) & 0xFF;", "nx = (x + 2) & 0xFF;")
m("walk.c", "keys: a step marks the pass", "    w->v2e = 1;\n    if (k == 0x48)", "    if (k == 0x48)")
m("walk.c", "keys: blocked marks", "        w->v2e = 5;\n        w->v48 = 2;", "        w->v48 = 2;")
m("walk.c", "keys: blocked step still goes", "if (walk_foot_ok(s->gmap, s->list, nx, ny)) {", "if (1) {")
m("walk.c", "keys: repeat of 1 2 A", "if ((k == 2 || k == 3 || k == 0x1E) && c2 == k)", "if ((k == 2 || k == 3) && c2 == k)")
m("walk.c", "keys: extended prefix", "    if (k == 0xE0)\n        k = c2;", "")
m("walk.c", "keys: unknown keys reported", "if (!arrow && k != 0) {", "if (!arrow && k != 0 && k != 0x39) {")
m("walk.c", "counter: threshold uses the step marker", "if (++w->ctr > (unsigned)s->speed_class + w->v2e) {", "if (++w->ctr > (unsigned)s->speed_class + 3) {")
m("walk.c", "counter: position read after the keys", "    unsigned x = s->tab_b[0], y = s->tab_b[1];\n    int event = 0;\n    w->unbuilt_new = 0;", "    unsigned x = s->tab_b[0], y = s->tab_b[1];\n    int event = 0;\n    w->unbuilt_new = 1;")

# ---- loader
m("walkscene.c", "tile set entry size", "size_t ent = 0x109 + 10 *", "size_t ent = 0x109 + 9 *")
m("walkscene.c", "tile length offset", "size_t len1 = w16(tab, ent + 3), len2 = w16(tab, ent + 8);",
  "size_t len1 = w16(tab, ent + 3), len2 = w16(tab, ent + 7);")
m("walkscene.c", "sprite bank number", "s->header[1] > 0x13 ? s->header[1] - 5u", "s->header[1] > 0x14 ? s->header[1] - 5u")
m("walkscene.c", "sprite bank number not reduced", "s->header[1] > 0x13 ? s->header[1] - 5u : s->header[1]", "s->header[1]")
m("walkscene.c", "object map start", "memcpy(s->omap, map + 25700, WS_OMAP_BYTES);", "memcpy(s->omap, map + 25701, WS_OMAP_BYTES);")
m("walkscene.c", "ground map planes", "s->gmap[2 * k] = map[100 + k];\n        s->gmap[2 * k + 1] = map[100 + 12800 + k];",
  "s->gmap[2 * k + 1] = map[100 + k];\n        s->gmap[2 * k] = map[100 + 12800 + k];")
m("walkscene.c", "list tail", "memcpy(s->list + 100, map + 38740, 100);", "memcpy(s->list + 100, map + 38742, 100);")
m("walkscene.c", "map header copied", "memcpy(s->header, raw, 6);", "memcpy(s->header, raw + 1, 6);")
m("walkscene.c", "scene number from 501", "memcpy(tb + 510, tb + 500, 4);", "memcpy(tb + 510, tb + 501, 4);")
m("walkscene.c", "state block start", "size_t blk = (size_t)s->scene * WS_STATE;", "size_t blk = (size_t)s->scene * (WS_STATE + 1);")
m("walkscene.c", "villager byte 8 copy", "ta[WS_STATE + i] = ta[8 + WS_VILLAGER_SIZE * i];", "ta[WS_STATE + i] = ta[9 + WS_VILLAGER_SIZE * i];")
m("walkscene.c", "map byte 1 into the table", "tb[518] = raw[1];", "tb[518] = raw[2];")
m("walkscene.c", "party start y", "y = (tb[513] << 1) & 0xFF;", "y = tb[513];")
m("walkscene.c", "party start x", "unsigned x = tb[512],", "unsigned x = tb[511],")
m("walkscene.c", "party records filled", "for (unsigned di = 0; di < 0xC8; di += 0x28) {\n        tb[di] = (uint8_t)x;",
  "for (unsigned di = 0; di < 0xA0; di += 0x28) {\n        tb[di] = (uint8_t)x;")
m("walkscene.c", "trail length", "for (int k = 0; k < WS_TRAIL_BYTES; k += 3) {", "for (int k = 0; k < WS_TRAIL_BYTES - 3; k += 3) {")
m("walkscene.c", "slow machine limit", "if (s->speed_class < 2) {", "if (s->speed_class < 1) {")
m("walkscene.c", "slow machine floor", "v[8] = v[8] > k ? (uint8_t)(v[8] - k) : 1;", "v[8] = v[8] > k ? (uint8_t)(v[8] - k) : 0;")
m("walkscene.c", "slow machine byte 9", "            v[9] = 0;\n", "")
m("walkscene.c", "kind 2 omap word", "put16(s->omap, sizeof s->omap, (yy << 7) + (xx >> 1), R(7) | (R(8) << 8));",
  "put8(s->omap, sizeof s->omap, (yy << 7) + (xx >> 1), R(7));")
m("walkscene.c", "kind 2 omap x", "(yy << 7) + (xx >> 1)", "(yy << 7) + xx")
m("walkscene.c", "kind 2 second word", "                put16(s->gmap, sizeof s->gmap, (yy << 8) + xx + 2, R(9) | (R(10) << 8));\n", "")
m("walkscene.c", "kind 2 r13", "if (kind == 0x02 && R(13) == 0) {", "if (kind == 0x02) {")
m("walkscene.c", "kind 2 r6", "            if (R(6) != 0) {\n                put16(s->omap", "            if (R(5) != 0) {\n                put16(s->omap")
m("walkscene.c", "kind 0xa second flag", "walk_flag_test(tb, R(13), R(14)) && W(9) != 0", "walk_flag_test(tb, R(11), R(12)) && W(9) != 0")
m("walkscene.c", "kind 0xa second value", "put16(s->gmap, sizeof s->gmap, W(9), W(11));", "put16(s->gmap, sizeof s->gmap, W(9), W(13));")
m("walkscene.c", "kind 0xc x y", "put8(s->omap, sizeof s->omap, (R(4) << 7) + R(3), R(5));", "put8(s->omap, sizeof s->omap, (R(3) << 7) + R(4), R(5));")
m("walkscene.c", "kind 0xc second entry", "walk_flag_test(tb, R(11), R(12)) && W(8) != 0", "0 && W(8) != 0")
m("walkscene.c", "kind 0xd target", "put16(ta, WS_TAB_A, W(5), W(7));", "put16(ta, WS_TAB_A, W(7), W(5));")
m("walkscene.c", "kind 0xb member limit", "if (tb[di + 3] > 10)", "if (tb[di + 3] > 11)")
m("walkscene.c", "kind 0xb dead member", "break;   /* a member who is not in the party ends the search (0x11430) */", "continue;")
m("walkscene.c", "kind 0xb item range start", "unsigned b = di + 0x14;", "unsigned b = di + 0x13;")
m("walkscene.c", "kind 0xb item range end", "b < di + 0x28; b++)", "b < di + 0x27; b++)")
m("walkscene.c", "kind 0xf limit", "if (v > 0x226)\n                        break;", "if (v >= 0x226)\n                        break;")
m("walkscene.c", "kind 0xf list limit", "if (wv == v || wv > 0x226)", "if (wv == v || wv > 0x225)")
m("walkscene.c", "kind 0xf flag", "if (kind == 0x0F) {\n            if (walk_flag_test(tb, R(3), R(4))) {",
  "if (kind == 0x0F) {\n            if (1) {")
m("walkscene.c", "kind 0x10 clears r5", "                s->events++;\n                ta[o + 5] = 0;", "                s->events++;")
m("walkscene.c", "flag bit order", "(tab_b[WS_FLAGS_AT + idx] >> (n - 1)) & 1 : 0;", "(tab_b[WS_FLAGS_AT + idx] >> (8 - n)) & 1 : 0;")
m("walkscene.c", "flag byte", "(tab_b[WS_FLAGS_AT + idx] >> (n - 1))", "(tab_b[WS_FLAGS_AT + idx + 1] >> (n - 1))")
m("walkscene.c", "flag count zero", "    if (n == 0)\n        return 1;", "    if (n == 0)\n        return 0;")
m("walkscene.c", "set flag ignores the bit", "|= (uint8_t)((1u << (bit & 31)) & 0xFF);", "|= 1;")
m("walkscene.c", "speed class byte", "s->speed_class = rp[0];", "s->speed_class = rp[1];")

# Not in the list because no input can show them: in label_sprite the first recolour range [120, 270) and the starts of the
# second and third ([240, ...), [360, ...) up to byte 389) only touch pixels that are not 0x88 (rows 0..9, or bytes already
# recoloured), and the third range's end (510) is beyond the sprite's 448 bytes. In the loader, kind 0x0b's `break` after a
# flag is set only saves work (setting the same flag again changes nothing).


def build(mut_file, text, out_test, out_dump):
    """Compile the unit test and walk_dump with `text` replacing src/<mut_file>. Returns an error string or None."""
    WORK.mkdir(parents=True, exist_ok=True)
    mutated = WORK / mut_file
    mutated.write_text(text)
    srcs = [str(mutated) if Path(s).name == mut_file else str(ROOT / s) for s in SOURCES]
    for out, main in ((out_test, "tests/test_walk.c"), (out_dump, "tests/walk_dump.c")):
        r = subprocess.run(["cc", "-std=c99", "-O1", "-w", "-I", str(ROOT / "src")] + srcs + [str(ROOT / main), "-o", str(out)],
                           capture_output=True, text=True, cwd=ROOT)
        if r.returncode != 0:
            return r.stderr.strip()[:300]
    return None


def run(cmd, limit):
    """Run a command; a time-out gives a result with return code -1 (a mutation that makes a check hang is noticed)."""
    try:
        return subprocess.run(cmd, capture_output=True, text=True, cwd=ROOT, timeout=limit)
    except subprocess.TimeoutExpired:
        return subprocess.CompletedProcess(cmd, -1, "", "timed out")


def main():
    n = sys.argv[1] if len(sys.argv) > 1 else "100"
    oracle = ROOT / "work/check_walk/oracle_first.raw"
    start = ROOT / "work/check_walk/start.raw"
    if not oracle.exists() or not start.exists():
        print("run tools/check_walk.sh first")
        return 2
    want = oracle.read_bytes()
    only = os.environ.get("ONLY", "")   # run only the mutations whose name contains this text
    traces = sorted((ROOT / "work/check_walk").glob("idle_*.pkl")) + sorted((ROOT / "work/check_walk").glob("walk_*.pkl"))
    if not traces:
        print("no idle traces in work/check_walk (tools/check_walk.sh makes them): the idle comparison is skipped")
    missed = bad = ran = 0
    try:
        for mut_file, name, old, new in M:
            if only and only not in name:
                continue
            ran += 1
            text = (ROOT / "src" / mut_file).read_text()
            if text.count(old) != 1:
                print(f"BAD PATTERN ({text.count(old)} matches): {name}")
                bad += 1
                continue
            t, d = WORK / "t", WORK / "d"
            err = build(mut_file, text.replace(old, new), t, d)
            if err:
                print(f"DOES NOT BUILD: {name}: {err}")
                bad += 1
                continue
            caught = None
            r = run([str(t), "original_bin"], 120)
            if r.returncode != 0:
                caught = "unit" if r.returncode > 0 else "hang"   # a hang (the time limit) counts as noticed
            if not caught:
                out = WORK / "first.raw"
                r = run([str(d), "original_bin", "first", str(out), str(start)], 120)
                if r.returncode != 0 or out.read_bytes() != want:
                    caught = "screen"
            if not caught:
                for tr in traces:
                    r = run([PY, "tools/walk_idle.py", "cross", str(d), str(tr), str(0)], 300)
                    if r.returncode != 0:
                        caught = "idle"
                        break
            if not caught:
                r = run([PY, "tools/walk_scene.py", "cross", str(d), n, "5"], 900)
                if r.returncode != 0:
                    caught = "loader"
            if caught:
                print(f"caught by {caught:6s}: {name}")
            else:
                print(f"MISSED: {name}")
                missed += 1
            sys.stdout.flush()
    finally:
        shutil.rmtree(WORK, ignore_errors=True)
    print(f"{ran - missed - bad} of {ran} caught, {missed} missed, {bad} bad")
    return 1 if missed or bad else 0


if __name__ == "__main__":
    sys.exit(main())
