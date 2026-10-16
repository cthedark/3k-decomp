#!/bin/sh
# Compare the port's menu (src/menu.c) with the ORIGINAL's, frame by frame, in the oracle emulator.
#
# For a set of loop passes (both phases of the backdrop roll, both wraps of the pass counter) and each
# highlighted item, the oracle runs WALK0.SL unpacked and writes its screen at the end of that pass;
# the port writes its own after the same number of passes (build/3k_headless --scene menu). The two
# raw dumps (64,000 pixels + 768 palette bytes) must be identical. Also each speed class 0..4 (the CPU
# label), using a throwaway copy of the data with RPG.LCP byte 0 changed.
#
# Also scripted keyboard and mouse input, per pass (frames and the pass in which the original confirms).
#
# Needs: work/venv with unicorn, work/unpacked/WALK0.SL.unpacked.exe (see docs/environment.md),
#        original_bin/, and `make build/3k_headless`. Writes only under work/.
# Usage: tools/check_menu.sh
set -u
cd "$(dirname "$0")/.." || exit 2
OUT=work/check_menu
mkdir -p "$OUT"
PY=work/venv/bin/python
EXE=work/unpacked/WALK0.SL.unpacked.exe
[ -x build/3k_headless ] || make build/3k_headless > /dev/null || exit 2
fail=0; total=0

oracle() {   # dir, highlight-keys, passes... : writes $OUT/o_<tag>_<pass>.raw
    dir=$1; keys=$2; tag=$3; shift 3
    args=""
    # The Nth arrival at 0x10e97 (0-based) is just after the present of pass N + 1: N + 1 passes completed.
    for n in "$@"; do args="$args --snap-hit 0x10e97:$((n - 1)):$OUT/o_${tag}_$n.raw"; done
    perl -e 'alarm 280; exec @ARGV' -- "$PY" tools/dosemu.py "$EXE" --data "$dir" --ticks 600 \
        --instr-per-tick 500000 $keys $args > "$OUT/oracle_$tag.log" 2>&1
}
port() {     # dir, highlight, tag, passes...
    dir=$1; hl=$2; tag=$3; shift 3
    args=""
    for n in "$@"; do args="$args --step-dump $n:$OUT/c_${tag}_$n.raw"; done
    build/3k_headless --scene menu --data "$dir" --highlight "$hl" $args > "$OUT/port_$tag.log" 2>&1
}
compare() {  # tag, passes...
    tag=$1; shift
    for n in "$@"; do
        total=$((total + 1))
        r=$(python3 tools/compare_raw.py "$OUT/c_${tag}_$n.raw" "$OUT/o_${tag}_$n.raw" 2>&1)
        if [ "$r" != identical ]; then fail=$((fail + 1)); echo "DIFFERENT $tag pass $n: $r"; fi
    done
}

# item 0 (no keys), many passes
P0="1 2 3 10 100 118 119 120 121 125 150 199 200 201 202 260 399 401"
oracle original_bin "" i0 $P0; port original_bin 0 i0 $P0; compare i0 $P0
# items 1..3: a release first (the menu ignores keys until it has seen one), then Down presses, ticks apart
for k in 1 2 3; do
    keys="--key 60:0x9c"; t=70
    for i in $(seq 1 $k); do keys="$keys --key $t:0x50 --key $((t + 2)):0xd0"; t=$((t + 10)); done
    P="300 301 320 399"
    oracle original_bin "$keys" i$k $P; port original_bin $k i$k $P; compare i$k $P
done
# speed classes
for v in 0 1 2 3 4; do
    d=$OUT/data_$v; mkdir -p "$d"; cp original_bin/GMAIN.LCZ "$d/"
    python3 - "$v" "$d" <<'PYEOF'
import sys
v, d = int(sys.argv[1]), sys.argv[2]
b = bytearray(open("original_bin/RPG.LCP", "rb").read())
b[0] = v
open(d + "/RPG.LCP", "wb").write(b)
PYEOF
    oracle "$d" "" s$v 1 50; port "$d" 0 s$v 1 50; compare s$v 1 50
done

# ---- input: scripted per pass, in the oracle's pass-synchronised mode (--loop-top), against the port's
# stepped mode. Screens are the
# oracle's snapshot after N completed passes.
inputcase() {   # tag, "oracle options", "port options", passes...
    tag=$1; oo=$2; po=$3; shift 3
    oargs=""; pargs="$po"
    for n in "$@"; do oargs="$oargs --snap-pass $n:$OUT/o_${tag}_$n.raw"; pargs="$pargs --step-dump $n:$OUT/c_${tag}_$n.raw"; done
    perl -e 'alarm 200; exec @ARGV' -- "$PY" tools/dosemu.py "$EXE" --data original_bin --ticks 1200 \
        --instr-per-tick 500000 --loop-top 0x10b91 --tick-div 3 $oo $oargs > "$OUT/oracle_$tag.log" 2>&1
    build/3k_headless --scene menu --data original_bin $pargs > "$OUT/port_$tag.log" 2>&1
    compare "$tag" "$@"
}
# keyboard: the latch opens at 5; Down at 10 (taken); 14 again after a break (taken); Up at 16, again at 18
# without a break (ignored); Up at 22, 26; Down at 28. Wraps both ways are reached.
KO=""; KP=""
for k in 5:0x9c 10:0x50 11:0x9c 14:0x50 15:0x9c 16:0x48 18:0x48 19:0x9c 22:0x48 23:0x9c 26:0x48 27:0x9c 28:0x50 29:0x9c; do
    KO="$KO --key-pass $k"; KP="$KP --menu-key $k"; done
inputcase keys "$KO" "$KP" 5 10 11 12 13 14 15 16 17 19 20 22 23 24 26 27 28 29 30 31
# mouse: every band, the gaps, the x edges, the four clamped corners, the band boundaries
MO=""; MP=""
for k in 0:150:50:0 2:150:80:0 4:150:110:0 6:150:140:0 8:150:67:0 10:150:100:0 12:100:50:0 14:101:50:0 \
         16:219:80:0 18:220:80:0 20:10:10:0 22:300:190:0 24:64:16:0 26:237:162:0 28:238:163:0 30:150:63:0 \
         32:150:64:0 34:150:71:0 36:150:72:0 38:150:41:0 40:150:40:0 \
         42:150:104:0 44:150:105:0 46:150:127:0 48:150:128:0 50:150:136:0 52:150:137:0 54:150:159:0 56:150:160:0 \
         58:150:95:0 60:150:94:0 62:150:96:0 64:150:63:0; do
    MO="$MO --mouse-pass $k"; MP="$MP --menu-mouse $k"; done
inputcase mouse "$MO" "$MP" 1 3 5 7 9 11 13 15 17 19 21 23 25 27 29 31 33 35 37 39 41 43 45 47 49 51 53 55 57 59 61 63 65

# ---- confirmation: in which pass does the original take its confirm branch (0x10e9d), and the port?
confirm() {   # name, oracle options, port options
    name=$1; oo=$2; po=$3
    total=$((total + 1))
    o=$(perl -e 'alarm 60; exec @ARGV' -- "$PY" tools/dosemu.py "$EXE" --data original_bin --ticks 600 \
        --instr-per-tick 500000 --loop-top 0x10b91 --tick-div 3 --hit-pass 0x10e9d --hit-pass-stop \
        --snap-pass 40:$OUT/dummy.raw $oo 2>&1)
    if echo "$o" | grep -q 'reached 0x10e9d'; then oc=$(echo "$o" | grep 'reached 0x10e9d' | head -1 | sed 's/reached 0x10e9d after //; s/ completed pass(es)//')
    elif echo "$o" | grep -q 'snapshots done'; then oc=never
    else oc="UNKNOWN"; fi
    pc=$(build/3k_headless --scene menu --data original_bin $po --step-dump 40:$OUT/dummy2.raw 2>&1 | grep confirmed | sed 's/confirmed item \([0-9]\) after \([0-9]*\) completed.*/\2/')
    [ -n "$pc" ] || pc=never
    if [ "$oc" != "$pc" ]; then fail=$((fail + 1)); echo "DIFFERENT confirm '$name': original $oc, port $pc"; fi
}
confirm "Enter after a break"            "--key-pass 4:0x9c --key-pass 10:0x1c" "--menu-key 4:0x9c --menu-key 10:0x1c"
confirm "Space after a break"            "--key-pass 4:0x9c --key-pass 10:0x39" "--menu-key 4:0x9c --menu-key 10:0x39"
confirm "Enter without a break"          "--key-pass 10:0x1c" "--menu-key 10:0x1c"
confirm "Down, break, Enter"             "--key-pass 4:0x9c --key-pass 8:0x50 --key-pass 9:0x9c --key-pass 12:0x1c" "--menu-key 4:0x9c --menu-key 8:0x50 --menu-key 9:0x9c --menu-key 12:0x1c"
confirm "click on an item"               "--mouse-pass 0:150:110:0 --mouse-pass 10:150:110:1" "--menu-mouse 0:150:110:0 --menu-mouse 10:150:110:1"
confirm "click in a gap"                 "--mouse-pass 0:150:67:0 --mouse-pass 10:150:67:1" "--menu-mouse 0:150:67:0 --menu-mouse 10:150:67:1"
confirm "click left of the column"       "--mouse-pass 0:90:80:0 --mouse-pass 10:90:80:1" "--menu-mouse 0:90:80:0 --menu-mouse 10:90:80:1"
confirm "right button"                   "--mouse-pass 0:150:140:0 --mouse-pass 10:150:140:2" "--menu-mouse 0:150:140:0 --menu-mouse 10:150:140:2"
confirm "click at x 219 (inside)"        "--mouse-pass 0:219:80:0 --mouse-pass 10:219:80:1" "--menu-mouse 0:219:80:0 --menu-mouse 10:219:80:1"
confirm "click at x 220 (outside)"       "--mouse-pass 0:220:80:0 --mouse-pass 10:220:80:1" "--menu-mouse 0:220:80:0 --menu-mouse 10:220:80:1"
confirm "click at x 101 (inside)"        "--mouse-pass 0:101:80:0 --mouse-pass 10:101:80:1" "--menu-mouse 0:101:80:0 --menu-mouse 10:101:80:1"
confirm "click at x 100 (outside)"       "--mouse-pass 0:100:80:0 --mouse-pass 10:100:80:1" "--menu-mouse 0:100:80:0 --menu-mouse 10:100:80:1"
confirm "button held from the start"     "--mouse-pass 0:150:80:1" "--menu-mouse 0:150:80:1"

# ---- the opening animation: the screen after each of its 182 steps (plus the black one before the first):
# the oracle at the places where WALK0.SL has just shown a zoom step (0x103b6), the whole picture (0x103e2),
# a shrink step (0x10483) and the final clear (0x10537); the port with --open-dump.
oargs="--snap-hit 0x10364:0:$OUT/o_open_0.raw"; pargs="--open-dump 0:$OUT/c_open_0.raw"
for i in $(seq 0 19); do oargs="$oargs --snap-hit 0x103b6:$i:$OUT/o_open_$((i + 1)).raw"; pargs="$pargs --open-dump $((i + 1)):$OUT/c_open_$((i + 1)).raw"; done
oargs="$oargs --snap-hit 0x103e2:0:$OUT/o_open_21.raw"; pargs="$pargs --open-dump 21:$OUT/c_open_21.raw"
for k in $(seq 0 159); do oargs="$oargs --snap-hit 0x10483:$k:$OUT/o_open_$((k + 22)).raw"; pargs="$pargs --open-dump $((k + 22)):$OUT/c_open_$((k + 22)).raw"; done
oargs="$oargs --snap-hit 0x10537:0:$OUT/o_open_182.raw"; pargs="$pargs --open-dump 182:$OUT/c_open_182.raw"
perl -e 'alarm 280; exec @ARGV' -- "$PY" tools/dosemu.py "$EXE" --data original_bin --ticks 21 \
    --instr-per-tick 500000 $oargs > "$OUT/oracle_open.log" 2>&1
build/3k_headless --scene menu --data original_bin $pargs > "$OUT/port_open.log" 2>&1
compare open $(seq 0 182)

# ---- the transition after a choice (33 steps: a check mark drawn in the chosen label while the backdrop rolls):
# the oracle at the places where WALK0.SL has just shown a step (0x10faf), after a scripted confirmation.
transition() {   # name, oracle options, port options
    tag=conf_$1; oo=$2; po=$3
    oargs=""; pargs=""
    for n in $(seq 1 33); do
        oargs="$oargs --snap-hit 0x10faf:$((n - 1)):$OUT/o_${tag}_$n.raw"
        pargs="$pargs --confirm-dump $n:$OUT/c_${tag}_$n.raw"
    done
    perl -e 'alarm 200; exec @ARGV' -- "$PY" tools/dosemu.py "$EXE" --data original_bin --ticks 900 \
        --instr-per-tick 500000 --loop-top 0x10b91 --tick-div 3 $oo $oargs > "$OUT/oracle_$tag.log" 2>&1
    build/3k_headless --scene menu --data original_bin $po --step-dump 400:$OUT/dummy3.raw $pargs > "$OUT/port_$tag.log" 2>&1
    compare "$tag" $(seq 1 33)
}
transition item0 "--key-pass 4:0x9c --key-pass 10:0x1c" "--menu-key 4:0x9c --menu-key 10:0x1c"
transition item1 "--key-pass 4:0x9c --key-pass 8:0x50 --key-pass 9:0x9c --key-pass 12:0x1c" \
                 "--menu-key 4:0x9c --menu-key 8:0x50 --menu-key 9:0x9c --menu-key 12:0x1c"
transition item2 "--key-pass 4:0x9c --key-pass 8:0x50 --key-pass 9:0x9c --key-pass 10:0x50 --key-pass 11:0x9c --key-pass 14:0x39" \
                 "--menu-key 4:0x9c --menu-key 8:0x50 --menu-key 9:0x9c --menu-key 10:0x50 --menu-key 11:0x9c --menu-key 14:0x39"
transition item3 "--key-pass 4:0x9c --key-pass 8:0x48 --key-pass 9:0x9c --key-pass 12:0x1c" \
                 "--menu-key 4:0x9c --menu-key 8:0x48 --menu-key 9:0x9c --menu-key 12:0x1c"
# confirmed where the backdrop rolls up (from pass 120 of 200), at the turn, and after the counter wrapped
transition turn "--key-pass 4:0x9c --key-pass 119:0x1c" "--menu-key 4:0x9c --menu-key 119:0x1c"
transition uproll "--key-pass 4:0x9c --key-pass 130:0x1c" "--menu-key 4:0x9c --menu-key 130:0x1c"
transition wrap "--key-pass 4:0x9c --key-pass 204:0x1c" "--menu-key 4:0x9c --menu-key 204:0x1c"
transition mouse "--mouse-pass 0:150:140:0 --mouse-pass 20:150:140:1" "--menu-mouse 0:150:140:0 --menu-mouse 20:150:140:1"

# ---- the slot picker (item 1). The main menu chooses item 1 at pass 12; the oracle then counts the picker's
# passes on from there (two loop tops), so the picker's pass k is the oracle's pass k + 13.
P1=12
PMO="--key-pass 4:0x9c --key-pass 8:0x50 --key-pass 9:0x9c --key-pass 12:0x1c"
PMP="--menu-key 4:0x9c --menu-key 8:0x50 --menu-key 9:0x9c --menu-key 12:0x1c"
picker_opts() {   # keys "k:code ...", mice "k:x:y:b ..." -> sets PO (oracle) and PP (port)
    PO="$PMO"; PP="$PMP"
    for kv in $1; do k=${kv%%:*}; c=${kv#*:}; PO="$PO --key-pass $((k + P1 + 1)):$c"; PP="$PP --picker-key $k:$c"; done
    for mv in $2; do k=${mv%%:*}; r=${mv#*:}; PO="$PO --mouse-pass $((k + P1 + 1)):$r"; PP="$PP --picker-mouse $k:$r"; done
}
picker_frames() {   # name, keys, mice, passes to compare
    tag=pick_$1; picker_opts "$2" "$3"; shift 3
    oargs=""; pargs=""
    for k in "$@"; do
        oargs="$oargs --snap-pass $((k + P1 + 1)):$OUT/o_${tag}_$k.raw"
        pargs="$pargs --picker-dump $k:$OUT/c_${tag}_$k.raw"
    done
    perl -e 'alarm 250; exec @ARGV' -- "$PY" tools/dosemu.py "$EXE" --data original_bin --ticks 1500 \
        --instr-per-tick 500000 --loop-top 0x10b91,0x11304 --tick-div 3 $PO $oargs > "$OUT/oracle_$tag.log" 2>&1
    build/3k_headless --scene menu --data original_bin $PP --step-dump 400:$OUT/dummy4.raw $pargs > "$OUT/port_$tag.log" 2>&1
    compare "$tag" "$@"
}
picker_pick() {   # name, keys, mice: the pass and the slot of the pick (or none) must agree
    name=$1; picker_opts "$2" "$3"; total=$((total + 1))
    perl -e 'alarm 200; exec @ARGV' -- "$PY" tools/dosemu.py "$EXE" --data original_bin --ticks 1500 \
        --instr-per-tick 500000 --loop-top 0x10b91,0x11304 --tick-div 3 --hit-pass 0x115c2 --log-files $PO \
        > "$OUT/oracle_pk_$name.log" 2>&1
    n=$(grep 'reached 0x115c2' "$OUT/oracle_pk_$name.log" | head -1 | sed 's/.*after \([0-9]*\) completed.*/\1/')
    if [ -n "$n" ]; then
        s=$(awk '/reached 0x115c2/{f=1;next} f && /open save[0-9][0-9]\.000/{print $4; exit}' "$OUT/oracle_pk_$name.log" | sed 's/save\([0-9][0-9]\)\.000/\1/')
        o="pass $((n - P1 - 1)), slot $((10#${s:-99}))"
    else
        o="no pick"
    fi
    build/3k_headless --scene menu --data original_bin $PP --step-dump 400:$OUT/dummy4.raw --picker-dump 150:$OUT/dummy5.raw \
        > "$OUT/port_pk_$name.log" 2>&1
    p=$(grep picked "$OUT/port_pk_$name.log" | sed 's/picked slot \([0-9]*\) after \([0-9]*\) completed.*/pass \2, slot \1/')
    [ -n "$p" ] || p="no pick"
    if [ "$o" != "$p" ]; then fail=$((fail + 1)); echo "DIFFERENT pick '$name': original $o, port $p"; fi
}
# the blink and the first passes, then Left/Right with the latch (a held key is ignored; both ends), then the mouse
picker_frames blink "" "" 0 1 2 3 4 5 6 7 8 9 10 11 12 20 30 40 41 42
picker_frames keys "3:0x9c 5:0x4b 6:0xcb 8:0x4d 9:0xcd 11:0x4d 12:0xcd 14:0x4d 15:0xcd 17:0x4d 19:0x4d 20:0xcd 22:0x4b 23:0xcb 25:0x4d 26:0xcd 28:0x4d 29:0xcd 31:0x4d 32:0xcd 34:0x4d 35:0xcd 37:0x4d 38:0xcd 40:0x4d 41:0xcd 43:0x4d 44:0xcd 46:0x4d 47:0xcd 49:0x4d 50:0xcd" "" \
    2 4 6 7 9 10 12 13 15 16 18 21 23 24 26 27 29 30 32 33 35 36 38 39 41 42 44 45 47 48 50 51 52 55
M=""; k=1
for pos in 100:80 110:80 111:80 120:80 121:80 150:80 160:80 219:80 220:80 221:80 99:80 150:69 150:70 150:95 150:96 150:97 150:60 300:190 64:16 150:80; do M="$M $k:$pos:0"; k=$((k + 2)); done
picker_frames mouse "" "$M" $(seq 2 2 42)
picker_pick enter_slot0 "3:0x9c 5:0x1c" ""
picker_pick space_slot0 "3:0x9c 5:0x39" ""
picker_pick enter_without_break "5:0x1c" ""
picker_pick right3_enter "3:0x9c 5:0x4d 6:0xcd 8:0x4d 9:0xcd 11:0x4d 12:0xcd 14:0x1c" ""
picker_pick enter_on_empty "3:0x9c 5:0x4d 6:0xcd 8:0x4d 9:0xcd 11:0x1c 12:0x9c 13:0x4b" ""
picker_pick right_to_the_end "3:0x9c 5:0x4d 6:0xcd 8:0x4d 9:0xcd 11:0x4d 12:0xcd 14:0x4d 15:0xcd 17:0x4d 18:0xcd 20:0x4d 21:0xcd 23:0x4d 24:0xcd 26:0x4d 27:0xcd 29:0x4d 30:0xcd 32:0x4d 33:0xcd 35:0x4d 36:0xcd 38:0x39" ""
picker_pick click_cell9 "" "1:100:80:0 3:200:80:0 5:200:80:1"
picker_pick click_cell10 "" "1:100:80:0 3:219:80:1"
picker_pick click_empty2_then_3 "" "1:125:80:0 3:125:80:1 5:125:80:0 7:136:80:1"
picker_pick click_empty8 "" "1:195:80:1"
picker_pick click_left_of_cells "" "1:100:80:0 3:99:80:1"
picker_pick click_below_cells "" "1:100:80:0 3:150:96:1"
picker_pick button_held_at_start "" "0:150:80:1"
picker_pick key_then_mouse_away "3:0x9c 5:0x4d 6:0xcd 9:0x1c" "7:300:190:0"

echo "menu check: $((total - fail)) of $total comparisons identical to the original"
[ "$fail" -eq 0 ]
