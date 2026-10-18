#!/bin/sh
# Compare WALK.SL's battle, stage 1 (src/battle.c), with the original, pass by pass (tools/battle_check.py):
#   - starts on an enemy in scenes 1, 34 and 42 and on four more enemies of scene 1 (monster groups 0 .. 4): the hand-over, the
#     first screen and 120 to 300 idle battle passes (the party acts first in all of them);
#   - a walk into an enemy in scene 1 at two clock readings: 9 world passes, then the battle; at the clock 30000 the party acts first
#     (241 battle passes compared), at 20 the monsters do, which is not built, so only the passes before their first move (12) count.
# Screen, palette, rand state, the monster table, the sway, who acts and the turn counter must be identical at every pass.
# The original's recordings are kept in work/check_battle (delete it to record again). PORT=path uses another build of walk_dump.
# Exit status 0 when everything is identical.
set -u
cd "$(dirname "$0")/.." || exit 2
PY=work/venv/bin/python
OUT=work/check_battle
PORT=${PORT:-build/walk_dump}
mkdir -p "$OUT"
[ "$PORT" = build/walk_dump ] && { make -s build/walk_dump || exit 2; }
fail=0

run() {   # NAME SCENE X Y_HALF N [battle_check options]
    name=$1 sc=$2 x=$3 y=$4 n=$5
    shift 5
    D="$OUT/data_$name"
    [ -d "$D" ] || $PY tools/walk_scene_data.py "$D" "$sc" "$x" "$y" > /dev/null || exit 2
    res=$($PY tools/battle_check.py "$D" "$n" --keep "$OUT/k_$name" --cache "$OUT/$name.rec" --port "$PORT" "$@" 2>/dev/null | grep -v '^pass [0-9]*: wrote')
    case "$res" in
        *"identical"*) echo "$name: $(echo "$res" | tail -1)" ;;
        *) echo "$name: DIFFERENT"; echo "$res" | head -4; fail=1 ;;
    esac
}

run s1_rec4 1 106 36 300
run s1_rec5 1 180 24 150
run s1_rec9 1 94 55 150
run s1_rec13 1 77 66 150
run s1_rec15 1 73 59 150
run s1_rec19 1 214 43 150
run s34 34 106 30 120
run s42 42 124 28 120
run walk_30000 1 110 36 250 --tick-base 30000 --key 1:0x4b --key 9:0xcb
run walk_20 1 110 36 22 --tick-base 20 --key 1:0x4b --key 9:0xcb
[ $fail = 0 ] && echo "battle: all identical"
exit $fail
