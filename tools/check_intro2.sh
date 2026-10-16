#!/bin/sh
# Regression check for slide 2: write the C port's frames and compare them with the reference
# frames saved by tools/dosemu.py (work/oracle2 and work/oracle2s; see docs/port.md for the
# commands that make them). Usage: tools/check_intro2.sh   (from the project root, after
# `make build/3k_headless`)
set -e
out=work/cport2
mkdir -p "$out"
anim="$(seq 0 30) 40 44 45 46 59 60 61 74 75 76 89 90 91 92 100 105 126 150 200 250 300 400 499"
args=""
for n in $anim; do args="$args --dump anim:$n:$out/p$(printf %04d "$n").raw"; done
for n in $(seq 0 41); do args="$args --dump sub:$n:$out/s$(printf %03d "$n").raw"; done
for n in $(seq 0 14); do args="$args --dump out:$n:$out/f$(printf %02d "$n").raw"; done
# shellcheck disable=SC2086
build/3k_headless --scene intro2 --data original_bin $args > /dev/null
python3 tools/compare_set.py work/oracle2 'p{}.raw' "$out" 'p{}.raw'
python3 tools/compare_set.py work/oracle2s 's{}.raw' "$out" 's{}.raw'
python3 tools/compare_set.py work/oracle2s 'f{}.raw' "$out" 'f{}.raw'
cmp work/oracle2s/after.raw "$out/f14.raw" && echo "restore state: identical"
