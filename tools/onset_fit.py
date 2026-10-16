#!/usr/bin/env python3
"""Fit a time scale and offset that map the port's rendering of a song onto a recording of the original.

Usage: tools/onset_fit.py CAPTURE.raw RENDER.wav [--rate 48000] [--cap-from 25] [--cap-to 100]
                          [--ren-to 50] [--scale-lo 0.90] [--scale-hi 1.10] [--tol 0.03]

Note onsets (sudden rises of energy between 100 Hz and 3 kHz) are detected in both signals. For each
candidate (scale, offset), where capture time = offset + scale * render time, the number of capture
onsets that fall within TOL seconds of a predicted onset is counted. Prints the best candidates and,
for reference, the best count at scale 1.000 and at the edges of the range. A sharp peak in scale
with a count far above the reference means the two really are the same music at that relative speed.
Needs numpy. Reads both files, writes nothing.
"""
import sys

import numpy as np

sys.path.insert(0, __file__.rsplit("/", 1)[0])
import compare_music as cm  # noqa: E402


def onsets(x, rate, t_from, t_to, hop=0.01, win=0.04):
    n, h = int(win * rate), int(hop * rate)
    w = np.hanning(n)
    prev, flux, times = None, [], []
    s = int(t_from * rate)
    while s + n < min(len(x), int(t_to * rate)):
        mag = np.sqrt(np.abs(np.fft.rfft(x[s:s + n] * w))[int(100 * win):int(3000 * win)])
        if prev is not None:
            flux.append(np.maximum(mag - prev, 0).sum())
            times.append(s / rate)
        prev = mag
        s += h
    flux, times = np.array(flux), np.array(times)
    thr = flux.mean() + 1.5 * flux.std()
    return np.array([times[i] for i in range(2, len(flux) - 2) if flux[i] > thr and flux[i] == flux[i - 2:i + 3].max()])


def main(argv):
    args = [a for a in argv[1:] if not a.startswith("--")]
    opt = {}
    for k in ("--rate", "--cap-from", "--cap-to", "--ren-to", "--scale-lo", "--scale-hi", "--tol"):
        if k in argv:
            opt[k] = argv[argv.index(k) + 1]
            args.remove(opt[k])
    if len(args) != 2:
        raise SystemExit(__doc__)
    rate = int(opt.get("--rate", 48000))
    c0, c1 = float(opt.get("--cap-from", 25)), float(opt.get("--cap-to", 100))
    r1 = float(opt.get("--ren-to", 50))
    lo, hi = float(opt.get("--scale-lo", 0.90)), float(opt.get("--scale-hi", 1.10))
    tol = float(opt.get("--tol", 0.03))
    cap, _ = cm.load_capture(args[0])
    ren, rr = cm.load_wav(args[1])
    oc = onsets(cap, rate, c0, c1)
    orr = onsets(ren, rr, 0, r1)
    print("onsets: render %d (0..%.0f s), capture %d (%.0f..%.0f s)" % (len(orr), r1, len(oc), c0, c1))
    res = []
    for scale in np.arange(lo, hi + 1e-9, 0.001):
        for off in np.arange(c0 - 2, c0 + 12, 0.01):
            pred = orr * scale + off
            res.append((sum(1 for t in oc if np.abs(pred - t).min() <= tol), scale, off))
    res.sort(reverse=True)
    print("best (scale, offset) by capture onsets matched within %.0f ms:" % (tol * 1000))
    for h, s, o in res[:5]:
        inrange = sum(1 for t in oc if o <= t <= o + r1 * s)
        print("  scale %.3f offset %.2f s: %d matched of %d capture onsets in range" % (s, o, h, inrange))
    for ref in (1.0, lo, hi):
        near = [h for h, s, o in res if abs(s - ref) < 0.003]
        print("  best near scale %.3f: %d" % (ref, max(near) if near else -1))


if __name__ == "__main__":
    main(sys.argv)
