#!/usr/bin/env python3
"""How much do two OPL emulations of the SAME original differ, under the similarity score used here?

Usage: tools/emu_baseline.py A.raw B.raw [C.raw ...] --render RENDER.wav --scale S --offset O
                             [--from 3] [--to 14] [--rate 48000]

Each .raw is a capture of the same original program (the title, whose first seconds play the intro
song) made with a different DOSBox-X OPL core (tools/capture_original_audio.sh, CAP_OPLEMU=...). They
start at the same emulated moment, so captures are compared with each other at offset 0.
The port's render is compared with each capture using capture time = OFFSET + SCALE * render time
(from tools/onset_fit.py). The score is the one of tools/pitch_check.py at pitch shift 0: the mean
cosine similarity of log-frequency spectra (0.5 s windows every 0.5 s between FROM and TO seconds).
If the render scores about as well against a capture as the captures do against each other, the
remaining difference is what different chip emulations cause, not necessarily a mapping fault.
Needs numpy. Reads the files, writes nothing.
"""
import sys

import numpy as np

sys.path.insert(0, __file__.rsplit("/", 1)[0])
import compare_music as cm  # noqa: E402
import pitch_check as pc  # noqa: E402


def mean_score(x, rx, tx0, sx, y, ry, ty0, sy, t_from, t_to):
    """Mean log-spectrum cosine at 0 shift; signal x at time tx0 + sx*t, y at ty0 + sy*t."""
    out = []
    t = t_from
    while t < t_to:
        a = pc.logspec(x, rx, tx0 + sx * t)
        b = pc.logspec(y, ry, ty0 + sy * t)
        out.append(pc.shift_score(a, b, 0))
        t += 0.5
    return float(np.mean(out)), float(np.min(out))


def main(argv):
    args = [a for a in argv[1:] if not a.startswith("--")]
    opt = {}
    for k in ("--render", "--scale", "--offset", "--from", "--to", "--rate"):
        if k in argv:
            opt[k] = argv[argv.index(k) + 1]
            args.remove(opt[k])
    if len(args) < 2 or "--render" not in opt:
        raise SystemExit(__doc__)
    rate = int(opt.get("--rate", 48000))
    scale, off = float(opt.get("--scale", 1.0)), float(opt.get("--offset", 0.0))
    t0, t1 = float(opt.get("--from", 3)), float(opt.get("--to", 14))
    caps = [(p, cm.load_capture(p)[0]) for p in args]
    ren, rr = cm.load_wav(opt["--render"])
    # when does the music start in each capture? first second with energy
    starts = []
    for p, c in caps:
        e = np.sqrt((c[: len(c) // rate * rate].reshape(-1, rate) ** 2).mean(axis=1))
        starts.append(int(np.argmax(e > 100)))
    print("music starts (whole second) in the captures:", dict(zip([p.rsplit('/', 1)[-1] for p, _ in caps], starts)))
    print("\ncapture against capture (same moment of the same program), %.0f..%.0f s of the song:" % (t0, t1))
    for i in range(len(caps)):
        for j in range(i + 1, len(caps)):
            m, lo = mean_score(caps[i][1], rate, 0.0 + 0, 1.0, caps[j][1], rate, 0.0, 1.0, t0 + starts[i], t1 + starts[i])
            print("  %-16s vs %-16s mean %.3f  worst window %.3f" % (caps[i][0].rsplit('/', 1)[-1], caps[j][0].rsplit('/', 1)[-1], m, lo))
    print("\nrender against each capture (scale %.3f, offset %.2f):" % (scale, off))
    for (p, c), st in zip(caps, starts):
        m, lo = mean_score(ren, rr, 0.0, 1.0, c, rate, off, scale, t0, t1)
        print("  render vs %-16s mean %.3f  worst window %.3f" % (p.rsplit('/', 1)[-1], m, lo))


if __name__ == "__main__":
    main(sys.argv)
