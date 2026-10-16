#!/usr/bin/env python3
"""Compare the OPL register writes of the original's FM driver (tools/dosemu.py --opl-log, or
tools/capture_song_opl.py) with those of the port's music player (build/music_regs). Both files have lines
"tick register value" (hex register and value).

The original's tick counts timer interrupts delivered since the program started; the player's counts song ticks from 0.
They are aligned on the first key-on (a write of 0xb0..0xb8 with bit 5 set, or of 0xbd with a drum bit): the song
tick of that note is the same in both, so the original's clock minus a constant gives the song tick. Writes before
song tick 0 are the program's own set-up and are left out.

Per song tick the lists of writes are compared in order ("identical"); a list that has the same writes in another
order is counted apart ("same writes, other order"). Song tick 0 is reported separately: the driver resets itself there
(default patches for all voices, 0xbd, registers 0x00..0x1a that no chip uses) and the port does not; only the
writes to registers the port also writes are compared.

Usage: tools/compare_opl.py ORIGINAL.txt PLAYER.txt [--ticks N] [--show K]
"""
import argparse
import collections
import sys


# What the original's stop routine writes after the last event of a song (seen in all 25 songs): the release rate of the
# carrier of voice 3 nine times (once per voice), and the depth/rhythm register cleared.
STOP_WRITES = {(0x83, 0x13), (0xbd, 0xc0), (0xbd, 0xe0)}


def load(path):
    out = []
    for line in open(path):
        t, r, v = line.split()
        out.append((int(t), int(r, 16), int(v, 16)))
    return out


def first_key_on(w):
    for t, r, v in w:
        if 0xb0 <= r <= 0xb8 and v & 0x20:
            return t
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("original")
    ap.add_argument("player")
    ap.add_argument("--ticks", type=int, default=0, help="compare song ticks 0..N (default: all the original has)")
    ap.add_argument("--show", type=int, default=12, help="how many differing ticks to print")
    a = ap.parse_args()
    o, p = load(a.original), load(a.player)
    to, tp = first_key_on(o), first_key_on(p)
    if to is None or tp is None:
        print("no key-on in", "the original" if to is None else "the player")
        return 1
    off = to - tp
    print(f"first key-on: original at interrupt {to}, player at song tick {tp}: song tick = interrupt - {off}")
    go, gp = collections.defaultdict(list), collections.defaultdict(list)
    for t, r, v in o:
        if t - off >= 0:
            go[t - off].append((r, v))
    for t, r, v in p:
        gp[t].append((r, v))
    # Only the song itself is compared: the player stops at the song's last event, and what the original's driver does after
    # that (its stop routine, then the restart that CAST.SL's poll makes) is not part of the song.
    last = min(max(go) if go else 0, max(gp) if gp else 0)
    if a.ticks:
        last = min(last, a.ticks)
    same = diff = reorder = 0
    shown = 0
    kinds = collections.Counter()
    port_regs = {r for _, r, _ in p}
    t0o = [x for x in go.get(0, []) if x[0] in port_regs]
    t0p = gp.get(0, [])
    print(f"song tick 0: original {len(go.get(0, []))} writes ({len(t0o)} to registers the player also writes), player {len(t0p)};"
          f" same set: {sorted(t0o) == sorted(t0p)}")
    if sorted(t0o) != sorted(t0p):
        a_, b_ = collections.Counter(t0o), collections.Counter(t0p)
        print("   only in the original:", sorted((a_ - b_).elements())[:12], " only in the player:", sorted((b_ - a_).elements())[:12])
    for t in range(1, last + 1):
        x, y = go.get(t, []), gp.get(t, [])
        if t == last and x[:len(y)] == y and all(w in STOP_WRITES for w in x[len(y):]):
            same += 1     # the driver's stop routine runs in the interrupt of the last event: its writes follow the song's
            continue
        if x == y:
            same += 1
            continue
        if sorted(x) == sorted(y):
            reorder += 1
            continue
        diff += 1
        for r, v in set(x) ^ set(y):
            kinds[f"register 0x{r & 0xe0:02x}.. differs" if r >= 0x20 else f"register 0x{r:02x} differs"] += 1
        if shown < a.show:
            shown += 1
            print(f"tick {t}: original {[f'{r:02x}={v:02x}' for r, v in x]}  player {[f'{r:02x}={v:02x}' for r, v in y]}")
    print(f"song ticks 1..{last}: {same} identical, {reorder} same writes in another order, {diff} different")
    for k, n in kinds.most_common():
        print(f"  {n:6d}  {k}")
    return 0 if diff == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
