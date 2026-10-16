#!/usr/bin/env python3
"""Find where a known clip occurs in a captured recording, by matching loudness envelopes.

Usage: tools/find_clip.py CAPTURE.raw CLIP.wav [--rate 48000] [--channels 2] [--step 0.05] [--min-score 0.8]

Both signals are reduced to an RMS envelope with one value per STEP seconds (default 0.05). For every
offset the Pearson correlation of the clip's envelope with the capture's envelope is computed;
the local maxima above MIN-SCORE are reported as "start second, score". It finds WHERE the clip
plays, not whether it was played exactly: music or other sounds on top lower the score. The score
at a place that does not hold the clip is printed too (the best score elsewhere) as a reference.
Standard library only; reads both files, writes nothing.
"""
import array
import math
import sys
import wave


def envelope(samples, ch, rate, step):
    n = int(step * rate)
    out = []
    for i in range(0, len(samples) // ch - n + 1, n):
        seg = samples[i * ch:(i + n) * ch]
        out.append(math.sqrt(sum(x * x for x in seg) / len(seg)))
    return out


def main(argv):
    args = [a for a in argv[1:] if not a.startswith("--")]
    opt = {}
    for k in ("--rate", "--channels", "--step", "--min-score"):
        if k in argv:
            opt[k] = argv[argv.index(k) + 1]
            args.remove(opt[k])
    if len(args) != 2:
        raise SystemExit(__doc__)
    rate = int(opt.get("--rate", 48000))
    ch = int(opt.get("--channels", 2))
    step = float(opt.get("--step", 0.05))
    min_score = float(opt.get("--min-score", 0.8))
    cap = array.array("h")
    cap.frombytes(open(args[0], "rb").read()[: -1 if len(open(args[0], "rb").read()) % 2 else None])
    w = wave.open(args[1])
    cw = array.array("h")
    cw.frombytes(w.readframes(w.getnframes()))
    cch, crate = w.getnchannels(), w.getframerate()
    ce = envelope(cw, cch, crate, step)
    pe = envelope(cap, ch, rate, step)
    m = len(ce)
    mean_c = sum(ce) / m
    dc = [x - mean_c for x in ce]
    nc = math.sqrt(sum(x * x for x in dc))
    scores = []
    for off in range(0, len(pe) - m + 1):
        seg = pe[off:off + m]
        mean_s = sum(seg) / m
        ds = [x - mean_s for x in seg]
        ns = math.sqrt(sum(x * x for x in ds))
        scores.append(sum(a * b for a, b in zip(dc, ds)) / (nc * ns) if ns > 0 else 0.0)
    print("clip %.2f s, capture %.2f s, envelope step %.2f s" % (m * step, len(pe) * step, step))
    peaks = []
    for i, s in enumerate(scores):
        lo, hi = max(0, i - int(5 / step)), min(len(scores), i + int(5 / step) + 1)
        if s >= min_score and s == max(scores[lo:hi]):
            peaks.append((i * step, s))
    for t, s in peaks:
        print("  match at %7.2f s  (clip ends %7.2f s)  score %.3f" % (t, t + m * step, s))
    if not peaks:
        print("  no match above %.2f; best score %.3f at %.2f s" % (min_score, max(scores), scores.index(max(scores)) * step))
    else:
        far = [s for i, s in enumerate(scores) if all(abs(i * step - t) > 5 for t, _ in peaks)]
        print("  best score away from the matches: %.3f" % (max(far) if far else float("nan")))


if __name__ == "__main__":
    main(sys.argv)
