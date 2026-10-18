#!/usr/bin/env python3
"""Model of WALK.SL's entity layer (the hero, the party and the villagers) in the 224-pixel-wide world buffer.

Written from the disassembly of 0x19680 (the frame routine), 0x2c05a (positions), 0x2be49 (draw order),
0x2bbec (screen position), 0x2bc7c and 0x2beca (sprite offsets) and 0x26c04 (the 16 x 16 blit), and checked
against the oracle: the model's output must equal the oracle's buffer byte for byte
(see docs/findings.md, "WALK.SL step 2: the entities").

Inputs are memory dumps from tools/walk_dump.py. The first frame (the routine at 0x19680, called once):

  work/venv/bin/python tools/walk_dump.py work/walk3/d 14 \\
      --dump-at 19729:0:tabA:76816:1750 --dump-at 19729:0:tabB:67618:520 --dump-at 19729:0:lst:67300:0x400 \\
      --dump-at 19729:0:bank1:74016:10240 --dump-at 19729:0:bank2:86c16:0x6000 --dump-at 19729:0:hdr:2dec6:16 \\
      --dump-at 19729:0:bufG:2dec6:44800 --dump-at 2c456:0:bufPreO:2dec6:44800
  work/venv/bin/python tools/walk_entities.py check work/walk3/d

Later frames come from the main loop, which has its own copy of this code (ground call returns to 0x11e23, objects
call to 0x12311).  Log the calls, list the frames, then dump the ones you want (hook 11e23 = the Nth ground call,
2c456 = the objects call of the same frame, whose index `frames` prints):

  work/venv/bin/python tools/walk_dump.py work/walk3/g 300 --key 30:0x4d:300 \\
      --call 2c362:ground:12 --call 2c456:obj:12 --call 2bbec:pos:6 --call 2beca:offB:3
  work/venv/bin/python tools/walk_entities.py frames work/walk3/g/reads.txt          # frame, objects index, X, Y, hero frame
  work/venv/bin/python tools/walk_dump.py work/walk3/f 300 --key 30:0x4d:300 \\
      --dump-at 19729:0:bank1:74016:10240 --dump-at 19729:0:bank2:86c16:0x6000 \\
      --dump-at 11e23:M:tabA_M:76816:1750 --dump-at 11e23:M:tabB_M:67618:520 --dump-at 11e23:M:lst_M:67300:0x400 \\
      --dump-at 11e23:M:bufG_M:2dec6:44800 --dump-at 2c456:O:bufPreO_M:2dec6:44800     (M = frame, O = its objects index)
  work/venv/bin/python tools/walk_entities.py frame work/walk3/f M X Y HEROFRAME

Structures (the linear addresses are the oracle's; the allocator picks them, they are not in any file):
  header   4 bytes at the start of the world buffer: [0] draw-entities flag, [1] X, [2] Y (view centre, half tiles), [3] ground flag.
           X, Y are the hero's x, y (tabB record 0): the hero is always drawn at buffer (104, 96).
  tabA     35 x 50-byte records = __1.NOW[0:1750] (CONFIRMED byte for byte).  The first 20 are drawn: the villagers.
             [0] x  [1] y (half tiles)  [2] direction 0..3  [0xa] animation frame 0/1  [0xb] sprite set 1..5 (0 or >= 6: hidden)
           USED here: only these. [3..9] look like a walking area and a step counter (0x2bccd, read, not run).
  tabB     13 x 40-byte records = MAN_INFO.NOW[0:520], with x, y of record 0 and x, y, direction of records 1-4 overwritten by code
           at start-up (the file has the last saved position).  The first 5 are drawn: the party.
             [0] x  [1] y  [2] direction 0..3  [3] character number (>= 0x64: not in the party, not drawn)
  bank1    10,240 bytes = 40 sprites of 16 x 16: the villagers' sprites.  M_M.ALL offset 0x1a4a9, 0x18a0 bytes read, RLE (CONFIRMED).
  bank2    the party's sprites, 16 x 16, 12 per character (4 directions x 3 poses).  RPG.LCP offset 0x1ae37, 0x2696 bytes read,
           RLE, decodes to 15,360 bytes (CONFIRMED; the oracle's buffer is longer, the rest is other data).
  list     25 entity numbers in draw order: 0..19 = tabA records, 20..24 = tabB records 0..4.
"""
import sys
from pathlib import Path

W = 224                 # world buffer width in pixels
HIDDEN = 0xEA60         # "not on screen" from the position routine; anything >= 0xC350 is skipped


def build_list(tab_a, tab_b):
    """0x2c05a + 0x2be49: the (x, y) of each of the 25 entities, then a bubble sort by y.

    The sort swaps only the y bytes of the pair array (x stays where it is), and the list bytes with them.
    One pass is 24 adjacent compares; there are 24 passes. A swap needs strictly greater, so it is stable.
    """
    xs = [tab_a[i * 50] for i in range(20)] + [tab_b[i * 40] for i in range(5)]
    ys = [tab_a[i * 50 + 1] for i in range(20)] + [tab_b[i * 40 + 1] for i in range(5)]
    order = list(range(25))
    for _ in range(24):
        for j in range(24):
            if ys[j] > ys[j + 1]:
                ys[j], ys[j + 1] = ys[j + 1], ys[j]
                order[j], order[j + 1] = order[j + 1], order[j]
    return order, xs, ys


def screen_pos(px, py, x, y, flag, vis):
    """0x2bbec. Buffer offset of the sprite's top-left corner (relative to the buffer start), or HIDDEN.

    flag 0 (villagers): visible only inside a box around the view; vis (the sprite set) must be 1..5.
    flag 1 (party): no box test.  Positions are in half tiles = 4 pixels: x*4 across, y*4*224 down.
    """
    if vis == 0 or vis >= 6 or px == 0 or py == 0:
        return HIDDEN
    dl = (px + 4) & 0xFF
    dh = (py + 4) & 0xFF
    bl = (x - 0x16) & 0xFF
    bh = (y - 0x14) & 0xFF
    cl, ch = bl, bh
    if flag == 0:
        if dl < bl:
            return HIDDEN
        if bl <= 0xCB and dl > ((bl + 0x34) & 0xFF):
            return HIDDEN
        if dh < bh:
            return HIDDEN
        if dh > ((bh + 0x2E) & 0xFF):
            return HIDDEN
    al = ((dl - cl) << 2) & 0xFF
    row = (dh - ch) & 0xFF
    return (row * 0x380 + al) & 0xFFFF


def sprite_off_a(direction, frame, vis):
    """0x2bc7c. Offset of a villager's sprite: bank 1 if < 0x2800, else bank 2 at offset - 0x9470.

    (vis - 1) * 0x800: eight sprites per set.  direction 0..3 * 0x200 (directions 4..7 map to 0..3):
    two frames per direction.  frame * 0x100.  The 0x61 case is not seen running.
    """
    if direction == 0x61:
        return ((frame >> 2) + 1) * 0x100 + 0xE80A & 0xFFFF
    off = ((vis - 1) * 0x800) & 0xFFFF
    d = direction - 4 if direction > 3 else direction
    off = (off + d * 0x200) & 0xFFFF
    if frame:
        off = (off + 0x100) & 0xFFFF
    return off


def sprite_off_b(direction, frame, number):
    """0x2beca. Offset of a party member's sprite in bank 2: number * 0xc00 + direction * 0x300 + frame * 0x100.

    frame is the walk counter 0..3; the offsets are 0, 0x100, 0x200, 0x100 (a counter of 3 is taken as 1).
    The first frame of the program passes 1.
    """
    f = frame if frame + 1 < 4 else 1
    return (direction * 0x300 + f * 0x100 + number * 0xC00) & 0xFFFF


def blit16(buf, pos, src):
    """0x26c04: 16 x 16 pixels, colour 0 transparent, stride 224. pos is relative to the buffer start (0x16 offset)."""
    for r in range(16):
        for c in range(16):
            p = src[r * 16 + c]
            if p:
                buf[pos + r * W + c] = p


def draw_entities(buf, tab_a, tab_b, bank1, bank2, x, y, order=None, hero_frame=1, puff=None):
    """`puff`: the smoke frames (RPG.LCP 0x1a4cc decoded), which the direction-0x61 offsets 0xe90a .. 0xee09 point into."""
    if order is None:
        order = build_list(tab_a, tab_b)[0]
    for k in order:
        if k < 0x14:
            r = tab_a[k * 50:k * 50 + 50]
            pos = screen_pos(r[0], r[1], x, y, 0, r[0xB])
            if pos >= 0xC350:
                continue
            off = sprite_off_a(r[2], r[0xA], r[0xB])
            if puff is not None and off >= 0xE90A and off - 0xE90A + 256 <= len(puff):
                src = puff[off - 0xE90A:off - 0xE90A + 256]
            elif off < 0x2800:
                src = bank1[off:off + 256]
            else:
                o2 = (off - 0x9470) & 0xFFFF
                src = bank2[o2:o2 + 256]
        else:
            r = tab_b[(k - 0x14) * 40:(k - 0x14) * 40 + 40]
            if r[3] >= 0x64:
                continue
            off = sprite_off_b(r[2], hero_frame, r[3])
            pos = screen_pos(r[0], r[1], x, y, 1, 1)
            src = bank2[off:off + 256]
        blit16(buf, pos, src)


def check(dirname):
    d = Path(dirname)
    rd = lambda n: (d / n).read_bytes()
    tab_a, tab_b, bank1, bank2, hdr = rd("tabA.bin"), rd("tabB.bin"), rd("bank1.bin"), rd("bank2.bin"), rd("hdr.bin")
    lst = rd("lst.bin")
    ok = True
    order, _, _ = build_list(tab_a, tab_b)
    want = list(lst[0x236:0x236 + 25])
    print(f"draw order: {'identical' if order == want else 'DIFFERENT'} ({want})")
    ok &= order == want
    x, y = hdr[1], hdr[2]
    buf = bytearray(rd("bufG.bin"))
    draw_entities(buf, tab_a, tab_b, bank1, bank2, x, y)
    ref = rd("bufPreO.bin")
    n = sum(1 for a, b in zip(buf, ref) if a != b)
    print(f"entities: {n} differing bytes of {len(ref)}")
    ok &= n == 0
    return 0 if ok else 1


def check_frame(dirname, tag, x, y, hero_frame=1):
    """A later frame of the main loop, captured by work/walk3/capframes.sh as tabA_TAG.bin, tabB_TAG.bin, lst_TAG.bin,
    bufG_TAG.bin (ground only) and bufPreO_TAG.bin (the buffer the objects pass starts from); banks are shared.
    X, Y are the view centre, taken from the logged calls to 0x2bbec."""
    d = Path(dirname)
    rd = lambda n: (d / n).read_bytes()
    tab_a, tab_b, lst = rd(f"tabA_{tag}.bin"), rd(f"tabB_{tag}.bin"), rd(f"lst_{tag}.bin")
    bank1, bank2 = rd("bank1.bin"), rd("bank2.bin")
    order, _, _ = build_list(tab_a, tab_b)
    want = list(lst[0x236:0x236 + 25])
    buf = bytearray(rd(f"bufG_{tag}.bin"))
    draw_entities(buf, tab_a, tab_b, bank1, bank2, x, y, order, hero_frame)
    ref = rd(f"bufPreO_{tag}.bin")
    n = sum(1 for a, b in zip(buf, ref) if a != b)
    shown = [k for k in order if frame_pos(tab_a, tab_b, k, x, y) < 0xC350]
    print(f"frame {tag} (X={x}, Y={y}, hero frame {hero_frame}): order {'identical' if order == want else 'DIFFERENT'}, "
          f"{len(shown)} drawn {shown}, {n} differing bytes of {len(ref)}")
    return n == 0 and order == want


def frame_pos(tab_a, tab_b, k, x, y):
    if k < 0x14:
        r = tab_a[k * 50:k * 50 + 50]
        return screen_pos(r[0], r[1], x, y, 0, r[0xB])
    r = tab_b[(k - 0x14) * 40:(k - 0x14) * 40 + 40]
    return screen_pos(r[0], r[1], x, y, 1, 1) if r[3] < 0x64 else HIDDEN


def list_frames(log):
    """Read a walk_dump.py log made with --call 2c362:ground --call 2c456:obj --call 2bbec:pos --call 2beca:offB and print, for
    each main-loop frame, the index of its objects call, the view centre and the hero's walk counter."""
    import re
    g = -1
    o = -1
    pend = None
    rows = {}
    for ln in open(log):
        m = re.match(r"t(\d+) CALL (\w+) ret=(0x[0-9a-f]+) ?(.*)", ln)
        if not m:
            continue
        name, ret, args = m[2], m[3], m[4].split()
        if name == "ground" and ret == "0x11e23":
            g += 1
            pend = g
            rows[g] = {"tick": int(m[1]), "obj": None, "xy": None, "hero": None}
        elif name == "obj":
            o += 1
            if ret == "0x12311" and pend is not None:
                rows[pend]["obj"] = o
                pend = None
        elif name == "pos" and ret == "0x11eac" and g >= 0:
            rows[g]["xy"] = (int(args[2], 16), int(args[3], 16))
        elif name == "offB" and ret == "0x12015" and g >= 0:
            rows[g]["hero"] = int(args[1], 16)
    for k, r in rows.items():
        print(f"frame {k} tick {r['tick']} objects-index {r['obj']} X,Y {r['xy']} hero-frame {r['hero']}")


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "frames":
        list_frames(sys.argv[2])
        raise SystemExit(0)
    if len(sys.argv) >= 3 and sys.argv[1] == "check":
        raise SystemExit(check(sys.argv[2]))
    if len(sys.argv) in (6, 7) and sys.argv[1] == "frame":
        hf = int(sys.argv[6]) if len(sys.argv) == 7 else 1
        raise SystemExit(0 if check_frame(sys.argv[2], sys.argv[3], int(sys.argv[4]), int(sys.argv[5]), hf) else 1)
    raise SystemExit(__doc__)
