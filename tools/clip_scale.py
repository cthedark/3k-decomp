#!/usr/bin/env python3
"""Is a capture's time base true? Fit a clip of known length at several time scales.

Usage: tools/clip_scale.py CAPTURE.raw CLIP.wav START_S [--rate 48000]

START_S is a rough start time of one play of the clip in the capture (from tools/find_clip.py).
The clip's loudness envelope is stretched by factors from 0.94 to 1.06 and compared (Pearson
correlation) with the capture's envelope, trying start times within +-1.5 s of START_S. The factor
with the best score says by how much the capture's clock runs relative to the clip's own nominal
length (1.000 = the capture plays the clip at the nominal speed, so its clock is true). Also prints
the scores at the ends of the range so the sharpness of the peak can be judged. Needs numpy.
Reads both files, writes nothing.
"""
import sys
import wave

import numpy as np

STEP = 0.02


def env(x, rate, step=STEP):
    n = int(step * rate)
    m = len(x) // n
    return np.sqrt((x[: m * n].reshape(m, n) ** 2).mean(axis=1))


def main(argv):
    args = [a for a in argv[1:] if not a.startswith("--")]
    rate = 48000
    if "--rate" in argv:
        rate = int(argv[argv.index("--rate") + 1])
        args.remove(argv[argv.index("--rate") + 1])
    if len(args) != 3:
        raise SystemExit(__doc__)
    raw = np.fromfile(args[0], dtype="<i2")
    cap = raw[: len(raw) // 2 * 2].reshape(-1, 2).astype(np.float64).mean(axis=1)
    w = wave.open(args[1])
    clip = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").astype(np.float64)
    if w.getnchannels() == 2:
        clip = clip.reshape(-1, 2).mean(axis=1)
    ce = env(clip, w.getframerate())
    pe = env(cap, rate)
    start = float(args[2])
    best_all = None
    rows = []
    for scale in np.arange(0.94, 1.0601, 0.005):
        m = int(round(len(ce) * scale))
        xs = np.linspace(0, len(ce) - 1, m)
        stretched = np.interp(xs, np.arange(len(ce)), ce)
        best = None
        for off in np.arange(-1.5, 1.5001, STEP):
            i0 = int(round((start + off) / STEP))
            if i0 < 0 or i0 + m > len(pe):
                continue
            seg = pe[i0:i0 + m]
            if seg.std() == 0:
                continue
            c = np.corrcoef(stretched, seg)[0, 1]
            if best is None or c > best[0]:
                best = (c, start + off)
        if best:
            rows.append((scale, best[0], best[1]))
    top = max(rows, key=lambda r: r[1])
    print("clip nominal length %.3f s; capture rate %d Hz" % (len(clip) / w.getframerate(), rate))
    for scale, c, t in rows:
        mark = "  <-- best" if (scale, c, t) == top else ""
        if abs(scale - round(scale / 0.01) * 0.01) < 1e-6 or mark:
            print("  scale %.3f: score %.4f (start %.2f s)%s" % (scale, c, t, mark))


if __name__ == "__main__":
    main(sys.argv)
