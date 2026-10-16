#!/bin/sh
# Regression check for slide 4: write the C port's frames and compare them with the reference
# frames saved by tools/dosemu.py (work/oracle4 and work/oracle4s; see docs/port.md for the
# commands that make them). Usage: tools/check_intro4.sh   (from the project root, after
# `make build/3k_headless`)
#
# The sparkles are placed with the program's random generator, whose state at this point depends on
# how many numbers slide 0 used. The reference run printed it ("peek at 0x11659: ... = 8942c90f",
# the 32-bit state DS:0x220, little endian), and the C side starts from it with --rand-state.
set -e
out=work/cport4
mkdir -p "$out"
args="--dump end:11401:$out/end.raw"
for n in $(cat work/oracle4/passes.txt); do args="$args --dump anim:$n:$out/p$(printf %05d "$n").raw"; done
for n in $(seq 0 69); do args="$args --dump sub:$n:$out/s$(printf %03d "$n").raw"; done
for n in $(seq 0 14); do args="$args --dump out:$n:$out/f$(printf %02d "$n").raw"; done
# shellcheck disable=SC2086
build/3k_headless --scene intro4 --data original_bin --rand-state 0fc94289 $args > /dev/null
python3 tools/compare_set.py work/oracle4 'p{}.raw' "$out" 'p{}.raw'
cmp work/oracle4s/end.raw "$out/end.raw" && echo "end of loop: identical"
python3 tools/compare_set.py work/oracle4s 's{}.raw' "$out" 's{}.raw'
python3 tools/compare_set.py work/oracle4s 'f{}.raw' "$out" 'f{}.raw'
cmp work/oracle4s/after.raw "$out/f14.raw" && echo "restore state: identical"
