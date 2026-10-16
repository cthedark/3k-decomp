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

tryp() {   # name, perl substitution (for edits a one-line sed cannot express)
    name=$1; expr=$2
    perl -0pe "$expr" src/music.c > "$D/music.c"
    if cmp -s src/music.c "$D/music.c"; then
        echo "MUTATION DID NOT APPLY: $name"; missed=$((missed + 1)); return
    fi
    # shellcheck disable=SC2086
    if ! cc -std=c99 -O1 -Isrc $CORE "$D/music.c" tests/test_music.c $OBJ -o "$D/test_music" -lc++ 2> "$D/cc.log"; then
        echo "MUTATION DID NOT COMPILE: $name"; missed=$((missed + 1)); return
    fi
    perl -e 'alarm 40; exec @ARGV' -- "$D/test_music" original_bin > "$D/out.txt" 2>&1
    rc=$?
    if [ "$rc" -eq 0 ]; then echo "NOT CAUGHT: $name"; missed=$((missed + 1)); else echo "caught (exit $rc): $name"; fi
}

try "the level rule uses the velocity without the 0x80"      's/(vel | 0x80u)/(vel)/'
try "the level is not subtracted from 0x3f"                  's/return (uint8_t)((0x3fu - ((base \* (vel | 0x80u)) >> 8)) | ksl);/return (uint8_t)(((base * (vel | 0x80u)) >> 8) | ksl);/'
try "the key-scale bits are dropped from the level"          's/ >> 8)) | ksl);/ >> 8)));/'
try "the level shift is 7, not 8"                            's/(vel | 0x80u)) >> 8)/(vel | 0x80u)) >> 7)/'
try "A4 table entry off by one"                              's/0x220, 0x241, 0x263/0x220, 0x242, 0x263/'
try "the block is the MIDI octave (not one lower)"           's/block = block ? block - 1 : 0;/block = block;/'
try "the block is not clamped at 7"                          's/if (block > 7)/if (block > 8)/'
try "notes above 119 are not special"                        's/if (n >= 120) {/if (n >= 128) {/'
try "a note-off writes only the high pitch register"         's/^    wr(m, (uint8_t)(0xa0 + v), (uint8_t)(m->v\[v\].word \& 0xff));$/    (void)0;/'
try "a note-off releases any note of the channel"            's/if (m->v\[v\].note == note \&\& m->v\[v\].tag == ch) {/if (m->v[v].tag == ch) {/'
try "a note-off releases any channel holding the note"       's/if (m->v\[v\].note == note \&\& m->v\[v\].tag == ch) {/if (m->v[v].note == note) {/'
try "the patch is loaded at every note"                      's/if ((old \& 0x7f) != ch)/if (1)/'
try "the patch is never reloaded for a new owner"            's/if ((old \& 0x7f) != ch)/if (old == TAG_FREE)/'
tryp "voices are not reused by their channel"                'BEGIN{$/=undef} s/(static unsigned allocate.*?)m->v\[v\]\.tag == \(ch \| TAG_RELEASED\)/$1m->v[v].tag == 0x7e/s'
try "a program change does not reload held voices"           's/            load_voice(m, v, prog);/            (void)prog;/'
try "released voices are not freed by a program change"      's/            m->v\[v\].tag = TAG_FREE;/            (void)v;/'
try "the oldest voice is not cut (no key-off)"               's/^    key_off_voice(m, best);$/    (void)best;/'
try "the carrier level is also written at load"              's/    wr(m, (uint8_t)(0x40 + mo), p\[2\]);/    wr(m, (uint8_t)(0x40 + mo), p[2]);\n    wr(m, (uint8_t)(0x40 + ca), p[3]);/'
try "rhythm mode keeps nine voices"                          's/m->nvoices = rhythm ? 6 : VOICES;/m->nvoices = VOICES;/'
try "rhythm mode is the wrong register value"                's/m->bd = rhythm ? 0xe0 : 0xc0;/m->bd = rhythm ? 0xa0 : 0xc0;/'
try "the bass drum bit is the snare's"                       's/k_drum_bit\[DRUMS\] = {0x10, 0x08/k_drum_bit[DRUMS] = {0x08, 0x08/'
try "the hi-hat uses the tom-tom's operator"                 's/k_drum_op\[DRUMS\] = {0x10, 0x14, 0x12, 0x15, 0x11}/k_drum_op[DRUMS] = {0x10, 0x14, 0x12, 0x15, 0x12}/'
try "the tom-tom and cymbal use the wrong voice"             's/k_drum_voice\[DRUMS\] = {6, 7, 8, 8, 7}/k_drum_voice[DRUMS] = {6, 7, 8, 7, 7}/'
try "the bass drum level goes to the modulator"              's/(d == 0 ? 3u : 0u)/0u/'
try "single-operator drums take the carrier level byte"      's/m->drum\[d\].base = (uint8_t)(0x3f - (p\[2\] \& 0x3f));/m->drum[d].base = (uint8_t)(0x3f - (p[3] \& 0x3f));/'
try "a drum note-off does not clear its bit"                 's/m->bd \&= (uint8_t)~k_drum_bit\[ch - DRUM_CHANNEL\];/(void)ch;/'
try "controller 0x66 counts as ignored"                      's/else if (e->a != 0x66)/else if (1)/'
try "the controller 0x68 transposes"                         's/vo->word = pitch_word(note);/vo->word = pitch_word(note + (e_dummy));/;s/^static void note_on/static unsigned e_dummy = 1;\nstatic void note_on/'
try "events at the wrong sample rate"                        's/return (uint64_t)tick \* opl_native_rate() \/ m->tps;/return (uint64_t)tick * 44100u \/ m->tps;/'
try "zero-length songs restart forever"                      's/} else if (m->end_sample == 0) {/} else if (0) {/'
try "velocity 0 is a note on"                                's/        if (e->b != 0) {/        if (1) {/'
try "the song never loops"                                   's/^    m->loops++;/    m->loops += 0;/'
echo "mutations not caught: $missed"
exit "$missed"
