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
#   - MARK.EXE's game-over screen from the screen a lost battle ends on, 400 passes (tools/gameover_check.py);
#   - the other three commands (step 17): fleeing, the magic list with its member picker, the item list, and any key mixed;
#   - later sections: the mouse, F1 talks, selling and asking, the dark scenes (steps 18 to 22).
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
    # RUNS="prefix ..." runs only the starts whose names begin with one of the prefixes ("gameover" for the game-over screen); used by
    # tools/mutate_battle.py for the mutations of one part
    if [ -n "${RUNS:-}" ]; then
        ok=0
        for p in $RUNS; do case "$name" in "$p"*) ok=1 ;; esac; done
        [ $ok = 1 ] || return 0
    fi
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
# the other three commands (docs/findings.md "WALK.SL step 17"). Fleeing (the fourth tab, tools/battle_keys.py --flee): one try in
# two leaves for the rewards bar; with one member and with three
for seed in 1 2; do run "s1_rec4_flee$seed" 1 106 36 1200 $($PY tools/battle_keys.py $seed 1200 --flee); done
DOPTS="--join 1 --join 2"; run party3_flee5 1 106 36 900 $($PY tools/battle_keys.py 5 900 --flee); DOPTS=""
# the magic list (the second tab): a hero of level 120 with 300 MP casts his first spell (on the monsters); a hero with 100 HP heals
# himself in a party of three (the picker); a hero of level 255 in a party with a fallen member moves round the list, leaves the
# picker with Esc and revives him; a stay of 88 rounds in the list and 120 in the picker (palette steps in both)
MOPEN="--key 2:0x81 --key 5:0x4d --key 7:0xcd --key 10:0x39 --key 12:0xb9"
DOPTS="--byte 0:0xc:120 --word 0:0xd:300"; run magic_cast 1 106 36 300 $MOPEN --key 20:0x02 --key 22:0x82
DOPTS="--join 1 --join 2 --byte 0:0xc:120 --word 0:0xd:300 --hp 0:100 --hp 1:0"
run magic_heal 1 106 36 300 $MOPEN --key 16:0x50 --key 18:0xd0 --key 22:0x02 --key 24:0x82 --key 28:0x1c --key 30:0x9c
run magic_long 1 106 36 300 $MOPEN --key 16:0x50 --key 18:0xd0 --key 100:0x02 --key 102:0x82 --key 220:0x1c --key 222:0x9c
DOPTS="--join 1 --join 2 --byte 0:0xc:255 --word 0:0xd:5000 --hp 1:0 --byte 2:0xc:100 --word 2:0xd:10"
K="$MOPEN --key 16:0x4d --key 18:0xcd --key 20:0x50 --key 21:0xd0 --key 24:0x50 --key 25:0xd0 --key 28:0x48 --key 29:0xc8 --key 31:0x50"
K="$K --key 32:0xd0 --key 35:0x4b --key 36:0xcb --key 38:0x4d --key 39:0xcd --key 42:0x02 --key 43:0x82 --key 46:0x4d --key 47:0xcd"
K="$K --key 50:0x4b --key 51:0xcb --key 53:0x4b --key 54:0xcb --key 56:0x01 --key 57:0x81 --key 60:0x02 --key 61:0x82 --key 64:0x4d"
run magic_revive 1 106 36 400 $K --key 65:0xcd --key 68:0x1c --key 69:0x9c
# the list walked from top to bottom and back, Space and Enter on a spell that can be cast (they do nothing there), spells greyed by MP
# between once and twice their level (key 1 on one does nothing), then a cast; heals that reach the most HP and one on a fallen member
K="$MOPEN"
for p in 16 19 22 25 28 31 34 37 40; do K="$K --key $p:0x50 --key $((p + 1)):0xd0"; done
for p in 43 46 49 52 55 58 61 64 67; do K="$K --key $p:0x48 --key $((p + 1)):0xc8"; done
K="$K --key 70:0x39 --key 71:0xb9 --key 73:0x1c --key 74:0x9c --key 76:0x4d --key 77:0xcd --key 79:0x50 --key 80:0xd0 --key 82:0x50"
K="$K --key 83:0xd0 --key 85:0x4b --key 86:0xcb --key 88:0x02 --key 89:0x82 --key 91:0x48 --key 92:0xc8 --key 94:0x02 --key 95:0x82"
DOPTS="--byte 0:0xc:120 --word 0:0xd:150"; run magic_walk 1 106 36 300 $K
DOPTS="--join 1 --join 2 --byte 0:0xc:120 --word 0:0xd:300 --hp 0:500 --hp 1:0"
run magic_heal_top 1 106 36 300 $MOPEN --key 16:0x50 --key 18:0xd0 --key 22:0x02 --key 24:0x82 --key 28:0x1c --key 30:0x9c
run magic_heal_fallen 1 106 36 300 $MOPEN --key 16:0x50 --key 18:0xd0 --key 22:0x02 --key 24:0x82 --key 26:0x4d --key 27:0xcd \
    --key 30:0x1c --key 32:0x9c
# casts again and again (Space opens the list in the party's turn, key 1 casts): after the first kills the three in front, two from the
# back step forward and the third front slot stays empty
K="--key 2:0x81 --key 5:0x4d --key 7:0xcd"
for p in 10 40 70 100 130 160 190 220 250 280 310 340 370; do K="$K --key $p:0x39 --key $((p + 2)):0xb9 --key $((p + 6)):0x02 --key $((p + 8)):0x82"; done
DOPTS="--byte 0:0xc:120 --word 0:0xd:3000"; run magic_casts 1 106 36 600 $K
# the item list (the third tab), opened at pass 14, then any key (tools/battle_keys.py --all): a hero with 14 things, two pages, worn
# things changed, food eaten; alone and in a party of three with a fallen member
I="--byte 0:0x14:1 --byte 0:0x15:3 --byte 0:0x17:12 --byte 0:0x18:23 --byte 0:0x19:21 --byte 0:0x1a:14 --byte 0:0x1b:9 --byte 0:0x1c:10"
I="$I --byte 0:0x1d:11 --byte 0:0x1e:13 --byte 0:0x1f:16 --byte 0:0x20:17 --byte 0:0x21:26 --byte 0:0x24:37 --byte 0:0x12e:5 --hp 0:100"
I="$I --byte 0:0xc:120 --word 0:0xd:10"
IOPEN="--key 2:0x81 --key 5:0x4b --key 7:0xcb --key 9:0x4b --key 11:0xcb --key 14:0x39 --key 16:0xb9"
for seed in 4 6; do
    DOPTS="$I"; run "items$seed" 1 106 36 1200 $IOPEN $($PY tools/battle_keys.py $seed 1200 --all --from 18 | sed 's/--key 18:0x81//')
    DOPTS="$I --join 1 --join 2 --hp 1:0 --byte 1:0x14:1 --byte 1:0x15:27 --byte 1:0xc:100 --word 1:0xd:2000"
    run "items3_$seed" 1 106 36 1200 $IOPEN $($PY tools/battle_keys.py $seed 1200 --all --from 18 | sed 's/--key 18:0x81//')
done
# key 3 (scan 4) and the third icon in the battle's item list: nothing there (step 30), in a party of three
DOPTS="$I --join 1 --join 2"; run items_key3 1 106 36 120 $IOPEN --key 30:0x04 --key 32:0x84 --key 50:0x50 --key 52:0xd0 \
    --key 60:0x04 --key 62:0x84 --key 90:0x01 --key 92:0x81
# any key from the start, the same party: all four commands mixed
for seed in 1 2 3; do run "allkeys$seed" 1 106 36 1500 $($PY tools/battle_keys.py $seed 1500 --all); done
DOPTS=""
# the mouse (docs/findings.md "WALK.SL step 18"; a driver is there, tools/battle_check.py --mouse): the pointer standing still in the
# battle; a press on the magic tab, a row, the icon, the picker (left, right to give up, then a member), the item tab, a row, the
# icons and the left-hand corner; a press held from a tab onto a row of each list; random scripts (tools/battle_mouse.py) with
# items, spells and level-ups. The comparison stops when the world comes back (its own buttons are not built: --world-passes 0)
MW="--world-passes 0"
run mouse_still 1 106 36 300 $MW --mouse 0:160:100:0
# a walk into the enemy with the pointer outside the window, then over it: the world's pointer on the screen for 9 passes
run mouse_walk 1 110 36 200 $MW --tick-base 20 --key 1:0x4b --key 9:0xcb --mouse 0:20:190:0 --mouse 4:300:30:0 --mouse 7:200:120:0
M="--key 1:0x81 --mouse 0:160:100:0 --mouse 2:112:66:0 --mouse 4:112:66:1 --mouse 6:112:66:0 --mouse 10:96:124:0 --mouse 12:96:124:1"
M="$M --mouse 14:96:124:0 --mouse 16:90:82:0 --mouse 18:90:82:1 --mouse 20:90:82:0 --mouse 24:154:80:0 --mouse 28:174:80:0"
M="$M --mouse 30:174:80:2 --mouse 32:174:80:0 --mouse 36:90:82:0 --mouse 38:90:82:1 --mouse 40:90:82:0 --mouse 44:138:80:0"
M="$M --mouse 46:138:80:1 --mouse 48:138:80:0 --mouse 60:160:66:0 --mouse 62:160:66:1 --mouse 64:160:66:0 --mouse 70:180:140:0"
M="$M --mouse 72:180:140:1 --mouse 74:180:140:0 --mouse 78:92:82:0 --mouse 80:92:82:1 --mouse 82:92:82:0 --mouse 86:108:82:0"
M="$M --mouse 88:108:82:1 --mouse 90:108:82:0 --mouse 94:16:112:0 --mouse 96:16:112:1 --mouse 98:16:112:0"
DOPTS="$I --join 1 --join 2 --byte 0:0xc:120 --word 0:0xd:300 --hp 0:100 --hp 1:0"; run mouse_lists 1 106 36 300 $MW $M
run mouse_held_magic 1 106 36 40 $MW --key 1:0x81 --mouse 0:160:100:0 --mouse 2:112:66:0 --mouse 4:112:66:1 --mouse 6:96:140:1 \
    --mouse 9:96:140:0
run mouse_held_items 1 106 36 40 $MW --key 1:0x81 --mouse 0:160:100:0 --mouse 2:160:66:0 --mouse 4:160:66:1 --mouse 6:96:140:1 \
    --mouse 9:96:140:0
for seed in 15 16 18 24; do run "mouse_magic$seed" 1 106 36 1500 $MW $($PY tools/battle_mouse.py $seed 1500); done
DOPTS="$I"; for seed in 3 7 8; do run "mouse_items$seed" 1 106 36 1000 $MW $($PY tools/battle_mouse.py $seed 1000); done
DOPTS="--word 0:4:199"; run mouse_lvl13 1 106 36 1500 $MW $($PY tools/battle_mouse.py 13 1500)
DOPTS=""
# a press on the music button held onto the magic tab (it chooses the tab but does not confirm it); a battle with no back row (a walk from
# (114, 72) at the clock 30000, the party first) attacked with the keys until a front monster is dead, the pointer going over the three
# front places all the while (a dead one is not taken as the target)
run mouse_held_tab 1 106 36 40 $MW --key 1:0x81 --mouse 0:288:104:0 --mouse 2:288:104:1 --mouse 4:112:66:1 --mouse 8:112:66:0 \
    --mouse 12:112:66:1 --mouse 14:112:66:0
M=""; for p in $(seq 60 4 900); do M="$M --mouse $p:$(( (p / 4 % 3) * 64 + 96 )):144:0"; done
run mouse_dead_slot 1 114 36 900 $MW --tick-base 30000 --key 1:0x4b --key 21:0xcb $($PY tools/battle_keys.py 3 900 --attack --from 60) \
    --mouse 0:300:30:0 $M
# the item list opened with the mouse, then rows pressed in the same pass the pointer arrives (the palette counter then is the item,
# not the 0x78 a move leaves), 60 passes in the list, the right button; the picker with the pointer on a box's edge for 40 passes
DOPTS="$I"; run mouse_item_rows 1 106 36 120 $MW --key 1:0x81 --mouse 0:160:100:0 --mouse 2:160:66:0 --mouse 4:160:66:1 \
    --mouse 6:160:66:0 --mouse 10:180:140:1 --mouse 12:180:140:0 --mouse 16:100:110:1 --mouse 18:100:110:0 --mouse 80:100:110:2 \
    --mouse 82:100:110:0
M="--key 1:0x81 --mouse 0:160:100:0 --mouse 2:112:66:0 --mouse 4:112:66:1 --mouse 6:112:66:0 --mouse 10:96:124:0 --mouse 12:96:124:1"
M="$M --mouse 14:96:124:0 --mouse 16:90:82:0 --mouse 18:90:82:1 --mouse 20:90:82:0 --mouse 24:150:80:0 --mouse 64:150:80:2 --mouse 66:150:80:0"
DOPTS="$I --join 1 --join 2 --byte 0:0xc:120 --word 0:0xd:300 --hp 0:100 --hp 1:0"; run mouse_picker_edge 1 106 36 120 $MW $M
# a won battle (the keys) whose rewards bar is left with the right button, held while the level-up's message box opens (it does not
# close it), then a new right press that does
DOPTS="--word 0:4:199"; run mouse_box_held 1 106 36 1300 $MW $($PY tools/battle_keys.py 3 1219 --attack) --mouse 0:300:30:0 \
    --mouse 1221:300:30:2 --mouse 1260:300:30:0 --mouse 1270:300:30:2 --mouse 1272:300:30:0
DOPTS=""
# the world's mouse buttons (step 19): presses over the play window only (tools/battle_mouse.py --world: walking towards the pointer,
# held up to 30 passes, and shots with the right button), from (110, 72) in scene 1 at the clock 20: pure walks, and walks into
# battles fought with the same presses and back in the world (the world compared to the end)
for seed in 2 4 6; do run "mouse_world$seed" 1 110 36 1500 --tick-base 20 $($PY tools/battle_mouse.py $seed 1500 --world); done
for seed in 1 8 18; do run "mouse_world$seed" 1 110 36 2500 --tick-base 20 $($PY tools/battle_mouse.py $seed 2500 --world); done
# a press held from the window onto the left-hand buttons (it walks on, it does not press them); a walk released while the keyboard
# port holds an arrow's break code (the walk does not go on as a held arrow); from (100, 50) in scene 1, away from the enemies
run mouse_world_drag 1 100 50 120 --tick-base 20 --mouse 0:160:100:0 --mouse 10:220:100:1 --mouse 30:20:50:1 --mouse 60:20:50:0 \
    --mouse 70:160:40:1 --mouse 90:20:150:1 --mouse 100:20:150:0
run mouse_world_release 1 100 50 120 --tick-base 20 --key 1:0xc8 --mouse 0:160:100:0 --mouse 10:220:100:1 --mouse 40:220:100:0 \
    --mouse 70:110:150:1 --mouse 80:110:150:0
# F1, talking (step 20), in scene 0: a villager two cells to the right of the hero; one whose text goes on over five boxes (texts
# ending in 0x7f); walks with F1 now and then and Space or Enter to close the boxes (tools/walk_keys.py talk) from the shipped start
# and from (108, 70); the same in scene 1 (F1 finds nobody); F1 pressed with the mouse and the box closed with the right button
DOPTS="--byte 0:2:3"; run talk_one 0 108 35 300 --key 1:0x81 --key 3:0x3b --key 5:0xbb --key 100:0x39 --key 102:0xb9
K="--key 1:0x81 --key 3:0x3b --key 5:0xbb"; for p in 60 120 180 240 300 360; do K="$K --key $p:0x39 --key $((p + 2)):0xb9"; done
DOPTS="--byte 0:2:2"; run talk_five 0 80 29 420 $K
DOPTS=""; for seed in 5 6 7 10; do run "talk_walk$seed" 0 86 35 2500 $($PY tools/walk_keys.py talk $seed 2500); done
DOPTS="--byte 0:2:3"; for seed in 1 2; do run "talk_near$seed" 0 108 35 2500 $($PY tools/walk_keys.py talk $seed 2500); done
DOPTS=""; run talk_nobody 1 110 36 2000 --tick-base 20 $($PY tools/walk_keys.py talk 8 2000)
DOPTS="--byte 0:2:3"; run talk_mouse 0 108 35 300 --mouse 0:160:100:0 --mouse 3:20:5:1 --mouse 5:20:5:0 --mouse 60:200:100:2 \
    --mouse 62:200:100:0 --mouse 90:200:100:2 --mouse 92:200:100:0
# selling and asking talks (step 21), given to the villager at (111, 70) in scene 0 (walk_scene_data.py --talk): scene 2's seller (item 7
# for 15 coins) with 100 coins, yes; no; with 10 coins; with member 0's 20 item places full; scene 27's seller (a two-row box); the
# mouse on the buttons (no, then yes); scene 9's question (38945) answered right and wrong; scene 23's villager 5, two questions and
# then a box that waits, three talks in a row (short boxes); scene 23's villagers 7 (a two-row box) and 8 (13 bytes); the number set
# with the mouse
F1="--key 1:0x81 --key 3:0x3b --key 5:0xbb"
DOPTS="--talk 8:2:0 --word 7:10:100"; run sell_yes 0 108 35 120 $F1 --key 40:0x4d --key 42:0xcd --key 60:0x4b --key 62:0xcb --key 80:0x1c --key 82:0x9c
run sell_no 0 108 35 120 $F1 --key 40:0x4d --key 42:0xcd --key 80:0x39 --key 82:0xb9
DOPTS="--talk 8:2:0 --word 7:10:10"; run sell_poor 0 108 35 120 $F1 --key 80:0x1c --key 82:0x9c
FULL="--talk 8:2:0 --word 7:10:100"; for o in $(seq 20 39); do FULL="$FULL --byte 0:$o:1"; done
DOPTS="$FULL"; run sell_full 0 108 35 120 $F1 --key 80:0x1c --key 82:0x9c
DOPTS="--talk 8:27:3 --word 7:10:100"; run sell_rows 0 108 35 120 $F1 --key 80:0x1c --key 82:0x9c
DOPTS="--talk 8:2:0 --word 7:10:100"; run sell_mouse 0 108 35 200 --mouse 0:160:100:0 --mouse 3:20:5:1 --mouse 5:20:5:0 \
    --mouse 30:300:45:0 --mouse 40:300:45:2 --mouse 42:300:45:0 --mouse 60:300:45:1 --mouse 62:300:45:0 --mouse 80:200:100:0 $F1 --key 90:0x1c --key 92:0x9c \
    --mouse 120:20:5:1 --mouse 122:20:5:0 --mouse 140:280:45:0 --mouse 160:280:45:1 --mouse 162:280:45:0
DOPTS="--talk 8:9:18"; run ask_right 0 108 35 260 $($PY tools/number_keys.py 38945 2>/dev/null)
run ask_wrong 0 108 35 260 $($PY tools/number_keys.py 38946 2>/dev/null)
K="$($PY tools/number_keys.py 13 2>/dev/null) $($PY tools/number_keys.py 23 82 2>/dev/null) --key 150:0x3b --key 152:0xbb --key 210:0x39 --key 212:0xb9"
DOPTS="--talk 8:23:5"; run ask_three 0 108 35 360 $K
DOPTS="--talk 8:23:7"; run ask_rows 0 108 35 260 $($PY tools/number_keys.py 1 2>/dev/null)
DOPTS="--talk 8:23:8"; run ask_short 0 108 35 260 $($PY tools/number_keys.py 1 2>/dev/null)
DOPTS="--talk 8:9:18"; run ask_mouse 0 108 35 300 --key 1:0x81 --mouse 0:160:100:0 --mouse 3:20:5:1 --mouse 5:20:5:0 --mouse 20:120:5:0 \
    --mouse 30:120:5:1 --mouse 34:120:5:0 --mouse 40:120:5:1 --mouse 50:120:5:0 --mouse 60:150:50:2 --mouse 62:150:50:0 \
    --mouse 70:200:5:1 --mouse 72:200:5:0 --mouse 80:200:5:1 --mouse 82:200:5:0 --mouse 100:150:5:1 --mouse 102:150:5:0
# the number box: a key made right after F1's make (the latch is set at the start); 50,000 and one more (at most 50,000); Down at the
# tens with 1 (nothing); the yes / no buttons waited on for 600 passes (palette steps)
run ask_latch 0 108 35 120 --key 3:0x3b --key 4:0x48 --key 7:0xc8 --key 30:0x1c --key 32:0x9c
run ask_cap 0 108 35 260 $($PY tools/number_keys.py 50000 2>/dev/null)
run ask_down 0 108 35 120 $F1 --key 20:0x48 --key 22:0xc8 --key 26:0x4b --key 28:0xcb --key 32:0x50 --key 34:0xd0 --key 60:0x1c --key 62:0x9c
DOPTS="--talk 8:2:0 --word 7:10:100"; run sell_wait 0 108 35 700 $F1 --key 640:0x1c --key 642:0x9c
DOPTS=""
# which entry: the villager at (82, 70) with flag 0 bit 3 set (his second entry) and not
for f in 9 1; do DOPTS="--byte 0:2:2 --byte 0:200:$f"; run "talk_flag$f" 0 84 35 300 --key 1:0x81 --key 3:0x3b --key 5:0xbb --key 100:0x39 --key 102:0xb9; done
DOPTS=""
DOPTS=""
# the dark scenes (map header byte 5 set; step 22): idle and walking with Space (tools/walk_keys.py shoot) in scenes 6 (byte 5 = 2), 9
# (byte 3 = 1, keeps shaking; walks into an enemy), 13, 23 and 47; F1 walks among scene 23's ten villagers; the mouse walking and
# shooting in scene 13; the villagers who join in scenes 37, 39 and 43, and in 13 (flag 0 bit 4 set); scene 9's question and scene 23's
# villager 5, in their own scenes (the hero above each: the start faces him down)
for sc in 6:100:40 9:100:50 13:100:50 23:100:50 47:48:45; do
    IFS=: read -r s x y <<EOF
$sc
EOF
    run "dark${s}_idle" "$s" "$x" "$y" 300
    run "dark${s}_shoot" "$s" "$x" "$y" 1500 $($PY tools/walk_keys.py shoot "$s" 1500)
done
for seed in 3 4; do run "dark23_talk$seed" 23 69 22 2500 $($PY tools/walk_keys.py talk $seed 2500); done
run dark13_mouse 13 100 50 1500 --tick-base 20 $($PY tools/battle_mouse.py 4 1500 --world)
K="--key 1:0x81 --key 3:0x3b --key 5:0xbb"; for p in 60 120 180 240 300 360; do K="$K --key $p:0x39 --key $((p + 2)):0xb9"; done
run dark37_join 37 68 42 420 $K; run dark39_join 39 126 23 420 $K; run dark43_join 43 74 16 420 $K
DOPTS="--byte 0:200:16"; run dark13_join 13 108 45 420 $K
DOPTS=""; run dark9_ask 9 202 29 260 $($PY tools/number_keys.py 38945 2>/dev/null)
K="$($PY tools/number_keys.py 13 2>/dev/null) $($PY tools/number_keys.py 23 82 2>/dev/null) --key 150:0x3b --key 152:0xbb --key 210:0x39 --key 212:0xb9"
run dark23_ask 23 72 21 360 $K
DOPTS=""
# story enemies (record byte 0x2f set; step 24): scene 4's (an opening message box, colour 0xc2, closed with Space, then a lost battle;
# fled by a strong hero three times: the party is put back at the record's bytes 0x28, 0x29); scene 35's record 8 won (all five killed:
# the enemy goes, its flag is set and the pass ends, then 1,800 world passes) and record 11 fled twice; scene 40's box closed with the
# right button
B4="--key 1:0x81 --key 30:0x39 --key 32:0xb9"
run boss4_lose 4 60 41 300 $B4 $($PY tools/battle_keys.py 3 300 --attack --from 40)
DOPTS="--word 0:4:30000 --hp 0:9000"
for seed in 1 2 3; do run "boss4_flee$seed" 4 60 41 1200 $B4 $($PY tools/battle_keys.py $seed 1200 --flee --from 40); done
run boss35_win 35 96 40 2500 $($PY tools/battle_keys.py 3 2500 --attack)
for seed in 1 2; do run "boss35_flee$seed" 35 120 40 1500 $($PY tools/battle_keys.py $seed 1500 --flee); done
DOPTS=""
run boss40_mouse 40 86 38 300 --world-passes 0 --mouse 0:300:30:0 --mouse 40:300:30:2 --mouse 42:300:30:0
run boss40_wait 40 86 38 300 --key 1:0x81 --key 120:0x39 --key 122:0xb9
# keys 1 and 2 in the fight with an FM card (--opl: the oracle's AdLib; the music button starts pressed): key 1 four times (the button
# goes up and down), key 2 (no Sound Blaster: nothing); a won battle after key 1, back in the world; walks into the enemy at two clock
# readings (the FM timer leaves a different [bp-0x58]); the music button pressed with the mouse
run fm_keys 1 106 36 200 --opl --key 1:0x81 --key 20:0x02 --key 22:0x82 --key 40:0x02 --key 42:0x82 --key 60:0x03 --key 62:0x83 \
    --key 80:0x02 --key 82:0x82
run fm_win 1 106 36 2500 --opl --key 30:0x02 --key 32:0x82 $($PY tools/battle_keys.py 3 2500 --attack --from 40)
run fm_walk_20 1 110 36 300 --opl --tick-base 20 --key 1:0x4b --key 9:0xcb
run fm_walk_30000 1 110 36 300 --opl --tick-base 30000 --key 1:0x4b --key 9:0xcb
run fm_mouse 1 106 36 300 --opl --world-passes 0 --key 1:0x81 --mouse 0:160:100:0 --mouse 10:290:105:0 --mouse 12:290:105:1 \
    --mouse 14:290:105:0 --mouse 30:290:105:1 --mouse 32:290:105:0 --mouse 50:160:100:0 --mouse 60:290:125:1 --mouse 62:290:125:0
# the music button pressed with the pointer on its top-left edge, which the press turns dark, then the pointer moved away
run fm_mouse_edge 1 106 36 120 --opl --world-passes 0 --key 1:0x81 --mouse 0:160:100:0 --mouse 10:270:99:0 --mouse 12:270:99:1 \
    --mouse 14:270:99:0 --mouse 30:160:100:0 --mouse 40:270:99:0 --mouse 42:270:99:1 --mouse 44:270:99:0 --mouse 60:160:100:0
# the Sound Blaster's sounds (step 25; --sb: the oracle's voice driver stubbed, tools/sb_stub.py; the sound button starts pressed): each
# sound started and cut is compared by pass and by which block of SND_EF.1 or _1_.LOC, the card's busy word following the model both
# sides share (a sound lasts its length at 35 passes a second). A won battle (the touch, the hits, the monsters' blows, the deaths, the
# rewards bar cutting the sound) and the sparks in the world after it; a party of five (each member's shout); spells; fleeing; the house
# door's roof both ways; shots that destroy an enemy; a right answer; a story enemy beaten by a strong hero; a walk in with the monsters
# first; key 2 turning the sounds off and on again; the sound button clicked
run sb_win 1 106 36 2500 --sb $($PY tools/battle_keys.py 3 2500 --attack)
DOPTS="--join 1 --join 2 --join 3 --join 4"; run sb_party5 1 106 36 2500 --sb $($PY tools/battle_keys.py 5 2500 --attack); DOPTS=""
DOPTS="--byte 0:0xc:120 --word 0:0xd:3000"
run sb_magic 1 106 36 600 --sb --key 2:0x81 --key 5:0x4d --key 7:0xcd --key 10:0x39 --key 12:0xb9 --key 20:0x02 --key 22:0x82; DOPTS=""
run sb_flee 1 106 36 1200 --sb $($PY tools/battle_keys.py 1 1200 --flee)
run sb_door 0 36 22 420 --sb $($PY tools/walk_keys.py door 0 420)
run sb_volley 1 106 33 900 --sb $($PY tools/walk_keys.py volley 1 900)
DOPTS="--talk 8:9:18"; run sb_ask 0 108 35 260 --sb $($PY tools/number_keys.py 38945 2>/dev/null); DOPTS=""
DOPTS="--word 0:4:30000 --hp 0:9000"; run sb_boss35 35 96 40 2500 --sb $($PY tools/battle_keys.py 3 2500 --attack); DOPTS=""
run sb_walk_20 1 110 36 300 --sb --tick-base 20 --key 1:0x4b --key 9:0xcb
KS="$($PY tools/battle_keys.py 3 1500 --attack --from 20 | tr ' ' '\n' | grep -v -- --key | awk -F: '$1 < 576 {printf "--key %s ", $0}')"
run sb_keys 1 106 36 1500 --sb --key 1:0x81 --key 10:0x03 --key 12:0x83 $KS --key 578:0x03 --key 580:0x83 \
    $($PY tools/battle_keys.py 4 1500 --attack --from 590)
run sb_mouse 1 106 36 300 --sb --world-passes 0 --key 1:0x81 --mouse 0:160:100:0 --mouse 10:290:125:0 --mouse 12:290:125:1 \
    --mouse 14:290:125:0 --mouse 30:290:125:1 --mouse 32:290:125:0 $($PY tools/battle_keys.py 3 300 --attack --from 40)
# F2, looking (step 26): scene 1's sign at (38, 55) (picture 4, message 219), looked at from above (the start faces the hero down), held
# 100 passes (two palette steps); from three cells up it is found the third time, from four not at all; from the left by the second
# cell; facing left, right (the cell below) and up, after a step; the signs with pictures 6, 3 and 5, scene 5's (2) and scene 6's
# (8, a dark scene); texts that go on (scene 6: five boxes, scene 2: two); scene 2's record 24 with flag 6 bit 0 set (its second entry)
# and not; F2 clicked and its box closed with the right button; walks with F2 (and F1) now and then in scenes 1 and 0
L="--key 1:0x81 --key 3:0x3c --key 5:0xbc --key 60:0x39 --key 62:0xb9 --key 120:0x1c --key 122:0x9c --key 180:0x39 --key 182:0xb9"
L="$L --key 240:0x39 --key 242:0xb9 --key 300:0x39 --key 302:0xb9"
run look_held 1 38 54 200 --key 1:0x81 --key 50:0x3c --key 52:0xbc --key 150:0x39 --key 152:0xb9
run look_far3 1 38 53 340 $L; run look_far4 1 38 52 340 $L; run look_second 1 36 54 340 $L
# the other three ways (the start faces down whatever the party record says): a step first, then F2 as above from pass 20
L2="$(echo "$L" | sed 's/--key 1:0x81 --key 3:0x3c --key 5:0xbc/--key 20:0x3c --key 22:0xbc/')"
run look_left 1 42 55 340 --key 1:0x4b --key 3:0xcb $L2; run look_right 1 34 54 340 --key 1:0x4d --key 3:0xcd $L2
run look_up 1 38 57 340 --key 1:0x48 --key 3:0xc8 $L2
run look_pic6 1 116 21 340 $L; run look_pic3 1 220 42 340 $L; run look_pic5 1 150 76 340 $L; run look_pic2 5 128 30 340 $L
run look_dark8 6 36 12 340 --key 1:0x48 --key 3:0xc8 $L2; run look_five 6 120 23 340 $L; run look_two 2 230 17 340 $L
run look_flag0 2 98 31 340 $L; DOPTS="--byte 5:6:1"; run look_flag1 2 98 31 340 $L; DOPTS=""
run look_mouse 1 38 54 200 --mouse 0:160:100:0 --mouse 3:20:25:1 --mouse 5:20:25:0 --mouse 60:200:100:2 --mouse 62:200:100:0 \
    --mouse 90:200:100:2 --mouse 92:200:100:0
for seed in 1 2 3; do run "look_walk$seed" 1 40 54 2500 $($PY tools/walk_keys.py look $seed 2500); done
run look_walk0 0 86 35 2500 $($PY tools/walk_keys.py look 4 2500)
# the view past the map's edges (step 27): above the top (lit, and dark scene 13), left of it, and below the bottom in scenes 2, 4, 7
# and 24, idle; walks along the bottom of scenes 4 and 2 (tools/walk_keys.py fuzz); F2 near the top (a look's text over the map
# buffer is not modelled: no start goes there)
run edge_top5 1 116 5 300; run edge_top9 1 116 9 300; run edge_top9r 1 230 9 300; run edge_dark13 13 100 5 300; run edge_left 1 10 40 300
run edge_s2 2 22 90 300; run edge_s4 4 22 94 300; run edge_s7 7 138 94 300; run edge_s24 24 26 92 300
run edge_walk4 4 22 94 1500 $($PY tools/walk_keys.py fuzz 4 1500); run edge_walk2 2 22 90 1500 $($PY tools/walk_keys.py fuzz 2 1500)
# F3, taking (step 28): scene 1's item at (126, 45) (item 1; its object goes when taken), from above (the start faces down), held 100
# passes; then F3 again (nothing left) and from the second and third cells and three cells away; the sound waited for (--sb); closed
# with Esc, Enter and the right button; F3 clicked; every item place full (message 11); a first member with no HP (the second gets it);
# scene 5's record 5 with its flag (3, 3) not set and set; coins in scenes 6 (dark, 50) and 11 (10), and too many (message 11); walks
# with F3 now and then (tools/walk_keys.py take)
T="--key 1:0x81 --key 3:0x3d --key 5:0xbd --key 100:0x39 --key 102:0xb9 --key 140:0x3d --key 142:0xbd --key 200:0x39 --key 202:0xb9"
run take_held 1 126 44 260 $T
run take_second 1 124 44 160 --key 1:0x81 --key 3:0x3d --key 5:0xbd --key 100:0x1c --key 102:0x9c
run take_third 1 128 44 160 --key 1:0x81 --key 3:0x3d --key 5:0xbd --key 100:0x01 --key 102:0x81
run take_far 1 126 43 160 --key 1:0x81 --key 3:0x3d --key 5:0xbd --key 100:0x39 --key 102:0xb9
run take_far4 1 126 42 160 --key 1:0x81 --key 3:0x3d --key 5:0xbd --key 100:0x39 --key 102:0xb9
run take_sb 1 126 44 160 --sb --key 1:0x81 --key 3:0x3d --key 5:0xbd --key 100:0x39 --key 102:0xb9
run take_mouse 1 126 44 200 --mouse 0:160:100:0 --mouse 3:20:45:1 --mouse 5:20:45:0 --mouse 60:200:100:3 --mouse 62:200:100:0 \
    --mouse 90:200:100:2 --mouse 92:200:100:0
# facing left and right (a step first): the item from the third cell and the second, the hero's frame from the third cell's y
TL="--key 20:0x3d --key 22:0xbd --key 120:0x39 --key 122:0xb9"
run take_left 1 130 45 200 --key 1:0x4b --key 3:0xcb $TL; run take_right 1 122 46 200 --key 1:0x4d --key 3:0xcd $TL
FULL=""; for o in $(seq 20 39); do FULL="$FULL --byte 0:$o:1"; done
DOPTS="$FULL"; run take_full 1 126 44 200 $T; DOPTS="--join 1 --hp 0:0"; run take_second_member 1 126 44 200 $T
DOPTS=""; run take_flag0 5 148 33 200 $T; DOPTS="--byte 5:3:8"; run take_flag1 5 148 33 200 $T
DOPTS=""; run take_coins6 6 68 9 260 $T; run take_coins11 11 64 70 200 $T
DOPTS="--word 7:10:59995"; run take_rich 11 64 70 200 $T; DOPTS=""
for seed in 1 2; do run "take_walk$seed" 1 126 40 2500 $($PY tools/walk_keys.py take $seed 2500); done
run take_walk11 11 66 66 2500 $($PY tools/walk_keys.py take 3 2500)
# F4, casting (step 29): the hero at level 120 with 300 MP and member 1 with 5 HP (step 17's spells): the caster picked with Enter, his
# second spell (a heal) on member 1, with long waits in the picker and the list; a spell on the monsters (nothing; the list stays);
# Esc in the caster's picker; a fallen caster (nothing); Esc in the helped member's picker (back to the list); a defence spell and a
# revive (level 200, member 1 fallen); a greyed spell (MP 99); member 1 casting (his own list); the mouse; a dark scene; walks with F4
# (tools/walk_keys.py cast)
CA="--byte 0:0xc:120 --word 0:0xd:300 --join 1 --hp 1:5"
DOPTS="$CA"
run cast_heal 1 106 30 200 --key 1:0x81 --key 3:0x3e --key 5:0xbe --key 40:0x1c --key 42:0x9c --key 80:0x50 --key 82:0xd0 \
    --key 100:0x02 --key 102:0x82 --key 140:0x4d --key 142:0xcd --key 150:0x1c --key 152:0x9c
run cast_attack 1 106 30 160 --key 1:0x81 --key 3:0x3e --key 5:0xbe --key 20:0x1c --key 22:0x9c --key 40:0x02 --key 42:0x82 \
    --key 80:0x01 --key 82:0x81
run cast_esc 1 106 30 100 --key 1:0x81 --key 3:0x3e --key 5:0xbe --key 40:0x01 --key 42:0x81
run cast_cancel 1 106 30 200 --key 1:0x81 --key 3:0x3e --key 5:0xbe --key 20:0x1c --key 22:0x9c --key 40:0x50 --key 42:0xd0 \
    --key 60:0x02 --key 62:0x82 --key 80:0x01 --key 82:0x81 --key 120:0x01 --key 122:0x81
DOPTS="$CA --byte 0:0xc:200 --word 0:0xd:600"
run cast_defence 1 106 30 200 --key 1:0x81 --key 3:0x3e --key 5:0xbe --key 20:0x1c --key 22:0x9c --key 40:0x50 --key 42:0xd0 \
    --key 50:0x50 --key 52:0xd0 --key 60:0x50 --key 62:0xd0 --key 80:0x02 --key 82:0x82 --key 100:0x1c --key 102:0x9c
DOPTS="$CA --byte 0:0xc:200 --word 0:0xd:600 --hp 1:0"
run cast_dead 1 106 30 100 --key 1:0x81 --key 3:0x3e --key 5:0xbe --key 20:0x4d --key 22:0xcd --key 40:0x1c --key 42:0x9c
run cast_revive 1 106 30 200 --key 1:0x81 --key 3:0x3e --key 5:0xbe --key 20:0x1c --key 22:0x9c --key 30:0x4d --key 32:0xcd \
    --key 40:0x50 --key 42:0xd0 --key 45:0x50 --key 47:0xd0 --key 50:0x02 --key 52:0x82 --key 80:0x4d --key 82:0xcd --key 100:0x1c \
    --key 102:0x9c
DOPTS="$CA --word 0:0xd:99"
run cast_grey 1 106 30 160 --key 1:0x81 --key 3:0x3e --key 5:0xbe --key 20:0x1c --key 22:0x9c --key 40:0x50 --key 42:0xd0 \
    --key 60:0x02 --key 62:0x82 --key 100:0x01 --key 102:0x81
DOPTS="$CA --byte 1:0xc:60 --word 1:0xd:300"
run cast_member1 1 106 30 200 --key 1:0x81 --key 3:0x3e --key 5:0xbe --key 20:0x4d --key 22:0xcd --key 40:0x1c --key 42:0x9c \
    --key 60:0x50 --key 62:0xd0 --key 80:0x02 --key 82:0x82 --key 120:0x1c --key 122:0x9c
DOPTS="$CA"
run cast_mouse 1 106 30 300 --mouse 0:160:100:0 --mouse 3:20:65:1 --mouse 5:20:65:0 --mouse 30:140:80:0 --mouse 40:140:80:1 \
    --mouse 42:140:80:0 --mouse 70:100:125:1 --mouse 72:100:125:0 --mouse 100:90:80:1 --mouse 102:90:80:0 --mouse 130:160:80:0 \
    --mouse 140:160:80:1 --mouse 142:160:80:0 --mouse 200:160:100:2 --mouse 202:160:100:0
run cast_dark13 13 100 50 200 --key 1:0x81 --key 3:0x3e --key 5:0xbe --key 40:0x1c --key 42:0x9c --key 80:0x50 --key 82:0xd0 \
    --key 100:0x02 --key 102:0x82 --key 140:0x4d --key 142:0xcd --key 150:0x1c --key 152:0x9c
for seed in 1 2 3; do run "cast_walk$seed" 1 106 30 2500 $($PY tools/walk_keys.py cast $seed 2500); done
DOPTS=""
# F5, items (step 30): the hero with items 1 (food), 3 (a sword), 9 (water) and member 1 with 5 HP: key 3 gives the first to member 1
# (with long waits); key 1 eats and wears; key 2 turns the page; key 3 to himself, to a fallen member, to one with no room, and Esc in its
# picker; Esc in the first picker; member 1's own list; twelve items over both pages; the mouse (the third icon gives); a dark scene;
# walks with F5 (tools/walk_keys.py items)
IT="--byte 0:0x14:1 --byte 0:0x15:3 --byte 0:0x17:9 --join 1 --hp 1:5 --hp 0:20"
DOPTS="$IT"
run items_give 1 106 30 300 --key 1:0x81 --key 3:0x3f --key 5:0xbf --key 40:0x1c --key 42:0x9c --key 100:0x04 --key 102:0x84 \
    --key 140:0x4d --key 142:0xcd --key 160:0x1c --key 162:0x9c --key 200:0x01 --key 202:0x81
run items_use 1 106 30 200 --key 1:0x81 --key 3:0x3f --key 5:0xbf --key 20:0x1c --key 22:0x9c --key 40:0x02 --key 42:0x82 \
    --key 60:0x50 --key 62:0xd0 --key 70:0x02 --key 72:0x82 --key 90:0x50 --key 92:0xd0 --key 100:0x02 --key 102:0x82 \
    --key 120:0x03 --key 122:0x83 --key 140:0x03 --key 142:0x83 --key 160:0x01 --key 162:0x81
run items_self 1 106 30 200 --key 1:0x81 --key 3:0x3f --key 5:0xbf --key 20:0x1c --key 22:0x9c --key 40:0x04 --key 42:0x84 \
    --key 60:0x1c --key 62:0x9c --key 80:0x04 --key 82:0x84 --key 100:0x01 --key 102:0x81 --key 140:0x01 --key 142:0x81
run items_esc 1 106 30 100 --key 1:0x81 --key 3:0x3f --key 5:0xbf --key 40:0x01 --key 42:0x81
run items_member1 1 106 30 200 --key 1:0x81 --key 3:0x3f --key 5:0xbf --key 20:0x4d --key 22:0xcd --key 40:0x1c --key 42:0x9c \
    --key 140:0x01 --key 142:0x81
DOPTS="$IT --hp 1:0"
run items_dead 1 106 30 200 --key 1:0x81 --key 3:0x3f --key 5:0xbf --key 20:0x1c --key 22:0x9c --key 40:0x04 --key 42:0x84 \
    --key 60:0x4d --key 62:0xcd --key 80:0x1c --key 82:0x9c --key 140:0x01 --key 142:0x81
FULL1=""; for o in $(seq 20 39); do FULL1="$FULL1 --byte 1:$o:1"; done
DOPTS="$IT$FULL1"
run items_full 1 106 30 200 --key 1:0x81 --key 3:0x3f --key 5:0xbf --key 20:0x1c --key 22:0x9c --key 40:0x04 --key 42:0x84 \
    --key 60:0x4d --key 62:0xcd --key 80:0x1c --key 82:0x9c --key 140:0x01 --key 142:0x81
MANY="$IT"; for o in 24 25 26 27 28 29 30 31 32; do MANY="$MANY --byte 0:$o:$((o - 13))"; done
DOPTS="$MANY"
run items_pages 1 106 30 200 --key 1:0x81 --key 3:0x3f --key 5:0xbf --key 20:0x1c --key 22:0x9c --key 40:0x03 --key 42:0x83 \
    --key 60:0x50 --key 62:0xd0 --key 70:0x04 --key 72:0x84 --key 90:0x4d --key 92:0xcd --key 100:0x1c --key 102:0x9c \
    --key 120:0x03 --key 122:0x83 --key 160:0x01 --key 162:0x81
DOPTS="$IT"
run items_mouse 1 106 30 300 --key 1:0x81 --mouse 0:160:100:0 --mouse 3:20:85:1 --mouse 5:20:85:0 --mouse 30:140:80:0 --mouse 40:140:80:1 \
    --mouse 42:140:80:0 --mouse 70:100:125:1 --mouse 72:100:125:0 --mouse 100:124:80:1 --mouse 102:124:80:0 --mouse 130:165:80:0 \
    --mouse 140:165:80:1 --mouse 142:165:80:0 --mouse 200:160:100:2 --mouse 202:160:100:0
run items_dark13 13 100 50 200 --key 1:0x81 --key 3:0x3f --key 5:0xbf --key 40:0x1c --key 42:0x9c --key 100:0x04 --key 102:0x84 \
    --key 140:0x4d --key 142:0xcd --key 160:0x1c --key 162:0x9c
for seed in 1 2 3; do run "items_walk$seed" 1 106 30 2500 $($PY tools/walk_keys.py items $seed 2500); done
# F6, the equipment screen (step 31): the hero wears a hat, another's armour (0x27, halved), a sword, gloves, member 1's shield and
# boots: Enter and Space take two off, F5 to the list and F6 back, Esc; Esc in the picker; nothing worn (Enter on nothing); no room
# for what is taken off; member 1 (his own shield in 0xa0); F6 from the item list's second page and the second row; a long wait; the
# mouse (rows lit by the pointer, a press takes off, F5's button, the right button); a dark scene; walks with F5 and F6
EQ="$IT --byte 0:0x12c:14 --byte 0:0x12d:27 --byte 0:0x12e:5 --byte 0:0x12f:12 --byte 0:0x130:42 --byte 0:0x131:13"
DOPTS="$EQ"
run equip_off 1 106 30 300 --key 1:0x81 --key 3:0x40 --key 5:0xc0 --key 40:0x1c --key 42:0x9c --key 70:0x50 --key 72:0xd0 \
    --key 90:0x1c --key 92:0x9c --key 110:0x50 --key 112:0xd0 --key 130:0x39 --key 132:0xb9 --key 150:0x48 --key 152:0xc8 \
    --key 170:0x3f --key 172:0xbf --key 200:0x40 --key 202:0xc0 --key 240:0x01 --key 242:0x81
run equip_esc 1 106 30 100 --key 1:0x81 --key 3:0x40 --key 5:0xc0 --key 40:0x01 --key 42:0x81
run equip_wait 1 106 30 400 --key 1:0x81 --key 3:0x40 --key 5:0xc0 --key 20:0x1c --key 22:0x9c --key 300:0x50 --key 302:0xd0 \
    --key 310:0x50 --key 312:0xd0 --key 320:0x50 --key 322:0xd0 --key 330:0x50 --key 332:0xd0 --key 340:0x50 --key 342:0xd0 \
    --key 350:0x50 --key 352:0xd0 --key 360:0x1c --key 362:0x9c --key 380:0x01 --key 382:0x81
# the pointer kept at (0, 0), where the item list does not move it, so the list's palette count starts from the screen's last y; then a
# left press held from the item list into the screen over a row (it takes nothing off until released)
run equip_corner 1 106 30 160 --key 1:0x81 --mouse 0:0:0:0 --key 3:0x40 --key 5:0xc0 --key 20:0x1c --key 22:0x9c --key 50:0x3f \
    --key 52:0xbf --key 120:0x01 --key 122:0x81
run equip_held 1 106 30 200 --key 1:0x81 --key 3:0x40 --key 5:0xc0 --key 20:0x1c --key 22:0x9c --key 40:0x3f --key 42:0xbf \
    --mouse 0:300:30:0 --mouse 60:100:110:0 --mouse 70:100:110:1 --key 80:0x40 --key 82:0xc0 --mouse 120:100:110:0 \
    --mouse 140:100:126:1 --mouse 142:100:126:0 --key 170:0x01 --key 172:0x81
run equip_fromlist 1 106 30 300 --key 1:0x81 --key 3:0x3f --key 5:0xbf --key 20:0x1c --key 22:0x9c --key 40:0x03 --key 42:0x83 \
    --key 50:0x50 --key 52:0xd0 --key 60:0x40 --key 62:0xc0 --key 90:0x50 --key 92:0xd0 --key 100:0x1c --key 102:0x9c \
    --key 120:0x3f --key 122:0xbf --key 160:0x40 --key 162:0xc0 --key 200:0x01 --key 202:0x81
DOPTS="$EQ --join 1 --hp 1:30 --byte 0:0x132:40 --byte 0:0x133:51 --byte 0:0x134:83 --byte 0:0x136:42"
run equip_member1 1 106 30 200 --key 1:0x81 --key 3:0x40 --key 5:0xc0 --key 20:0x4d --key 22:0xcd --key 40:0x1c --key 42:0x9c \
    --key 70:0x50 --key 72:0xd0 --key 90:0x1c --key 92:0x9c --key 140:0x01 --key 142:0x81
DOPTS="$IT"
run equip_none 1 106 30 200 --key 1:0x81 --key 3:0x40 --key 5:0xc0 --key 40:0x1c --key 42:0x9c --key 60:0x1c --key 62:0x9c \
    --key 80:0x50 --key 82:0xd0 --key 100:0x39 --key 102:0xb9 --key 140:0x01 --key 142:0x81
FULL0=""; for o in $(seq 20 39); do FULL0="$FULL0 --byte 0:$o:1"; done
DOPTS="$EQ$FULL0"
run equip_full 1 106 30 200 --key 1:0x81 --key 3:0x40 --key 5:0xc0 --key 40:0x1c --key 42:0x9c --key 60:0x1c --key 62:0x9c \
    --key 80:0x50 --key 82:0xd0 --key 100:0x1c --key 102:0x9c --key 140:0x01 --key 142:0x81
DOPTS="$EQ"
run equip_mouse 1 106 30 360 --key 1:0x81 --mouse 0:160:100:0 --mouse 3:20:105:1 --mouse 5:20:105:0 --mouse 30:140:80:0 \
    --mouse 40:140:80:1 --mouse 42:140:80:0 --mouse 60:100:96:0 --mouse 70:100:115:0 --mouse 80:100:115:1 --mouse 82:100:115:0 \
    --mouse 100:100:150:0 --mouse 110:100:150:1 --mouse 112:100:150:0 --mouse 130:20:90:0 --mouse 140:20:90:1 --mouse 142:20:90:0 \
    --mouse 230:20:105:1 --mouse 232:20:105:0 --mouse 260:100:130:0 --mouse 290:100:130:2 --mouse 292:100:130:0
run equip_dark13 13 100 50 200 --key 1:0x81 --key 3:0x40 --key 5:0xc0 --key 40:0x1c --key 42:0x9c --key 100:0x50 --key 102:0xd0 \
    --key 120:0x1c --key 122:0x9c --key 160:0x01 --key 162:0x81
for seed in 1 2 3; do run "equip_walk$seed" 1 106 30 2500 $($PY tools/walk_keys.py equip $seed 2500); done
# F7, use (step 32): F7 (pass 3); the prompt's Right (20) and Enter (30) for an item, the picker's Enter (50), the list's key 1 (70).
# Scene 0's record 11 at (82, 35) takes item 1 (food, byte 0x11 = 0: may be thrown away), item 2 may not; scene 1's record 15 at
# (130, 14) takes item 35 and its flag fires a cut-scene record (the window drawn again); scene 21's record 23 at (64, 27) and dark
# scene 23's records 17 and 18 take coins (100, 50)
U="--key 1:0x81 --key 3:0x41 --key 5:0xc1"
UI="$U --key 20:0x4d --key 22:0xcd --key 30:0x1c --key 32:0x9c --key 50:0x1c --key 52:0x9c --key 70:0x02 --key 72:0x82"
DOPTS="--byte 0:0x14:1 --byte 0:0x15:2 --byte 0:0x16:3"
run use_item 0 82 34 200 $UI
run use_item_left 0 84 35 200 --key 1:0x4b --key 4:0xcb --key 10:0x41 --key 12:0xc1 --key 25:0x4d --key 27:0xcd --key 35:0x1c \
    --key 37:0x9c --key 55:0x1c --key 57:0x9c --key 75:0x02 --key 77:0x82
run use_throw 0 82 30 300 $UI --key 120:0x1c --key 122:0x9c
run use_throw_no 0 82 30 300 $UI --key 100:0x4d --key 102:0xcd --key 120:0x1c --key 122:0x9c
run use_wrong 0 82 34 300 $U --key 20:0x4d --key 22:0xcd --key 30:0x1c --key 32:0x9c --key 50:0x1c --key 52:0x9c --key 60:0x50 \
    --key 62:0xd0 --key 64:0x50 --key 66:0xd0 --key 70:0x02 --key 72:0x82 --key 120:0x1c --key 122:0x9c
run use_keep 0 82 30 300 $U --key 20:0x4d --key 22:0xcd --key 30:0x1c --key 32:0x9c --key 50:0x1c --key 52:0x9c --key 60:0x50 \
    --key 62:0xd0 --key 70:0x02 --key 72:0x82 --key 150:0x39 --key 152:0xb9
run use_esc 0 82 34 100 $U --key 20:0x4d --key 22:0xcd --key 40:0x01 --key 42:0x81
run use_esc_list 0 82 34 150 $U --key 20:0x4d --key 22:0xcd --key 30:0x1c --key 32:0x9c --key 50:0x1c --key 52:0x9c --key 70:0x01 \
    --key 72:0x81
run use_f6 0 82 34 200 $U --key 20:0x4d --key 22:0xcd --key 30:0x1c --key 32:0x9c --key 50:0x1c --key 52:0x9c --key 70:0x40 \
    --key 72:0xc0 --key 150:0x1c --key 152:0x9c
run use_wait 0 82 30 600 $U --key 100:0x4d --key 102:0xcd --key 200:0x1c --key 202:0x9c --key 300:0x1c --key 302:0x9c --key 400:0x02 \
    --key 402:0x82 --key 500:0x1c --key 502:0x9c
run use_mouse 0 82 34 300 --key 1:0x81 --mouse 0:160:100:0 --mouse 3:20:130:1 --mouse 5:20:130:0 --mouse 20:120:157:0 \
    --mouse 25:200:157:0 --mouse 30:200:157:1 --mouse 32:200:157:0 --key 50:0x1c --key 52:0x9c --key 70:0x02 --key 72:0x82
run use_mouse_esc 0 82 34 200 --key 1:0x81 --mouse 0:160:100:0 --mouse 3:20:130:1 --mouse 5:20:130:0 --mouse 20:110:157:0 \
    --mouse 40:110:157:2 --mouse 42:110:157:0
# the item word's left end; Space takes the lit word; the pointer in the window before F7 ([bp-0x96] = x - 0x30: the name not shown)
run use_mouse_edge 0 82 34 200 --key 1:0x81 --mouse 0:160:100:0 --mouse 3:20:130:1 --mouse 5:20:130:0 --mouse 20:184:157:0 \
    --mouse 30:184:157:1 --mouse 32:184:157:0 --key 50:0x1c --key 52:0x9c --key 70:0x02 --key 72:0x82
run use_space 0 82 34 200 $U --key 20:0x4d --key 22:0xcd --key 30:0x39 --key 32:0xb9 --key 50:0x1c --key 52:0x9c --key 70:0x02 --key 72:0x82
run use_pointer 0 82 34 200 --mouse 0:160:100:0 --mouse 4:150:100:0 --key 1:0x81 --key 10:0x41 --key 12:0xc1 --key 20:0x4d \
    --key 22:0xcd --key 30:0x1c --key 32:0x9c --key 50:0x1c --key 52:0x9c --key 70:0x02 --key 72:0x82
# facing left two cells from the record; coins on an item's place (passed over: message 0x173), with the pointer about
run use_item_left2 0 86 35 200 --key 1:0x4b --key 4:0xcb --key 10:0x41 --key 12:0xc1 --key 25:0x4d --key 27:0xcd --key 35:0x1c \
    --key 37:0x9c --key 55:0x1c --key 57:0x9c --key 75:0x02 --key 77:0x82
DOPTS="--byte 0:0x14:3 --byte 0:0x15:3 --byte 0:0x16:3 --byte 0:0x17:3 --byte 0:0x18:3 --byte 0:0x19:3 --byte 0:0x1a:3 --byte 0:0x1b:3 \
    --byte 0:0x1c:3 --byte 0:0x1d:3 --byte 0:0x1e:1"
run use_page2 0 82 34 200 $U --key 20:0x4d --key 22:0xcd --key 30:0x1c --key 32:0x9c --key 50:0x1c --key 52:0x9c --key 60:0x03 \
    --key 62:0x83 --key 70:0x02 --key 72:0x82
DOPTS="--word 0:0x122:500"
run use_coins_place 0 82 34 300 --key 1:0x81 --mouse 0:160:100:0 --key 3:0x41 --key 5:0xc1 --key 20:0x1c --key 21:0x9c \
    --key 40:0x48 --key 42:0xc8 --key 60:0x1c --key 62:0x9c --mouse 100:150:120:0 --key 140:0x1c --key 142:0x9c --mouse 200:170:60:0
DOPTS="--byte 0:0x14:1 --byte 0:0x15:2 --byte 0:0x16:3"
DOPTS="--byte 0:0x14:35"
run use_cut 1 130 13 200 $UI --tick-base 16000
run use_cut_20 1 130 13 200 $UI
# coins: Enter (20) takes the lit coins; the number box: Left twice (tens, hundreds), Up, Enter
UC="$U --key 20:0x1c --key 21:0x9c --key 40:0x4b --key 42:0xcb --key 50:0x4b --key 52:0xcb --key 60:0x48 --key 62:0xc8"
DOPTS="--word 0:0x122:500"
run use_coins 21 64 26 200 $UC --key 80:0x1c --key 82:0x9c
run use_coins_wrong 21 64 26 300 $UC --key 70:0x48 --key 72:0xc8 --key 80:0x1c --key 82:0x9c --key 150:0x1c --key 152:0x9c
run use_coins_low 21 64 26 200 $UC --key 70:0x48 --key 72:0xc8 --key 74:0x48 --key 76:0xc8 --key 77:0x4d --key 79:0xcd \
    --key 90:0x48 --key 92:0xc8 --key 94:0x48 --key 96:0xc8 --key 98:0x48 --key 100:0xc8 --key 102:0x48 --key 104:0xc8 \
    --key 106:0x48 --key 108:0xc8 --key 110:0x4d --key 112:0xcd --key 120:0x48 --key 122:0xc8 --key 124:0x48 --key 126:0xc8 \
    --key 128:0x48 --key 130:0xc8 --key 132:0x48 --key 134:0xc8 --key 136:0x48 --key 138:0xc8 --key 140:0x48 --key 142:0xc8 \
    --key 160:0x1c --key 162:0x9c
run use_coins_zero 21 64 26 150 $U --key 20:0x1c --key 21:0x9c --key 80:0x1c --key 82:0x9c
DOPTS="--word 0:0x122:60"
run use_coins_poor 21 64 26 200 $UC --key 80:0x1c --key 82:0x9c
DOPTS="--word 0:0x122:500"
run use_coins_dark23 23 70 17 300 $U --key 20:0x1c --key 21:0x9c --key 40:0x4b --key 42:0xcb --key 60:0x48 --key 62:0xc8 \
    --key 64:0x48 --key 66:0xc8 --key 68:0x48 --key 70:0xc8 --key 72:0x48 --key 74:0xc8 --key 76:0x48 --key 78:0xc8 --key 100:0x1c \
    --key 102:0x9c
DOPTS="--byte 0:0x14:1 --byte 0:0x15:2 --byte 0:0x16:3 --word 0:0x122:500"
for seed in 1 2 3; do run "use_walk$seed" 0 82 30 2500 $($PY tools/walk_keys.py use $seed 2500); done
# F7 at a sell counter (kind 0x14, step 36): scene 0's record 17 at (106, 11), scene 2's 31 at (88, 75), 21's 47 at (108, 36), 33's 43
# at (134, 59), 41's 38 at (114, 36). Items 1 (price 20), 2 (may not be sold: message 0xd4) and 5 (6300); yes / no at 150 (Enter: yes)
UI="$U --key 20:0x4d --key 22:0xcd --key 30:0x1c --key 32:0x9c --key 50:0x1c --key 52:0x9c --key 70:0x02 --key 72:0x82"
DOPTS="--byte 0:0x14:1 --byte 0:0x15:2 --byte 0:0x16:5"
run pawn_item 0 106 10 300 $UI --key 150:0x1c --key 152:0x9c
run pawn_no 0 106 10 300 $UI --key 120:0x4d --key 122:0xcd --key 150:0x1c --key 152:0x9c
run pawn_no_yes 0 106 10 300 $UI --key 100:0x4d --key 102:0xcd --key 120:0x4b --key 122:0xcb --key 150:0x39 --key 152:0xb9
run pawn_keep 0 106 10 300 $U --key 20:0x4d --key 22:0xcd --key 30:0x1c --key 32:0x9c --key 50:0x1c --key 52:0x9c --key 60:0x50 \
    --key 62:0xd0 --key 70:0x02 --key 72:0x82 --key 150:0x1c --key 152:0x9c
run pawn_big 0 106 10 300 $U --key 20:0x4d --key 22:0xcd --key 30:0x1c --key 32:0x9c --key 50:0x1c --key 52:0x9c --key 60:0x50 \
    --key 62:0xd0 --key 64:0x50 --key 66:0xd0 --key 70:0x02 --key 72:0x82 --key 150:0x1c --key 152:0x9c
run pawn_left 0 108 11 300 --key 1:0x4b --key 4:0xcb --key 10:0x41 --key 12:0xc1 --key 25:0x4d --key 27:0xcd --key 35:0x1c \
    --key 37:0x9c --key 55:0x1c --key 57:0x9c --key 75:0x02 --key 77:0x82 --key 150:0x1c --key 152:0x9c
run pawn_far 0 106 14 300 --key 1:0x48 --key 4:0xc8 --key 10:0x41 --key 12:0xc1 --key 25:0x4d --key 27:0xcd --key 35:0x1c \
    --key 37:0x9c --key 55:0x1c --key 57:0x9c --key 75:0x02 --key 77:0x82 --key 150:0x1c --key 152:0x9c
run pawn_sb 0 106 10 300 --sb $UI --key 150:0x1c --key 152:0x9c
run pawn_mouse 0 106 10 300 --key 1:0x81 --mouse 0:160:100:0 --mouse 3:20:130:1 --mouse 5:20:130:0 --mouse 20:200:157:0 \
    --mouse 30:200:157:1 --mouse 32:200:157:0 --key 50:0x1c --key 52:0x9c --key 70:0x02 --key 72:0x82 --mouse 120:280:45:0 \
    --mouse 150:280:45:1 --mouse 152:280:45:0
run pawn_mouse_no 0 106 10 300 --key 1:0x81 --mouse 0:160:100:0 --mouse 3:20:130:1 --mouse 5:20:130:0 --mouse 20:200:157:0 \
    --mouse 30:200:157:1 --mouse 32:200:157:0 --key 50:0x1c --key 52:0x9c --key 70:0x02 --key 72:0x82 --mouse 120:300:45:0 \
    --mouse 150:300:45:1 --mouse 152:300:45:0
run pawn_keep_mouse 0 106 10 300 --key 1:0x81 --mouse 0:160:100:0 --mouse 3:20:130:1 --mouse 5:20:130:0 --mouse 20:200:157:0 \
    --mouse 30:200:157:1 --mouse 32:200:157:0 --key 50:0x1c --key 52:0x9c --key 60:0x50 --key 62:0xd0 --key 70:0x02 --key 72:0x82 \
    --mouse 150:200:100:2 --mouse 152:200:100:0
run pawn_twice 0 106 10 400 $UI --key 150:0x1c --key 152:0x9c --key 180:0x41 --key 182:0xc1 --key 200:0x4d --key 202:0xcd \
    --key 210:0x1c --key 212:0x9c --key 230:0x1c --key 232:0x9c --key 250:0x02 --key 252:0x82 --key 330:0x1c --key 332:0x9c
DOPTS="--byte 0:0x14:5 --word 0:0x122:65000"
run pawn_wrap 0 106 10 300 $UI --key 150:0x1c --key 152:0x9c
# an odd price (item 16, 15: half 7); the pointer resting inside the sale's box while the keys work
DOPTS="--byte 0:0x14:16"
run pawn_odd 0 106 10 300 $UI --key 150:0x1c --key 152:0x9c
run pawn_mouse_in 0 106 10 300 --mouse 0:150:80:0 $UI --key 150:0x1c --key 152:0x9c
DOPTS="--join 1 --byte 1:0x14:1"
run pawn_member1 0 106 10 300 $U --key 20:0x4d --key 22:0xcd --key 30:0x1c --key 32:0x9c --key 40:0x4d --key 42:0xcd --key 50:0x1c \
    --key 52:0x9c --key 70:0x02 --key 72:0x82 --key 150:0x1c --key 152:0x9c
DOPTS="--byte 0:0x14:1 --byte 0:0x15:2 --byte 0:0x16:5"
run pawn_s2 2 88 74 300 $UI --key 150:0x1c --key 152:0x9c
run pawn_s21 21 108 35 300 $UI --key 150:0x1c --key 152:0x9c
run pawn_s33 33 134 58 300 $UI --key 150:0x1c --key 152:0x9c
run pawn_s41 41 114 35 300 $UI --key 150:0x1c --key 152:0x9c
for seed in 4 5 6; do run "pawn_walk$seed" 0 106 10 2500 $($PY tools/walk_keys.py use $seed 2500); done
DOPTS=""
# F8, the map (step 33): F8 (pass 3) with the map (item 0x13) in the hero's first item place; the mark blinks (45 rounds shown, 36
# not); Esc, Space, Enter or the right button alone close it. In scenes 0 to 23 (the right edge, the bottom rows, a dark scene, row 5)
M="--key 1:0x81 --key 3:0x42 --key 5:0xc2"
DOPTS="--byte 0:0x14:0x13"
run map_s1 1 106 30 400 $M --key 300:0x01 --key 302:0x81
run map_space 1 106 30 120 $M --key 60:0x39 --key 62:0xb9
run map_enter 1 106 30 120 $M --key 60:0x1c --key 62:0x9c --key 80:0x42 --key 82:0xc2 --key 110:0x01 --key 112:0x81
run map_s0 0 82 34 150 $M --key 100:0x01 --key 102:0x81; run map_s2 2 230 17 150 $M --key 100:0x01 --key 102:0x81
run map_s4 4 22 94 150 $M --key 100:0x01 --key 102:0x81; run map_s7 7 138 94 150 $M --key 100:0x01 --key 102:0x81
run map_dark13 13 100 50 150 $M --key 100:0x01 --key 102:0x81; run map_s6 6 36 12 150 $M --key 100:0x01 --key 102:0x81
run map_top 1 116 5 150 $M --key 100:0x01 --key 102:0x81; run map_s23 23 70 17 150 $M --key 100:0x01 --key 102:0x81
# the mouse: F8's button clicked; the left button and both do nothing, the right one alone closes it
run map_mouse 1 106 30 200 --key 1:0x81 --mouse 0:160:100:0 --mouse 3:20:150:1 --mouse 5:20:150:0 --mouse 40:160:100:1 \
    --mouse 42:160:100:0 --mouse 60:160:100:3 --mouse 62:160:100:0 --mouse 100:160:100:2 --mouse 102:160:100:0
# the map in the last item place, in the second member's, or nowhere (the button only); [bp-0x96] after it, for F7's name blink
DOPTS="--byte 0:0x27:0x13"
run map_last 1 106 30 100 $M --key 60:0x01 --key 62:0x81
DOPTS="--join 1 --byte 1:0x1b:0x13"
run map_member2 1 106 30 100 $M --key 60:0x01 --key 62:0x81
DOPTS=""
run map_none 1 106 30 60 $M
DOPTS="--byte 0:0x14:1 --byte 0:0x15:0x13"
run map_use 0 82 34 250 $M --key 40:0x01 --key 41:0x81 --key 45:0x41 --key 47:0xc1 --key 65:0x4d --key 67:0xcd --key 75:0x1c \
    --key 77:0x9c --key 95:0x1c --key 97:0x9c --key 115:0x02 --key 117:0x82
DOPTS="--byte 0:0x14:1"
run map_none_use 0 82 34 250 $M --key 45:0x41 --key 47:0xc1 --key 65:0x4d --key 67:0xcd --key 75:0x1c --key 77:0x9c --key 95:0x1c \
    --key 97:0x9c --key 115:0x02 --key 117:0x82
DOPTS="--byte 0:0x14:0x13"
for seed in 1 2 3; do run "map_walk$seed" 1 106 30 2500 $($PY tools/walk_keys.py map $seed 2500); done
DOPTS=""
# F9, the party's order (step 34): F9 (pass 3) shows a 20 x 20 row per member before the first record not in the party; Up and Down
# move the lit row (it blinks: 40 rounds of 81), Space picks the member up or puts him down, moving a picked-up member swaps his record
# (bytes 3 .. 0x27) with the next; Esc or the right button alone ends it. Long waits (the frame's count, the palette steps)
O="--key 1:0x81 --key 3:0x43 --key 5:0xc3"
DOPTS="--join 1 --join 2"
run order_s1 1 106 30 400 $O --key 20:0x50 --key 22:0xd0 --key 40:0x39 --key 42:0xb9 --key 60:0x50 --key 62:0xd0 --key 80:0x39 \
    --key 82:0xb9 --key 100:0x48 --key 102:0xc8 --key 300:0x01 --key 302:0x81 --key 320:0x4d --key 340:0xcd
# picked up, moved to the bottom and past it, back to the top and past it; Esc while picked up; F9 again (its leftovers)
run order_ends 1 106 30 300 $O --key 20:0x39 --key 22:0xb9 --key 30:0x50 --key 32:0xd0 --key 40:0x50 --key 42:0xd0 --key 50:0x50 \
    --key 52:0xd0 --key 60:0x48 --key 62:0xc8 --key 70:0x48 --key 72:0xc8 --key 80:0x48 --key 82:0xc8 --key 90:0x50 --key 92:0xd0 \
    --key 100:0x01 --key 102:0x81 --key 150:0x43 --key 152:0xc3 --key 170:0x50 --key 172:0xd0 --key 200:0x01 --key 202:0x81
# a held key (no release: one move), Enter (nothing), Space twice
run order_held 1 106 30 200 $O --key 20:0x50 --key 60:0xd0 --key 70:0x1c --key 72:0x9c --key 80:0x39 --key 82:0xb9 \
    --key 90:0x39 --key 92:0xb9 --key 120:0x01 --key 122:0x81
# Down in the pass after F9's make code (the loop's first round), with no break code between: the latch is a leftover not 0, so it moves
run order_nobreak 1 106 30 120 --key 1:0x81 --key 3:0x43 --key 4:0x50 --key 7:0xd0 --key 60:0x01 --key 62:0x81
DOPTS="--join 1 --join 2 --join 3 --join 4"
run order_five 1 106 30 300 $O --key 20:0x39 --key 22:0xb9 --key 30:0x50 --key 32:0xd0 --key 40:0x50 --key 42:0xd0 --key 50:0x50 \
    --key 52:0xd0 --key 60:0x50 --key 62:0xd0 --key 70:0x50 --key 72:0xd0 --key 80:0x39 --key 82:0xb9 --key 90:0x48 --key 92:0xc8 \
    --key 200:0x01 --key 202:0x81 --key 220:0x48 --key 230:0xc8
DOPTS=""
run order_one 1 106 30 150 $O --key 20:0x50 --key 22:0xd0 --key 40:0x39 --key 42:0xb9 --key 60:0x50 --key 62:0xd0 --key 100:0x01 \
    --key 102:0x81
# member 2 joined but not member 1: the list stops at record 1
DOPTS="--join 2"
run order_gap 1 106 30 150 $O --key 20:0x50 --key 22:0xd0 --key 40:0x39 --key 42:0xb9 --key 60:0x50 --key 62:0xd0 --key 100:0x01 \
    --key 102:0x81
DOPTS="--join 1 --join 2"
run order_dark13 13 100 50 200 $O --key 20:0x39 --key 22:0xb9 --key 40:0x50 --key 42:0xd0 --key 100:0x01 --key 102:0x81
# the mouse: F9's button clicked; the pointer's y moves the row (its first round compares with 0x498d), the left button after a
# release picks up or puts down, both buttons do nothing, the right one alone ends it
run order_mouse 1 106 30 250 --key 1:0x81 --mouse 0:160:100:0 --mouse 3:290:65:1 --mouse 5:290:65:0 --mouse 30:290:80:0 \
    --mouse 50:290:95:0 --mouse 60:290:95:1 --mouse 62:290:95:0 --mouse 80:290:85:0 --mouse 100:290:85:1 --mouse 102:290:85:0 \
    --mouse 120:290:86:3 --mouse 122:290:86:0 --mouse 140:290:50:0 --mouse 180:290:50:2 --mouse 182:290:50:0
for seed in 1 2 3; do run "order_walk$seed" 1 106 30 2500 $($PY tools/walk_keys.py order $seed 2500); done
DOPTS=""
# F10, the party's data (step 35): F10 (pass 3) shows a 64-wide panel per member (name, face, numbers) over both button columns; the
# palette steps every 41st round and the three pictures at (5, 90) turn every 201st; Esc or the right button alone ends it
TEN="--key 1:0x81 --key 3:0x44 --key 5:0xc4"
DOPTS="--join 1 --join 2"
run data_s1 1 106 30 900 $TEN --key 100:0x39 --key 102:0xb9 --key 120:0x1c --key 122:0x9c --key 140:0x50 --key 142:0xd0 \
    --key 850:0x01 --key 852:0x81 --key 870:0x4d --key 890:0xcd
DOPTS="--join 1 --join 2 --join 3 --join 4 --byte 4:0x13:0xc5"
run data_five 1 106 30 300 $TEN --key 250:0x01 --key 252:0x81
DOPTS="--join 1 --join 2 --join 3 --join 4"
run data_five0 1 106 30 300 $TEN --key 250:0x01 --key 252:0x81
DOPTS=""
run data_one 1 106 30 300 $TEN --key 250:0x01 --key 252:0x81
DOPTS="--join 2"
run data_gap 1 106 30 150 $TEN --key 100:0x01 --key 102:0x81
# a fallen member's face shaded; numbers with zeros inside, five digits and 0
DOPTS="--join 1 --join 2 --hp 1:0 --word 0:4:10005 --word 0:6:60000 --byte 0:0xc:7 --word 0:0xd:100 --word 2:0x10:909"
run data_nums 1 106 30 150 $TEN --key 100:0x01 --key 102:0x81
DOPTS="--join 1 --join 2"
run data_dark13 13 100 50 150 $TEN --key 100:0x01 --key 102:0x81
# after F9 has moved the hero to the second place: names and faces follow the numbers, not the places
run data_order 1 106 30 250 --key 1:0x81 --key 3:0x43 --key 5:0xc3 --key 20:0x39 --key 22:0xb9 --key 30:0x50 --key 32:0xd0 \
    --key 40:0x01 --key 42:0x81 --key 60:0x44 --key 62:0xc4 --key 200:0x01 --key 202:0x81
# the mouse: F10's button clicked; the left button and both do nothing, the right one alone ends it; then F10 again by key
run data_mouse 1 106 30 300 --key 1:0x81 --mouse 0:160:100:0 --mouse 3:290:85:1 --mouse 5:290:85:0 --mouse 40:200:100:1 \
    --mouse 42:200:100:0 --mouse 60:200:100:3 --mouse 62:200:100:0 --mouse 100:200:100:2 --mouse 102:200:100:0 --key 150:0x44 \
    --key 152:0xc4 --key 250:0x01 --key 252:0x81
for seed in 1 2 3; do run "data_walk$seed" 1 106 30 2500 $($PY tools/walk_keys.py data $seed 2500); done
DOPTS=""
# keys 1 to 4 and A (step 37): key 1 the music off and on (with an FM card; held: once; no card: nothing, not even the button), key 2
# the sounds (off before the house door, on again inside: only the way out sounds); A held, and twice; sliding round corners in
# scene 0 (random walks with A on: 33 slides between them); key 3 saving into slots 5, 0 (the shipped SAVE00.002 written again the
# same) and 11 (nothing written), on slow machines (speed classes 0 and 1: the villagers' byte 8), in a dark scene; key 4 left with yes
# (the original then starts MARK.EXE) and refused; the five buttons with the mouse; walks with all of them
SV="--key 10:0x04 --key 12:0x84"
UP5="--key 14:0x48 --key 16:0xc8 --key 18:0x48 --key 20:0xc8 --key 22:0x48 --key 24:0xc8 --key 26:0x48 --key 28:0xc8 --key 30:0x48 --key 32:0xc8"
run sys_music 1 106 30 80 --opl --key 10:0x02 --key 12:0x82 --key 40:0x02 --key 42:0x82
run sys_music_held 1 106 30 80 --opl --key 10:0x02 --key 50:0x82
run sys_nocard 1 106 30 80 --key 10:0x02 --key 12:0x82 --key 20:0x03 --key 22:0x83
run sys_sound 0 36 22 420 --sb --key 1:0x03 --key 3:0x83 --key 140:0x03 --key 142:0x83 $($PY tools/walk_keys.py door 0 420)
run sys_slide_key 1 106 30 80 --key 10:0x1e --key 30:0x9e --key 40:0x1e --key 42:0x9e
run sys_slide0 0 36 22 1500 --key 0:0x1e --key 1:0x9e $($PY tools/walk_keys.py fuzz 4 1500)
run sys_slide1 0 106 34 1500 --key 0:0x1e --key 1:0x9e $($PY tools/walk_keys.py fuzz 4 1500)
# sliding where only the side test two rows down is open (a step down to an odd row first), and a sideways slide whose up is blocked
# too (work/b10/slidesearch.c found them over the 53 scenes)
A="--key 1:0x1e --key 3:0x9e"
run sys_slide_mid0 0 24 11 40 $A --key 6:0x50 --key 9:0xd0 --key 14:0x4b --key 20:0xcb
run sys_slide_mid1 1 192 11 40 $A --key 6:0x50 --key 9:0xd0 --key 14:0x4d --key 20:0xcd
run sys_slide_up0 0 76 29 40 $A --key 14:0x4b --key 20:0xcb
run sys_slide_up1 1 178 16 40 $A --key 14:0x4d --key 20:0xcd
run sys_save5 1 106 30 120 $SV $UP5 --key 40:0x1c --key 42:0x9c
run sys_save0 1 106 30 60 $SV --key 20:0x1c --key 22:0x9c
run sys_save11 1 106 30 120 $SV $UP5 --key 34:0x4b --key 36:0xcb --key 38:0x48 --key 40:0xc8 --key 42:0x50 --key 44:0xd0 \
    --key 46:0x48 --key 48:0xc8 --key 60:0x1c --key 62:0x9c
DOPTS="--speed 0"; run sys_save_slow0 1 106 30 160 $SV $UP5 --key 40:0x1c --key 42:0x9c
DOPTS="--speed 1"; run sys_save_slow1 1 106 30 160 $SV $UP5 --key 40:0x1c --key 42:0x9c; DOPTS=""
run sys_save_dark 13 100 40 120 $SV --key 20:0x48 --key 22:0xc8 --key 40:0x1c --key 42:0x9c
run sys_leave 1 106 30 60 --key 10:0x05 --key 12:0x85 --key 30:0x1c --key 32:0x9c
run sys_leave_no 1 106 30 80 --key 10:0x05 --key 12:0x85 --key 20:0x4d --key 22:0xcd --key 30:0x1c --key 32:0x9c
M="--key 1:0x81 --mouse 0:160:100:0"
run sys_mouse 1 106 30 120 --opl --sb $M --mouse 10:290:108:0 --mouse 12:290:108:1 --mouse 14:290:108:0 --mouse 30:290:128:1 \
    --mouse 32:290:128:0 --mouse 50:20:185:1 --mouse 52:20:185:0 --mouse 70:20:185:1 --mouse 90:20:185:0
run sys_mouse_save 1 106 30 120 $M --mouse 10:290:148:0 --mouse 12:290:148:1 --mouse 14:290:148:0 --mouse 30:160:100:0 \
    --key 40:0x1c --key 42:0x9c
run sys_mouse_leave 1 106 30 100 $M --mouse 10:290:168:0 --mouse 12:290:168:1 --mouse 14:290:168:0 --mouse 30:300:45:0 \
    --mouse 40:300:45:1 --mouse 42:300:45:0
# the pointer resting where keys 3 and 4 write their message: the corner put back before the number box or the buttons
run sys_mouse_over_save 1 106 30 60 --key 1:0x81 --mouse 0:150:149:0 --key 10:0x04 --key 12:0x84 --key 30:0x1c --key 32:0x9c
run sys_mouse_over_leave 1 106 30 60 --key 1:0x81 --mouse 0:150:149:0 --key 10:0x05 --key 12:0x85 --key 20:0x4d --key 22:0xcd \
    --key 30:0x1c --key 32:0x9c
for seed in 1 2 3; do run "sys_walk$seed" 0 36 22 2500 $($PY tools/walk_keys.py sys $seed 2500); done
# doors (kind 1 records, step 38): through scene 0's south door to scene 1, and from scenes 1 (both ways), 6 and the dark 13; scene 0 to
# 1 and back (the state the first door wrote into __1.NOW read again), with music, the Sound Blaster, slow machines and the mouse;
# three random walks from beside the south door. The zoom out and the zoom in after it are compared step by step.
run door_s0s1 0 62 49 150 --key 1:0x50 --key 60:0xd0
run door_s1s0 1 42 49 150 --key 1:0x4d --key 60:0xcd
run door_s1s2 1 60 66 150 --key 1:0x4d --key 60:0xcd
run door_s6s0 6 76 42 150 --key 1:0x50 --key 60:0xd0
run door_s13s1 13 26 17 150 --key 1:0x4d --key 60:0xcd
# the shake's floor (map header byte 3) is 1 in scene 9 and 0 in 12; scene 42 (also 1) to 10 (a battle follows); scene 43 into 9
run door_s9s12 9 94 14 150 --key 1:0x50 --key 60:0xd0
run door_s42s10 42 62 77 150 --key 1:0x50 --key 60:0xd0
run door_s43s9 43 46 57 150 --key 1:0x50 --key 60:0xd0
# a shot still flying when the door is taken
run door_shot 0 62 49 150 --key 1:0x50 --key 15:0x39 --key 16:0xb9 --key 17:0x50 --key 60:0xd0
TRIP="--key 1:0x50 --key 20:0xd0 --key 30:0x48 --key 34:0xc8 --key 40:0x4b --key 60:0xcb"
run door_trip 0 62 49 250 $TRIP
run door_trip_opl 0 62 49 200 $TRIP --opl
run door_trip_sb 0 62 49 200 $TRIP --sb
DOPTS="--speed 0"; run door_trip_slow0 0 62 49 200 $TRIP
DOPTS="--speed 1"; run door_trip_slow1 0 62 49 200 $TRIP; DOPTS=""
run door_trip_mouse 0 62 49 200 $TRIP --mouse 0:150:60:0
for seed in 1 2 3; do run "door_walk$seed" 0 62 49 1500 $($PY tools/walk_keys.py fuzz $seed 1500); done
DOPTS=""
DOPTS=""
# the world's start zoom (step 23; tools/zoom_check.py): its 13 steps in scenes 0, 1, 34 and four dark scenes (6, 9, 23, 47), from the
# data directories of the starts above
case " ${RUNS:-zoom} " in *" zoom"*)
for z in talk_one s1_rec4 s34 dark6_idle dark9_idle dark23_idle dark47_idle; do
    [ -d "$OUT/data_$z" ] || { echo "zoom_$z: no data (run the whole script once)"; fail=1; continue; }
    res=$($PY tools/zoom_check.py "$OUT/data_$z" --keep "$OUT/z_$z" --port "$PORT" 2>/dev/null)
    case "$res" in
        *identical*) echo "zoom_$z: $res" ;;
        *) echo "zoom_$z: DIFFERENT"; echo "$res" | head -4; fail=1; [ "${FIRST_FAIL_EXIT:-0}" = 1 ] && exit 1 ;;
    esac
done
;; esac
# after the loss: MARK.EXE's game-over screen from the screen the battle ended on (tools/gameover_check.py)
case " ${RUNS:-gameover} " in *" gameover"*)
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
# how it ends: Space, Enter; with a mouse driver the left or the right button (no release waited for: one held from the start ends it
# at once); Esc does nothing
for g in "space:--key 50:0x39" "enter:--key 50:0x1c" "left:--mouse 0:100:100:0 --mouse 40:100:100:1" \
         "right:--mouse 0:100:100:0 --mouse 60:200:50:2" "held:--mouse 0:100:100:1" "esc:--mouse 0:100:100:0 --key 30:0x01 --key 40:0x81"; do
    res=$($PY tools/gameover_check.py "$START" 120 --keep "$OUT/k_gameover_${g%%:*}" --port "$PORT" ${g#*:} 2>/dev/null)
    case "$res" in
        *identical*) echo "gameover_${g%%:*}: $res" ;;
        *) echo "gameover_${g%%:*}: DIFFERENT"; echo "$res" | head -4; fail=1; [ "${FIRST_FAIL_EXIT:-0}" = 1 ] && exit 1 ;;
    esac
done
;; esac
[ $fail = 0 ] && echo "battle: all identical"
exit $fail
