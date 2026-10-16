/*
 * Slide 0 of the intro (the first part of STORY2.SL): a drifting star field with Chinese
 * subtitles that cross-fade, one line at a time.
 *
 * Reproduces STORY2.SL main(), 0x1031c .. 0x10b60 (see docs/code.md, "STORY2.SL"). The scene
 * advances one step per call and takes the BIOS clock as an argument, so it can be run in
 * real time or stepped deterministically for comparison with the original.
 *
 * Phases:
 *   FADE_IN  64 steps: palette entries 0..239 move one step toward the picture's palette.
 *   RUN      the main loop. Per pass the original does, in this order:
 *              1. rotate the work picture W one pixel left, and scroll it up one row, with
 *                 the old top row wrapping to the bottom;
 *              2. draw the 20 stars into W (remembering what they cover);
 *              3. save the 15 rows under the subtitle strip (rows 185..199);
 *              4. run the subtitle state machine (draws into W);
 *              5. copy W to the screen;
 *              6. restore the strip, restore the pixels under the stars and move the stars
 *                 (2 rows and 2 pixels up per pass, respawning at the bottom).
 *            The loop ends when the 8th (blank) line has finished fading in.
 *   MUSIC_WAIT  (only with sound) until the clip STORY0_1 has finished: the screen stays as it is.
 *   FADE_OUT 64 steps toward black.
 *
 * What the original does not do: nothing in this loop waits for the clock, except the 56-tick
 * hold of a fully shown line. The pass rate is therefore a free choice (src/timing.h).
 */
#ifndef TK_INTRO0_H
#define TK_INTRO0_H
#include "screen.h"

typedef enum { INTRO0_FADE_IN, INTRO0_RUN, INTRO0_MUSIC_WAIT, INTRO0_FADE_OUT, INTRO0_DONE } intro0_phase;

typedef struct intro0 intro0_t;

/* Loads STORY0.LCZ, STORY0_T.LCZ and the palette from ANI.LCP; shows the picture with
 * entries 0..239 black (entries 240..255 as in the file). Returns NULL, with a message on
 * stderr, if a file is missing or has an unexpected shape. Restarts the program's random
 * generator at seed 1, as a new original program would. */
intro0_t *intro0_create(screen_t *scr);
void intro0_free(intro0_t *t);

/* With sound, the clip STORY0_1.LCZ is started on the 20th loop pass. The original's code starts
 * it again 20 passes after it ends, whenever that happens before the loop does, and then waits for
 * that second playing at the end. A recording of the original in DOSBox-X (Sound Blaster on) shows
 * exactly two plays at every CPU speed tried (docs/findings.md, 2026-10-15), so this is ON by
 * default; `on` = 0 plays the clip once (a shorter slide, not what the original does). */
void intro0_set_music_replay(intro0_t *t, int on);

/* One step of the current phase (a fade step or one loop pass). `tick` is the BIOS clock;
 * only the low 16 bits are used. Returns the phase that is current after the step. */
intro0_phase intro0_step(intro0_t *t, screen_t *scr, unsigned tick);
intro0_phase intro0_phase_now(const intro0_t *t);

#endif
