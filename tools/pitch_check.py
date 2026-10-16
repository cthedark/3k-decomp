#!/usr/bin/env python3
"""Does a recording of the original play the song at the same PITCH as the port's rendering?

Usage: tools/pitch_check.py CAPTURE.raw RENDER.wav SCALE OFFSET
       tools/pitch_check.py --selftest RENDER.wav    (check the tool on signals with a known answer)

SCALE and OFFSET map render time to capture time (capture = OFFSET + SCALE * render), from an onset
fit. If the capture were just a time-stretched copy of the render (an artefact of recording), every
note would also move by -12*log2(SCALE) semitones; if the original driver really runs the song at a
different tempo, the notes keep their pitch. Positive = the capture is higher than the render.
The tool finds the pitch shift (in 1/4 semitone steps, on log-frequency spectra at 48 bins per
octave) that best aligns the two, over 40 windows spread over the song, and repeats it with
deliberately wrong alignments as controls. Needs numpy. Reads its inputs, writes nothing.
"""
import sys

import numpy as np

sys.path.insert(0, __file__.rsplit("/", 1)[0])
import compare_music as cm  # noqa: E402

BPO = 48
CAP_RATE = 48000   # the SDL disk driver writes 48,000 Hz (its own log line says so), not DOSBox-X's 44,100
FREQS = 110.0 * 2 ** (np.arange(5 * BPO) / BPO)


def logspec(x, rate, t0, dur=0.5):
    n = int(dur * rate)
    s = int(t0 * rate)
    seg = x[s:s + n] * np.hanning(n)
    mag = np.abs(np.fft.rfft(seg))
    f = np.fft.rfftfreq(n, 1.0 / rate)
    return np.sqrt(np.interp(FREQS, f, mag))


def shift_score(a, b, k):
    """Similarity of render spectrum a and capture spectrum b when b is moved DOWN by k bins
    (k > 0: the capture is k bins higher than the render)."""
    if k >= 0:
        aa, bb = a[:len(a) - k], b[k:]
    else:
        aa, bb = a[-k:], b[:len(b) + k]
    aa, bb = aa - aa.mean(), bb - bb.mean()
    return float(aa @ bb / (np.linalg.norm(aa) * np.linalg.norm(bb) + 1e-12))


def run(cap, ren, rr, scale, off, label, step=2.0, nwin=40):
    ks = list(range(-30, 31))
    tot = np.zeros(len(ks))
    for i in range(nwin):
        tr = 4.0 + i * step
        a = logspec(ren, rr, tr)
        b = logspec(cap, CAP_RATE, off + scale * tr)
        tot += np.array([shift_score(a, b, k) for k in ks])
    tot /= nwin
    k = ks[int(np.argmax(tot))]
    print("%-34s pitch of capture vs render: %+5.2f semitones (score %.3f; at 0 shift %.3f; worst %.3f)"
          % (label, k * 12.0 / BPO, tot.max(), tot[ks.index(0)], tot.min()))
    return k * 12.0 / BPO


def selftest(render_path):
    """Fake captures: the render played faster by n semitones (so n semitones higher and 2**(n/12)
    times as fast), at the capture rate like a real capture. The tool must read back +n."""
    ren, rr = cm.load_wav(render_path)
    for n in (1.0, -2.0):
        f = 2 ** (n / 12)
        t = np.arange(int(len(ren) / f)) * f
        fast = np.interp(t, np.arange(len(ren)), ren)
        fake = np.interp(np.arange(int(len(fast) * CAP_RATE / rr)) * rr / CAP_RATE, np.arange(len(fast)), fast)
        got = run(fake, ren, rr, 1.0 / f, 0.0, "selftest, expected %+5.2f" % n)
        print("   -> %s" % ("ok" if abs(got - n) <= 0.26 else "WRONG"))


def main(argv):
    if len(argv) == 3 and argv[1] == "--selftest":
        return selftest(argv[2])
    if len(argv) != 5:
        raise SystemExit(__doc__)
    cap, _ = cm.load_capture(argv[1])
    ren, rr = cm.load_wav(argv[2])
    scale, off = float(argv[3]), float(argv[4])
    run(cap, ren, rr, scale, off, "aligned (scale %.3f, off %.2f)" % (scale, off))
    run(cap, ren, rr, scale, off + 0.37, "control: 0.37 s late")
    run(cap, ren, rr, scale, off + 5.0, "control: 5 s late")
    run(cap, ren, rr, 1.0, off, "control: no time scale")
    print("a capture that is only a time-stretched copy (scale %.3f) would read %+5.2f semitones"
          % (scale, -12 * np.log2(scale)))


if __name__ == "__main__":
    main(sys.argv)
