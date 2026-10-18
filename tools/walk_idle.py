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
the music change a roof makes (0x1c68e), and hero-villager contact (0x2c0da).

Usage: work/venv/bin/python tools/walk_idle.py check TRACE.pkl [NPASSES]
       work/venv/bin/python tools/walk_idle.py cross build/walk_dump TRACE.pkl [NPASSES]     (the C port)
       (TRACE.pkl from tools/walk_idle_trace.py)
"""
import pickle
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
    as 'not found', so an entry in the very last place does not count."""
    word = gmap[idx] | gmap[idx + 1] << 8
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
        return rec[0xA]
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


# ---- one pass ---------------------------------------------------------------------------------------------------------


class Idle:
    def __init__(self, ticks_base=0, tick_div=3, seed=0):
        self.tiles, self.sprites, _g, self.bank1, self.bank2 = wf.load_world()
        scene = wsc.load(DATA)
        self.gmap = bytearray(scene.gmap)
        self.omap = bytearray(scene.omap)
        self.blockX = bytearray(scene.blockX)
        self.blockY = bytearray(scene.blockY)
        self.lstb = bytearray(scene.lst)
        self.trail = bytearray(scene.trail)
        tab_a, tab_b = wf.load_state()
        self.tab_a, self.tab_b = bytearray(tab_a), bytearray(tab_b)
        rp = (DATA / "RPG.LCP").read_bytes()
        self.pal = Palette(rp[:768])
        self.cls = rp[0]
        self.weather = 1
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
        we.draw_entities(buf, self.tab_a, self.tab_b, self.bank1, self.bank2, x, y, hero_frame=self.hero_frame)
        return buf

    def draw_top(self, buf):
        """Objects, then the specks or the rain; returns the rain stride."""
        x, y = self.tab_b[0], self.tab_b[1]
        wl.draw_objects(buf, self.sprites, self.omap, x, y)
        if self.weather in (1, 2) and self.ticks() > 0xA000:
            specks(buf, self.other)
        stride = self.rain_stride()
        if stride:
            wf.draw_rain(buf, self.drops, stride)
        return stride

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
        stride = self.draw_top(buf)
        a = self.shake()
        scr = bytearray(screen)
        wf.copy_window(scr, buf[a:] if a else buf)
        for k in range(20):
            rec = self.tab_a[k * 50:k * 50 + 50]
            ret = villager_step(rec, self.gmap, self.lst)
            if ret == 0xC:
                raise NotImplementedError("villager step returned 0xc (0x13356, not read)")
            if ret == 0xA:
                decide(rec, self.rnd, self.ticks(), self.tab_b[0], self.tab_b[1], stride)
            self.tab_a[k * 50:k * 50 + 50] = rec
        self.v98 += 1
        if self.v98 > self.cls:
            self.v98 = 0
        self.keys(port60)
        self.ctr += 1
        if self.ctr > self.cls + self.v2e:
            self.ctr = 0
            self.hero_frame = (self.hero_frame + 1) & 3
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


def check(trace, npass=None):
    rec = load_trace(trace)
    npass = min(npass or len(rec) - 1, len(rec) - 1)
    m = Idle(ticks_base=rec[0]["ticks"], tick_div=3, seed=0)     # the trace's 'ticks' is the BIOS value at pass 0
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
        for k, v in checks.items():
            if not v and k not in first_bad:
                first_bad[k] = n
        screen = m.run_pass(screen, port_at(keys, n))
    print(f"passes compared: {npass}")
    for k in ("villagers", "party", "trail", "hero locals", "roof", "drops", "rand", "palbuf", "dac", "other", "blockX", "blockY", "screen"):
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
    keys = rec[0].get("keys", {})
    size = 64000 + 768 + sum(n for _, n, _ in parts)
    if not keyed:      # an older trace: the port's extra state (party, trail, locals) is in the dump but not compared
        size += 200 + 39 + 6
    if keyed and "alt0" not in rec[0]:
        size += 2
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
        raise SystemExit(cross(sys.argv[2], sys.argv[3], int(sys.argv[4]) if len(sys.argv) > 4 else None))
    if len(sys.argv) >= 3 and sys.argv[1] == "check":
        raise SystemExit(check(sys.argv[2], int(sys.argv[3]) if len(sys.argv) > 3 else None))
    raise SystemExit(__doc__)
