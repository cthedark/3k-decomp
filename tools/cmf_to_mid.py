#!/usr/bin/env python3
"""Extract the songs of MUC.ALL as standard MIDI files (format 0).

Usage: tools/cmf_to_mid.py MUC.ALL OUTDIR [--song N] [--no-verify]

A CMF song is a MIDI track body plus OPL instrument patches and a clock in ticks per second. The
files written here keep every channel event at its exact tick, and set the tempo so that the
playing time equals the CMF's: division = ticks per quarter note (header), tempo = 1,000,000 x
ticks-per-quarter / ticks-per-second microseconds per quarter note.

What is NOT carried over: the sound. The patches are OPL (FM) instruments, which MIDI cannot hold;
a player will use its own General MIDI sounds. The 16-byte patches are kept, unchanged, in one
sequencer-specific meta event at tick 0 (id 0x7D, tag "CMFPATCH", count, then the patches) so the
information is not lost; players ignore it. Also: MIDI channel 9 is percussion in General MIDI
players, whatever the song meant by it; the report lists the songs that use it.

Every file is read back by an independent MIDI reader (written separately below, sharing no code
with the writer) and compared event by event with the source, unless --no-verify is given.
Files are derived from the game's data: write them under work/ (git-ignored), never commit them.
Standard library only; reads MUC.ALL, writes only into OUTDIR.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cmf_survey import parse, events  # noqa: E402  (the reader src/cmf.c was checked against)


def split_songs(data):
    """Offsets of the CTMF songs; each runs to the next one (docs/formats.md)."""
    offs, p = [], 0
    while True:
        p = data.find(b"CTMF", p)
        if p < 0:
            break
        offs.append(p)
        p += 4
    return [(o, (offs[i + 1] if i + 1 < len(offs) else len(data))) for i, o in enumerate(offs)]


def vlq(n):
    out = [n & 0x7F]
    n >>= 7
    while n:
        out.append((n & 0x7F) | 0x80)
        n >>= 7
    return bytes(reversed(out))


KIND_STATUS = {"off": 0x80, "on": 0x90, "poly": 0xA0, "ctl": 0xB0, "prog": 0xC0, "chanpress": 0xD0, "bend": 0xE0}


def song_events(song):
    """[(tick, status, data bytes)] for channel events, and the tick of the end marker."""
    evs, end = [], 0
    for t, kind, a in events(song["body"]):
        if kind == "meta":
            end = t
            continue
        if kind == "sysex":
            continue
        evs.append((t, KIND_STATUS[kind] | a[0], bytes(a[1:])))
        end = max(end, t)
    return evs, end


def build_mid(song, name):
    tpq, tps = song["tpq"], song["tps"]
    if not (0 < tpq < 0x8000) or tps <= 0:
        raise ValueError("bad timing in the header: tpq=%d tps=%d" % (tpq, tps))
    tempo = round(1000000 * tpq / tps)
    evs, end = song_events(song)
    trk = bytearray()
    trk += vlq(0) + b"\xff\x03" + vlq(len(name)) + name.encode("ascii")
    trk += vlq(0) + b"\xff\x51\x03" + tempo.to_bytes(3, "big")
    patches = b"".join(bytes(i) for i in song["insts"])
    blob = b"\x7d" + b"CMFPATCH" + struct.pack("<H", len(song["insts"])) + patches
    trk += vlq(0) + b"\xff\x7f" + vlq(len(blob)) + blob
    last = 0
    for t, status, data in evs:
        trk += vlq(t - last) + bytes([status]) + data
        last = t
    trk += vlq(max(end, last) - last) + b"\xff\x2f\x00"
    head = b"MThd" + struct.pack(">IHHH", 6, 0, 1, tpq)
    return head + b"MTrk" + struct.pack(">I", len(trk)) + bytes(trk), tempo, evs, max(end, last)


# ---- the independent reader used for verification (does not use any writer code) ----------------

def read_back(mid):
    """Parse a format-0 file: (division, tempo list, channel events [(tick, status, data)], end tick,
    other meta events [(tick, type, payload)])."""
    assert mid[:4] == b"MThd" and struct.unpack(">I", mid[4:8])[0] == 6
    fmt, ntrk, div = struct.unpack(">HHH", mid[8:14])
    assert (fmt, ntrk) == (0, 1), (fmt, ntrk)
    assert mid[14:18] == b"MTrk"
    length = struct.unpack(">I", mid[18:22])[0]
    body = mid[22:22 + length]
    assert len(mid) == 22 + length, "trailing bytes"
    pos, tick, status = 0, 0, 0
    chan, metas = [], []
    while pos < len(body):
        delta = 0
        while True:
            b = body[pos]
            pos += 1
            delta = (delta << 7) | (b & 0x7F)
            if not b & 0x80:
                break
        tick += delta
        b = body[pos]
        if b == 0xFF:
            typ = body[pos + 1]
            n, q = 0, pos + 2
            while True:
                c = body[q]
                q += 1
                n = (n << 7) | (c & 0x7F)
                if not c & 0x80:
                    break
            metas.append((tick, typ, bytes(body[q:q + n])))
            pos = q + n
            if typ == 0x2F:
                assert pos == len(body), "data after the end of track"
                return div, metas, chan, tick
            continue
        assert b & 0x80, "running status not expected in these files"
        status = b
        n = 1 if (status & 0xF0) in (0xC0, 0xD0) else 2
        chan.append((tick, status, bytes(body[pos + 1:pos + 1 + n])))
        pos += 1 + n
    raise AssertionError("no end of track")


def verify(mid, song, evs, end):
    div, metas, chan, endtick = read_back(mid)
    assert div == song["tpq"], "division"
    tempos = [m for m in metas if m[1] == 0x51]
    assert len(tempos) == 1 and tempos[0][0] == 0
    us = int.from_bytes(tempos[0][2], "big")
    assert abs(us - 1000000 * song["tpq"] / song["tps"]) <= 0.5, "tempo"
    assert chan == evs, "channel events differ"
    assert endtick == end, "end tick %d != %d" % (endtick, end)
    blob = [m for m in metas if m[1] == 0x7F][0][2]
    assert blob[:9] == b"\x7dCMFPATCH" and struct.unpack("<H", blob[9:11])[0] == len(song["insts"])
    assert blob[11:] == b"".join(bytes(i) for i in song["insts"]), "patches"
    # playing time: ticks / ticks-per-second, independent of the MIDI tempo arithmetic
    return endtick / song["tps"], us


def main(argv):
    args = [a for a in argv[1:] if not a.startswith("--")]
    only = int(argv[argv.index("--song") + 1]) if "--song" in argv else None
    if only is not None:
        args.remove(argv[argv.index("--song") + 1])
    if len(args) != 2:
        raise SystemExit(__doc__)
    src, outdir = args
    data = open(src, "rb").read()
    songs = split_songs(data)
    print("%s: %d songs" % (src, len(songs)))
    os.makedirs(outdir, exist_ok=True)
    total_notes = 0
    for i, (a, b) in enumerate(songs):
        if only is not None and i != only:
            continue
        song = parse(data[a:b])
        name = "MUC.ALL song %d at 0x%x" % (i, a)
        mid, tempo, evs, end = build_mid(song, name)
        notes = sum(1 for _, s, d in evs if s & 0xF0 == 0x90 and d[1] > 0)
        chans = sorted({s & 0x0F for _, s, _ in evs})
        secs, us = (verify(mid, song, evs, end) if "--no-verify" not in argv else (end / song["tps"], tempo))
        path = os.path.join(outdir, "song%02d_%05x.mid" % (i, a))
        with open(path, "wb") as f:
            f.write(mid)
        total_notes += notes
        warn = "  uses channel 9 (percussion in GM players)" if 9 in chans else ""
        print("  %-26s %6d bytes  %4d notes  %6.1f s  tpq %2d  tempo %d us/q  patches %2d  channels %s%s"
              % (os.path.basename(path), len(mid), notes, secs, song["tpq"], us, len(song["insts"]),
                 ",".join(map(str, chans)), warn))
    print("%d notes in total" % total_notes)


if __name__ == "__main__":
    main(sys.argv)
