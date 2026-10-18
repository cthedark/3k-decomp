#!/bin/sh
# Check the C port of WALK.SL's first screen against the original running in the oracle (tools/dosemu.py), and its
# loader against the Python model (which tools/walk_scene.py checks against the oracle).
#
#   1. the first screen: the original's screen at BIOS tick 21 (right after its first window copy), started over the
#      port's own menu screen, against the port's walk_first_screen over the same screen: pixels and palette.
#   2. the loader: build/walk_dump's state for all 53 scene numbers and N randomised state files, against the model.
#   3. the world in motion: 600 to 1500 idle passes of the original's main loop at seven BIOS clock readings, against the
#      Python model and the port (tools/walk_idle.py); IDLE=0 skips this.
#
# Needs the original's files in original_bin/, work/unpacked/WALK.SL.exe (docs/environment.md) and work/venv.
# Usage: tools/check_walk.sh [N]       N = randomised state files for part 2 (default 60)
# Writes only under work/check_walk/.
set -u
cd "$(dirname "$0")/.." || exit 2
N=${1:-60}
OUT=work/check_walk
mkdir -p "$OUT"
PY=work/venv/bin/python
fail=0

make build/walk_dump build/3k_headless build/music_regs > "$OUT/build.log" 2>&1 || { echo "build failed, see $OUT/build.log"; exit 2; }

# the screen the menu leaves: the port's menu a few passes in (equal to the original's, tools/check_menu.sh)
START=$OUT/start.raw
build/3k_headless --scene menu --data original_bin --step-dump 3:"$START" > "$OUT/start.log" 2>&1 || { echo "cannot make $START"; exit 2; }

# ---- 1. the first screen
echo "== first screen"
$PY tools/walk_dump.py "$OUT/ff" 22 --start "$START" \
    --dump-at 126fa:0:vga0:a0000:64000 > "$OUT/oracle.log" 2>&1 || { echo "oracle run failed, see $OUT/oracle.log"; exit 2; }
{ head -c 64000 "$OUT/ff/vga0.bin"; tail -c 768 "$START"; } > "$OUT/oracle_first.raw"
build/walk_dump original_bin first "$OUT/port_first.raw" "$START" || { echo "walk_dump first failed"; exit 2; }
r=$(python3 tools/compare_raw.py "$OUT/port_first.raw" "$OUT/oracle_first.raw" 2>&1)
echo "port against the original: $r"
[ "$r" = identical ] || fail=1

# ---- 2. the loader
echo "== loader (53 scenes + $N randomised state files)"
$PY tools/walk_scene.py cross build/walk_dump "$N" 5 || fail=1

# ---- 3. the world in motion: the original's main loop with nothing pressed, in the oracle (tools/walk_idle_trace.py), against
#         the Python model (tools/walk_idle.py check) and the C port (tools/walk_idle.py cross). The BIOS clock at the first pass
#         is varied: 20 (the oracle's own), 4096 / 11264 (rain of different strength), 20224 (rain, then dry from pass 768),
#         28672 (dry), 40704 (dry, then the dark specks from pass 768), 45056 (specks).
#         Set IDLE=0 to skip this part (it takes about a minute).
if [ "${IDLE:-1}" != 0 ]; then
    echo "== idle passes"
    for spec in 20:1500 4096:600 11264:600 20224:900 28672:600 40704:900 45056:600; do
        base=${spec%%:*}
        n=${spec##*:}
        T="$OUT/idle_$base.pkl"
        $PY tools/walk_idle_trace.py "$T" "$n" --tick-base "$base" > "$OUT/idle_$base.log" 2>&1 || { echo "trace $base failed, see $OUT/idle_$base.log"; exit 2; }
        $PY tools/walk_idle.py check "$T" "$((n - 1))" > "$OUT/idle_$base.model" 2>&1 || { echo "model against the original, clock $base: DIFFERENT"; fail=1; }
        $PY tools/walk_idle.py cross build/walk_dump "$T" "$((n - 1))" || fail=1
    done
    # ---- 4. the hero walking: arrow keys held, released and mixed (tools/walk_keys.py), at several clock readings. The hero bumps into
    #         walls (the shake), steps on the house door's scene records (the pass that does nothing else), and the trail and the party
    #         records follow; the 'door' scripts go up through the house door (the roof comes off, the rain stops) and down again. Every pass is compared: screen, palette, villagers, party, trail, rand state and the hero's counters.
    echo "== walking"
    for spec in fuzz:1:900:20 fuzz:11:1100:4096 fuzz:12:1100:11264 fuzz:13:1100:20224 fuzz:15:1100:40704 yoyo:21:900:20 yoyo:22:900:28672 yoyo:23:900:45056 door:0:420:20 door:1:420:4096 door:2:420:45056 route:0:480:20 route:1:480:28672; do
        kind=${spec%%:*}; rest=${spec#*:}
        seed=${rest%%:*}; rest=${rest#*:}
        n=${rest%%:*}; base=${rest##*:}
        T="$OUT/walk_${kind}_$seed.pkl"
        $PY tools/walk_idle_trace.py "$T" "$n" --tick-base "$base" $($PY tools/walk_keys.py "$kind" "$seed" "$n") > "$OUT/walk_${kind}_$seed.log" 2>&1 || { echo "trace $spec failed, see $OUT/walk_${kind}_$seed.log"; exit 2; }
        $PY tools/walk_idle.py check "$T" "$((n - 1))" > "$OUT/walk_${kind}_$seed.model" 2>&1 || { echo "model against the original, $spec: DIFFERENT"; fail=1; }
        res=$($PY tools/walk_idle.py cross build/walk_dump "$T" "$((n - 1))") || fail=1
        echo "$kind $seed: $res"
    done
fi

# ---- 5. the scene's song. WALK.SL plays the song its map names (scene 0: MUC.ALL 0x1e085, 0x2593 bytes) through the same FM driver as the other
#         programs; the OPL register writes it makes in the oracle over the whole first pass of the song (6,180 song ticks, 1,250 BIOS ticks of
#         run time) against the player's. The driver's own reset writes at tick 0 are not reproduced by the player (tools/compare_opl.py).
#         Set MUSIC=0 to skip this part (about 15 seconds).
if [ "${MUSIC:-1}" != 0 ]; then
    echo "== music"
    $PY tools/dosemu.py work/unpacked/WALK.SL.exe --data original_bin --work "$OUT/music_emu" --screen-from "$START" --palette-from "$START" \
        --ticks 1250 --opl-log "$OUT/opl_all.txt" > "$OUT/opl.log" 2>&1 || { echo "oracle music run failed, see $OUT/opl.log"; exit 2; }
    awk '$1 < 6199' "$OUT/opl_all.txt" > "$OUT/opl_pass1.txt"
    build/music_regs original_bin 0x1e085 0x2593 7000 "$OUT/opl_player.txt" > /dev/null || { echo "music_regs failed"; exit 2; }
    r=$($PY tools/compare_opl.py "$OUT/opl_pass1.txt" "$OUT/opl_player.txt" --ticks 6180 | tail -1)
    echo "song against the original's driver: $r"
    case "$r" in *"6180 identical, 0 same writes in another order, 0 different"*) ;; *) fail=1 ;; esac
fi

if [ $fail -eq 0 ]; then echo "check_walk: all identical"; else echo "check_walk: DIFFERENCES"; fi
exit $fail
