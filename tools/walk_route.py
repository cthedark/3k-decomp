#!/usr/bin/env python3
"""Make a key script that walks the hero through given waypoints in scene 0, planned with the Python model of the loop.

Usage: work/venv/bin/python tools/walk_route.py X:Y [X:Y ...] [--max N] [--tick-base B] [--then NPASSES]
Prints the '--key PASS:CODE ...' arguments for tools/walk_idle_trace.py (and the number of passes the walk needs, on stderr).

The route between two waypoints is the shortest path of half-tile positions on which the hero's footprint is walkable (walk_idle.foot_ok,
the map as loaded), found by a breadth-first search from the target.  The model then walks it pass by pass, with the arrow that follows
the path held from the position it is at; so the script is right for the model, and the oracle is asked to do the same (the model has
been compared with the oracle on such scripts, tools/check_walk.sh).  `--then N` is how many passes to add after the last waypoint.
Nothing here knows about scene records: stepping on them is part of what is being tested.
"""
import sys
from collections import deque
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import walk_idle as wi  # noqa: E402

DIRS = ((0x48, 0, -1), (0x50, 0, 1), (0x4B, -1, 0), (0x4D, 1, 0))


def distances(gmap, lst, target):
    dist = {target: 0}
    q = deque([target])
    while q:
        x, y = q.popleft()
        for _, dx, dy in DIRS:
            nx, ny = x + dx, y + dy
            if 0 <= nx < 256 and 0 <= ny < 256 and (nx, ny) not in dist and wi.foot_ok(gmap, lst, nx, ny):
                dist[(nx, ny)] = dist[(x, y)] + 1
                q.append((nx, ny))
    return dist


def plan(waypoints, max_passes=3000, tick_base=20):
    m = wi.Idle(ticks_base=tick_base)
    lst = m.lst
    screen = bytes(64000)
    events, port, n = {}, 0, 0
    for wx, wy in waypoints:
        dist = distances(bytes(m.gmap), lst, (wx, wy))
        stuck = 0
        while (m.tab_b[0], m.tab_b[1]) != (wx, wy):
            if n >= max_passes:
                raise SystemExit(f"no route to {(wx, wy)} within {max_passes} passes (hero at {(m.tab_b[0], m.tab_b[1])})")
            x, y = m.tab_b[0], m.tab_b[1]
            if (x, y) not in dist:
                raise SystemExit(f"the hero is at {(x, y)}, from where {(wx, wy)} cannot be reached")
            want = min(((dist.get((x + dx, y + dy), 10 ** 6), k) for k, dx, dy in DIRS))[1]
            if want != port:
                port = want
                events[n] = port
            before = (x, y)
            screen = m.run_pass(screen, port)
            n += 1
            stuck = stuck + 1 if (m.tab_b[0], m.tab_b[1]) == before else 0
            if stuck > 60:
                raise SystemExit(f"stuck at {before} on the way to {(wx, wy)}")
    return events, n, m


def main(argv):
    wps, mx, base, then = [], 3000, 20, 12
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == "--max":
            mx = int(argv[i + 1])
            i += 1
        elif a == "--tick-base":
            base = int(argv[i + 1])
            i += 1
        elif a == "--then":
            then = int(argv[i + 1])
            i += 1
        else:
            x, y = a.split(":")
            wps.append((int(x), int(y)))
        i += 1
    if not wps:
        raise SystemExit(__doc__)
    events, n, m = plan(wps, mx, base)
    last = events[max(events)] if events else 0
    events[n] = last + 0x80          # the release, once the last waypoint is reached
    print(" ".join(f"--key {p}:{c:#x}" for p, c in sorted(events.items())))
    print(f"{n + then} passes", file=sys.stderr)
    if m.unbuilt:
        print("the model reports:", m.unbuilt[:4], file=sys.stderr)


if __name__ == "__main__":
    main(sys.argv[1:])
