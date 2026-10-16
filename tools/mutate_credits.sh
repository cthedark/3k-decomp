#!/bin/sh
# Show that tools/check_credits.sh can fail: apply one deliberate bug at a time to a scratch copy of
# src/credits.c and expect the comparison with the original's screens (or the end of the loop) to differ.
# Works in the git-ignored work/mut_credits; deletes nothing. Needs tools/check_credits.sh to have been run
# (it leaves the original's screens in work/check_credits).
set -u
cd "$(dirname "$0")/.." || exit 2
D=work/mut_credits
R=work/check_credits
mkdir -p "$D"
missed=0
CORE=$(sed -n 's/^CORE_SRC := //p' Makefile | sed 's# src/credits.c##; s#src/credits.c ##')
OBJ="build/opl/ymfm_opl.o build/opl/ymfm_adpcm.o build/opl/ymfm_pcm.o build/opl/opl.o"
[ -f "$R/start.raw" ] && [ -f "$R/o_loop_1790.raw" ] || { echo "run tools/check_credits.sh first"; exit 2; }

# A mutant may loop for ever (that counts as caught): every run has a time limit.
lim() { perl -e 'alarm 60; exec @ARGV' -- "$@"; }

try() {   # name, sed expression
    name=$1; expr=$2
    sed "$expr" src/credits.c > "$D/credits.c"
    if cmp -s src/credits.c "$D/credits.c"; then echo "MUTATION DID NOT APPLY: $name"; missed=$((missed + 1)); return; fi
    # shellcheck disable=SC2086
    if ! cc -std=c99 -O1 -Isrc $CORE "$D/credits.c" tests/platform_null.c src/main.c $OBJ -o "$D/3k_mut" -lc++ 2> "$D/cc.log"; then
        echo "MUTATION DID NOT COMPILE: $name"; missed=$((missed + 1)); return
    fi
    caught=0
    wargs=""; for k in 1 100 101 150 200; do wargs="$wargs --credits-wipe-dump $k:$D/w_$k.raw"; done
    lim "$D/3k_mut" --scene credits --data original_bin --screen-from "$R/start.raw" $wargs --step-dump 0:$D/x.raw > /dev/null 2>&1
    for k in 1 100 101 150 200; do
        [ "$(python3 tools/compare_raw.py "$D/w_$k.raw" "$R/o_wipe_$k.raw" 2>&1)" = identical ] || caught=1
    done
    largs=""; for n in 1 3 28 100 300 1000 1661 1790; do largs="$largs --step-dump $n:$D/l_$n.raw"; done
    lim "$D/3k_mut" --scene credits --data original_bin --screen-from "$R/start.raw" $largs > /dev/null 2>&1
    for n in 1 3 28 100 300 1000 1661 1790; do
        [ "$(python3 tools/compare_raw.py "$D/l_$n.raw" "$R/o_loop_$n.raw" 2>&1)" = identical ] || caught=1
    done
    # the end: no key = 1791 passes started; Space at 5 = 6; Enter at 0 = 1; Space at 1789 = 1790
    e1=$(lim "$D/3k_mut" --scene credits --data original_bin --screen-from "$R/start.raw" --stepped 2>&1 | sed -n 's/.*after \([0-9]*\) pass.*/\1/p')
    e2=$(lim "$D/3k_mut" --scene credits --data original_bin --screen-from "$R/start.raw" --credits-key 5:0x39 2>&1 | sed -n 's/.*after \([0-9]*\) pass.*/\1/p')
    e3=$(lim "$D/3k_mut" --scene credits --data original_bin --screen-from "$R/start.raw" --credits-key 0:0x1c 2>&1 | sed -n 's/.*after \([0-9]*\) pass.*/\1/p')
    e4=$(lim "$D/3k_mut" --scene credits --data original_bin --screen-from "$R/start.raw" --credits-key 3:0x01 --credits-key 4:0x39 2>&1 | sed -n 's/.*after \([0-9]*\) pass.*/\1/p')
    [ "$e1:$e2:$e3:$e4" = "1791:6:1:5" ] || caught=1
    # the unit tests too (they check what the screens cannot, e.g. the pass count)
    # shellcheck disable=SC2086
    if cc -std=c99 -O1 -Isrc $CORE "$D/credits.c" tests/test_credits.c $OBJ -o "$D/test_mut" -lc++ 2>> "$D/cc.log"; then
        lim "$D/test_mut" original_bin > "$D/test.log" 2>&1 || caught=1
    fi
    if [ "$caught" -eq 1 ]; then echo "caught: $name"; else echo "NOT CAUGHT: $name"; missed=$((missed + 1)); fi
}

try "the new row enters at row 25"                      's/NEW_ROW = 26,/NEW_ROW = 25,/'
try "the resize step is 0xfff0 * srclen / dstlen"         's/(0xffffu \* srclen)/(0xfff0u * srclen)/'
try "the resize accumulator starts at 0x8000"           's/uint32_t acc = 1;/uint32_t acc = 0x8000;/'
try "the floor shears by 320 instead of 319"            's/si += 0x13f;/si += 0x140;/'
try "the floor repeats rows every 3 changes, not 4"     's/++sub > 3/++sub > 2/'
try "the floor starts 2 bytes to the left"              's/si = 0x68;/si = 0x66;/'
try "the lower colour ramp starts one band early"       's/off = 0x20d4;/off = 0x20d4 - 0xf0;/'
try "the upper colour ramp has one band fewer"          's/col > 0xd0; col--, off += 0xf0/col > 0xd1; col--, off += 0xf0/'
try "the floor ramp is one row short"                   's/col > 0xcf; col--, off += W/col > 0xd0; col--, off += W/'
try "the window is one pixel to the right"              's/SCREEN_W + 100, c->c/SCREEN_W + 101, c->c/'
try "the floor is copied without its mask"              's/if (c->f\[4 + i\])/if (1)/'
try "B never receives A's top row"      's/^    memcpy(c->b + 4 + (size_t)(HB - 1) \* W, c->a + 4, W);$/    (void)0;/'
try "the wipe does not clear the old picture's last row" 's/^        memset(scr->pix + rem, 0, 320);.*$/        (void)rem;/'
try "the wipe copies the old picture one row too low"   's/c->saved + off, rem/c->saved + off - 320, rem/'
try "the loop ends one hold pass early"                 's/++c->hold > CREDITS_HOLD_PASSES/++c->hold >= CREDITS_HOLD_PASSES/'
try "Enter does not end the loop"                       's/port60 == 0x1c/port60 == 0x1d/'
try "a Space pass is not counted as drawn"               's/^    c->passes++;$/    if (port60 == 0x39) return CREDITS_END_KEY;\n    c->passes++;/'
echo "missed: $missed"
[ "$missed" -eq 0 ]
