#!/bin/sh
# Compare WALK.SL's battle (src/battle.c) and what follows it with the original, pass by pass (tools/battle_check.py):
#   - starts on an enemy in scenes 1, 34 and 42 and on four more enemies of scene 1 (monster groups 0 .. 4): the hand-over, the
#     first screen and 120 to 300 idle battle passes (the party acts first in all of them);
#   - a walk into an enemy in scene 1 at two clock readings: 9 world passes, then the battle; at the clock 30000 the party acts first
#     (241 battle passes compared), at 20 the monsters do (291 battle passes: their three blows, then the party's turn);
#   - five key scripts (stage 2: the command tabs, choosing and moving the target, Esc), 400 to 450 passes;
#   - six whole battles with attacks (stage 3): two won, then the rewards bar and over 1,200 world passes after it; four lost
#     (correction, 2026-10-19: this comment said five were won; the comparison now tells won from lost);
#   - level-ups after a win (step 16): one message box, and three with long waits in a party of three;
#   - MARK.EXE's game-over screen from the screen a lost battle ends on, 400 passes (tools/gameover_check.py).
# Screen, palette, rand state and, by loop, the battle's tables and locals or the world's villager and party tables must be identical
# at every pass.
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
# stage 3, attacks and the monsters' turns: Space, Enter and Esc only (tools/battle_keys.py --attack), whole battles: a lost one ends
# the comparison, a won one goes on through the rewards bar into the world, with the same keys; a party of three; a member with 40 HP
run s1_rec4_attack3 1 106 36 2500 $($PY tools/battle_keys.py 3 2500 --attack)
run s1_rec9_attack9 1 94 55 2500 $($PY tools/battle_keys.py 9 2500 --attack)
run s1_rec19_attack6 1 214 43 2500 $($PY tools/battle_keys.py 6 2500 --attack)
run s34_attack7 34 106 30 2500 $($PY tools/battle_keys.py 7 2500 --attack)
DOPTS="--join 1 --join 2"; run party3_attack5 1 106 36 2500 $($PY tools/battle_keys.py 5 2500 --attack); DOPTS=""
DOPTS="--hp 0:40"; run lose_attack5 1 106 36 2500 $($PY tools/battle_keys.py 5 2500 --attack); DOPTS=""
# what follows a win (step 16): the rewards bar, back in the world (the six battles above go on in the world after a win, with the
# same key script); level-ups with their message boxes: the hero one short of his next level, and a party of three each one short,
# with long waits in the boxes (more than one palette step each)
DOPTS="--word 0:4:199"; run lvl1_attack3 1 106 36 1500 $($PY tools/battle_keys.py 3 2500 --attack); DOPTS=""
K3="$($PY tools/battle_keys.py 5 2500 --attack | tr ' ' '\n' | grep -v -- --key | awk -F: '$1 <= 801 {printf "--key %s ", $0}')"
K3="$K3 --key 805:0xb9 --key 900:0x1c --key 902:0x9c --key 990:0x39 --key 992:0xb9 --key 1080:0x1c --key 1082:0x9c --key 1100:0x4d --key 1160:0xcd"
DOPTS="--join 1 --join 2 --word 0:4:199 --word 1:4:249 --word 2:4:499"; run lvl3_attack5 1 106 36 1300 $K3; DOPTS=""
# the rewards bar held for 100 passes (it bobs down 23 rows and back), then walking in the world after the battle
KR="$($PY tools/battle_keys.py 3 2500 --attack | tr ' ' '\n' | grep -v -- --key | awk -F: '$1 <= 1220 {printf "--key %s ", $0}')"
KR="$KR --key 1320:0x39 --key 1322:0xb9 --key 1400:0x4d --key 1450:0xcd --key 1460:0x50 --key 1500:0xd0 --key 1520:0x39 --key 1522:0xb9"
run rewards_walk 1 106 36 1700 $KR
# a walk into an enemy, then a won battle that ends after the rain's strength changed (tick 1024): the rest of the touching pass uses
# the stride from before the battle; the hero's frame at the touch is not 0
run walkwin_850 1 110 36 1200 --tick-base 850 --key 1:0x4b --key 9:0xcb $($PY tools/battle_keys.py 3 2500 --attack --from 12)
# the same at the clock 20300: after the won battle the hero walks into a second enemy (pass 1148), and the second battle starts with
# [bp-0x84] as the first one left it (1), not 0
run walkwin_20300 1 110 36 1200 --tick-base 20300 --key 1:0x4b --key 9:0xcb $($PY tools/battle_keys.py 3 2500 --attack --from 12)
# a strong hero (experience 30,000, HP 9,000) against monster groups whose coins and experience differ (records 9 and 19 of scene 1:
# won, with a level-up), and in scene 34 (lost)
DOPTS="--word 0:4:30000 --hp 0:9000"
run strong_r9 1 94 55 1100 $($PY tools/battle_keys.py 9 1100 --attack)
run strong_r19 1 214 43 1100 $($PY tools/battle_keys.py 6 1100 --attack)
run strong_s34 34 106 30 1100 $($PY tools/battle_keys.py 7 1100 --attack)
DOPTS=""
# after the loss: MARK.EXE's game-over screen from the screen the battle ended on (tools/gameover_check.py)
START=$($PY -c "
import sys
F = 64000 + 768 + 1 + 4 + 1000 + 0x124
b = open('$OUT/k_lose_attack5/port.bin', 'rb').read()
i = next(i for i in range(len(b) // F) if b[F * i + 64768] == 2)
open('$OUT/lost.raw', 'wb').write(b[F * i:F * i + 64768])
print('$OUT/lost.raw')")
res=$($PY tools/gameover_check.py "$START" 400 --keep "$OUT/k_gameover" --port "$PORT" 2>/dev/null)
case "$res" in
    *identical*) echo "gameover: $res" ;;
    *) echo "gameover: DIFFERENT"; echo "$res" | head -4; fail=1; [ "${FIRST_FAIL_EXIT:-0}" = 1 ] && exit 1 ;;
esac
[ $fail = 0 ] && echo "battle: all identical"
exit $fail
