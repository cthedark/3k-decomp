#!/bin/sh
# Regression check for slide 3: write the C port's frames and compare them with the reference
# frames saved by tools/dosemu.py (work/oracle3 and work/oracle3s; see docs/port.md for the
# commands that make them). Usage: tools/check_intro3.sh   (from the project root, after
# `make build/3k_headless`)
set -e
out=work/cport3
mkdir -p "$out"
args="--dump end:142:$out/end.raw"
for n in $(seq 0 141); do args="$args --dump anim:$n:$out/p$(printf %04d "$n").raw"; done
for n in $(seq 0 139); do args="$args --dump sub:$n:$out/s$(printf %03d "$n").raw"; done
for n in $(seq 0 14); do args="$args --dump out:$n:$out/f$(printf %02d "$n").raw"; done
# shellcheck disable=SC2086
build/3k_headless --scene intro3 --data original_bin $args > /dev/null
python3 tools/compare_set.py work/oracle3 'p{}.raw' "$out" 'p{}.raw'
cmp work/oracle3/end.raw "$out/end.raw" && echo "end of loop: identical"
python3 tools/compare_set.py work/oracle3s 's{}.raw' "$out" 's{}.raw'
python3 tools/compare_set.py work/oracle3s 'f{}.raw' "$out" 'f{}.raw'
cmp work/oracle3s/after.raw "$out/f14.raw" && echo "restore state: identical"
