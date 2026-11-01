#!/bin/sh
# Mutation check for tests/test_audio.c: break the code in a scratch copy (work/mut, git-ignored),
# and confirm the audio tests notice. The real sources are never touched.
set -u
ROOT=/Users/cthedark/projects/personal/3k-decomp
M=$ROOT/work/mut
DATA=$ROOT/original_bin
mkdir -p "$M"
SRCS="src/lcz.c src/assets.c src/screen.c src/gfx.c src/title.c src/tkrand.c src/intro0.c src/intro1.c src/intro2.c src/intro3.c src/intro4.c src/intro5.c src/intro6.c src/subs.c src/slide.c src/voc.c src/voice.c src/audio.c src/l10n.c"

fresh() { mkdir -p "$M/src" "$M/tests"; cp "$ROOT"/src/* "$M/src/"; cp "$ROOT"/tests/* "$M/tests/"; }
run() {
    ( cd "$M" && cc -std=c99 -O1 -Isrc $SRCS tests/test_audio.c -o t 2>&1 | grep error
      ./t "$DATA" 2>/dev/null | grep -E "FAIL|^ok|^FAILED" | head -3 )
}

echo "--- baseline";                       fresh; run
echo "--- M1: no half-step bias in ADPCM"; fresh
sed -i.o 's/ + ((1u << \*r) >> 1))/)/' "$M/src/voc.c"; run
echo "--- M2: slide 0 restart gap 21";     fresh
sed -i.o 's/MUSIC_GAP = 20,/MUSIC_GAP = 21,/' "$M/src/intro0.c"; run
echo "--- M3: wrong clip for each line";   fresh
sed -i.o 's|s->line_off / LINE_STEP) : NULL|s->line_off / LINE_STEP + 1) : NULL|' "$M/src/subs.c"; run
echo "--- M4: Space does not stop clip";   fresh
python3 - "$M/src/subs.c" <<'PY'
import sys
p = sys.argv[1]
s = open(p).read()
old = "            if (skip) {   /* 0x127ee: Space ends the hold and silences the clip */\n                audio_stop();\n                over = 1;"
assert old in s
open(p, "w").write(s.replace(old, "            if (skip) {\n                over = 1;", 1))
PY
run
echo "--- M5: hold ignores the clip (timed hold only)"; fresh
sed -i.o 's/if (s->clip_started) {$/if (0) {/' "$M/src/subs.c"; run
