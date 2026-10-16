#!/usr/bin/env python3
"""Facts about how a CMF song uses its channels, for deciding how a player must behave.

Usage: tools/cmf_behaviour.py FILE OFFSET LENGTH
Per MIDI channel: notes, programs, overlapping note-ons (a note-on while that channel already holds
a note), note-offs that match nothing held, zero-length notes (on and off at the same tick),
velocity range; then the events that are neither note, program nor controller 0x67.
Reads only; writes nothing. Uses the reader in cmf_survey.py.
"""
import sys
from collections import defaultdict

from cmf_survey import parse, events


def main(argv):
    path, off, length = argv[1], int(argv[2], 0), int(argv[3], 0)
    with open(path, "rb") as f:
        f.seek(off)
        d = f.read(length)
    s = parse(d)
    held = {}                       # channel -> (note, tick of the note-on)
    ch_stats = defaultdict(lambda: defaultdict(int))
    progs = defaultdict(list)
    vel = defaultdict(lambda: [999, -1])
    other = defaultdict(int)
    ctls = defaultdict(set)
    for t, kind, a in events(s["body"]):
        if kind == "meta" or kind == "sysex":
            continue
        ch = a[0]
        if kind == "on" and a[2] > 0:
            st = ch_stats[ch]
            st["notes"] += 1
            if ch in held:
                st["overlap_on"] += 1
            held[ch] = (a[1], t)
            vel[ch][0] = min(vel[ch][0], a[2])
            vel[ch][1] = max(vel[ch][1], a[2])
        elif kind == "off" or (kind == "on" and a[2] == 0):
            st = ch_stats[ch]
            if ch in held and held[ch][0] == a[1]:
                if held[ch][1] == t:
                    st["zero_length"] += 1
                del held[ch]
            else:
                st["unmatched_off"] += 1
        elif kind == "prog":
            progs[ch].append((t, a[1]))
        elif kind == "ctl":
            ctls[ch].add((a[1], a[2]))
        else:
            other[kind] += 1
    for ch in sorted(ch_stats):
        st = ch_stats[ch]
        print(f"ch {ch:2d}: notes={st['notes']:4d} overlap_on={st['overlap_on']} unmatched_off={st['unmatched_off']} "
              f"zero_length={st['zero_length']} vel={vel[ch][0]}..{vel[ch][1]} "
              f"programs={sorted({p for _, p in progs[ch]})} first_prog_tick={progs[ch][0][0] if progs[ch] else None}")
    print("held at end:", dict(held))
    print("controllers (ctl,value) by channel:", {c: sorted(v) for c, v in ctls.items()})
    print("other events:", dict(other))
    for i, ins in enumerate(s["insts"]):
        print(f"  patch {i}: {ins[:11].hex(' ')}")


if __name__ == "__main__":
    main(sys.argv)
