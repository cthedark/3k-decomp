#!/usr/bin/env python3
"""Survey one Creative Music Format (CMF) song inside MUC.ALL.

CMF = a header, an optional 16-byte-per-instrument OPL patch block, and a MIDI track body.
Format notes: docs/formats.md ("CMF songs in MUC.ALL"); the layout is the public CMF format.

Usage: tools/cmf_survey.py FILE OFFSET [LENGTH] [--events N] [--instruments]
  FILE OFFSET  a file and the offset of a "CTMF" signature (MUC.ALL song 22 is 0x27137)
Standard library only. Prints facts; writes nothing.
"""
import struct
import sys
from collections import Counter


def read_vlq(d, p):
    v = 0
    while True:
        b = d[p]
        p += 1
        v = (v << 7) | (b & 0x7F)
        if not b & 0x80:
            return v, p


def parse(d):
    if d[:4] != b"CTMF":
        raise SystemExit("no CTMF signature")
    minor, major = d[4], d[5]
    oi, om, tpq, tps, ot, oc, orr = struct.unpack("<7H", d[6:20])
    chans = list(d[20:36])
    if (major, minor) == (1, 1):
        ninst, tempo = struct.unpack("<HH", d[36:40])
    else:
        ninst, tempo = d[36], None
    insts = [d[oi + 16 * i: oi + 16 * i + 16] for i in range(ninst)]
    return dict(ver=(major, minor), oi=oi, om=om, tpq=tpq, tps=tps, chans=chans,
                ninst=ninst, tempo=tempo, insts=insts, body=d[om:])


def events(body):
    """Yield (abs_tick, kind, args) for the MIDI body, with running status."""
    p, t, status = 0, 0, 0
    while p < len(body):
        dt, p = read_vlq(body, p)
        t += dt
        b = body[p]
        if b == 0xFF:                       # meta
            typ = body[p + 1]
            ln, q = read_vlq(body, p + 2)
            yield t, "meta", (typ, bytes(body[q:q + ln]))
            p = q + ln
            if typ == 0x2F:
                return
            continue
        if b in (0xF0, 0xF7):               # sysex
            ln, q = read_vlq(body, p + 1)
            yield t, "sysex", bytes(body[q:q + ln])
            p = q + ln
            continue
        if b & 0x80:
            status = b
            p += 1
        hi, ch = status & 0xF0, status & 0x0F
        n = 1 if hi in (0xC0, 0xD0) else 2
        args = tuple(body[p:p + n])
        p += n
        yield t, {0x80: "off", 0x90: "on", 0xA0: "poly", 0xB0: "ctl", 0xC0: "prog", 0xD0: "chanpress", 0xE0: "bend"}[hi], (ch,) + args


def main(argv):
    args = [a for a in argv[1:] if not a.startswith("--")]
    show_ev = int(argv[argv.index("--events") + 1]) if "--events" in argv else 0
    if "--events" in argv:
        args.remove(argv[argv.index("--events") + 1])
    d = open(args[0], "rb").read()
    off = int(args[1], 0)
    end = off + int(args[2], 0) if len(args) > 2 else len(d)
    s = parse(d[off:end])
    print(f"CMF v{s['ver'][0]}.{s['ver'][1]}  ticks/quarter={s['tpq']}  ticks/second={s['tps']}  "
          f"tempo={s['tempo']}  instruments={s['ninst']}  channels-in-use={sum(s['chans'])}")
    if "--instruments" in argv:
        for i, ins in enumerate(s["insts"]):
            print(f"  inst {i:2d}: {ins.hex(' ')}")
    kinds, progs, ctls, notes_by_ch = Counter(), Counter(), Counter(), Counter()
    last, polyphony, active, maxpoly = 0, 0, {}, 0
    shown = 0
    for t, k, a in events(s["body"]):
        kinds[k] += 1
        last = t
        if k == "prog":
            progs[(a[0], a[1])] += 1
        if k == "ctl":
            ctls[(a[1])] += 1
        if k in ("on", "off"):
            on = k == "on" and a[2] > 0
            key = (a[0], a[1])
            if on:
                active[key] = 1
                notes_by_ch[a[0]] += 1
            else:
                active.pop(key, None)
            maxpoly = max(maxpoly, len(active))
        if shown < show_ev:
            print(f"  t={t:6d} {k} {a}")
            shown += 1
    print("event counts:", dict(kinds))
    print(f"last event at tick {last}  = {last / s['tps']:.2f} s at {s['tps']} ticks/s")
    print("program changes (channel, program):", sorted(progs))
    print("controllers used:", sorted(f"{c:#x}x{n}" for c, n in ctls.items()))
    print("note-ons per channel:", dict(sorted(notes_by_ch.items())))
    print("max simultaneous notes:", maxpoly)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
