/*
 * The subtitle player of STORY2.SL (0x1225a .. 0x1293c), shared by slides 1 to 6.
 *
 * Eight (or fewer) lines of narration cross-fade one after the other in a 320x14 strip at the
 * bottom of the screen (rows 185..198). Per line: 14 cross-fade passes, a hold, and at the end of
 * the last line 14 fade-out passes and a restore of the strip. See docs/code.md, "Slide 1,
 * verified", for the algorithm and the addresses.
 *
 * Like the scenes it advances one step per call and takes the BIOS clock as an argument. The
 * original plays a voice clip per line and then waits for it; without a sound card it waits 74
 * BIOS ticks. Both are modelled: see subs_load_voice.
 */
#ifndef TK_SUBS_H
#define TK_SUBS_H

#include <stddef.h>
#include <stdint.h>

#include "screen.h"

typedef enum {
    SUBS_PASS,   /* next step draws one cross-fade pass */
    SUBS_WAIT,   /* the line is held: 74 ticks or Space */
    SUBS_OUT,    /* next step draws one fade-out pass, or restores the strip after the last */
    SUBS_DONE
} subs_state;

typedef struct subs subs_t;

/* `lines` holds the decoded STORYn_T.LCZ chunk: `len` bytes, 600 per line (14 rows of 40
 * bytes of 1-bit pixels, then padding). The player shows len/600 lines, like the original,
 * which stops when the line offset passes len - 300. Returns NULL if len is not a positive
 * multiple of 600. */
subs_t *subs_create(const uint8_t *lines, size_t len);
void subs_free(subs_t *s);

/* Give the player the voice-over of slide `slide` (1..6), one clip per line. With a voice and a
 * working audio sink, a line's hold lasts as long as its clip: the clip starts when the cross-fade
 * of the line is complete and the hold ends when it has finished, or at Space, which cuts it
 * short (STORY2.SL 0x1279f..0x127f8). Without either, the hold is the 74 ticks of the no-sound
 * path. A no-op, and not an error, when no audio sink is installed. Returns 0, or -1 if the
 * sink exists but the clips cannot be loaded (the player then keeps the timed hold). */
int subs_load_voice(subs_t *s, unsigned slide);

/* Save the strip under the subtitles from the screen and get ready for the first line. */
void subs_begin(subs_t *s, screen_t *scr);

/* One step. `skip` is non-zero if Space was hit since the last step; it ends a hold early. */
subs_state subs_step(subs_t *s, screen_t *scr, unsigned tick, int skip);

subs_state subs_state_now(const subs_t *s);
unsigned subs_pass_done(const subs_t *s);   /* cross-fade passes completed, all lines together */
unsigned subs_out_done(const subs_t *s);    /* fade-out passes completed */

#endif
