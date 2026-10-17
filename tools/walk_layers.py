#!/usr/bin/env python3
"""Model of WALK.SL's two map layers (ground tiles, object sprites) in the 224-pixel-wide world buffer.

Written from the disassembly of 0x2c362 (ground) and 0x2c456 (objects) and checked against the oracle:
the model's output must equal the oracle's buffer byte for byte (see docs/findings.md, "WALK.SL step 1").

Inputs are memory dumps taken from the oracle with tools/walk_dump.py, e.g. (all at the first draw):

  work/venv/bin/python tools/walk_dump.py work/walk2/d4 14 \\
      --dump-at 2c362:0:bufBefore:2dec6:44800 --dump-at 2c456:0:bufPreO:2dec6:44800 \\
      --dump-at 19710:0:bufG:2dec6:44800     --dump-at 19974:0:bufO:2dec6:44800 \\
      --dump-at 19974:0:tiles:55e46:16000    --dump-at 19974:0:sprites:4dec6:32640 \\
      --dump-at 19974:0:gmap:6dc16:25600     --dump-at 19974:0:omap:76f00:10240

  work/venv/bin/python tools/walk_layers.py check work/walk2/d4 [X Y]     (X, Y default 36, 44)

Layout (all linear addresses are the oracle's; they depend on the allocator and are NOT in the files):
  tiles   8x8 pixels, 64 bytes each, id = 16-bit word of the ground map.  From GTAB.ALL offset 0x11040 (16,000 bytes).
  sprites 8x8 masked, 128 bytes each: 64 pixels then 64 mask bytes (0xff keeps the screen, 0 takes the pixel).
          From GTAB.ALL offset 0x14ec0 (15,872 bytes read); id 0xff in the map means "no object".
  gmap    128 x 100 16-bit words (the ground layer); from __2.ALL (see findings.md).
  omap    128-byte rows of object ids, at gmap + 0x92ea.
  X, Y    the view centre in half-tile units (the hero's position); odd values shift the picture by 4 pixels.
"""
import struct
import sys
from pathlib import Path

W = 224          # world buffer width in pixels
H = 200


def draw_ground(buf, tiles, gmap, x, y):
    """25 x 23 tiles from map tile (x/2 - 11, y/2 - 10), first tile at buffer (16, 16), minus 4 px for odd X / Y."""
    bx = (((y >> 1) - 10) << 8) + ((x & 0xFE) - 0x16)      # byte offset into the word map (256 bytes per row)
    dx = 0xE10 - (4 if x & 1 else 0) - (0x380 if y & 1 else 0)
    for _ in range(23):
        b, d = bx, dx
        for _ in range(25):
            off = (struct.unpack_from("<H", gmap, b)[0] << 6) & 0xFFFF      # the original shifts in 16 bits
            for k in range(8):
                buf[d + k * W:d + k * W + 8] = tiles[off + k * 8:off + k * 8 + 8]
            b += 2
            d += 8
        bx += 256
        dx += 8 * W


def draw_objects(buf, sprites, omap, x, y):
    """Same 25 x 23 grid over the byte map; each id < 0xff is blitted as (screen & mask) | pixel."""
    bx = (((y >> 1) - 10) << 7) + (((x & 0xFF) - 0x16) >> 1)
    dx = 0xE10 - (4 if x & 1 else 0) - (0x380 if y & 1 else 0)
    for _ in range(23):
        b, d = bx, dx
        for _ in range(25):
            i = omap[b]
            if i <= 0xFE:
                off = i << 7
                for k in range(8):
                    for j in range(8):
                        p = d + k * W + j
                        buf[p] = (buf[p] & sprites[off + 0x40 + k * 8 + j]) | sprites[off + k * 8 + j]
            b += 1
            d += 8
        bx += 128
        dx += 8 * W


def check(dirname, x=36, y=44):
    d = Path(dirname)
    rd = lambda n: (d / n).read_bytes()
    tiles, sprites, gmap, omap = rd("tiles.bin"), rd("sprites.bin"), rd("gmap.bin"), rd("omap.bin")
    ok = True
    g = bytearray(rd("bufBefore.bin"))
    draw_ground(g, tiles, gmap, x, y)
    n = sum(1 for a, b in zip(g, rd("bufG.bin")) if a != b)
    print(f"ground : {n} differing bytes of {W * H}")
    ok &= n == 0
    o = bytearray(rd("bufPreO.bin"))      # the buffer at the start of the objects call: the hero is already in it
    draw_objects(o, sprites, omap, x, y)
    n = sum(1 for a, b in zip(o, rd("bufO.bin")) if a != b)
    print(f"objects: {n} differing bytes of {W * H}")
    ok &= n == 0
    return 0 if ok else 1


if __name__ == "__main__":
    if len(sys.argv) >= 3 and sys.argv[1] == "check":
        raise SystemExit(check(sys.argv[2], *(int(a) for a in sys.argv[3:5])))
    raise SystemExit(__doc__)
