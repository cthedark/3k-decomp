#!/usr/bin/env python3
"""Describe a raw audio capture from DOSBox-X (SDL 'disk' audio driver): sound/silence timeline.

Usage: tools/audio_timeline.py FILE.raw [--rate 48000] [--channels 2] [--block 0.5] [--wav OUT.wav]
Assumes signed 16-bit little-endian samples. Prints the RMS per block and the stretches that are
silent (RMS below a threshold) or sounding, so that the start, end and repetition of clips can be
read off. Standard library only. Reads FILE, writes only the optional WAV.
"""
import array
import math
import sys
import wave


def main(argv):
    args = [a for a in argv[1:] if not a.startswith("--")]
    opt = {}
    for k in ("--rate", "--channels", "--block", "--wav", "--thresh"):
        if k in argv:
            opt[k] = argv[argv.index(k) + 1]
            args.remove(opt[k])
    if len(args) != 1:
        raise SystemExit(__doc__)
    rate = int(opt.get("--rate", 48000))
    ch = int(opt.get("--channels", 2))
    block = float(opt.get("--block", 0.5))
    thresh = float(opt.get("--thresh", 30))
    a = array.array("h")
    with open(args[0], "rb") as f:
        data = f.read()
    a.frombytes(data[: len(data) // 2 * 2])
    if sys.byteorder == "big":
        a.byteswap()
    frames = len(a) // ch
    print("%s: %d bytes, %d frames at %d Hz x %d ch = %.2f s" % (args[0], len(data), frames, rate, ch, frames / rate))
    n = int(block * rate)
    rms = []
    for i in range(0, frames, n):
        seg = a[i * ch:(i + n) * ch]
        rms.append(math.sqrt(sum(x * x for x in seg) / max(1, len(seg))))
    # runs of sounding / silent blocks
    runs, cur, start = [], None, 0
    for i, r in enumerate(rms):
        state = r >= thresh
        if state != cur:
            if cur is not None:
                runs.append((cur, start, i))
            cur, start = state, i
    runs.append((cur, start, len(rms)))
    for st, s, e in runs:
        seg = rms[s:e]
        print("  %-8s %7.1f s .. %7.1f s  (%5.1f s)  mean RMS %6.0f  peak block RMS %6.0f"
              % ("sound" if st else "silence", s * block, e * block, (e - s) * block, sum(seg) / len(seg), max(seg)))
    if "--wav" in opt:
        w = wave.open(opt["--wav"], "wb")
        w.setnchannels(ch)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(a.tobytes())
        w.close()
        print("wrote", opt["--wav"])


if __name__ == "__main__":
    main(sys.argv)
