#!/bin/sh
# Show that tests/test_music.c can fail: apply one deliberate bug at a time to a scratch copy of
# src/music.c (in the git-ignored work/mut_music) and expect the test to fail or time out.
# Needs `make build/test_music` to have run once (it reuses the compiled ymfm objects).
# Writes only under work/mut_music; deletes nothing.
set -u
cd "$(dirname "$0")/.." || exit 2
D=work/mut_music
mkdir -p "$D"
CORE=$(sed -n 's/^CORE_SRC := //p' Makefile | sed 's# src/music.c##; s#src/music.c ##')
OBJ="build/opl/ymfm_opl.o build/opl/ymfm_adpcm.o build/opl/ymfm_pcm.o build/opl/opl.o"
missed=0

try() {   # name, sed expression
    name=$1; expr=$2
    sed "$expr" src/music.c > "$D/music.c"
    if cmp -s src/music.c "$D/music.c"; then
        echo "MUTATION DID NOT APPLY: $name"; missed=$((missed + 1)); return
    fi
    # shellcheck disable=SC2086
    if ! cc -std=c99 -O1 -Isrc $CORE "$D/music.c" tests/test_music.c $OBJ -o "$D/test_music" -lc++ 2> "$D/cc.log"; then
        echo "MUTATION DID NOT COMPILE: $name"; missed=$((missed + 1)); return
    fi
    perl -e 'alarm 40; exec @ARGV' -- "$D/test_music" original_bin > "$D/out.txt" 2>&1
    rc=$?
    if [ "$rc" -eq 0 ]; then
        echo "NOT CAUGHT: $name"; missed=$((missed + 1))
    else
        echo "caught (exit $rc): $name"
    fi
}

try "velocity maps to the wrong end of the level scale" 's/unsigned level = 63u - (vel >> 1);/unsigned level = (vel >> 1);/'
try "A4 tuned to 450 Hz"                                   's/440.0 \* pow/450.0 * pow/'
try "a note-off releases any note"                         's/if (m->ch\[c\].sounding \&\& m->ch\[c\].note == (e->a \& 0x7f))/if (m->ch[c].sounding)/'
try "events at the wrong sample rate"                      's/return (uint64_t)tick \* opl_native_rate() \/ m->tps;/return (uint64_t)tick * 44100u \/ m->tps;/'
try "zero-length songs restart forever"                    's/} else if (m->end_sample == 0) {/} else if (0) {/'
try "velocity 0 is a note on"                              's/if (e->b != 0) {\n            note_on/XX/;s/case 0x90:/case 0x90: if (0) {} /;s/        if (e->b != 0) {/        if (1) {/'
try "the song never loops"                                 's/^    m->loops++;/    m->loops += 0;/'
echo "mutations not caught: $missed"
exit "$missed"
