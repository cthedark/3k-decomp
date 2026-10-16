#!/bin/sh
# Compare the port's credits (src/credits.c) with the ORIGINAL's CAST.SL, in the oracle emulator.
#
# Both start from the same screen and palette: the port's own menu screen after 3 passes (tools/check_menu.sh
# shows it equals the original's), standing for the picture and the interface palette the menu leaves.
#   1. The wipe: the screen after k of its 200 steps (oracle: the k-th arrival at the loop test 0x10237).
#   2. The loop: the screen after N completed passes (oracle: --snap-pass N, the N-th arrival at 0x103fa),
#      N from 0 to 1790, both sides of the scroll's end and of the 130 held passes.
#   3. The end: after how many passes the loop ends, with no key and with Space, Enter and Esc pressed at
#      several passes (oracle: the arrival at the exit 0x106b2; the port counts passes started, one more
#      than the oracle's completed passes).
# The raw dumps (64,000 pixels + 768 palette bytes) must be identical.
#
# Needs: work/venv with unicorn, work/unpacked/CAST.SL.exe (CAST.SL itself is not packed: copy it, see
#        docs/environment.md), original_bin/, and `make build/3k_headless`. Writes only under work/.
# Usage: tools/check_credits.sh
set -u
cd "$(dirname "$0")/.." || exit 2
OUT=work/check_credits
mkdir -p "$OUT"
PY=work/venv/bin/python
EXE=work/unpacked/CAST.SL.exe
[ -x build/3k_headless ] || make build/3k_headless > /dev/null || exit 2
fail=0; total=0

# the common starting screen: the menu a few passes in, from the port (equal to the original's, check_menu.sh)
START=$OUT/start.raw
build/3k_headless --scene menu --data original_bin --step-dump 3:"$START" > "$OUT/start.log" 2>&1 || { echo "cannot make $START"; exit 2; }

oracle_run() {   # seconds, extra options...
    secs=$1; shift
    perl -e 'alarm shift; exec @ARGV' -- "$secs" "$PY" tools/dosemu.py "$EXE" --data original_bin --ticks 3000 \
        --instr-per-tick 500000 --palette-from "$START" --screen-from "$START" "$@"
}
compare() {   # tag, kind, ns... (files $OUT/o_<tag>_<n>.raw and c_<tag>_<n>.raw)
    tag=$1; shift
    for n in "$@"; do
        total=$((total + 1))
        r=$(python3 tools/compare_raw.py "$OUT/c_${tag}_$n.raw" "$OUT/o_${tag}_$n.raw" 2>&1)
        if [ "$r" != identical ]; then fail=$((fail + 1)); echo "DIFFERENT $tag $n: $r"; fi
    done
}

# ---- 1. the wipe
WIPE="0 1 2 3 10 50 99 100 101 102 150 199 200"
oargs=""; pargs=""
for k in $WIPE; do
    oargs="$oargs --snap-hit 0x10237:$k:$OUT/o_wipe_$k.raw"; pargs="$pargs --credits-wipe-dump $k:$OUT/c_wipe_$k.raw"
done
oracle_run 120 $oargs > "$OUT/oracle_wipe.log" 2>&1
build/3k_headless --scene credits --data original_bin --screen-from "$START" $pargs > "$OUT/port_wipe.log" 2>&1
compare wipe $WIPE

# ---- 2. the loop
PASSES="0 1 2 3 10 26 27 28 29 30 100 299 300 301 600 1000 1500 1659 1660 1661 1662 1700 1789 1790"
oargs=""; pargs=""
for n in $PASSES; do
    oargs="$oargs --snap-pass $n:$OUT/o_loop_$n.raw"; pargs="$pargs --step-dump $n:$OUT/c_loop_$n.raw"
done
oracle_run 250 --loop-top 0x103fa $oargs > "$OUT/oracle_loop.log" 2>&1
build/3k_headless --scene credits --data original_bin --screen-from "$START" $pargs > "$OUT/port_loop.log" 2>&1
compare loop $PASSES

# ---- 3. the end
endcase() {   # name, "oracle --key-pass options", "port --credits-key options"
    name=$1; oo=$2; po=$3
    total=$((total + 1))
    o=$(oracle_run 250 --loop-top 0x103fa --hit-pass 0x106b2 --hit-pass-stop $oo 2>&1)
    oc=$(echo "$o" | grep 'reached 0x106b2' | head -1 | sed 's/reached 0x106b2 after //; s/ completed pass(es)//')
    [ -n "$oc" ] || oc=never
    pc=$(build/3k_headless --scene credits --data original_bin --screen-from "$START" --stepped $po 2>&1 \
         | grep 'credits ended' | sed 's/credits ended by \([a-z]*\) after \([0-9]*\) pass.*/\2 \1/')
    pn=${pc%% *}; pk=${pc##* }
    if [ -z "$pn" ] || [ "$oc" = never ] || [ "$((pn - 1))" != "$oc" ]; then
        fail=$((fail + 1)); echo "DIFFERENT end '$name': original $oc completed pass(es), port ${pc:-never} started"
    else
        echo "ok end '$name': original $oc completed, port $pn started ($pk)"
    fi
}
endcase "no key"                "" ""
endcase "Space at 4"            "--key-pass 4:0x39" "--credits-key 4:0x39"
endcase "Enter at 0"            "--key-pass 0:0x1c" "--credits-key 0:0x1c"
endcase "Esc at 3, Space at 9"  "--key-pass 3:0x01 --key-pass 9:0x39" "--credits-key 3:0x01 --credits-key 9:0x39"
endcase "Space then break at 5" "--key-pass 5:0x39 --key-pass 6:0xb9" "--credits-key 5:0x39 --credits-key 6:0xb9"
endcase "break only"            "--key-pass 2:0xb9 --key-pass 8:0x9c" "--credits-key 2:0xb9 --credits-key 8:0x9c"
endcase "Space at 1000"         "--key-pass 1000:0x39" "--credits-key 1000:0x39"
endcase "Space at 1789"         "--key-pass 1789:0x39" "--credits-key 1789:0x39"
endcase "Space at 1790"         "--key-pass 1790:0x39" "--credits-key 1790:0x39"

echo "credits: $((total - fail)) of $total comparisons identical"
[ "$fail" -eq 0 ]
