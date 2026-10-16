#!/bin/sh
# Show that tools/check_menu.sh (and tests/test_menu.c) can fail: apply one deliberate bug at a time to a
# scratch copy of src/menu.c and expect the frame comparison to report differences.
# Works in the git-ignored work/mut_menu; deletes nothing. Needs the same set-up as check_menu.sh.
set -u
cd "$(dirname "$0")/.." || exit 2
D=work/mut_menu
mkdir -p "$D"
missed=0
CORE=$(sed -n 's/^CORE_SRC := //p' Makefile | sed 's# src/menu.c##; s#src/menu.c ##')
OBJ="build/opl/ymfm_opl.o build/opl/ymfm_adpcm.o build/opl/ymfm_pcm.o build/opl/opl.o"

# A mutant may loop for ever (that counts as caught: it writes no frames): every run has a time limit.
lim() { perl -e 'alarm 20; exec @ARGV' -- "$@"; }

try() {   # name, sed expression
    name=$1; expr=$2
    sed "$expr" src/menu.c > "$D/menu.c"
    if cmp -s src/menu.c "$D/menu.c"; then echo "MUTATION DID NOT APPLY: $name"; missed=$((missed + 1)); return; fi
    # shellcheck disable=SC2086
    if ! cc -std=c99 -O1 -Isrc $CORE "$D/menu.c" tests/platform_null.c src/main.c $OBJ -o "$D/3k_mut" -lc++ 2> "$D/cc.log"; then
        echo "MUTATION DID NOT COMPILE: $name"; missed=$((missed + 1)); return
    fi
    args=""
    for n in 1 10 119 120 200 260; do args="$args --step-dump $n:$D/m_$n.raw"; done
    lim "$D/3k_mut" --scene menu --data original_bin $args > /dev/null 2>&1
    caught=0
    for n in 1 10 119 120 200 260; do
        ref=work/check_menu/o_i0_$n.raw
        [ -f "$ref" ] || { echo "no reference $ref: run tools/check_menu.sh first"; exit 2; }
        [ "$(python3 tools/compare_raw.py "$D/m_$n.raw" "$ref" 2>&1)" = identical ] || caught=1
    done
    # the other speed classes (the CPU label), from the data copies check_menu.sh makes
    for v in 0 1 3 4; do
        lim "$D/3k_mut" --scene menu --data work/check_menu/data_$v --step-dump 1:$D/s$v.raw > /dev/null 2>&1
        [ "$(python3 tools/compare_raw.py "$D/s$v.raw" work/check_menu/o_s${v}_1.raw 2>&1)" = identical ] || caught=1
    done
    if [ "$caught" -eq 1 ]; then echo "caught: $name"; else echo "NOT CAUGHT: $name"; missed=$((missed + 1)); fi
}

try "the roll turns after 0x77 passes instead of 0x78"   's/COUNTER_TURN = 0x78/COUNTER_TURN = 0x77/'
try "the counter wraps after 0xc9"                       's/COUNTER_MAX = 0xc8/COUNTER_MAX = 0xc9/'
try "the pass counter starts at 1 instead of 0"          's/^    m->counter = 0;$/    m->counter = 1;/'
try "the highlight recolours to the wrong colour"        's/HIGHLIGHT_TO = 0x10/HIGHLIGHT_TO = 0x11/'
try "labels one pixel to the right"                      's/LABEL_X = 100 - PANEL_X/LABEL_X = 101 - PANEL_X/'
try "the CPU label has no shadow"                        's/^    gfx_stamp(frame + DIGITS_AT + DIGITS_SHADOW.*$/    (void)0;/'
try "the colour ramp starts at 0xf1"                     's/(uint8_t)(0xf0 + k)/(uint8_t)(0xf1 + k)/'
try "the speed class is ignored"                         's/(size_t)speed_class \* 64 + 0x80/(size_t)2 * 64 + 0x80/'
try "the vertical roll does not wrap the top row"        's/^    memcpy(m->backdrop + 4 + (size_t)(PANEL_H - 1) \* PANEL_W, row0, PANEL_W);$/    (void)row0;/'

# ---- input mutations: judged by the input scenarios of check_menu.sh (needs its o_keys_* / o_mouse_* files)
tryin() {   # name, sed expression
    name=$1; expr=$2
    sed "$expr" src/menu.c > "$D/menu.c"
    if cmp -s src/menu.c "$D/menu.c"; then echo "MUTATION DID NOT APPLY: $name"; missed=$((missed + 1)); return; fi
    # shellcheck disable=SC2086
    if ! cc -std=c99 -O1 -Isrc $CORE "$D/menu.c" tests/platform_null.c src/main.c $OBJ -o "$D/3k_mut" -lc++ 2> "$D/cc.log"; then
        echo "MUTATION DID NOT COMPILE: $name"; missed=$((missed + 1)); return
    fi
    caught=0
    KP=""; for k in 5:0x9c 10:0x50 11:0x9c 14:0x50 15:0x9c 16:0x48 18:0x48 19:0x9c 22:0x48 23:0x9c 26:0x48 27:0x9c 28:0x50 29:0x9c; do KP="$KP --menu-key $k"; done
    MP=""; for k in 0:150:50:0 2:150:80:0 4:150:110:0 6:150:140:0 8:150:67:0 10:150:100:0 12:100:50:0 14:101:50:0 16:219:80:0 18:220:80:0 20:10:10:0 22:300:190:0 24:64:16:0 26:237:162:0 28:238:163:0 30:150:63:0 32:150:64:0 34:150:71:0 36:150:72:0 38:150:41:0 40:150:40:0 42:150:104:0 44:150:105:0 46:150:127:0 48:150:128:0 50:150:136:0 52:150:137:0 54:150:159:0 56:150:160:0 58:150:95:0 60:150:94:0 62:150:96:0 64:150:63:0; do MP="$MP --menu-mouse $k"; done
    for n in 5 10 12 16 19 24 28 30; do
        lim "$D/3k_mut" --scene menu --data original_bin $KP --step-dump $n:$D/k_$n.raw > /dev/null 2>&1
        [ "$(python3 tools/compare_raw.py "$D/k_$n.raw" work/check_menu/o_keys_$n.raw 2>&1)" = identical ] || caught=1
    done
    for n in 1 3 5 7 11 15 19 21 23 25 27 31 35 39 43 45 47 49 51 53 55 57 59 61 63 65; do
        lim "$D/3k_mut" --scene menu --data original_bin $MP --step-dump $n:$D/m_$n.raw > /dev/null 2>&1
        [ "$(python3 tools/compare_raw.py "$D/m_$n.raw" work/check_menu/o_mouse_$n.raw 2>&1)" = identical ] || caught=1
    done
    # confirmation: the pass in which a confirmed key/click is reported
    c1=$(lim "$D/3k_mut" --scene menu --data original_bin --menu-key 4:0x9c --menu-key 10:0x1c --step-dump 40:$D/x.raw 2>&1 | grep -c 'after 10 completed')
    c2=$(lim "$D/3k_mut" --scene menu --data original_bin --menu-key 10:0x1c --step-dump 40:$D/x.raw 2>&1 | grep -c confirmed)
    c3=$(lim "$D/3k_mut" --scene menu --data original_bin --menu-mouse 0:90:80:0 --menu-mouse 10:90:80:1 --step-dump 40:$D/x.raw 2>&1 | grep -c confirmed)
    c4=$(lim "$D/3k_mut" --scene menu --data original_bin --menu-mouse 0:150:67:0 --menu-mouse 10:150:67:1 --step-dump 40:$D/x.raw 2>&1 | grep -c 'confirmed item 0 after 10')
    c5=$(lim "$D/3k_mut" --scene menu --data original_bin --menu-key 4:0x9c --menu-key 10:0x39 --step-dump 40:$D/x.raw 2>&1 | grep -c 'after 10 completed')
    [ "$c1" = 1 ] && [ "$c2" = 0 ] && [ "$c3" = 0 ] && [ "$c4" = 1 ] && [ "$c5" = 1 ] || caught=1
    if [ "$caught" -eq 1 ]; then echo "caught: $name"; else echo "NOT CAUGHT: $name"; missed=$((missed + 1)); fi
}
[ -f work/check_menu/o_keys_5.raw ] || { echo "run tools/check_menu.sh first (it records the original's input frames)"; exit 2; }
tryin "a make code is taken without a break code first"  's/^    if (!m->key_latch)$/    if (0)/'
tryin "the latch is not closed after a key"              's/^        m->key_latch = 0;$/        (void)0;/'
tryin "Down does not wrap"                               's/m->highlight < MENU_ITEMS - 1 ? m->highlight + 1 : 0/m->highlight < MENU_ITEMS - 1 ? m->highlight + 1 : m->highlight/'
tryin "Up does not wrap"                                 's/m->highlight > 0 ? m->highlight - 1 : MENU_ITEMS - 1/m->highlight > 0 ? m->highlight - 1 : 0/'
tryin "Space does not confirm"                           's/if (key == SC_SPACE || key == SC_ENTER)/if (key == SC_ENTER)/'
tryin "hover accepts x = 100"                            's/x > HOVER_X_ABOVE \&\& x < HOVER_X_BELOW/x >= HOVER_X_ABOVE \&\& x < HOVER_X_BELOW/'
tryin "hover bands one pixel wider"                      's/y > k_band_above\[k\] \&\& y < k_band_below\[k\]/y >= k_band_above[k] \&\& y < k_band_below[k]/'
tryin "a click outside the column confirms"              's/if (mouse->buttons \& 0xff)/if (mouse->buttons \& 0xff || 1)/'
tryin "the arrow is not clamped on the right"            's/if (x > MOUSE_X_MAX) x = MOUSE_X_MAX;/(void)0;/'
tryin "the arrow is drawn before the labels"             's/^        gfx_blit_masked(panel + 4 + (size_t)(y - PANEL_Y) \* PANEL_W + (x - PANEL_X), PANEL_W, m->cursor);$/        (void)0;/'

# ---- opening-animation mutations: judged by the opening frames check_menu.sh records (o_open_N.raw)
tryopen() {   # name, sed expression
    name=$1; expr=$2
    sed "$expr" src/menu.c > "$D/menu.c"
    if cmp -s src/menu.c "$D/menu.c"; then echo "MUTATION DID NOT APPLY: $name"; missed=$((missed + 1)); return; fi
    # shellcheck disable=SC2086
    if ! cc -std=c99 -O1 -Isrc $CORE "$D/menu.c" tests/platform_null.c src/main.c $OBJ -o "$D/3k_mut" -lc++ 2> "$D/cc.log"; then
        echo "MUTATION DID NOT COMPILE: $name"; missed=$((missed + 1)); return
    fi
    N="0 1 2 5 10 15 19 20 21 22 23 27 28 37 38 60 61 100 101 130 150 151 160 170 180 181 182"
    rm -f "$D"/p_*.raw   # so that a mutant which hangs cannot be judged by the files of the one before
    args=""; for n in $N; do args="$args --open-dump $n:$D/p_$n.raw"; done
    lim "$D/3k_mut" --scene menu --data original_bin $args > /dev/null 2>&1
    caught=0
    for n in $N; do
        [ "$(python3 tools/compare_raw.py "$D/p_$n.raw" work/check_menu/o_open_$n.raw 2>&1)" = identical ] || caught=1
    done
    if [ "$caught" -eq 1 ]; then echo "caught: $name"; else echo "NOT CAUGHT: $name"; missed=$((missed + 1)); fi
}
[ -f work/check_menu/o_open_21.raw ] || { echo "run tools/check_menu.sh first (it records the opening frames)"; exit 2; }
tryopen "the zoom starts one pixel off"                  's/WIPE_FIRST_AT = 0x7c5f/WIPE_FIRST_AT = 0x7c60/'
tryopen "the zoom moves 0x647 per step"                  's/WIPE_AT_STEP = 0x648/WIPE_AT_STEP = 0x647/'
tryopen "the zoom grows 15 columns per step"             's/w = 2 + 16 \* i/w = 2 + 15 * i/'
tryopen "the zoom grows 9 rows per step"                 's/h = 2 + 10 \* i/h = 2 + 9 * i/'
tryopen "the shrink narrows every 6 steps"               's/if (++cnt > 4)/if (++cnt > 5)/'
tryopen "the shrink sits one column too far left"        's/            si += 1;$/            si += 0;/'
tryopen "the shrink drops one row too few"               's/                si += READY_W;$/                si += READY_W - 1;/'
tryopen "the shrink lowers every step"                   's/                flag = 1;$/                flag = 0;/'
# (A clear 83 instead of 84 wide cannot be seen: the picture's 84th column is already colour 0.)
tryopen "the final clear is skipped"                     's/    if (n == total) {/    if (n == total + 1) {/'
tryopen "the shrink frame is not cleared"                's/    memset(frame, 0, sizeof frame);/    memset(frame, 1, sizeof frame);/'
tryopen "the resize resets its fraction every row"       's/^        unsigned si = row;$/        unsigned si = row; xacc = 0;/'
tryopen "the resize ignores the vertical carry"          's/            row = (row + sw) \& 0xffff;/            row = (row + 0) \& 0xffff;/'

# ---- transition mutations: judged by the transition frames check_menu.sh records (o_conf_*)
tryconf() {   # name, sed expression
    name=$1; expr=$2
    sed "$expr" src/menu.c > "$D/menu.c"
    if cmp -s src/menu.c "$D/menu.c"; then echo "MUTATION DID NOT APPLY: $name"; missed=$((missed + 1)); return; fi
    # shellcheck disable=SC2086
    if ! cc -std=c99 -O1 -Isrc $CORE "$D/menu.c" tests/platform_null.c src/main.c $OBJ -o "$D/3k_mut" -lc++ 2> "$D/cc.log"; then
        echo "MUTATION DID NOT COMPILE: $name"; missed=$((missed + 1)); return
    fi
    caught=0
    N="1 2 3 6 11 12 13 14 20 21 32 33"
    for sc in "item2:--menu-key 4:0x9c --menu-key 8:0x50 --menu-key 9:0x9c --menu-key 10:0x50 --menu-key 11:0x9c --menu-key 14:0x39" \
              "uproll:--menu-key 4:0x9c --menu-key 130:0x1c" \
              "item0:--menu-key 4:0x9c --menu-key 10:0x1c"; do
        tag=${sc%%:*}; po=${sc#*:}
        rm -f "$D"/t_*.raw
        args=""; for n in $N; do args="$args --confirm-dump $n:$D/t_$n.raw"; done
        # shellcheck disable=SC2086
        lim "$D/3k_mut" --scene menu --data original_bin $po --step-dump 400:$D/x.raw $args > /dev/null 2>&1
        for n in $N; do
            [ "$(python3 tools/compare_raw.py "$D/t_$n.raw" work/check_menu/o_conf_${tag}_$n.raw 2>&1)" = identical ] || caught=1
        done
    done
    if [ "$caught" -eq 1 ]; then echo "caught: $name"; else echo "NOT CAUGHT: $name"; missed=$((missed + 1)); fi
}
[ -f work/check_menu/o_conf_item0_1.raw ] || { echo "run tools/check_menu.sh first (it records the transition frames)"; exit 2; }
tryconf "the check mark has another colour"              's/m->labels\[at\] = 0x24;/m->labels[at] = 0x25;/'
tryconf "the check mark is one row tall"                 's/for (int row = 0; row < 2; row++) {/for (int row = 0; row < 1; row++) {/'
tryconf "the mark's erasing pixel is missing"            's/m->labels\[at + 1\] = 0x00;/(void)0;/'
tryconf "the mark starts one row lower"                  's/hv + 0x4c4/hv + 0x4c4 + 0x7a/'
tryconf "the mark steps down by 0x7a"                    's/m->mark_at += 0x7b;/m->mark_at += 0x7a;/'
tryconf "the mark steps up by 0x7a"                      's/m->mark_at -= 0x79;/m->mark_at -= 0x7a;/'
tryconf "the stroke turns after 11 steps"                's/m->confirm_step < 12) {   \/\*/m->confirm_step < 11) {   \/*/'
tryconf "the arrow starts one column off"                's/ + 0x25;   \/\* panel row/ + 0x26;   \/* panel row/'
tryconf "the arrow steps down by 0xc0"                   's/m->arrow_at += 0xc1;/m->arrow_at += 0xc0;/'
tryconf "the arrow steps up by 0xc0"                     's/m->arrow_at -= 0xbf;/m->arrow_at -= 0xc0;/'
tryconf "the arrow is not drawn"                         's/^    gfx_blit_masked(panel + 4 + m->arrow_at, PANEL_W, m->cursor);.*$/    (void)0;/'
tryconf "the backdrop does not roll"                     's/^    gfx_rotate_rows_left(m->backdrop);   \/\* 0x1328b: always the horizontal roll here \*\//    (void)0;/'

# ---- slot-picker mutations: judged by the picker frames and picks check_menu.sh records (o_pick_*)
trypick() {   # name, sed expression
    name=$1; expr=$2
    sed "$expr" src/menu.c > "$D/menu.c"
    if cmp -s src/menu.c "$D/menu.c"; then echo "MUTATION DID NOT APPLY: $name"; missed=$((missed + 1)); return; fi
    # shellcheck disable=SC2086
    if ! cc -std=c99 -O1 -Isrc $CORE "$D/menu.c" tests/platform_null.c src/main.c $OBJ -o "$D/3k_mut" -lc++ 2> "$D/cc.log"; then
        echo "MUTATION DID NOT COMPILE: $name"; missed=$((missed + 1)); return
    fi
    caught=0
    MP4="--menu-key 4:0x9c --menu-key 8:0x50 --menu-key 9:0x9c --menu-key 12:0x1c"
    # frames: three scenarios, a few passes of each
    KEYS=""; for kv in 3:0x9c 5:0x4b 6:0xcb 8:0x4d 9:0xcd 11:0x4d 12:0xcd 14:0x4d 15:0xcd 17:0x4d 19:0x4d 20:0xcd 22:0x4b 23:0xcb 25:0x4d 26:0xcd 28:0x4d 29:0xcd 31:0x4d 32:0xcd 34:0x4d 35:0xcd 37:0x4d 38:0xcd 40:0x4d 41:0xcd 43:0x4d 44:0xcd 46:0x4d 47:0xcd 49:0x4d 50:0xcd; do KEYS="$KEYS --picker-key $kv"; done
    MICE=""; k=1; for pos in 100:80 110:80 111:80 120:80 121:80 150:80 160:80 219:80 220:80 221:80 99:80 150:69 150:70 150:95 150:96 150:97 150:60 300:190 64:16 150:80; do MICE="$MICE --picker-mouse $k:$pos:0"; k=$((k + 2)); done
    for sc in "blink||0 1 2 3 4 5 6 7 8 9 10 11 12 20 30 40 41 42" "keys|$KEYS|2 6 9 13 18 23 27 33 39 44 48 52 55" "mouse|$MICE|2 6 10 14 16 20 24 26 30 34 38 42"; do
        tag=${sc%%|*}; rest=${sc#*|}; opts=${rest%%|*}; passes=${rest#*|}
        rm -f "$D"/q_*.raw
        args=""; for n in $passes; do args="$args --picker-dump $n:$D/q_$n.raw"; done
        # shellcheck disable=SC2086
        lim "$D/3k_mut" --scene menu --data original_bin $MP4 $opts --step-dump 400:$D/x.raw $args > /dev/null 2>&1
        for n in $passes; do
            [ "$(python3 tools/compare_raw.py "$D/q_$n.raw" work/check_menu/o_pick_${tag}_$n.raw 2>&1)" = identical ] || caught=1
        done
    done
    # picks: (name|port options|what the original did)
    for pk in "--picker-key 3:0x9c --picker-key 5:0x1c|pass 5, slot 0" \
              "--picker-key 5:0x1c|no pick" \
              "--picker-key 3:0x9c --picker-key 5:0x4d --picker-key 6:0xcd --picker-key 8:0x4d --picker-key 9:0xcd --picker-key 11:0x4d --picker-key 12:0xcd --picker-key 14:0x1c|pass 14, slot 3" \
              "--picker-key 3:0x9c --picker-key 5:0x4d --picker-key 6:0xcd --picker-key 8:0x4d --picker-key 9:0xcd --picker-key 11:0x1c --picker-key 12:0x9c --picker-key 13:0x4b|no pick" \
              "--picker-mouse 1:125:80:0 --picker-mouse 3:125:80:1 --picker-mouse 5:125:80:0 --picker-mouse 7:136:80:1|pass 7, slot 3" \
              "--picker-mouse 1:195:80:1|no pick" \
              "--picker-mouse 1:100:80:0 --picker-mouse 3:99:80:1|no pick" \
              "--picker-mouse 1:100:80:0 --picker-mouse 3:150:96:1|no pick" \
              "--picker-mouse 0:150:80:1|pass 0, slot 4"; do
        po=${pk%%|*}; want=${pk#*|}
        # shellcheck disable=SC2086
        got=$(lim "$D/3k_mut" --scene menu --data original_bin $MP4 $po --step-dump 400:$D/x.raw --picker-dump 150:$D/y.raw 2>&1 | grep picked | sed 's/picked slot \([0-9]*\) after \([0-9]*\) completed.*/pass \2, slot \1/')
        [ -n "$got" ] || got="no pick"
        [ "$got" = "$want" ] || caught=1
    done
    if [ "$caught" -eq 1 ]; then echo "caught: $name"; else echo "NOT CAUGHT: $name"; missed=$((missed + 1)); fi
}
[ -f work/check_menu/o_pick_blink_0.raw ] || { echo "run tools/check_menu.sh first (it records the picker frames)"; exit 2; }
trypick "the marker shows for 4 counter values, not 5"    's/if (m->blink < 5) {/if (m->blink < 4) {/'
trypick "the blink cycle is 12 passes"                    's/if (++m->blink > 10)/if (++m->blink > 11)/'
trypick "the blink counter starts at 1"                   's/^    m->blink = 0;$/    m->blink = 1;/'
trypick "icons for the slots WITHOUT a save"              's/if (!(present \& (1u << i)))/if (present \& (1u << i))/'
trypick "icons one pixel to the right"                    's/ICON_AT = 0x166c/ICON_AT = 0x166d/'
trypick "the cells one pixel to the right"                's/STRIP_IN_LABELS = 0xe50/STRIP_IN_LABELS = 0xe51/'
trypick "the marker one row lower"                        's/SELECT_AT = 0x129c/SELECT_AT = 0x1316/'
# (One pixel further along the icon's row is still colour 0x54, so that would not show; three is the 0x1d.)
trypick "the save test looks at another pixel"            's/EXISTS_PROBE = 0x44a/EXISTS_PROBE = 0x44d/'
trypick "Left does not stop at the first cell"            's/key == 0x4b \&\& m->slot > 0/key == 0x4b \&\& m->slot >= 0/'
trypick "Right does not stop at the last cell"            's/m->slot < MENU_SLOTS - 1)/m->slot < MENU_SLOTS)/'
trypick "the hover band starts one row early"             's/y > 0x45 \&\& y < 0x60/y >= 0x45 \&\& y < 0x60/'
trypick "the hover band ends one row late"                's/y > 0x45 \&\& y < 0x60/y > 0x45 \&\& y <= 0x60/'
trypick "the hover column starts one pixel early"         's/x > 0x63 \&\& x < 0xdd/x >= 0x63 \&\& x < 0xdd/'
trypick "the cells are 10 pixels wide for the mouse"      's/m->slot = (x - 0x64) \/ ICON_STEP;/m->slot = (x - 0x64) \/ 10;/'
trypick "a click does not pick"                           's/^                key = SC_SPACE;$/                (void)0;/'
trypick "the picker rolls the backdrop left"              's/^    roll_up(m);   \/\* 0x1131c/    gfx_rotate_rows_left(m->backdrop); \/* 0x1131c/'
echo "mutations not caught: $missed"
exit "$missed"
