/*
 * CMF music player: turns a parsed CMF song (cmf.h) into OPL2 register writes and renders the
 * chip's output with ymfm (opl.h).
 *
 * WHAT THIS FOLLOWS: the original's FM driver, as read from its code (CAST.SL 0x13500 ..) and checked against the
 * register writes it makes in the oracle emulator (tools/dosemu.py --opl-log, tools/compare_opl.py). Not the public
 * CMF description: the two differ in the pitch table, the volume rule and the voice handling.
 *   - Voices: a note on MIDI channel n does NOT play on OPL voice n. A voice is chosen at each note-on: one the
 *     channel released earlier, else a free one, else one another channel released, else the oldest is cut. The
 *     channel's patch is loaded into the voice when its last owner was another channel (so a patch is loaded at
 *     note-on, not at the program change); a program change reloads the voices the channel holds.
 *   - Pitch: a table of twelve f-numbers (343 .. 647) and the block one octave below the MIDI number: A4 is
 *     f-number 577, block 4, about 9 cents flat of equal temperament.
 *   - Volume: the carrier's output level is 0x3f - ((0x3f - patch level) * (0x80 | velocity) >> 8), key-scale bits
 *     of the patch kept; the modulator keeps the patch's level.
 *   - Rhythm mode: controller 0x67 (value non-zero) at the start of a song. MIDI channels 11..15 are then bass
 *     drum, snare, tom-tom, cymbal and hi-hat; only six melodic voices remain. See music.c.
 *   - Controllers other than 0x66 (a marker) and 0x67, pitch bend and pressure are ignored, and counted.
 *   - A note-off releases the voice that holds that note for that channel; a stale one does nothing.
 *
 * TIMING: the song runs at the header's ticks per second (96 for these songs), converted to
 * samples of the chip's own rate with integer arithmetic, so playback is exact and does not
 * depend on the machine. When the song ends it starts again from the beginning, as the intro's
 * poll (STORY2.SL 0x130b0) does: the sounding notes are released, not cut.
 */
#ifndef TK_MUSIC_H
#define TK_MUSIC_H

#include <stddef.h>
#include <stdint.h>

#include "cmf.h"

/* Where the intro's song is in MUC.ALL (STORY2.SL 0x10288 reads this block; docs/formats.md). The title
 * (MARK.EXE 0x10577) and the ending (ENDING.SL) read the same block. */
#define MUSIC_INTRO_OFFSET 0x27137u
#define MUSIC_INTRO_LENGTH 0x49a2u
/* The menu's song (WALK0.SL 0x10b0d: seek 2:0xbad9, read 0x1c92 bytes; also one of WALK.SL's reads). */
#define MUSIC_MENU_OFFSET 0x2bad9u
#define MUSIC_MENU_LENGTH 0x1c92u

/* The credits' song: song 0 of MUC.ALL (CAST.SL 0x10b0d reads 20,000 bytes from offset 2; the song itself is the first
 * 8,996 of them). It is the first song that uses rhythm mode. */
#define MUSIC_CREDITS_OFFSET 2u
#define MUSIC_CREDITS_LENGTH 8996u

typedef struct music music_t;

/* Takes ownership of `song` (freed by music_free). Returns NULL if the chip cannot be created. */
music_t *music_create(cmf_t *song);
void music_free(music_t *m);

/* Convenience: read the intro song from MUC.ALL (assets.h) and create a player. NULL, with a
 * message on stderr, on failure. */
music_t *music_load_intro(void);
/* The same for any block of MUC.ALL holding one song. */
music_t *music_load_at(size_t offset, size_t length);

/* The rate of the samples music_render produces. */
unsigned music_rate(void);

/* Render `frames` mono samples (at music_rate) into out[], advancing the song. Never fails; an
 * empty song gives silence. */
void music_render(music_t *m, int16_t *out, size_t frames);

/* The same, in the shape the platform's audio callback wants (user = the music_t). */
void music_render_cb(void *user, int16_t *out, size_t frames);

/* Output gain in percent (default 100). */
void music_set_gain(music_t *m, unsigned percent);

/* Number of times the song has started again after reaching its end. */
unsigned music_loops(const music_t *m);

/* Events the player does not act on: pitch bends, pressure, controllers other than 0x66 and 0x67, and a program
 * the song does not define. */
unsigned music_ignored_events(const music_t *m);

/* Test hook: called for every chip register write with the sample position (since the start of
 * the current pass through the song) at which it happens. NULL removes it. */
typedef void (*music_trace_fn)(void *user, uint64_t sample, uint8_t reg, uint8_t value);
void music_set_trace(music_t *m, music_trace_fn fn, void *user);

#endif
