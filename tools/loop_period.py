#!/usr/bin/env python3
"""Find the repeat period of looping music in a recording, by matching it against itself.

Usage: tools/loop_period.py CAPTURE.raw [--from 40] [--min-lag 100] [--max-lag 160] [--step 0.02]
                            [--rate 48000]

For every lag L in [MIN-LAG, MAX-LAG] (step STEP seconds) the spectra of windows at t and t+L are
compared by cosine similarity (80 Hz..4 kHz, square-root magnitudes), averaged over the windows that
fit in the recording starting at FROM seconds (one window per second). A real loop shows one sharp
peak far above the background. Prints the best lags, the background level, and the peak's
sharpness. Needs numpy. Reads the file; writes nothing. Use with a recording of OPL music only.
"""
import sys

import numpy as np

WIN = 0.5


def main(argv):
    args = [a for a in argv[1:] if not a.startswith("--")]
    opt = {}
    for k in ("--from", "--min-lag", "--max-lag", "--step", "--rate"):
        if k in argv:
            opt[k] = argv[argv.index(k) + 1]
            args.remove(opt[k])
    if len(args) != 1:
        raise SystemExit(__doc__)
    rate = int(opt.get("--rate", 48000))
    t_from = float(opt.get("--from", 40))
    lo, hi = float(opt.get("--min-lag", 100)), float(opt.get("--max-lag", 160))
    step = float(opt.get("--step", 0.02))
    x = np.fromfile(args[0], dtype="<i2")
    x = x[: len(x) // 2 * 2].reshape(-1, 2).astype(np.float64).mean(axis=1)
    total = len(x) / rate
    n = int(WIN * rate)
    k0, k1 = int(80 * WIN), int(4000 * WIN)
    hann = np.hanning(n)

    def feat(t):
        s = int(round(t * rate))
        return np.sqrt(np.abs(np.fft.rfft(x[s:s + n] * hann))[k0:k1])

    results = []
    lag = lo
    while lag <= hi:
        ts = np.arange(t_from, total - lag - WIN, 1.0)
        if len(ts) < 5:
            break
        sims = []
        for t in ts:
            a, b = feat(t), feat(t + lag)
            sims.append(float(a @ b / (np.linalg.norm(a) * np.linalg.norm(b) + 1e-12)))
        results.append((lag, float(np.mean(sims)), len(ts)))
        lag += step
    res = sorted(results, key=lambda r: -r[1])
    print("recording %.1f s; windows at t and t+lag, one per second from %.0f s" % (total, t_from))
    print("best lags:")
    shown = []
    for lag, s, c in res:
        if all(abs(lag - l) > 0.5 for l, _ in shown):
            shown.append((lag, s))
            print("  lag %.2f s: similarity %.3f (%d windows)" % (lag, s, c))
        if len(shown) >= 4:
            break
    allsim = np.array([r[1] for r in results])
    print("background: median %.3f, 90th percentile %.3f, max %.3f" % (np.median(allsim), np.percentile(allsim, 90), allsim.max()))
    best = res[0][0]
    side = [s for l, s, _ in results if 0.4 < abs(l - best) < 0.6]
    print("peak sharpness: %.3f at the best lag; %.3f at +-0.5 s" % (res[0][1], np.mean(side) if side else float("nan")))


if __name__ == "__main__":
    main(sys.argv)
