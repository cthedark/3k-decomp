/*
 * Slide 4 of the intro (the fifth part of STORY2.SL): the woman's face on a hillside. Her eyes
 * change through a long, slow sequence (they open, move, close and move again), while ten small
 * sparkles appear and fade in a patch of sky at the top right; then five lines of narration.
 *
 * Reproduces STORY2.SL main(), 0x1146b .. 0x11bf3 and the call of the subtitle player at
 * 0x11c82 (see docs/code.md, "Slide 4"). Same shape as slides 1-3 (intro1.h).
 *
 * The sparkles are placed with the program's random generator (src/tkrand.h), which has been
 * running since slide 0: their positions depend on how many random numbers slide 0 used.
 *
 * Not built: the voice-over (see intro1.h).
 */
#ifndef TK_INTRO4_H
#define TK_INTRO4_H
#include "screen.h"
#include "slide.h"
#include "subs.h"

typedef enum {
    INTRO4_FADE_DIM,
    INTRO4_FADE_BRIGHT,
    INTRO4_ANIM,
    INTRO4_FADE_DIM2,
    INTRO4_SUBS,
    INTRO4_FADE_BLACK,
    INTRO4_DONE
} intro4_phase;

typedef struct intro4 intro4_t;

/* Loads STORY4.LCZ, STORY4_2.LCZ .. STORY4_4.LCZ, STORY4_T.LCZ and ANI.LCP, draws the picture,
 * places the ten sparkles (this uses the random generator, 30 numbers), and sets the palette to
 * black in entries 0..239 (the state slide 3 leaves). `faithful_keys` is accepted for the common
 * interface and has no effect. Returns NULL, with a message on stderr, if a file is missing or has
 * an unexpected shape. */
intro4_t *intro4_create(screen_t *scr, int faithful_keys);
void intro4_free(intro4_t *t);

intro4_phase intro4_step(intro4_t *t, screen_t *scr, unsigned tick, int skip);
intro4_phase intro4_phase_now(const intro4_t *t);

extern const slide_ops intro4_ops;

#endif
