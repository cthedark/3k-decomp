#!/bin/sh
# Regression check for slide 5: write the C port's frames and compare them with the reference
# frames saved by tools/dosemu.py (work/oracle5 and work/oracle5s; see docs/port.md for the
# commands that make them). Usage: tools/check_intro5.sh   (from the project root, after
# `make build/3k_headless`)
set -e
out=work/cport5
mkdir -p "$out"
args="--dump end:2000:$out/end.raw"
for n in $(cat work/oracle5/passes.txt); do args="$args --dump anim:$n:$out/p$(printf %04d "$n").raw"; done
for n in $(seq 0 111); do args="$args --dump sub:$n:$out/s$(printf %03d "$n").raw"; done
for n in $(seq 0 14); do args="$args --dump out:$n:$out/f$(printf %02d "$n").raw"; done
# shellcheck disable=SC2086
build/3k_headless --scene intro5 --data original_bin $args > /dev/null
python3 tools/compare_set.py work/oracle5 'p{}.raw' "$out" 'p{}.raw'
cmp work/oracle5s/end.raw "$out/end.raw" && echo "end of loop: identical"
python3 tools/compare_set.py work/oracle5s 's{}.raw' "$out" 's{}.raw'
python3 tools/compare_set.py work/oracle5s 'f{}.raw' "$out" 'f{}.raw'
cmp work/oracle5s/after.raw "$out/f14.raw" && echo "restore state: identical"
