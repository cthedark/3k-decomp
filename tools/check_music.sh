#!/bin/sh
# Compare the port's music player (src/music.c) with the ORIGINAL's FM driver on every song of MUC.ALL.
#
# For each of the 25 songs the original's CAST.SL runs in the oracle emulator on a copy of the data in which that song
# is the one it plays (tools/capture_song_opl.py: its FM driver's OPL register writes, per timer interrupt); the port's
# player writes the same song through build/music_regs; tools/compare_opl.py compares them per song tick. The two
# must be identical for every tick after the first (song tick 0 has the driver's own reset writes, see compare_opl.py).
#
# Needs: work/venv with unicorn, work/unpacked/CAST.SL.exe, original_bin/, and `make build/music_regs`. The
# captures take about 40 s per song and are kept in work/opl/songs (delete one to record it again). Writes only under work/.
# Usage: tools/check_music.sh [SONG ...]     (default: all 25)
set -u
cd "$(dirname "$0")/.." || exit 2
OUT=work/opl/songs
mkdir -p "$OUT"
make build/music_regs > /dev/null || exit 2
SONGS=${*:-$(seq 0 24)}
# offsets and lengths of the songs in MUC.ALL
OFFS=$(python3 - <<'PYEOF'
d = open("original_bin/MUC.ALL", "rb").read()
o, i = [], 0
while True:
    i = d.find(b"CTMF", i)
    if i < 0:
        break
    o.append(i)
    i += 4
o.append(len(d))
print(" ".join(f"{o[n]}:{o[n + 1] - o[n]}" for n in range(len(o) - 1)))
PYEOF
)
fail=0; total=0
for n in $SONGS; do
    if [ ! -s "$OUT/song$n.txt" ]; then
        perl -e 'alarm 400; exec @ARGV' -- python3 tools/capture_song_opl.py "$n" "$OUT/song$n.txt" 1560 > "$OUT/cap$n.log" 2>&1
    fi
    spec=$(echo "$OFFS" | cut -d' ' -f$((n + 1)))
    off=${spec%%:*}; len=${spec##*:}
    # CAST.SL reads song 0 at offset 2; the capture put song n there, so the player reads song n from its own place
    build/music_regs original_bin "$off" "$len" 12000 "$OUT/mine$n.txt" > /dev/null 2>&1
    total=$((total + 1))
    r=$(python3 tools/compare_opl.py "$OUT/song$n.txt" "$OUT/mine$n.txt" --show 3 2>&1)
    line=$(echo "$r" | grep '^song ticks')
    if echo "$line" | grep -q ', 0 different'; then echo "ok   song $n: $line"; else fail=$((fail + 1)); echo "DIFF song $n: $line"; echo "$r" | sed -n '2,12p' | cut -c1-260; fi
done
echo "music: $((total - fail)) of $total songs identical to the original's driver"
[ "$fail" -eq 0 ]
