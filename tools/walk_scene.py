#!/usr/bin/env python3
"""WALK.SL's start-up scene loader (the code at 0x10b00-0x11c17), rebuilt from the game files, and a checker that
compares it with the original running in the oracle.

  work/venv/bin/python tools/walk_scene.py show  [DATADIR]       print what the loader reads and the state it builds
  work/venv/bin/python tools/walk_scene.py check [DATADIR]       one oracle run, compare every buffer
  work/venv/bin/python tools/walk_scene.py scenes                every scene number 0-52 with the shipped state files
  work/venv/bin/python tools/walk_scene.py fuzz N [SEED]         N random variants of the state files, each compared
  work/venv/bin/python tools/walk_scene.py dump DATADIR OUT      the model's state in the C port's dump layout
  work/venv/bin/python tools/walk_scene.py cross [CBIN [N [SEED]]]  the C port's loader (build/walk_dump) against the model: all
                                                                  53 scenes, then N randomised state files (tools/check_walk.sh)

What the loader does (all of it READ from the disassembly; the buffers are CONFIRMED against the oracle by `check`/`fuzz`):

  scene   s = MAN_INFO.NOW[500]  (the loader copies bytes 500-503 to 510-513 first; byte 510 is the scene number,
          511 unused, 512 = hero x in half tiles, 513 = hero y / 2)
  state   __1.NOW holds one 1750-byte block per scene: bytes 0-999 are 20 villager records of 50 bytes,
          bytes 1000-1749 are 50 "scene records" of 15 bytes.  Block s is read into tabA, then tabA[1750 + i] is
          set to villager i's byte 8 (20 bytes).  MAN_INFO.NOW (504 bytes) is tabB: 5 party records of 40 bytes,
          then a flag table at 200-499 (flag idx, bit b = tabB[200 + idx] >> b & 1).
  map     TAB.TAB (1000 bytes) is an index of 5-byte entries (lo16, hi8, len16 = file offset hi8 << 16 | lo16, length).
          Entry s at 5 * s gives the scene's block in __2.ALL; that block is run-length decoded after its 6-byte
          6-byte header: [0] tile set, [1] sprite-bank number, [2] song, [3] unknown, [4] weather mode (1 = rain),
          [5] unknown (passed to the event call).  Decoded layout (39,080 bytes):
              [0:100]        the first half of the 100-word list (see kind 0xf)
              [100:25700]    ground map, two planes of 12,800 (low bytes, then high bytes) -> 128 x 100 words
              [25700:38500]  OBJECT MAP, 12,800 bytes (128 x 100, byte per cell, 0xff = empty)
              [38500:38740]  240 bytes (not yet understood)
              [38740:38840]  100 bytes, the second half of the list: [0:100] + these = 100 words (see kind 0xf)
              [38840:39080]  240 bytes (not yet understood)
  tiles   GTAB.ALL: entry at 0x109 + 10 * header[0]: (lo16, hi8, len1 16 at +3, len2 16 at +8).  Tiles = len1 bytes
          at the offset, the object sprites = the next len2 bytes.
  bank 1  M_M.ALL: index n = header[1] (minus 5 if above 0x13); entry at 0x334 + 5 * n in TAB.TAB; run-length coded.
  song    RPG.LCP at 0x7add + 5 * header[2] holds a 5-byte entry (same form) that locates the song in MUC.ALL.
  party   the five party records get x = MAN_INFO.NOW[502], y = 2 * MAN_INFO.NOW[503], flag byte 2 = 1; a 13-entry
          3-byte trail array gets the same (x, y, 1).
  records the 50 scene records are walked in order (kind = byte 2):
          0x02  if r[13] == 0:  r[6] != 0: object map bytes at (r[1] << 7) + (r[0] >> 1) := r[7], r[8]   (one word copy)
                                else:      ground map bytes at (r[1] << 8) + r[0] := r[7..10]            (two word copies)
          0x0a  if flag(r[7], r[8]): ground map word at byte offset word(r+3) := word(r+5);
                if flag(r[13], r[14]) and word(r+9) != 0: ground map word at word(r+9) := word(r+11)
          0x0b  four triples (item, flag byte, flag bit) at r[3..14]: the members are tried in order; a member whose status
                byte 3 is above 10 ENDS the search for that item; otherwise if the item is among its 20 item bytes
                (member + 0x14 .. + 0x27) the flag is SET and the search ends
          0x0c  if flag(r[6], r[7]): object map byte (r[4] << 7) + r[3] := r[5];
                if flag(r[11], r[12]) and word(r+8) != 0: object map byte (r[9] << 7) + r[8] := r[10]
          0x0d  if flag(r[3], r[4]): word at tabA offset word(r+5) := word(r+7)   (it patches the state, even records)
          0x0f  if flag(r[3], r[4]): for each of r[5], r[7], r[9], r[11], r[13] as a word v (stop at one above 0x226):
                add v to the 100-word list (the first slot that already holds v, or holds a value above 0x226)
          0x10  if r[5] != 0 and flag(r[3], r[4]): r[5] := 0 and an event (cut scene) starts.  NOT modelled: the
                loader reports it as `event` and the check skips such cases.
  villagers  m = RPG.LCP byte 0 (the CPU speed class).  If m < 2, before the records run, for every villager
          byte 8 := byte 8 - (2 - m) if that is above 2 - m, else 1; byte 9 := 0.

The weather clock, the music start and the screen drawing are not part of this tool.
"""
import argparse
import random
import shutil
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

DATA = Path("original_bin")
VILL = 50          # bytes per villager record
NVILL = 20
SREC = 15          # bytes per scene record
NSREC = 50
STATE = 1750       # bytes per scene in __1.NOW
EVENT = "event"


def rle(block):
    """The .LCZ / __2.ALL run-length code (see walk_first_frame.rle)."""
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


def w16(b, o):
    return b[o] | (b[o + 1] << 8)


class OOB(Exception):
    """A record wrote outside the buffer it patches (the original would scribble over the next buffer)."""


def put16(b, o, v):
    if not 0 <= o <= len(b) - 2:
        raise OOB(o)
    b[o] = v & 0xFF
    b[o + 1] = (v >> 8) & 0xFF


def put8(b, o, v):
    if not 0 <= o < len(b):
        raise OOB(o)
    b[o] = v


def entry(tab, o):
    """A 5-byte index entry at o: (file offset, length)."""
    return ((tab[o + 2] << 16) + w16(tab, o), w16(tab, o + 3))


class Scene:
    pass


def apply_records(s, ta, tb):
    """The 50 scene records (the loop at 0x113c4 .. 0x11c0e), applied to the state in `s` (gmap, omap, lst, events), `ta` (the
    villager table plus records) and `tb` (the party table with the flags at 200).  The loader runs it once; the main loop runs it
    again every time the hero steps on a record's position (walk_idle.py), on the live state."""
    def flags(idx, bit):
        """0x2c158 mode 0: `inc cl; shr al, cl`.  The count is masked to 5 bits (286 and later; the oracle does the same)
        and a count of 0 leaves CF as the earlier `cmp ch, 1` left it (1)."""
        n = (bit + 1) & 31
        if n == 0:
            return 1
        return (tb[200 + idx] >> (n - 1)) & 1 if n <= 8 else 0

    def setflag(idx, bit):
        """mode 1: `mov ah, 1; shl ah, cl` (cl = bit, count masked to 5 bits, so bits 8-31 give 0)."""
        tb[200 + idx] |= (1 << (bit & 31)) & 0xFF if bit != 0 else 1

    for i in range(NSREC):
        o = 1000 + SREC * i
        r = ta                                      # records live in tabA: r(k) = ta[o + k]
        kind = ta[o + 2]
        R = lambda k: ta[o + k]
        W = lambda k: w16(ta, o + k)
        if kind == 0x0B:
            for k in range(3, 0x0F, 3):
                value = R(k)
                for di in range(0, 0xC8, 0x28):
                    if tb[di + 3] > 10:
                        break                       # 0x11430 jumps to the next item, ending the member loop
                    if any(tb[b] == value for b in range(di + 0x14, di + 0x28)):
                        setflag(R(k + 1), R(k + 2))
                        break
        if kind == 0x0A:
            if flags(R(7), R(8)):
                put16(s.gmap, W(3), W(5))
            if flags(R(13), R(14)) and W(9) != 0:
                put16(s.gmap, W(9), W(11))
        if kind == 0x0C:
            if flags(R(6), R(7)):
                put8(s.omap, (R(4) << 7) + R(3), R(5))
            if flags(R(11), R(12)) and W(8) != 0:
                put8(s.omap, (R(9) << 7) + R(8), R(10))
        if kind == 0x02 and R(13) == 0:
            xx, yy = R(0), R(1)
            if R(6) != 0:
                put16(s.omap, (yy << 7) + (xx >> 1), R(7) | (R(8) << 8))
            else:
                put16(s.gmap, (yy << 8) + xx, R(7) | (R(8) << 8))
                put16(s.gmap, (yy << 8) + xx + 2, R(9) | (R(10) << 8))
        if kind == 0x0D:
            if flags(R(3), R(4)):
                put16(ta, W(5), W(7))
        if kind == 0x0F:
            if flags(R(3), R(4)):
                for di in range(5, 0x0F, 2):
                    v = W(di)
                    if v > 0x226:
                        break
                    j = 0
                    while j < 0xC8:
                        wv = w16(s.lst, j)
                        if wv == v or wv > 0x226:
                            break
                        j += 2
                    if j >= 0xC8:
                        raise ValueError("list overflow")
                    put16(s.lst, j, v)
        if kind == 0x10:
            if R(5) != 0 and flags(R(3), R(4)):
                s.events.append(i)
                ta[o + 5] = 0


def load(data=DATA, mi_override=None, now_override=None, rp_file=None):
    """Everything the loader builds, from the files in `data` (a Path).  Returns a Scene."""
    data = Path(data)
    mi_file = mi_override if mi_override is not None else (data / "MAN_INFO.NOW").read_bytes()
    now_file = now_override if now_override is not None else (data / "__1.NOW").read_bytes()
    tab = (data / "TAB.TAB").read_bytes()[:1000]
    s = Scene()

    # tabB: the file, in a 520-byte buffer that starts zeroed (what the oracle shows; real DOS memory is unspecified)
    tb = bytearray(520)
    tb[:min(len(mi_file), 520)] = mi_file[:520]
    tb[510:514] = tb[500:504]                       # 0x10b17: copy two words
    s.scene = tb[510]
    # tabA
    ta = bytearray(STATE + NVILL)
    blk = now_file[s.scene * STATE:(s.scene + 1) * STATE]
    ta[:len(blk)] = blk
    for i in range(NVILL):
        ta[STATE + i] = ta[8 + VILL * i]
    # the scene's block of __2.ALL
    pos, ln = entry(tab, 5 * s.scene)
    s.map_read = (pos, ln)
    raw = (data / "__2.ALL").read_bytes()[pos:pos + ln]
    d = rle(raw[6:])
    s.header = bytes(raw[:6])                       # the file's own 6-byte header, read before the decode
    tb[518] = raw[1]                                # 0x10e01-0x10e08
    s.lst = bytearray(d[0:100] + d[38740:38840])     # the 100-word list
    gm = bytearray(25600)
    gm[0::2], gm[1::2] = d[100:100 + 12800], d[100 + 12800:100 + 25600]
    s.gmap = gm
    s.omap = bytearray(d[25700:25700 + 12800])
    s.blockX = bytes(d[38500:38740])
    s.blockY = bytes(d[38840:39080])
    s.decoded_len = len(d)
    h = s.header
    s.weather = h[4]
    # tile set, sprites
    ent = 0x109 + 10 * h[0]
    t = tab
    gpos = (t[ent + 2] << 16) + w16(t, ent)
    len1, len2 = w16(t, ent + 3), w16(t, ent + 8)
    gtab = (data / "GTAB.ALL").read_bytes()
    s.tiles = gtab[gpos:gpos + len1]
    s.sprites = gtab[gpos + len1:gpos + len1 + len2]
    s.gtab_reads = [(gpos, len1), (gpos + len1, len2)]
    # bank 1 and the song
    n = h[1] - 5 if h[1] > 0x13 else h[1]
    bpos, blen = entry(tab, 0x334 + 5 * n)
    s.bank1_read = (bpos, blen)
    s.bank1 = rle((data / "M_M.ALL").read_bytes()[bpos:bpos + blen])
    rp = (data / "RPG.LCP").read_bytes()
    s.song_entry_at = 0x7ADD + 5 * h[2]
    s.song_read = entry(rp, s.song_entry_at)
    s.bank2 = rle(rp[0x1AE37:0x1AE37 + 0x2696])

    # party and trail
    x, y = tb[512], (tb[513] << 1) & 0xFF
    s.start = (x, y)
    for di in range(0, 0xC8, 0x28):
        tb[di], tb[di + 1], tb[di + 2] = x, y, 1
    s.trail = bytearray()
    for _ in range(0, 0x27, 3):
        s.trail += bytes([x, y, 1])

    # the speed class m = RPG.LCP[0] (the first byte of the buffer the file is read into, 0x109d3); below 2 the villagers'
    # byte 8 is reduced and byte 9 cleared (0x11358)
    s.m = (rp_file if rp_file is not None else rp)[0]
    if s.m < 2:
        k = 2 - s.m
        for i in range(NVILL):
            o = VILL * i
            ta[o + 8] = ta[o + 8] - k if ta[o + 8] > k else 1
            ta[o + 9] = 0

    # records
    s.events = []
    s.villager_rule = None
    apply_records(s, ta, tb)
    s.tabA, s.tabB = ta, tb
    return s


def show(data):
    s = load(data)
    print(f"scene {s.scene}; __2.ALL block {s.map_read[0]:#x} + {s.map_read[1]:#x}; decoded {s.decoded_len} bytes")
    print(f"header {s.header.hex(chr(32))} weather {s.weather}; hero start {s.start}")
    print(f"GTAB reads {[(hex(a), hex(b)) for a, b in s.gtab_reads]}; bank1 {tuple(map(hex, s.bank1_read))}; "
          f"song entry at {s.song_entry_at:#x} -> {tuple(map(hex, s.song_read))}")
    print(f"object map: {sum(1 for b in s.omap if b != 0xFF)} cells set; events {s.events}")


# ---- the oracle ----------------------------------------------------------------------------------------------------

LOOP_END = 0x11C17        # the instruction after the record loop
LOCALS = {                # name: (bp offset of the far pointer, length)
    "gmap": (-0x54, 25600), "omap": (-0x92, 12800), "tabA": (-8, STATE + NVILL), "tabB": (-0xA8, 520),
    "lst": (-0x1C, 200), "trail": (-0xC8, 39), "tiles": (-0xA2, 16000), "sprites": (-0x6E, 0x3E00),
    "blockX": (-0x88, 240), "blockY": (-0xD0, 240), "bank1": (-0x18, 0x18A0),
}


def oracle(data, ticks=60):
    """Run WALK.SL on the files in `data`, stop at the end of the record loop, return {name: bytes} and m."""
    import dosemu
    from dosemu import Emu, VGA
    from unicorn import UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_BP, UC_X86_REG_SS
    import tempfile
    work = Path(tempfile.mkdtemp(prefix="walkscene_", dir="work"))
    try:
        for p in Path(data).iterdir():
            if p.is_file():
                shutil.copy2(p, work / p.name)
        emu = Emu("work/unpacked/WALK.SL.exe", work, 250_000, False)
        raw = Path("work/check_credits/start.raw").read_bytes()
        emu.pal[:] = raw[-768:]
        emu.uc.mem_write(VGA, raw[:64000])
        got = {}

        def at_end(uc, address, size, user):
            bp = uc.reg_read(UC_X86_REG_BP)
            ss = uc.reg_read(UC_X86_REG_SS)

            def local(off, n):
                return bytes(uc.mem_read(ss * 16 + ((bp + off) & 0xFFFF), n))
            for name, (off, ln) in LOCALS.items():
                p = struct.unpack("<HH", local(off, 4))
                got[name] = bytes(uc.mem_read(((p[1] << 4) + p[0]) & 0xFFFFF, ln))
            got["m"] = local(-0x3A, 1)[0]
            emu.stop_reason = "loader done"
            uc.emu_stop()
        emu.uc.hook_add(UC_HOOK_CODE, at_end, begin=LOOP_END, end=LOOP_END)
        emu.run(ticks, {})
        return got, emu.stop_reason
    finally:
        shutil.rmtree(work, ignore_errors=True)


def compare(data, mi=None, now=None, quiet=False):
    """Return (status, list of differences)."""
    try:
        s = load(data, mi, now)
    except OOB as e:
        return f"skip(out of range write {e})", []
    if s.events:
        return "skip(event)", []
    got, why = oracle(data)
    if "tabA" not in got:
        return f"skip(no loader end: {why})", []
    diffs = []
    mine = {"gmap": s.gmap, "omap": s.omap, "tabA": s.tabA, "tabB": s.tabB, "lst": s.lst, "trail": s.trail,
            "tiles": s.tiles, "sprites": s.sprites, "blockX": s.blockX, "blockY": s.blockY, "bank1": s.bank1}
    for k, v in mine.items():
        g = got[k]
        v = bytes(v)
        n = min(len(v), len(g))
        if k in ("trail", "lst", "gmap", "omap", "tabA", "tabB") and len(v) != len(g):
            diffs.append(f"{k}: model has {len(v)} bytes, oracle {len(g)}")
        if g[:n] != v[:n]:
            bad = [i for i in range(n) if g[i] != v[i]]
            diffs.append(f"{k}: {len(bad)} bytes differ, first at {bad[0]} (oracle {g[bad[0]]:#x}, model {v[bad[0]]:#x})")
    if got["m"] != s.m:
        diffs.append(f"m: oracle {got['m']}, model {s.m}")
    return f"m={got['m']}", diffs


def cmd_check(data):
    st, d = compare(data)
    print("status:", st)
    for x in d:
        print("  DIFF", x)
    print("identical" if not d and not st.startswith("skip") else "see above")
    return 1 if d else 0


def make_variant(rng, dst, base=DATA):
    """Copy the files and randomise the state: scene number, hero position, flags, party items/status, and some records."""
    for p in base.iterdir():
        if p.is_file():
            shutil.copy2(p, dst / p.name)
    mi = bytearray((base / "MAN_INFO.NOW").read_bytes())
    now = bytearray((base / "__1.NOW").read_bytes())
    rpg = bytearray((base / "RPG.LCP").read_bytes())
    rpg[0] = rng.randrange(0, 5)
    (dst / "RPG.LCP").write_bytes(bytes(rpg))
    scene = rng.randrange(0, 53)
    mi[500] = scene
    mi[502], mi[503] = rng.randrange(0, 128), rng.randrange(0, 50)
    density = rng.choice([0.0, 0.05, 0.3, 0.7, 1.0])
    for i in range(200, 500):
        v = 0
        for b in range(8):
            if rng.random() < density:
                v |= 1 << b
        mi[i] = v
    for di in range(0, 200, 40):
        if rng.random() < 0.5:
            mi[di + 3] = rng.randrange(0, 14)
        for b in range(0x10, 0x2A):          # a little outside the 20 item bytes, to catch off-by-one ranges
            if rng.random() < 0.4:
                mi[di + b] = rng.choice([0, 1, 2, 3, 5, 9, 17, 30, 80, 200, 255])
    # make records hit more often: random item ids in the 0x0b records, random small values in others
    base0 = scene * STATE + 1000
    for i in range(NSREC):
        o = base0 + SREC * i
        if o + SREC > len(now):
            continue
        if rng.random() < 0.3:
            now[o + 2] = rng.choice([0x02, 0x0A, 0x0B, 0x0C, 0x0D, 0x0F])
            for k in (3, 4, 6, 7, 8, 11, 12, 13, 14):
                if rng.random() < 0.5:
                    now[o + k] = rng.randrange(0, 8) if rng.random() < 0.7 else rng.randrange(0, 256)
            if now[o + 2] in (0x02, 0x0A, 0x0C):
                # keep the map offsets inside the maps
                now[o] = rng.randrange(0, 126)
                now[o + 1] = rng.randrange(0, 98)
            if now[o + 2] == 0x0D:
                put16(now, o + 5, rng.randrange(1000, 1700))
            if now[o + 2] == 0x0F:
                for k in (5, 7, 9, 11, 13):
                    if rng.random() < 0.6:
                        put16(now, o + k, rng.choice([0, 1, 5, 0x100, 0x225, 0x226, 0x227, 0x300, 0xFFFF]))
            if now[o + 2] == 0x0B:
                for k in (3, 6, 9, 12):          # item numbers that the fuzzed party records hold
                    now[o + k] = rng.choice([0, 1, 2, 3, 5, 9, 17, 30, 80, 200, 255])
    (dst / "MAN_INFO.NOW").write_bytes(bytes(mi))
    (dst / "__1.NOW").write_bytes(bytes(now))
    return scene


def cmd_fuzz(n, seed):
    import tempfile
    rng = random.Random(seed)
    ok = bad = skipped = 0
    kinds = {}
    for it in range(n):
        d = Path(tempfile.mkdtemp(prefix="walkvar_", dir="work"))
        try:
            scene = make_variant(rng, d)
            st, diffs = compare(d)
            if st.startswith("skip"):
                skipped += 1
                print(f"[{it}] scene {scene}: {st}")
                continue
            if diffs:
                bad += 1
                print(f"[{it}] scene {scene}: DIFF")
                for x in diffs:
                    print("    ", x)
            else:
                ok += 1
                print(f"[{it}] scene {scene}: identical ({st})")
        finally:
            shutil.rmtree(d, ignore_errors=True)
    print(f"{ok} identical, {bad} different, {skipped} skipped")
    return 1 if bad else 0


def model_blob(data):
    """The model's state as bytes in the layout of the C port's walk_scene_dump (src/walkscene.h): gmap, omap, tabA, tabB,
    list, trail, tiles, sprites, bank1, bank2, then a text line.  None if a record writes outside its map (the C
    loader refuses those too)."""
    try:
        s = load(data)
    except (OOB, ValueError):
        return None
    blob = (bytes(s.gmap) + bytes(s.omap) + bytes(s.tabA) + bytes(s.tabB) + bytes(s.lst) + bytes(s.trail) +
            bytes(s.tiles) + bytes(s.sprites) + bytes(s.bank1) + bytes(s.bank2))
    return blob + f"scene {s.scene} events {len(s.events)} speed {s.m} start {s.start[0]} {s.start[1]}\n".encode()


def dump(data, out):
    """Write model_blob to a file.  Exit status 3 if the model refuses the state."""
    blob = model_blob(data)
    if blob is None:
        print("model refuses")
        return 3
    Path(out).write_bytes(blob)
    return 0


def cmd_cross(cbin, n, seed):
    """The C port's loader against this model: all 53 scene numbers on the shipped files, then n randomised state files.
    The C binary is `build/walk_dump` (make build/walk_dump).  Both must give the same bytes, or both must refuse."""
    import os
    import subprocess
    import tempfile
    rng = random.Random(seed)
    same = diff = refused = 0
    for it in range(53 + n):
        d = Path(tempfile.mkdtemp(prefix="walkx_", dir="work"))
        try:
            if it < 53:
                for p in DATA.iterdir():
                    if p.is_file() and p.name != "MAN_INFO.NOW":
                        os.symlink(p.resolve(), d / p.name)
                mi = bytearray((DATA / "MAN_INFO.NOW").read_bytes())
                mi[500] = it
                (d / "MAN_INFO.NOW").write_bytes(bytes(mi))
                label = f"scene {it}"
            else:
                label = f"variant {it - 53} (scene {make_variant(rng, d)})"
            want = model_blob(d)
            out = d / "c.bin"
            r = subprocess.run([cbin, str(d), "scene", str(out)], capture_output=True)
            if want is None:
                if r.returncode == 1:
                    refused += 1
                else:
                    diff += 1
                    print(f"{label}: the model refuses, the C port exit status {r.returncode}")
            elif r.returncode != 0:
                diff += 1
                print(f"{label}: the C port failed (exit {r.returncode}): {r.stderr.decode().strip()}")
            elif out.read_bytes() != want:
                diff += 1
                got = out.read_bytes()
                bad = next((i for i in range(min(len(got), len(want))) if got[i] != want[i]), min(len(got), len(want)))
                print(f"{label}: DIFFERENT, sizes {len(got)} / {len(want)}, first byte {bad}")
            else:
                same += 1
        finally:
            shutil.rmtree(d, ignore_errors=True)
    print(f"{same} identical, {diff} different, {refused} refused by both")
    return 1 if diff else 0


def cmd_scenes():
    """Every scene number 0-52 with the shipped state files (only MAN_INFO.NOW byte 500 changes), compared."""
    import tempfile
    ok = bad = skipped = 0
    for scene in range(53):
        d = Path(tempfile.mkdtemp(prefix="walkscn_", dir="work"))
        try:
            for p in DATA.iterdir():
                if p.is_file():
                    shutil.copy2(p, d / p.name)
            mi = bytearray((DATA / "MAN_INFO.NOW").read_bytes())
            mi[500] = scene
            (d / "MAN_INFO.NOW").write_bytes(bytes(mi))
            st, diffs = compare(d)
            s = None if st.startswith("skip") else load(d)
            cells = "" if s is None else f"{sum(1 for b in s.omap if b != 0xFF)} object cells"
            print(f"scene {scene:2d}: {'DIFF' if diffs else st} {cells}")
            for x in diffs:
                print("    ", x)
            if st.startswith("skip"):
                skipped += 1
            elif diffs:
                bad += 1
            else:
                ok += 1
        finally:
            shutil.rmtree(d, ignore_errors=True)
    print(f"{ok} identical, {bad} different, {skipped} skipped")
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=["show", "check", "fuzz", "scenes", "dump", "cross"])
    ap.add_argument("arg", nargs="?")
    ap.add_argument("seed", nargs="?", default="1")   # the fuzz seed, or the output file of `dump`, or N of `cross`
    ap.add_argument("seed2", nargs="?", default="1")  # the seed of `cross`
    a = ap.parse_args()
    if a.cmd == "show":
        show(Path(a.arg) if a.arg else DATA)
    elif a.cmd == "cross":
        sys.exit(cmd_cross(a.arg or "build/walk_dump", int(a.seed) if a.seed != "1" else 40, int(a.seed2)))
    elif a.cmd == "dump":
        sys.exit(dump(Path(a.arg), a.seed))
    elif a.cmd == "scenes":
        sys.exit(cmd_scenes())
    elif a.cmd == "check":
        sys.exit(cmd_check(Path(a.arg) if a.arg else DATA))
    else:
        sys.exit(cmd_fuzz(int(a.arg or 20), int(a.seed)))


if __name__ == "__main__":
    main()
