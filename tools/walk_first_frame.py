#!/usr/bin/env python3
"""Rebuild the first screen WALK.SL shows (the screen right after its first window copy, BIOS tick 21) and compare it
with the oracle, pixel for pixel, palette included.

Everything is rebuilt from the game files except what is listed under INPUTS FROM THE ORACLE below.

  work/venv/bin/python tools/walk_dump.py work/walk4/ff 22 \\
      --dump-at 2c1ae:0:omap:76f00:10240 --dump-at 2c1ae:0:tiles:55e46:16000 --dump-at 2c1ae:0:sprites:4dec6:32640 \\
      --dump-at 2c1ae:0:gmap:6dc16:25600 --dump-at 2c1ae:0:tabA:76816:1750 --dump-at 2c1ae:0:tabB:67618:520 \\
      --dump-at 2c1ae:0:bank1:74016:10240 --dump-at 2c1ae:0:bank2:86c16:0x6000 --dump-at 2c1ae:0:list0:67470:150 \\
      --dump-at 2c1ae:1:list1:67470:150 --dump-at 126fa:0:vga0:a0000:64000
  work/venv/bin/python tools/walk_first_frame.py check work/walk4/ff [work/check_credits/start.raw]

(126fa is the instruction after the first window copy; 2c1ae is the rain routine, whose first call is in the same frame.)

What the screen is made of (see docs/findings.md, "WALK.SL step 3"):
  1. the screen the menu left (start.raw: the stone frame, the empty buttons, the "486" label);
  2. BIOS tick 0-2: sixteen button labels, each drawn three times: a copy in colour 0x8e two rows down, one in 0x8f one row
     down, then the label itself.  A label is its characters' 16 x 14 glyphs from NUMBER.LCZ side by side, shaded by row;
  3. BIOS tick 9: a 40 x 5 box of colour 0 at (273, 185) and a 7 x 5 piece of RPG.LCP (offset 0x9b8) at (289, 185);
  4. BIOS tick 11-21: the world buffer (ground, villagers/party, objects, rain), whose middle 192 x 168 pixels are
     copied to screen (64, 16).

INPUTS FROM THE ORACLE (not yet derived from the files): the screen of item 1.  The object map and the ground map patches are
now rebuilt by tools/walk_scene.py (the start-up loader) and only compared with the oracle's.
"""
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import walk_entities as we  # noqa: E402
import walk_layers as wl  # noqa: E402

W = 224
DATA = Path("original_bin")

# ---- the game's random number generator (the usual C one) -----------------------------------------------------------


def c_rand_stream(seed):
    """srand(seed); rand() ... : state = state * 0x343FD + 0x269EC3 (32 bits); result = (state >> 16) & 0x7FFF."""
    state = seed & 0xFFFFFFFF
    while True:
        state = (state * 0x343FD + 0x269EC3) & 0xFFFFFFFF
        yield (state >> 16) & 0x7FFF


# ---- files -----------------------------------------------------------------------------------------------------------


def rle(block):
    """The .LCZ / __2.ALL run-length code: escape byte, then a 16-bit length of the whole block, then data;
    escape, count, value repeats a value."""
    esc, end = block[0], int.from_bytes(block[1:3], "little")
    out = bytearray()
    i = 3
    while i < end:
        if block[i] == esc:
            out += bytes([block[i + 2]]) * block[i + 1]
            i += 3
        else:
            out.append(block[i])
            i += 1
    return bytes(out)


def load_world():
    """Tiles, object sprites, ground map and the two sprite banks, from the files at the offsets the game reads them from."""
    gtab = (DATA / "GTAB.ALL").read_bytes()
    tiles = gtab[0x11040:0x11040 + 16000]
    sprites = gtab[0x14EC0:0x14EC0 + 0x3E00]
    d = rle((DATA / "__2.ALL").read_bytes()[6:])
    lo, hi = d[100:100 + 12800], d[100 + 12800:100 + 25600]
    gmap = bytearray(25600)
    gmap[0::2], gmap[1::2] = lo, hi
    # The start-up loader (0x115c0-0x11b60) then walks 15-byte scene records in the saved state (__1.NOW bytes 1000 on).
    # Only ONE of its effects is reproduced here, the one that changes this view: the record at __1.NOW[1195:1210]
    # (x 30, y 15, kind 2) writes tile 0x19 into ground word (x/2, y) = byte 15 * 256 + 30.  The other effects of the loader
    # (it also fills the object map) are not derived: the object map still comes from the oracle.
    now = (DATA / "__1.NOW").read_bytes()
    r = now[1195:1210]
    gmap[(r[1] << 8) + (r[0] & 0xFE):(r[1] << 8) + (r[0] & 0xFE) + 2] = r[7:9]
    bank1 = rle((DATA / "M_M.ALL").read_bytes()[0x1A4A9:0x1A4A9 + 0x18A0])
    bank2 = rle((DATA / "RPG.LCP").read_bytes()[0x1AE37:0x1AE37 + 0x2696])
    return tiles, sprites, bytes(gmap), bank1, bank2


def load_state():
    """The villager and party tables as the game has them after start-up: the files' first bytes, with the party's
    positions overwritten (the file holds the last saved position)."""
    tab_a = (DATA / "__1.NOW").read_bytes()[:1750]
    tab_b = bytearray((DATA / "MAN_INFO.NOW").read_bytes()[:520])
    tab_b[0], tab_b[1] = 0x24, 0x2C
    for k in range(1, 5):
        tab_b[k * 40:k * 40 + 3] = bytes([0x24, 0x2C, 1])
    return tab_a, bytes(tab_b)


# ---- rain ----------------------------------------------------------------------------------------------------------------


def initial_drops():
    """75 drop positions: the first 75 rand() values after srand(0) (WALK.SL calls srand(0) first thing)."""
    g = c_rand_stream(0)
    return [next(g) for _ in range(75)]


def draw_rain(buf, drops, stride):
    """0x2c1ae, mode 1.  40 short drops (3 pixels, colours 0xae 0xad 0xac) then 35 long ones (6 pixels, 0xad .. 0xa8), each
    one pixel to the right per row (offset +0xe1 per row).  After drawing, a drop moves on by stride/2 (short) or stride (long),
    modulo 0xa100.  Mutates `drops` (the game keeps this list from frame to frame).
    The game adds the buffer's offset 0x16 to each drop; buf[0] here is already that address, so drops index buf directly."""
    def put(pos, row, colour):
        buf[pos + row * 0xE1] = colour
    for i in range(40):
        p = drops[i]
        for row, c in enumerate((0xAE, 0xAD, 0xAC)):
            put(p, row, c)
        p += stride >> 1
        drops[i] = p - 0xA100 if p >= 0xA100 else p
    for i in range(40, 75):
        p = drops[i]
        for row, c in enumerate((0xAD, 0xAC, 0xAB, 0xAA, 0xA9, 0xA8)):
            put(p, row, c)
        p += stride
        drops[i] = p - 0xA100 if p >= 0xA100 else p


# ---- the interface ---------------------------------------------------------------------------------------------------


def glyph_offset(ch):
    """NUMBER.LCZ holds 30-byte cells in Big5 order (157 trail bytes per lead byte, from lead 0xA4)."""
    hi, lo = ch.encode("big5")
    return 0x3B20 + 30 * ((hi - 0xA4) * 157 + (lo - 0x40 if lo <= 0x7E else lo - 0x62))


def label_sprite(text, recolour):
    """16 x 14 per character, side by side, header (width, height).  The renderer shades rows 0-4 with 0x89, 5-9 with 0x8a and
    leaves 10-13 at 0x88.  Labels of two characters then get three overlapping recolouring passes over the bytes of the
    pixel data (0x88 -> 0x89 over [120, 270), 0x88 -> 0x8a over [240, 390), 0x88 -> 0x8b over [360, 510)); the lone 是 and 否
    keep 0x88."""
    font = (DATA / "NUMBER.LCZ").read_bytes()
    w = 16 * len(text)
    px = bytearray(w * 14)
    for k, ch in enumerate(text):
        g = font[glyph_offset(ch):glyph_offset(ch) + 28]
        for y in range(14):
            v = g[2 * y] << 8 | g[2 * y + 1]
            colour = 0x89 if y < 5 else (0x8A if y < 10 else 0x88)
            for x in range(16):
                if v >> (15 - x) & 1:
                    px[y * w + 16 * k + x] = colour
    if recolour:
        for lo, hi, c in ((120, 270, 0x89), (240, 390, 0x8A), (360, 510, 0x8B)):
            for i in range(lo, min(hi, len(px))):
                if px[i] == 0x88:
                    px[i] = c
    return w, 14, bytes(px)


# (text, x, y): left column F1-F8 at x = 17, right column F9 .. at x = 282, the lone 是 / 否 at the top right.
LABELS = ([(t, 17, 4 + 20 * k) for k, t in enumerate(("交談", "觀看", "取物", "施法", "物品", "裝備", "使用", "地圖"))]
          + [(t, 282, 60 + 20 * k) for k, t in enumerate(("位置", "資料", "音樂", "音效", "儲存", "離開"))]
          + [("是", 273, 40), ("否", 297, 40)])


def blit_mono(scr, x, y, spr, colour):
    w, h, px = spr
    for r in range(h):
        for c in range(w):
            if px[r * w + c]:
                scr[(y + r) * 320 + x + c] = colour


def blit_masked(scr, x, y, spr):
    w, h, px = spr
    for r in range(h):
        for c in range(w):
            if px[r * w + c]:
                scr[(y + r) * 320 + x + c] = px[r * w + c]


def draw_interface(scr):
    for text, x, y in LABELS:
        spr = label_sprite(text, len(text) == 2)
        blit_mono(scr, x, y + 2, spr, 0x8E)
        blit_mono(scr, x, y + 1, spr, 0x8F)
        blit_masked(scr, x, y, spr)
    # BIOS tick 9: a box of colour 0 and a 7 x 5 piece of RPG.LCP.
    for r in range(5):
        scr[(185 + r) * 320 + 273:(185 + r) * 320 + 313] = bytes(40)
    piece = (DATA / "RPG.LCP").read_bytes()[0x9B8:0x9B8 + 35]
    for r in range(5):
        scr[(185 + r) * 320 + 289:(185 + r) * 320 + 296] = piece[r * 7:r * 7 + 7]


# ---- the world and the window --------------------------------------------------------------------------------------------


def draw_world(tiles, sprites, gmap, omap, tab_a, tab_b, bank1, bank2, drops, stride=0x384, hero_frame=0):
    x, y = tab_b[0], tab_b[1]
    buf = bytearray(W * 200)
    wl.draw_ground(buf, tiles, gmap, x, y)
    we.draw_entities(buf, tab_a, tab_b, bank1, bank2, x, y, hero_frame=hero_frame)
    wl.draw_objects(buf, sprites, omap, x, y)
    draw_rain(buf, drops, stride)
    return buf


def copy_window(scr, buf):
    for r in range(168):
        scr[(16 + r) * 320 + 64:(16 + r) * 320 + 256] = buf[(16 + r) * W + 16:(16 + r) * W + 208]


def first_screen(start_pix, omap):
    tiles, sprites, gmap, bank1, bank2 = load_world()
    tab_a, tab_b = load_state()
    scr = bytearray(start_pix)
    draw_interface(scr)
    buf = draw_world(tiles, sprites, gmap, omap, tab_a, tab_b, bank1, bank2, initial_drops())
    copy_window(scr, buf)
    return scr, (tiles, sprites, gmap, bank1, bank2, tab_a, tab_b)


def check(dirname, start="work/check_credits/start.raw"):
    d = Path(dirname)
    rd = lambda n: (d / n).read_bytes()
    start_raw = Path(start).read_bytes()
    ok = True
    # The object map and the ground map come from the start-up loader (tools/walk_scene.py), not from the oracle.
    import walk_scene as wsc
    scene = wsc.load(DATA)
    omap = bytes(scene.omap)
    ref_omap = rd("omap.bin")             # the oracle dump covers the first 10,240 bytes (80 rows of the 100-row map)
    same = omap[:len(ref_omap)] == ref_omap
    print(f"object map from the loader: {'identical' if same else 'DIFFERENT'} ({sum(1 for b in omap if b != 0xFF)} cells)")
    ok &= same
    same = bytes(scene.gmap) == rd("gmap.bin")[:len(scene.gmap)]
    print(f"ground map from the loader: {'identical' if same else 'DIFFERENT'}")
    ok &= same
    scr, parts = first_screen(start_raw[:64000], omap)
    # The inputs rebuilt from the files against the oracle's memory.
    names = ("tiles", "sprites", "gmap", "bank1", "bank2", "tabA", "tabB")
    for nm, mine in zip(names, parts):
        ref = rd(nm + ".bin")
        same = ref[:len(mine)] == mine
        print(f"{nm:8s} from files: {'identical' if same else 'DIFFERENT'} ({len(mine)} bytes)")
        ok &= same
    drops = struct.pack("<75H", *initial_drops())
    same = rd("list0.bin")[:150] == drops
    print(f"rain list from srand(0): {'identical' if same else 'DIFFERENT'}")
    ok &= same
    if (d / "list1.bin").exists():
        # One frame later (the second rain call, stride 0x384): the drops must have moved on as the game moved them.
        g = initial_drops()
        draw_rain(bytearray(W * 200), g, 0x384)
        same = rd("list1.bin")[:150] == struct.pack("<75H", *g)
        print(f"rain list one frame later: {'identical' if same else 'DIFFERENT'}")
        ok &= same
    ref = rd("vga0.bin")
    bad = [i for i in range(64000) if scr[i] != ref[i]]
    print(f"first screen: {len(bad)} differing pixels of 64000")
    if bad:
        xs, ys = [i % 320 for i in bad], [i // 320 for i in bad]
        print(f"  first at ({xs[0]}, {ys[0]}), bounding box x {min(xs)}-{max(xs)}, y {min(ys)}-{max(ys)}")
    ok &= not bad
    return 0 if ok else 1


if __name__ == "__main__":
    if len(sys.argv) >= 3 and sys.argv[1] == "check":
        raise SystemExit(check(*sys.argv[2:4]))
    raise SystemExit(__doc__)
