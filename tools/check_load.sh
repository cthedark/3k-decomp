#!/bin/sh
# Compare loading a save from the menu's slot picker (src/menu.c menu_load_save) with the original WALK0.SL (0x115c2 .. 0x11810):
# the original runs in the oracle emulator (tools/dosemu.py) until it starts WALK.SL, and the MAN_INFO.NOW and __1.NOW it wrote must
# be byte for byte the port's (build/3k_headless --scene menu --write-dir). Cases: every shipped save (slots 0, 1, 3, 4, 7, 9 and 10),
# and saves the port's key 3 wrote (tools/battle_check.py on two of check_battle.sh's starts: slot 5 in scene 1, slot 0 in the dark
# scene 13), which the original must then load as the port does.
# Exit status 0 when everything is identical.
set -u
cd "$(dirname "$0")/.." || exit 2
PY=work/venv/bin/python
EXE=work/unpacked/WALK0.SL.unpacked.exe
OUT=work/check_load
mkdir -p "$OUT"
MENU=${MENU:-build/3k_headless}   # another build of the headless game (tools/mutate_load.py); LOADS_ONLY=1 skips the saves made and the world
[ "$MENU" = build/3k_headless ] && { make -s build/3k_headless build/walk_dump || exit 2; }
fail=0 total=0
# the menu chooses item 1 at pass 12 (tools/check_menu.sh); the picker's pass k is the oracle's pass k + 13
MO="--key-pass 4:0x9c --key-pass 8:0x50 --key-pass 9:0x9c --key-pass 12:0x1c"
MP="--menu-key 4:0x9c --menu-key 8:0x50 --menu-key 9:0x9c --menu-key 12:0x1c"

load() {   # NAME SLOT DATA [SAVEDIR]: pick SLOT (Right SLOT times, then Enter) with DATA's files plus SAVEDIR's saves
    name=$1 slot=$2 data=$3 saves=${4:-}
    total=$((total + 1))
    # the oracle's data: the shipped files without MAN_INFO.NOW and __1.NOW (so that the load must write them: the shipped __1.NOW
    # is what slot 0 gives), with these saves over them
    src="$OUT/src_$name"
    mkdir -p "$src"
    cp "$data"/* "$src"/ || exit 2
    rm -f "$src/MAN_INFO.NOW" "$src/__1.NOW"
    [ -z "$saves" ] || cp "$saves"/SAVE* "$src"/ || exit 2
    po="$MO --key-pass 16:0x9c" pp="$MP --picker-key 3:0x9c" k=5 i=0
    while [ $i -lt "$slot" ]; do
        po="$po --key-pass $((k + 13)):0x4d --key-pass $((k + 14)):0xcd"
        pp="$pp --picker-key $k:0x4d --picker-key $((k + 1)):0xcd"
        k=$((k + 3)) i=$((i + 1))
    done
    po="$po --key-pass $((k + 13)):0x1c" pp="$pp --picker-key $k:0x1c"
    rm -f "$OUT/emu_$name/MAN_INFO.NOW" "$OUT/emu_$name/__1.NOW"   # dosemu.py copies over its work folder but removes nothing
    perl -e 'alarm 250; exec @ARGV' -- "$PY" tools/dosemu.py "$EXE" --data "$src" --work "$OUT/emu_$name" --ticks 1500 \
        --instr-per-tick 500000 --loop-top 0x10b91,0x11304 --tick-div 3 --log-files $po > "$OUT/oracle_$name.log" 2>&1
    pw="$OUT/port_$name"
    mkdir -p "$pw"
    rm -f "$pw/MAN_INFO.NOW" "$pw/__1.NOW"
    [ -n "$saves" ] && cp "$saves"/SAVE* "$pw"/
    "$MENU" --scene menu --data "$data" --write-dir "$pw" $pp --step-dump 400:"$OUT/d4.raw" \
        --picker-dump 150:"$OUT/d5.raw" > "$OUT/port_$name.log" 2>&1
    if ! grep -q "open walk.sl" "$OUT/oracle_$name.log"; then
        echo "$name: the original did not reach WALK.SL"; fail=$((fail + 1)); return
    fi
    if ! grep -q "loaded slot $slot " "$OUT/port_$name.log"; then
        echo "$name: the port did not load slot $slot"; fail=$((fail + 1)); return
    fi
    for f in MAN_INFO.NOW __1.NOW; do
        if [ ! -f "$OUT/emu_$name/$f" ]; then
            echo "$name: the original did not write $f"; fail=$((fail + 1)); return
        fi
        if ! cmp -s "$OUT/emu_$name/$f" "$pw/$f"; then
            echo "$name: $f DIFFERENT"; fail=$((fail + 1)); return
        fi
    done
    echo "$name: identical ($(wc -c < "$pw/__1.NOW" | tr -d ' ') bytes of __1.NOW)"
}

for s in 0 1 3 4 7 9 10; do load "shipped$s" $s original_bin; done

# saves the port wrote (key 3 in the world), with the start data of check_battle.sh's sys_save5 and sys_save_dark
SV="--key 10:0x04 --key 12:0x84"
UP5="--key 14:0x48 --key 16:0xc8 --key 18:0x48 --key 20:0xc8 --key 22:0x48 --key 24:0xc8 --key 26:0x48 --key 28:0xc8 --key 30:0x48 --key 32:0xc8"
made() {   # NAME SCENE X Y N [battle_check options]: the port's save files of that start, in $OUT/k_NAME/port_files
    name=$1 sc=$2 x=$3 y=$4 n=$5
    shift 5
    d="$OUT/data_$name"
    [ -d "$d" ] || $PY tools/walk_scene_data.py "$d" "$sc" "$x" "$y" > /dev/null || exit 2
    $PY tools/battle_check.py "$d" "$n" --keep "$OUT/k_$name" --cache "$OUT/$name.rec" "$@" > "$OUT/made_$name.log" 2>&1
    grep -q identical "$OUT/made_$name.log" || { echo "$name: the save itself differs from the original's (see $OUT/made_$name.log)"; exit 1; }
}
made save5 1 106 30 120 $SV $UP5 --key 40:0x1c --key 42:0x9c
load ported_save5 5 original_bin "$OUT/k_save5/port_files"
made save_dark 13 100 40 120 $SV --key 20:0x48 --key 22:0xc8 --key 40:0x1c --key 42:0x9c
load ported_dark0 0 original_bin "$OUT/k_save_dark/port_files"

if [ "${LOADS_ONLY:-0}" != 1 ]; then
# WALK.SL from what the load wrote: 150 world passes in scene 27 (slot 0), 2 (slot 3) and the dark scene 13 (slot 7)
for s in 0 3 7; do
    total=$((total + 1))
    d="$OUT/data_load$s"
    mkdir -p "$d"
    cp original_bin/* "$d"/ && cp "$OUT/port_shipped$s"/*.NOW "$d"/ || exit 2
    res=$($PY tools/battle_check.py "$d" 150 --keep "$OUT/k_load$s" --cache "$OUT/load$s.rec" 2>/dev/null | grep -v '^pass [0-9]*: wrote')
    case "$res" in
        *identical*) echo "world_slot$s: $(echo "$res" | tail -1)" ;;
        *) echo "world_slot$s: DIFFERENT"; echo "$res" | head -4; fail=$((fail + 1)) ;;
    esac
done

fi

echo "load check: $((total - fail)) of $total comparisons identical to the original"
[ "$fail" -eq 0 ]
