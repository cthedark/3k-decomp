#!/usr/bin/env python3
"""WALK.SL's idle main loop (no key, no mouse), as a Python model, and its comparison with the original in the oracle.

One pass of the loop at 0x11d36 does, in this order (READ from the disassembly, CONFIRMED by `check`):
  1. 0x1c284: five counters (data segment 0x532-0x536) cycle the palette: entries 1-3 and 4-6 rotate forwards (every 11th and
     7th pass), entries 7-10 and 11-14 rotate backwards (every 13th and 9th pass), entry 15's green value grows by 7 (every
     4th pass); each time the DAC entries 1-16 are rewritten (0x2c85e).
  2. the picture: ground, entities, objects, rain (the existing models), then the window copy 0x2b9ec.
  3. 0x2bccd for each of the 20 villager records (the walking step), then, when it returns 10, the decision code at 0x134ba
     (new direction, using rand() and the BIOS clock).
  4. the hero's walk counter: [bp-0x3c] counts passes; above class + [bp-0x2e] it resets and the frame [bp-0x5e] goes 0..3.
With a keyboard port value per pass (`run_pass(screen, port60)`) it also models the arrow keys (findings.md "WALK.SL step 7"): the key logic
(0x135f0-0x13ad9), the four walking handlers with the walkable-tile test (0x2bf02), the bump shake, the party trail (0x2bfd5) and what the
loop does when the hero stands on a scene record (0x2c098 and the re-entry at 0x113c4). Not modelled, and reported in `self.unbuilt`:
other keys, the mouse, the sliding round corners (needs the setting at [bp-0x76], toggled by the A key), scene-record events other than
the music change a roof makes (0x1c68e), the battle a touched enemy starts (the run ends there), and the sound effects (0x1c68e).

Space fires the party's shots (findings.md "WALK.SL step 11"): the shot list, the sparks, the enemy hit test (0x2bb5e), the smoke and the
enemy coming back (0x13356), the coin count; touching an enemy ends the model's run where the original leaves the loop for a battle.

Usage: work/venv/bin/python tools/walk_idle.py check TRACE.pkl [NPASSES]
       work/venv/bin/python tools/walk_idle.py cross build/walk_dump TRACE.pkl [NPASSES]     (the C port)
       (TRACE.pkl from tools/walk_idle_trace.py; WALK_DATA=DIR for a trace recorded with --data DIR, e.g. tools/walk_scene_data.py)
"""
import pickle
import os
import sys
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import walk_entities as we  # noqa: E402
import walk_first_frame as wf  # noqa: E402
import walk_layers as wl  # noqa: E402
import walk_scene as wsc  # noqa: E402

DATA = Path("original_bin")
W = 224


def load_trace(path):
    """A trace written by tools/walk_idle_trace.py (a pickle, zlib-compressed; an uncompressed one is accepted too)."""
    raw = Path(path).read_bytes()
    return pickle.loads(raw if raw[:1] == b"\x80" else zlib.decompress(raw))


class Rand:
    """The game's rand(): state = state * 0x343FD + 0x269EC3 (32 bits), result = (state >> 16) & 0x7FFF."""

    def __init__(self, state=0):
        self.state = state & 0xFFFFFFFF

    def next(self):
        self.state = (self.state * 0x343FD + 0x269EC3) & 0xFFFFFFFF
        return (self.state >> 16) & 0x7FFF


# ---- the palette cycle (0x1c284) --------------------------------------------------------------------------------


def rot_forward(p, first, n):
    """0x2c71d: entries first .. first+n-1 move one place down, the first goes last (entry k gets entry k+1)."""
    e = [bytes(p[3 * (first + i):3 * (first + i) + 3]) for i in range(n)]
    e = e[1:] + e[:1]
    for i in range(n):
        p[3 * (first + i):3 * (first + i) + 3] = e[i]


def rot_backward(p, first, n):
    """0x2c795: entry k gets entry k-1, the last goes first."""
    e = [bytes(p[3 * (first + i):3 * (first + i) + 3]) for i in range(n)]
    e = e[-1:] + e[:-1]
    for i in range(n):
        p[3 * (first + i):3 * (first + i) + 3] = e[i]


def ramp15(p):
    """0x2c82c: entry 15 is bytes 45, 46, 47; if byte 45 == 1 the green byte gains 7 (and landing exactly on 0x3f turns byte 45
    to 0, which makes it fall), else it loses 7 (and at most 1 sets byte 45 back to 1).  The byte wraps at 256; the DAC keeps
    the low 6 bits.  (The `mov al, 0` after the compare with 0x3f is not shown by tools/calls16.py; the oracle shows it.)"""
    al, ah = p[45], p[46]
    if al == 1:
        ah = (ah + 7) & 0xFF
        if ah == 0x3F:
            al = 0
    else:
        ah = (ah - 7) & 0xFF
        if ah <= 1:
            al = 1
    p[45], p[46] = al, ah


class Palette:
    """Counters at ds:0x532-0x536 and the palette buffer.  `dac` is what the screen shows (6 bits per value)."""

    def __init__(self, buf):
        self.buf = bytearray(buf)
        self.dac = bytearray(v & 0x3F for v in buf)
        self.c = {"c532": 0, "c533": 0, "c534": 0, "c535": 0, "c536": 0}

    def write(self):
        for i in range(48):
            self.dac[i] = self.buf[i] & 0x3F      # DAC entries 0 .. 15 (0x2c85e writes 48 bytes from index 0, SEEN)

    def cycle(self):
        c = self.c
        c["c533"] += 1
        if c["c533"] > 10:
            c["c533"] = 0
            rot_forward(self.buf, 1, 3)
            self.write()
        c["c534"] += 1
        if c["c534"] > 6:
            c["c534"] = 0
            rot_forward(self.buf, 4, 3)
            self.write()
        c["c535"] += 1
        if c["c535"] > 12:
            c["c535"] = 0
            rot_backward(self.buf, 7, 4)
            self.write()
        c["c536"] += 1
        if c["c536"] > 8:
            c["c536"] = 0
            rot_backward(self.buf, 11, 4)
            self.write()
        c["c532"] += 1
        if c["c532"] > 3:
            c["c532"] = 0
            ramp15(self.buf)
            self.write()


# ---- the dark specks of the last part of the hour (0x2c2c2) -----------------------------------------------------------


def specks(buf, pos):
    """0x2c2c2: 300 positions in the world buffer.  The first 150 darken their pixel by 2 (colour index - 2, bytes wrap) and move
    down-left by 0xdf (a row less one pixel; at 0xa0f0 or more they wrap by -0x92e0); the second 150 darken it and move the other
    way by 0xdf (at 0xe10 or less they wrap by +0xa0f0)."""
    for i in range(150):
        p = pos[i]
        buf[p] = (buf[p] - 2) & 0xFF
        p += 0xDF
        if p >= 0xA0F0:
            p -= 0x92E0
        pos[i] = p
    for i in range(150, 300):
        p = pos[i]
        buf[p] = (buf[p] - 2) & 0xFF
        p -= 0xDF
        if p <= 0xE10:
            p += 0xA0F0
        pos[i] = p


# ---- animated tiles (0x2b898 for the ground, 0x2b99a for the objects) ----------------------------------------------------


def animate_ground(table, gmap):
    """0x2b898: up to 30 entries of 8 bytes: word offset into the ground map, period, counter, first tile, last tile.
    The entry list ends at an offset of 0.  Every `period + 1` passes the tile steps on, from the last back to the first."""
    for e in range(30):
        o = e * 8
        bx = table[o] | table[o + 1] << 8
        if bx == 0:
            return
        period, counter = table[o + 2], table[o + 3] + 1
        if counter <= period:
            table[o + 3] = counter & 0xFF
            continue
        table[o + 3] = 0
        cell = gmap[bx] | gmap[bx + 1] << 8
        first = table[o + 4] | table[o + 5] << 8
        last = table[o + 6] | table[o + 7] << 8
        cell = first if cell == last else (cell + 1) & 0xFFFF
        gmap[bx], gmap[bx + 1] = cell & 0xFF, cell >> 8


def animate_objects(table, omap):
    """0x2b99a: up to 40 entries of 6 bytes: offset into the object map, period, counter, first id, last id.
    The counter must hit the period exactly; the id steps on and wraps from above the last to the first."""
    for e in range(40):
        o = e * 6
        bx = table[o] | table[o + 1] << 8
        if bx == 0:
            return
        period, counter = table[o + 2], (table[o + 3] + 1) & 0xFF
        if counter != period:
            table[o + 3] = counter
            continue
        table[o + 3] = 0
        v = (omap[bx] + 1) & 0xFF
        if v > table[o + 5]:
            v = table[o + 4]
        omap[bx] = v


# ---- the villagers (0x2bccd and the decision at 0x134ba) ---------------------------------------------------------------


def listed(gmap, idx, lst):
    """repne scasw over the 100-word list of walkable tiles: found at index j leaves cx = 99 - j, and the game takes cx == 0
    as 'not found', so an entry in the very last place does not count.  A cell past the map's end reads as tile 0 (the port's
    choice, as in src/walk.c tile_listed; the original reads whatever follows)."""
    word = gmap[idx] | gmap[idx + 1] << 8 if idx + 2 <= len(gmap) else 0
    return word in lst[:99]


def villager_step(rec, gmap, lst, flag=1):
    """0x2bccd.  Changes `rec` (50 bytes) and returns the low byte of the result, or None when the routine leaves AX as the
    caller had it (the record offset, which is never 0xa or 0xc for the 20 records)."""
    kind = rec[0xB]
    if kind == 0 or kind >= 6:
        return None
    if rec[2] >= 0x61:
        rec[0xA] += 1
        if rec[0xA] >= 0x14:
            rec[2] = 1
            rec[0xA] = 1
            return 0xC
        return 0      # 0x2bd15: while the smoke lasts the caller sees 0 (the timer would ask for a direction at 10)
    if flag == 0:
        return None
    rec[9] = (rec[9] + 1) & 0xFF
    if rec[9] != rec[8]:
        return None
    rec[9] = 0
    rec[0xA] += 1
    if rec[0xA] == 2:
        rec[0xA] = 0
    x, y = rec[0], rec[1]
    d = rec[2]
    nx, ny = x, y
    if d == 0:
        if y > rec[4]:
            ny -= 1
    elif d == 1:
        if y < rec[6]:
            ny += 1
    elif d == 2:
        if x > rec[3]:
            nx -= 1
    elif d == 3:
        if x < rec[5]:
            nx += 1
    idx = (((ny >> 1) + 1) << 8) | (nx & 0xFE)
    ok = listed(gmap, idx, lst) and listed(gmap, idx + 2, lst)
    if ok and (nx & 1):
        ok = listed(gmap, idx + 4, lst)
    if ok and (ny & 1):
        ok = listed(gmap, idx + 0x100, lst) and listed(gmap, idx + 0x102, lst)
        if ok and (nx & 1):
            ok = listed(gmap, idx + 0x104, lst)
    if ok:
        rec[0], rec[1] = nx, ny
    rec[7] += 1
    ret = rec[7]
    if rec[7] > 0xA:
        rec[7] = 0
    return ret


def decide(rec, rnd, ticks16, hero_x, hero_y, stride):
    """0x134ba: runs when the step returned 10."""
    rec[7] = ticks16 & 7
    r = rnd.next()
    d = 5
    if r < 0x2AAA:
        d = 2 if rec[0] > hero_x else 3
    if r > 0x5554:
        d = 0 if rec[1] > hero_y else 1
    if d == 5 or stride != 0 or rec[0x31] == 0:
        d = rnd.next() // 0x1000          # 0 .. 7
    rec[2] = d


# ---- the hero (0x2bf02, 0x2bfd5, 0x2c098) -------------------------------------------------------------------------


def foot_ok(gmap, lst, x, y):
    """0x2bf02: can the hero stand at (x, y)?  The same cells as for a villager: two in the row (y >> 1) + 1, a third when x is odd, and the
    next row's when y is odd; all must be on the list of walkable tiles (a hit in the list's last place does not count)."""
    idx = (((y >> 1) + 1) << 8) | (x & 0xFE)

    def L(i):
        return i + 2 <= len(gmap) and listed(gmap, i, lst)

    if not L(idx) or not L(idx + 2):
        return 0
    if (x & 1) and not L(idx + 4):
        return 0
    if y & 1:
        if not L(idx + 0x100) or not L(idx + 0x102):
            return 0
        if (x & 1) and not L(idx + 0x104):
            return 0
    return 1


def trail_step(trail, party, x, y):
    """0x2bfd5: every trail entry moves one place on, the hero's new (x, y, direction) goes in front, party record 0 gets (x, y) and the
    records 1-4 get trail entries 3, 6, 9, 12."""
    for k in range(11, -1, -1):
        trail[3 * (k + 1):3 * (k + 1) + 3] = trail[3 * k:3 * k + 3]
    trail[0], trail[1], trail[2] = x, y, party[2]
    party[0], party[1] = x, y
    for i, e in ((1, 3), (2, 6), (3, 9), (4, 12)):
        party[0x28 * i:0x28 * i + 3] = trail[3 * e:3 * e + 3]


def find_record(ta, x, y):
    """0x2c098: the offset of the first scene record at the hero's position (x with the low bit cleared, y >> 1), or 0x2ee."""
    for bx in range(0, 0x2EE, 15):
        if ta[1000 + bx] == (x & 0xFE) and ta[1000 + bx + 1] == (y >> 1):
            if ta[1000 + bx + 2] == 5 and (x & 1 or y & 1):
                return 0x320
            return bx
    return 0x2EE


ARROWS = {0x48: 0, 0x50: 1, 0x4B: 2, 0x4D: 3}      # scan code -> direction (party byte 2)


# ---- shots and sparks (findings.md "WALK.SL step 11") -------------------------------------------------------------------
# One 60-byte list: 5 shots of (x, y, direction, zigzag phase), one slot per party member number, then 10 sparks of
# (frame, x, y, direction).  The art is RPG.LCP from 0x9b8: ten 7 x 5 digits, the shot (a 4-byte header then 8 x 8), five 8 x 8 sparks.

NSHOTS, NSPARKS = 5, 10
ART_SHOT, ART_SPARKS = 0x15E, 0x15E + 0x44
ART_LEN = 0x15E + 0x44 + 0x140
GOLD_AT = 0x122        # the coins: a word in the party table, at most 60,000
SCREEN_W = 320


def shots_move(fx):
    """0x2bac3: all five shots, used or not, step 2 along their direction and 2 across, alternately to either side."""
    for i in range(NSHOTS):
        o = 4 * i
        d = fx[o + 2]
        if d == 1:
            along, step, across = o + 1, 2, o
        elif d == 2:
            along, step, across = o, -2, o + 1
        elif d == 3:
            along, step, across = o, 2, o + 1
        else:
            along, step, across = o + 1, -2, o
        fx[along] = (fx[along] + step) & 0xFF
        if fx[o + 3] == 1:
            fx[across] = (fx[across] + 2) & 0xFF
            fx[o + 3] = 0
        else:
            fx[across] = (fx[across] - 2) & 0xFF
            fx[o + 3] = 1


def shot_hit(shot, tab_a):
    """0x2bb5e: 0 when the shot has reached no live enemy (byte 0x31 set, not in smoke); 1 for a hit (the enemy turns the shot's
    way); 2 when the enemy's hit counter (byte 0x28) reaches twice its sprite set and it starts going up in smoke."""
    sx, sy = shot[0], shot[1]
    for k in range(20):
        o = 50 * k
        if tab_a[o + 2] == 0x61 or tab_a[o + 0x31] == 0:
            continue
        dl, dh = tab_a[o] & 0xFE, tab_a[o + 1] & 0xFE
        dh1, dl2 = (dh + 1) & 0xFF, (dl + 2) & 0xFF
        if (sx, sy) not in ((dl, dh), (dl, dh1), (dl2, dh1), (dl2, dh)):
            continue
        if tab_a[o + 0xB] != 6:
            tab_a[o + 0x28] = (tab_a[o + 0x28] + 1) & 0xFF
            if tab_a[o + 0x28] >= (tab_a[o + 0xB] << 1) & 0xFF:
                tab_a[o + 0x28] = 0
                tab_a[o + 2] = 0x61
                tab_a[o + 0xA] = 0
                return 2
        tab_a[o + 2] = shot[2]
        return 1
    return 0


def draw_gold(scr, value, digits):
    """0x1bd6e: a 40 x 5 box of colour 0 at (273, 185), then the number right-aligned in 7 x 5 digits, 8 pixels apart."""
    at, div = 0xE851, 10000
    for lim, a, dv in ((10000, 0xE855, 1000), (1000, 0xE859, 100), (100, 0xE85D, 10), (10, 0xE861, 1)):
        if value < lim:
            at, div = a, dv
    for r in range(5):
        scr[0xE851 + r * SCREEN_W:0xE851 + r * SCREEN_W + 40] = bytes(40)
    while div:
        d = (value // div) & 0xFF
        value -= d * div
        div //= 10
        for r in range(5):
            if at + r * SCREEN_W + 7 <= len(scr) and d < 10:
                scr[at + r * SCREEN_W:at + r * SCREEN_W + 7] = digits[d * 35 + r * 7:d * 35 + r * 7 + 7]
        at += 8


def blit_sprite(buf, pos, wd, ht, src):
    """Colour 0 transparent, stride 224, nothing past the 224 x 200 buffer (0x26ad2 for the shot, 8 x 8 for the sparks)."""
    for r in range(ht):
        for c in range(wd):
            v = src[r * wd + c]
            p = pos + r * W + c
            if v and p < W * 200:
                buf[p] = v


# ---- one pass ---------------------------------------------------------------------------------------------------------


class Idle:
    def __init__(self, ticks_base=0, tick_div=3, seed=0, data=DATA):
        data = Path(data)
        scene = wsc.load(data)       # the scene MAN_INFO.NOW names, as the loader builds it (tools/walk_scene.py)
        self.tiles, self.sprites = scene.tiles, scene.sprites
        # padded like the port's banks (src/walkscene.c) so a sprite near a bank's end reads zeros
        self.bank1 = bytes(scene.bank1) + bytes(0x10000)
        self.bank2 = bytes(scene.bank2) + bytes(0x10000)
        rp = (data / "RPG.LCP").read_bytes()
        self.puff = wf.rle(rp[0x1A4CC:0x1A4CC + 0x1EB])     # the smoke, at [bp-0xba] + 0xe90a (0x109cb)
        self.art = rp[0x9B8:0x9B8 + ART_LEN]
        self.gmap = bytearray(scene.gmap)
        self.omap = bytearray(scene.omap)
        self.blockX = bytearray(scene.blockX)
        self.blockY = bytearray(scene.blockY)
        self.lstb = bytearray(scene.lst)
        self.trail = bytearray(scene.trail)
        self.tab_a, self.tab_b = bytearray(scene.tabA), bytearray(scene.tabB)
        self.pal = Palette(rp[:768])
        self.cls = rp[0]
        self.weather = scene.weather
        # 0x102fe then 0x11cee: the list all 0xfa, each shot's direction 4 (none flies), each spark's frame 9 (none shows)
        self.fx = bytearray(b"\xfa" * (4 * (NSHOTS + NSPARKS)))
        for i in range(NSHOTS):
            self.fx[4 * i + 2] = 4
        for j in range(NSPARKS):
            self.fx[4 * NSHOTS + 4 * j] = 9
        self.stopped = False  # the original left the loop (a battle)
        self.rnd = Rand(seed)
        self.drops = [self.rnd.next() for _ in range(75)]     # 0x10610, once, at start-up
        # 0x10641: a second list of 300 distinct words (rand() + 0xe10, a repeat is drawn again), 303 draws in all
        other = []
        while len(other) < 300:
            v = self.rnd.next() + 0xE10
            if v not in other:
                other.append(v)
        self.other = other
        self.hero_frame = 0
        self.ctr = 0
        self.n = 0
        self.ticks_base, self.tick_div = ticks_base, tick_div
        # the hero (all as the locals of WALK.SL's main function)
        self.v2e = 3          # [bp-0x2e] 3 at start, 1 on the pass a step is made, 5 after
        self.v98 = 0          # [bp-0x98] 0, 1, 2 ... counts up to the speed class: a key is acted on when it wraps to 0
        self.v48 = 0          # [bp-0x48] the shake after bumping into something
        self.c2 = 0           # [bp-0xc2] the last key
        self.v76 = 0          # [bp-0x76] sliding round corners (toggled by the A key)
        self.alt = bytearray(1 + 256 * 256)   # [bp-0x9e]: byte 0 is the "roof is off" flag, the rest holds the roof's rectangle of the object map
        self.unbuilt = []     # what the original did that this model does not do
        self.events = 0

    @property
    def lst(self):
        b = self.lstb
        return [b[i] | b[i + 1] << 8 for i in range(0, len(b) - 1, 2)]

    def ticks(self):
        return (self.ticks_base + self.n // self.tick_div) & 0xFFFF

    def rain_stride(self):
        t = self.ticks()
        if self.weather not in (1, 2):
            return 0
        if t >= 0x5000:
            return 0
        n = t >> 10
        if n > 10:
            n = 20 - n
        return (0xE1 * n + 0x384) & 0xFFFF

    def draw_buf(self):
        x, y = self.tab_b[0], self.tab_b[1]
        # 0x400 bytes more than the 224 x 200 the layers use: the specks can reach 0xaf00, past the buffer's end (the original
        # writes into whatever follows; nothing in the window shows it, CHECKED over the passes below)
        buf = bytearray(W * 200 + 0x400)
        wl.draw_ground(buf, self.tiles, self.gmap, x, y)
        we.draw_entities(buf, self.tab_a, self.tab_b, self.bank1, self.bank2, x, y, hero_frame=self.hero_frame, puff=self.puff)
        return buf

    def shots_pass(self, buf, scr, x, y):
        """0x1208f .. 0x122d7: move the shots, draw each one that flies, and test what it has reached: a wall or a hit leaves a spark
        (and a shake of 4), a destroyed enemy adds a coin, shown at once."""
        fx = self.fx
        shots_move(fx)
        for i in range(NSHOTS):
            o = 4 * i
            if fx[o + 2] >= 4:
                continue
            pos = we.screen_pos(fx[o], fx[o + 1], x, y, 0, 1)
            if pos >= 0xC350:
                fx[o + 2] = 0xA
                continue
            spr = self.art[ART_SHOT:]
            blit_sprite(buf, pos, spr[0] | spr[1] << 8, spr[2] | spr[3] << 8, spr[4:])
            hit = shot_hit(fx[o:o + 4], self.tab_a)
            idx = (fx[o + 1] >> 1) << 8 | fx[o]       # 0x2ba52: the tile under the shot
            if not listed(self.gmap, idx, self.lst) or hit == 1:
                for j in range(NSPARKS):
                    sp = 4 * NSHOTS + 4 * j
                    if fx[sp] > 4:
                        fx[sp + 1:sp + 4] = fx[o:o + 3]
                        fx[sp] = 0
                        self.v48 = 4
                        break
                fx[o + 2] = 0xA
            if hit == 2:                                # the sound effect (0x1c68e) is not modelled
                g = min((self.tab_b[GOLD_AT] | self.tab_b[GOLD_AT + 1] << 8) + 1, 60000)
                self.tab_b[GOLD_AT], self.tab_b[GOLD_AT + 1] = g & 0xFF, g >> 8
                draw_gold(scr, g, self.art)
                fx[o + 2] = 0xA

    def sparks_pass(self, buf, x, y):
        """0x12314 .. 0x12464, after the objects: from the third frame on a spark drifts one step; frames 0-4 are drawn, then it ends."""
        fx = self.fx
        for j in range(NSPARKS):
            sp = 4 * NSHOTS + 4 * j
            if fx[sp] >= 2:
                k = {0: sp + 2, 1: sp + 2, 2: sp + 1}.get(fx[sp + 3], sp + 1)
                fx[k] = (fx[k] + (-1 if fx[sp + 3] in (0, 2) else 1)) & 0xFF
            f = fx[sp]
            pos = we.screen_pos(fx[sp + 1], fx[sp + 2], x, y, 0, 1)
            if f < 5 and pos < 0xC350:
                blit_sprite(buf, pos, 8, 8, self.art[ART_SPARKS + f * 64:ART_SPARKS + f * 64 + 64])
                fx[sp] += 1                             # the sound at frame 2 (0x1c68e) is not modelled
            else:
                fx[sp] = 0xA

    def space(self):
        """0x1424c: each party member who can shoot (word +8 not 0) and whose slot (by member number) is free fires the way he faces;
        the followers then face a random way."""
        tb, fx = self.tab_b, self.fx
        for si in range(5):
            p = 0x28 * si
            n = tb[p + 3]
            if n > 0x64:
                break
            if tb[p + 8] | tb[p + 9] << 8 == 0:
                continue
            if 4 * n + 4 > len(fx):
                self.unbuilt.append(("a shot slot outside the shot list", self.n, n))
                continue
            o = 4 * n
            if fx[o + 2] <= 3:
                continue
            fx[o:o + 3] = tb[p:p + 3]
            fx[o + 3] = self.rnd.next() // 0x4000
            fx[o] &= 0xFE
            fx[o + 1] &= 0xFE
            if tb[p + 2] == 0:
                fx[o + 1] = (fx[o + 1] + 2) & 0xFF
            if fx[o + 3] == 0:
                if fx[o + 2] < 2:
                    fx[o] = (fx[o] + 2) & 0xFF
                else:
                    fx[o + 1] = (fx[o + 1] + 2) & 0xFF
            if si != 0:
                tb[p + 2] = self.rnd.next() // 0x2000

    def respawn(self, rec):
        """0x13356: the smoke has cleared; the enemy comes back at a random open place (0x2c119: two cells) outside a box round the
        view.  The box's far corner is kept in the hero's walk counter and frame ([bp-0x3c], [bp-0x5e]), which keep those values."""
        hx, hy = self.tab_b[0], self.tab_b[1]
        lo_x, hi_x = (hx - 0x16) & 0xFF, (hx + 0x30) & 0xFF
        lo_y = ((hy >> 1) - 10) & 0xFF
        hi_y = (lo_y + 0x15) & 0xFF
        self.ctr, self.hero_frame = hi_x, hi_y
        for _ in range(1000000):
            rx = (((self.rnd.next() // 0x13C + 0xC) & 0xFF) * 2) & 0xFF
            ry = (self.rnd.next() // 0x19A + 0xA) & 0xFF
            if lo_x <= rx <= hi_x and lo_y <= ry <= hi_y:
                continue
            idx = ((ry + 1) & 0xFF) << 8 | rx
            if listed(self.gmap, idx, self.lst) and listed(self.gmap, idx + 2, self.lst):
                rec[0], rec[1] = rx, (ry * 2) & 0xFF
                return
        self.unbuilt.append(("no open place for an enemy to come back", self.n, 0))

    def contact(self):
        """0x12703 / 0x2c0da: the first villager record that is a live enemy at a party member's position (low bits cleared), or None."""
        tb, ta = self.tab_b, self.tab_a
        for i in range(5):
            p = 0x28 * i
            if tb[p + 3] > 0x64:
                break
            for k in range(20):
                o = 50 * k
                if ta[o + 2] != 0x61 and ta[o] & 0xFE == tb[p] & 0xFE and ta[o + 1] & 0xFE == tb[p + 1] & 0xFE and ta[o + 0x31] > 0:
                    return k
        return None

    def shake(self):
        """0x1252c: after a bump, for a few passes the window is copied from a little way into the buffer (0 to 2 pixels or 1 to 5 rows)."""
        a = 0
        if self.v48:
            t = (self.v48 * 4) & 0xFF
            d = (0x7FFF // t + 1) & 0xFFFF
            a = self.rnd.next() // d
            if a > self.v48:
                a = ((a - self.v48) * 0xE0) & 0xFFFF
            if self.v48 > 0:
                self.v48 -= 1
        return a

    def roof(self, rec, enter):
        """0x1b634 (called as 0x2a04 from the record block): the rectangle (x0, y0) .. (x1, y1) of the object map, bytes 4-7 of the record.
        Going in the rectangle is saved after the flag byte and cleared to 0xff (the roof disappears); coming out it is put back."""
        x0, y0, x1, y1 = rec[4], rec[5], rec[6], rec[7]
        w = x1 - x0 + 1
        off = x0 + (y0 << 7)
        idx = 1
        for _ in range(y0, y1 + 1):
            if enter:
                self.alt[idx:idx + w] = self.omap[off:off + w]
                self.omap[off:off + w] = b"\xff" * w
            else:
                self.omap[off:off + w] = self.alt[idx:idx + w]
            off += 0x80
            idx += w

    def hit_record(self, si):
        """What the original does when the hero stands on scene record `si` (0x172f0 .. 0x18b82, then the re-entry at 0x113c4).  Only the
        kinds seen are modelled: kind 3 (the roof of a house) takes the roof off when the hero goes up through the door and puts it
        back when he comes down (the weather mode also changes by 3, which stops the rain); the kinds the block names are reported;
        every other kind does nothing but the re-entry."""
        rec = bytes(self.tab_a[1000 + si:1000 + si + 15])
        kind = rec[2]
        d = self.tab_b[2]
        if kind == 3:
            if d == 0 and rec[3] == 0 and self.alt[0] == 0:
                self.roof(rec, True)
                self.alt[0] = 1
                self.weather += 3
            if d == 1 and rec[3] == 1 and self.alt[0] != 0:
                self.roof(rec, False)
                self.alt[0] = 0
                self.v48 = 2
                self.weather -= 3
        elif kind in (0, 1, 4, 5, 7, 8, 0xE, 0x11, 0x12, 0x13, 0x14):
            self.unbuilt.append((f"scene record kind {kind:#x}", self.n, si))
        # the re-entry: the record loop again, on the live state (the speed-class rule before it is not repeated)
        class S:
            pass
        sc = S()
        sc.gmap, sc.omap, sc.lst, sc.events = self.gmap, self.omap, self.lstb, []
        wsc.apply_records(sc, self.tab_a, self.tab_b)
        if sc.events:
            self.unbuilt.append(("cut scene", self.n, si))

    def keys(self, port60):
        """0x135f0 .. 0x13ad9 and the four walking handlers.  Returns when the pass's key work is done (the pass-end counters follow)."""
        k = port60
        c2 = self.c2
        if k > 0xC7 and k < 0xD1 and 0x47 < c2 < 0x51:      # the break code of another key does not release the held arrow
            if c2 == 0x48 and k != 0xC8:
                k = c2
            if c2 == 0x50 and k != 0xD0:
                k = c2
            if c2 == 0x4B and k != 0xCB:
                k = c2
            if c2 == 0x4D and k != 0xCD:
                k = c2
        if k == 0xE0:
            k = c2
        if k > 0x7F:
            c2 = 0
        if k in (2, 3, 0x1E) and c2 == k:
            k = 0x82
        if k >= 0x80:
            self.c2 = c2
            return
        c2 = k
        self.c2 = c2
        if k == 0x39:          # Space is read on every pass, not only every third
            self.space()
            return
        if k not in ARROWS and k != 0:
            self.unbuilt.append((f"key {k:#x}", self.n, 0))
            return
        if self.v98 != 0 or k == 0:
            return
        party = self.tab_b
        x, y = party[0], party[1]
        party[2] = ARROWS[k]
        self.v2e = 1
        nx, ny = x, y
        if k == 0x48:
            ny = (y - 1) & 0xFF
        elif k == 0x50:
            ny = (y + 1) & 0xFF
        elif k == 0x4B:
            nx = (x - 1) & 0xFF
        else:
            nx = (x + 1) & 0xFF
        if foot_ok(self.gmap, self.lst, nx, ny):
            trail_step(self.trail, party, nx, ny)
        else:
            self.v2e = 5
            if self.v76:
                self.unbuilt.append(("sliding round a corner", self.n, 0))
            else:
                self.v48 = 2

    def run_pass(self, screen, port60=0):
        """Run one pass on `screen` (the screen before it); return the screen after the window copy."""
        animate_ground(self.blockX, self.gmap)
        animate_objects(self.blockY, self.omap)
        self.pal.cycle()
        buf = self.draw_buf()
        if self.v2e == 1:
            self.v2e = 5
            si = find_record(self.tab_a, self.tab_b[0], self.tab_b[1])
            if si < 0x2EE:
                self.hit_record(si)
                self.n += 1
                return screen            # the rest of the pass, the window copy and the counters included, is skipped
        x, y = self.tab_b[0], self.tab_b[1]
        scr = bytearray(screen)
        self.shots_pass(buf, scr, x, y)
        wl.draw_objects(buf, self.sprites, self.omap, x, y)
        self.sparks_pass(buf, x, y)
        if self.weather in (1, 2) and self.ticks() > 0xA000:
            specks(buf, self.other)
        stride = self.rain_stride()
        if stride:
            wf.draw_rain(buf, self.drops, stride)
        a = self.shake()
        wf.copy_window(scr, buf[a:] if a else buf)
        k = self.contact()
        if k is not None:              # the original leaves the loop for a battle (not modelled)
            self.unbuilt.append((f"a battle with villager record {k}", self.n, k))
            self.stopped = True
            self.n += 1
            return scr
        for k in range(20):
            rec = self.tab_a[k * 50:k * 50 + 50]
            ret = villager_step(rec, self.gmap, self.lst)
            if ret == 0xC:
                self.respawn(rec)
            if ret == 0xA:
                decide(rec, self.rnd, self.ticks(), self.tab_b[0], self.tab_b[1], stride)
            self.tab_a[k * 50:k * 50 + 50] = rec
        self.v98 += 1
        if self.v98 > self.cls:
            self.v98 = 0
        self.keys(port60)
        self.ctr = (self.ctr + 1) & 0xFF
        if self.ctr > self.cls + self.v2e:
            self.ctr = 0
            self.hero_frame += 1          # 0x172d7 (a respawn can leave any value here)
            if self.hero_frame > 3:
                self.hero_frame = 0
        self.n += 1
        return scr


def port_at(keys, n):
    """The keyboard port's value during pass n: the last --key set at or before the arrival n (0 before the first)."""
    v = 0
    for p in sorted(keys):
        if p <= n:
            v = keys[p]
    return v


def local(d, off):
    """The byte at [bp - off] of the main function's frame in a trace record."""
    return d["locs"][0xE2 - off]


def check(trace, npass=None, data=DATA):
    rec = load_trace(trace)
    npass = min(npass or len(rec) - 1, len(rec) - 1)
    m = Idle(ticks_base=rec[0]["ticks"], tick_div=3, seed=0, data=data)     # the trace's 'ticks' is the BIOS value at pass 0
    keys = rec[0].get("keys", {})
    screen = bytes(rec[0]["screen"])
    first_bad = {}
    for n in range(npass):
        r = rec[n]
        checks = {
            "villagers": bytes(m.tab_a[:1000]) == r["vill"],
            "party": bytes(m.tab_b[:200]) == r["party"],
            "drops": b"".join(v.to_bytes(2, "little") for v in m.drops) == r["drops"],
            "rand": m.rnd.state.to_bytes(4, "little") == r["rnd"],
            "palbuf": bytes(m.pal.buf[:48]) == r["palbuf"][:48],
            "dac": bytes(m.pal.dac) == r["pal"],
            "other": b"".join(v.to_bytes(2, "little") for v in m.other) == r["other"],
            "blockX": bytes(m.blockX) == r["blkx"],
            "blockY": bytes(m.blockY) == r["blky"],
            "screen": n == 0 or bytes(screen) == r["screen"],
        }
        if "trail" in r:
            checks["trail"] = bytes(m.trail) == r["trail"]
            checks["hero locals"] = (m.tab_b[0], m.tab_b[1], m.v2e, m.v98, m.ctr, m.hero_frame, m.v48, m.c2) == (
                local(r, 0x8E), local(r, 0xB6), local(r, 0x2E), local(r, 0x98), local(r, 0x3C), local(r, 0x5E), local(r, 0x48),
                local(r, 0xC2))
        if "alt0" in r:
            checks["roof"] = (m.alt[0], m.weather) == (r["alt0"][0], local(r, 0xD8))
        if "shots" in r:
            checks["shots"] = bytes(m.fx) == r["shots"]
            checks["coins"] = bytes(m.tab_b[GOLD_AT:GOLD_AT + 2]) == r["gold"]
        for k, v in checks.items():
            if not v and k not in first_bad:
                first_bad[k] = n
        if m.stopped:          # the model left the loop (a battle) after the previous pass: the trace must end there
            if n != len(rec) - 1:
                first_bad.setdefault("left the loop", n)
            break
        screen = m.run_pass(screen, port_at(keys, n))
    if not m.stopped and npass == len(rec) - 1:
        # one pass past the trace's last record: where the original left the loop (a touch) the model must leave it too
        m.run_pass(screen, port_at(keys, npass))
    print(f"passes compared: {npass}")
    if m.stopped:
        print(f"  the model left the loop for a battle after pass {m.n - 1}")
    for k in ("villagers", "party", "trail", "hero locals", "roof", "shots", "coins", "drops", "rand", "palbuf", "dac", "other",
              "blockX", "blockY", "screen", "left the loop"):
        if k == "left the loop" and k not in first_bad:
            continue
        if k in ("shots", "coins") and "shots" not in rec[0]:
            continue
        if k == "trail" and "trail" not in rec[0]:
            continue
        if k == "hero locals" and "trail" not in rec[0]:
            continue
        if k == "roof" and "alt0" not in rec[0]:
            continue
        print(f"  {k:12s}", "identical at every pass" if k not in first_bad else f"FIRST DIFFERENCE at pass {first_bad[k]}")
    if m.unbuilt:
        print("  not modelled:", m.unbuilt[:5], "..." if len(m.unbuilt) > 5 else "")
    return 0 if not first_bad else 1


def cross(cbin, trace, npass=None, start="work/check_credits/start.raw", data="original_bin"):
    """The C port (build/walk_dump idle) against the oracle's trace, after every pass: the screen, the palette, and the state the
    screen does not show (the villager table, rand state, rain list, specks list, palette buffer, animation tables)."""
    import subprocess
    import tempfile
    rec = load_trace(trace)
    npass = min(npass or len(rec) - 1, len(rec) - 1)
    base = rec[0]["ticks"]
    with tempfile.TemporaryDirectory(dir="work") as td:
        out = Path(td) / "idle.bin"
        kargs = [f"{p}:{c:#x}" for p, c in sorted(rec[0].get("keys", {}).items())]
        r = subprocess.run([cbin, data, "idle", str(out), str(npass), str(base), "3", start] + kargs, capture_output=True, text=True)
        if r.returncode != 0:
            print("walk_dump failed:", r.returncode, r.stderr.strip())
            return 1
        blob = out.read_bytes()
    parts = [("villagers", 1000, "vill"), ("rand", 4, "rnd"), ("drops", 150, "drops"), ("specks", 600, "other"),
             ("palbuf", 48, "palbuf"), ("blockX", 240, "blkx"), ("blockY", 240, "blky")]
    keyed = "trail" in rec[0]
    if keyed:
        parts += [("party", 200, "party"), ("trail", 39, "trail"), ("hero locals", 6, None)]
        if "alt0" in rec[0]:
            parts += [("roof", 2, "roof")]
    if "shots" in rec[0]:
        parts += [("shots", 60, "shots"), ("coins", 2, "gold")]
    keys = rec[0].get("keys", {})
    size = 64000 + 768 + sum(n for _, n, _ in parts)
    if not keyed:      # an older trace: the port's extra state (party, trail, locals) is in the dump but not compared
        size += 200 + 39 + 6
    if keyed and "alt0" not in rec[0]:
        size += 2
    if "shots" not in rec[0]:   # a trace from before the shots: the port's 62 bytes are not compared
        size += 62
    assert len(blob) == size * npass, (len(blob), size * npass)
    first = {}
    for i in range(npass):
        chunk = blob[i * size:(i + 1) * size]
        o = rec[i + 1]
        bad = {"screen": chunk[:64000] != o["screen"], "palette": chunk[64000:64768] != o["pal"]}
        at = 64768
        for name, n, key in parts:
            mine = chunk[at:at + n]
            if key == "roof":     # the roof flag and the weather mode
                want = bytes([o["alt0"][0], local(o, 0xD8)])
            elif key is None:     # v2e, v98, ctr, hero_frame, v48, c2 as the port keeps them, against the trace's locals
                want = bytes([local(o, 0x2E), local(o, 0x98), local(o, 0x3C), local(o, 0x5E), local(o, 0x48), local(o, 0xC2)])
            else:
                want = o[key][:n] if key == "palbuf" else o[key]
            bad[name] = mine != want
            at += n
        for k, v in bad.items():
            if v and k not in first:
                first[k] = i
    print(f"C port against the oracle, {npass} passes from clock {base}: "
          + ("screens, palettes and hidden state identical" if not first else
             "DIFFERENT, first after pass " + ", ".join(f"{k} {v}" for k, v in sorted(first.items(), key=lambda kv: kv[1]))))
    return 0 if not first else 1


if __name__ == "__main__":
    if len(sys.argv) >= 4 and sys.argv[1] == "cross":
        raise SystemExit(cross(sys.argv[2], sys.argv[3], int(sys.argv[4]) if len(sys.argv) > 4 else None,
                               data=os.environ.get("WALK_DATA", "original_bin")))
    if len(sys.argv) >= 3 and sys.argv[1] == "check":
        raise SystemExit(check(sys.argv[2], int(sys.argv[3]) if len(sys.argv) > 3 else None,
                               data=os.environ.get("WALK_DATA", "original_bin")))
    raise SystemExit(__doc__)
