#!/bin/sh
# Regression check for slide 6: write the C port's frames and compare them with the reference
# frames saved by tools/dosemu.py (work/oracle6 and work/oracle6s; see docs/port.md for the
# commands that make them). Usage: tools/check_intro6.sh   (from the project root, after
# `make build/3k_headless`)
set -e
out=work/cport6
mkdir -p "$out"
args="--dump end:2000:$out/end.raw"
for n in $(cat work/oracle6/passes.txt); do args="$args --dump anim:$n:$out/p$(printf %04d "$n").raw"; done
for n in $(seq 0 153); do args="$args --dump sub:$n:$out/s$(printf %03d "$n").raw"; done
for n in $(seq 0 14); do args="$args --dump out:$n:$out/f$(printf %02d "$n").raw"; done
# shellcheck disable=SC2086
build/3k_headless --scene intro6 --data original_bin $args > /dev/null
python3 tools/compare_set.py work/oracle6 'p{}.raw' "$out" 'p{}.raw'
cmp work/oracle6s/end.raw "$out/end.raw" && echo "end of loop: identical"
python3 tools/compare_set.py work/oracle6s 's{}.raw' "$out" 's{}.raw'
python3 tools/compare_set.py work/oracle6s 'f{}.raw' "$out" 'f{}.raw'
cmp work/oracle6s/after.raw "$out/f14.raw" && echo "restore state: identical"
build/3k_headless --scene intro6 --data original_bin --dump final:0:$out/final.raw > /dev/null
cmp work/oracle6s/final.raw "$out/final.raw" && echo "final state (after the fade to black): identical"
