/*
 * CMF music player: turns a parsed CMF song (cmf.h) into OPL2 register writes and renders the
 * chip's output with ymfm (opl.h).
 *
 * WHAT THIS FOLLOWS: the public CMF semantics (ModdingWiki, "CMF Format"), NOT the original
 * driver's code, which has not been read. Where the public description is silent the choice made
 * here is marked ASSUMED in music.c and listed in docs/port.md, to be judged by ear:
 *   - MIDI channel n (0..8) plays on OPL channel n; events on channels 9..15 are ignored and
 *     counted (music_ignored_events).
 *   - Program change loads the 11 operator bytes of that patch into the channel.
 *   - Note-on velocity v sets the carrier's output level to 63 - v/2 (0 is loudest), keeping the
 *     patch's key-scale bits; the modulator keeps the patch's level.
 *   - A note-off only acts if it is for the note the channel is playing; a note-on on a channel
 *     that is sounding releases the old note first.
 *   - Pitch bend is ignored, as in the original CMF player. Controllers 0x63 (AM/vibrato depth,
 *     default both on), 0x68/0x69 (transpose in 1/128 semitone) are handled. Controller 0x67
 *     (rhythm mode) is not: a non-zero value is reported once and ignored.
 *   - Programs the song does not define use no patch (the original has 16 built-in defaults; the
 *     intro song defines everything it uses).
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

/* Where the intro's song is in MUC.ALL (STORY2.SL 0x10288 reads this block; docs/formats.md). */
#define MUSIC_INTRO_OFFSET 0x27137u
#define MUSIC_INTRO_LENGTH 0x49a2u

typedef struct music music_t;

/* Takes ownership of `song` (freed by music_free). Returns NULL if the chip cannot be created. */
music_t *music_create(cmf_t *song);
void music_free(music_t *m);

/* Convenience: read the intro song from MUC.ALL (assets.h) and create a player. NULL, with a
 * message on stderr, on failure. */
music_t *music_load_intro(void);

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

/* Events the player does not act on: pitch bends, events on channels 9..15, controllers other
 * than the ones listed above, and a note or program event it cannot place. */
unsigned music_ignored_events(const music_t *m);

/* Test hook: called for every chip register write with the sample position (since the start of
 * the current pass through the song) at which it happens. NULL removes it. */
typedef void (*music_trace_fn)(void *user, uint64_t sample, uint8_t reg, uint8_t value);
void music_set_trace(music_t *m, music_trace_fn fn, void *user);

#endif
