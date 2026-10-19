#!/bin/sh
# Compare WALK.SL's battle, stages 1 and 2 (src/battle.c), with the original, pass by pass (tools/battle_check.py):
#   - starts on an enemy in scenes 1, 34 and 42 and on four more enemies of scene 1 (monster groups 0 .. 4): the hand-over, the
#     first screen and 120 to 300 idle battle passes (the party acts first in all of them);
#   - a walk into an enemy in scene 1 at two clock readings: 9 world passes, then the battle; at the clock 30000 the party acts first
#     (241 battle passes compared), at 20 the monsters do (291 battle passes: their three blows, then the party's turn);
#   - five key scripts (stage 2: the command tabs, choosing and moving the target, Esc), 400 to 450 passes;
#   - six whole battles with attacks (stage 3), to their end: won in five (one with a party of three), lost in one.
# Screen, palette, rand state, the monster table, the sway, who acts, the turn counter, the chosen command, the choosing flag and the
# key latch must be identical at every pass.
# The original's recordings are kept in work/check_battle (delete it to record again). PORT=path uses another build of walk_dump;
# FIRST_FAIL_EXIT=1 stops at the first difference (tools/mutate_battle.py).
# Exit status 0 when everything is identical.
set -u
cd "$(dirname "$0")/.." || exit 2
PY=work/venv/bin/python
OUT=work/check_battle
PORT=${PORT:-build/walk_dump}
mkdir -p "$OUT"
[ "$PORT" = build/walk_dump ] && { make -s build/walk_dump || exit 2; }
fail=0
DOPTS=""   # extra tools/walk_scene_data.py options for the next run (--join, --hp)

run() {   # NAME SCENE X Y_HALF N [battle_check options]
    name=$1 sc=$2 x=$3 y=$4 n=$5
    shift 5
    D="$OUT/data_$name"
    [ -d "$D" ] || $PY tools/walk_scene_data.py "$D" "$sc" "$x" "$y" $DOPTS > /dev/null || exit 2
    res=$($PY tools/battle_check.py "$D" "$n" --keep "$OUT/k_$name" --cache "$OUT/$name.rec" --port "$PORT" "$@" 2>/dev/null | grep -v '^pass [0-9]*: wrote')
    case "$res" in
        *"identical"*) echo "$name: $(echo "$res" | tail -1)" ;;
        *) echo "$name: DIFFERENT"; echo "$res" | head -4; fail=1; [ "${FIRST_FAIL_EXIT:-0}" = 1 ] && exit 1 ;;
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
run walk_20 1 110 36 300 --tick-base 20 --key 1:0x4b --key 9:0xcb
# stage 2, the command and target choice: key scripts from tools/battle_keys.py (tabs, targets, Esc, keys 2 and 3, never a confirmed
# attack or command 1 to 3); the walk from (114, 72) gives a battle whose third front slot is empty
for seed in 11 12; do run "s1_rec4_keys$seed" 1 106 36 400 $($PY tools/battle_keys.py $seed 400); done
run s34_keys1 34 106 30 400 $($PY tools/battle_keys.py 1 400)
for seed in 21 22; do
    run "walk114_keys$seed" 1 114 36 450 --tick-base 20 --key 1:0x4b --key 21:0xcb $($PY tools/battle_keys.py $seed 450 --from 23)
done
# stage 3, attacks and the monsters' turns: Space, Enter and Esc only (tools/battle_keys.py --attack), whole battles up to their end (won
# or lost; what follows is not built, so the comparison stops there); a party of three; a party member with 40 HP, who loses
run s1_rec4_attack3 1 106 36 2500 $($PY tools/battle_keys.py 3 2500 --attack)
run s1_rec9_attack9 1 94 55 2500 $($PY tools/battle_keys.py 9 2500 --attack)
run s1_rec19_attack6 1 214 43 2500 $($PY tools/battle_keys.py 6 2500 --attack)
run s34_attack7 34 106 30 2500 $($PY tools/battle_keys.py 7 2500 --attack)
DOPTS="--join 1 --join 2"; run party3_attack5 1 106 36 2500 $($PY tools/battle_keys.py 5 2500 --attack); DOPTS=""
DOPTS="--hp 0:40"; run lose_attack5 1 106 36 2500 $($PY tools/battle_keys.py 5 2500 --attack); DOPTS=""
[ $fail = 0 ] && echo "battle: all identical"
exit $fail
