#!/bin/sh
# Regression check for slide 1: write the C port's frames and compare them with the reference
# frames saved by tools/dosemu.py (work/oracle1 and work/oracle1s; see docs/port.md).
# Usage: tools/check_intro1.sh      (run from the project root, after `make build/3k_headless`)
set -e
out=work/cport1
mkdir -p "$out"
args=""
for n in $(seq 0 30) 40 60 100 150 200 250 300 319; do
    args="$args --dump anim:$n:$out/p$(printf %04d "$n").raw"
done
for n in $(seq 0 111); do args="$args --dump sub:$n:$out/s$(printf %03d "$n").raw"; done
for n in $(seq 0 14); do args="$args --dump out:$n:$out/f$(printf %02d "$n").raw"; done
# shellcheck disable=SC2086
build/3k_headless --scene intro1 --data original_bin --faithful-keys $args > /dev/null
python3 tools/compare_set.py work/oracle1 'p{}.raw' "$out" 'p{}.raw' $(seq -f %04g 0 30) 0040 0060 0100 0150 0200 0250 0300 0319
python3 tools/compare_set.py work/oracle1s 's{}.raw' "$out" 's{}.raw'
python3 tools/compare_set.py work/oracle1s 'f{}.raw' "$out" 'f{}.raw'
cmp work/oracle1s/after.raw "$out/f14.raw" && echo "restore state: identical"
