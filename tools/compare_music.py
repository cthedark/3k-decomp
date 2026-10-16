#!/usr/bin/env python3
"""Compare the port's rendering of a song with a recording of the original driver (DOSBox-X).

Usage: tools/compare_music.py CAPTURE.raw RENDER.wav [--cap-rate 48000] [--seconds 30]
                              [--search 4] [--from 0] [--detail]

CAPTURE.raw  signed 16-bit LE stereo from tools/capture_original_audio.sh (the SDL disk driver, 48,000 Hz)
RENDER.wav   mono 16-bit WAV from build/music_dump (the port's player + ymfm)

1. ALIGN: the loudness envelopes (RMS per 10 ms) are cross-correlated to find the capture time at
   which the render's time 0 plays (searched over 0..SEARCH seconds).
2. COMPARE, window by window (0.25 s windows, hop 0.125 s, from FROM to FROM+SECONDS of the song):
   the magnitude spectra of both signals on the same frequency grid (4 Hz bins), restricted to
   80 Hz..4 kHz, are compared by cosine similarity; the same for 12 pitch-class (chroma) sums.
   1.0 = same spectrum, near 0 = unrelated.
3. CONTROLS, to show the score can tell right from wrong: the render shifted in time by 0.25, 0.5
   and 2 s, and the render played 1 semitone sharp (speed change). They must score clearly lower.
4. LEVEL: RMS of both over the compared stretch, and the ratio in dB. The two chips are different
   emulations with their own output scaling, so the absolute ratio says little; how the ratio changes
   from passage to passage (--detail) says more.

Reads both files, writes nothing. Needs numpy.
"""
import sys
import wave

import numpy as np

WIN = 0.25      # seconds
HOP = 0.125
LO, HI = 80.0, 4000.0


def load_capture(path):
    raw = np.fromfile(path, dtype="<i2")
    raw = raw[: len(raw) // 2 * 2].reshape(-1, 2).astype(np.float64)
    return raw.mean(axis=1), np.abs(raw[:, 0] - raw[:, 1]).mean() / (np.abs(raw).mean() + 1e-9)


def load_wav(path):
    w = wave.open(path)
    x = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").astype(np.float64)
    if w.getnchannels() == 2:
        x = x.reshape(-1, 2).mean(axis=1)
    return x, w.getframerate()


def envelope(x, rate, step=0.01):
    n = int(step * rate)
    m = len(x) // n
    return np.sqrt((x[: m * n].reshape(m, n) ** 2).mean(axis=1))


def spectra(x, rate, t0, count):
    """Magnitude spectra of `count` windows starting at second t0, on a 4 Hz grid."""
    n = int(round(WIN * rate))
    out = []
    win = np.hanning(n)
    for i in range(count):
        s = int(round((t0 + i * HOP) * rate))
        seg = x[s:s + n]
        if len(seg) < n:
            seg = np.pad(seg, (0, n - len(seg)))
        out.append(np.abs(np.fft.rfft(seg * win)))
    return np.array(out)       # bin k is at k / WIN Hz


def feature_bins():
    k = np.arange(int(LO * WIN), int(HI * WIN) + 1)
    return k


def cosine(a, b):
    na, nb = np.linalg.norm(a, axis=1), np.linalg.norm(b, axis=1)
    return (a * b).sum(axis=1) / (na * nb + 1e-12)


def chroma(spec):
    k = feature_bins()
    f = k / WIN
    pc = np.round(12 * np.log2(f / 440.0)).astype(int) % 12
    out = np.zeros((spec.shape[0], 12))
    for c in range(12):
        out[:, c] = spec[:, k[pc == c]].sum(axis=1)
    return out


def score(cap, cap_rate, ren, ren_rate, cap_t0, ren_t0, count):
    a = spectra(cap, cap_rate, cap_t0, count)
    b = spectra(ren, ren_rate, ren_t0, count)
    k = feature_bins()
    sa, sb = np.sqrt(a[:, k]), np.sqrt(b[:, k])      # square root: compress the dynamic range
    return cosine(sa, sb), cosine(chroma(a), chroma(b))


def main(argv):
    args = [a for a in argv[1:] if not a.startswith("--")]
    opt = {}
    for key in ("--cap-rate", "--seconds", "--search", "--from"):
        if key in argv:
            opt[key] = argv[argv.index(key) + 1]
            args.remove(opt[key])
    if len(args) != 2:
        raise SystemExit(__doc__)
    cap_rate = int(opt.get("--cap-rate", 48000))
    seconds = float(opt.get("--seconds", 30))
    search = float(opt.get("--search", 4))
    start = float(opt.get("--from", 0))
    cap, lr_diff = load_capture(args[0])
    ren, ren_rate = load_wav(args[1])
    print("capture %.1f s (left/right differ by %.1f%% of the level), render %.1f s at %d Hz"
          % (len(cap) / cap_rate, 100 * lr_diff, len(ren) / ren_rate, ren_rate))

    # 1. align on the loudness envelopes
    ec, er = envelope(cap, cap_rate), envelope(ren[: int((start + seconds) * ren_rate)], ren_rate)
    er = er[int(start * 100):int((start + seconds) * 100)]
    best = None
    for shift in range(0, int(search * 100) + 1):
        seg = ec[shift + int(start * 100): shift + int(start * 100) + len(er)]
        if len(seg) < len(er):
            break
        c = np.corrcoef(seg, er)[0, 1]
        if best is None or c > best[0]:
            best = (c, shift)
    corr, shift = best
    print("alignment: the render's second 0 plays at capture second %.2f (envelope correlation %.3f)" % (shift / 100, corr))
    t_cap0 = shift / 100 + start

    # 2. spectral comparison
    count = int((seconds - WIN) / HOP)
    cs, ch = score(cap, cap_rate, ren, ren_rate, t_cap0, start, count)
    print("\nwindows compared: %d (%.0f s of the song from second %.0f)" % (count, seconds, start))
    print("  spectrum similarity: mean %.3f  median %.3f  10th percentile %.3f  min %.3f" % (cs.mean(), np.median(cs), np.percentile(cs, 10), cs.min()))
    print("  chroma similarity:   mean %.3f  median %.3f  10th percentile %.3f  min %.3f" % (ch.mean(), np.median(ch), np.percentile(ch, 10), ch.min()))

    # 3. controls
    print("\ncontrols (the same comparison, deliberately wrong):")
    for dt in (0.25, 0.5, 2.0):
        s, c = score(cap, cap_rate, ren, ren_rate, t_cap0 + dt, start, count)
        print("  render %.2f s early:       spectrum %.3f  chroma %.3f" % (dt, s.mean(), c.mean()))
    f = 2 ** (1 / 12)
    t = np.arange(int(len(ren) / f)) * f
    sharp = np.interp(t, np.arange(len(ren)), ren)
    s, c = score(cap, cap_rate, sharp, ren_rate, t_cap0, start / f, count)
    print("  render 1 semitone sharp:    spectrum %.3f  chroma %.3f" % (s.mean(), c.mean()))

    # 4. level
    n0, n1 = int(t_cap0 * cap_rate), int((t_cap0 + seconds) * cap_rate)
    m0, m1 = int(start * ren_rate), int((start + seconds) * ren_rate)
    rc, rr = np.sqrt((cap[n0:n1] ** 2).mean()), np.sqrt((ren[m0:m1] ** 2).mean())
    print("\nlevel over the stretch: capture RMS %.0f, render RMS %.0f, capture is %+.1f dB relative to the render"
          % (rc, rr, 20 * np.log10(rc / rr)))
    if "--detail" in argv:
        print("\nper 2 s: capture RMS, render RMS, dB difference, spectrum similarity")
        w = int(2 / HOP)
        for i in range(0, count, w):
            a = cap[int((t_cap0 + i * HOP) * cap_rate):int((t_cap0 + i * HOP + 2) * cap_rate)]
            b = ren[int((start + i * HOP) * ren_rate):int((start + i * HOP + 2) * ren_rate)]
            print("  %6.1f s  %6.0f  %6.0f  %+5.1f dB  %.3f" % (start + i * HOP, np.sqrt((a ** 2).mean()), np.sqrt((b ** 2).mean()),
                                                                20 * np.log10(np.sqrt((a ** 2).mean()) / np.sqrt((b ** 2).mean())), cs[i:i + w].mean()))


if __name__ == "__main__":
    main(sys.argv)
