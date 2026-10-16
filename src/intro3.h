/*
 * Slide 3 of the intro (the fourth part of STORY2.SL): a picture whose upper rows stretch as it
 * slowly scrolls down, two small twinkling sparkles in the sky, and a large figure that sinks out
 * of sight; then ten lines of narration.
 *
 * Reproduces STORY2.SL main(), 0x111fe .. 0x1145a and the call of the subtitle player at
 * 0x114e4 (see docs/code.md, "Slide 3"). Same shape as slides 1 and 2 (intro1.h), with an animation
 * loop that ends when a position reaches a limit (142 passes) instead of after a fixed count.
 *
 * Not built: the voice-over (see intro1.h).
 */
#ifndef TK_INTRO3_H
#define TK_INTRO3_H
#include "screen.h"
#include "slide.h"
#include "subs.h"

typedef enum {
    INTRO3_FADE_DIM,
    INTRO3_FADE_BRIGHT,
    INTRO3_ANIM,
    INTRO3_FADE_DIM2,
    INTRO3_SUBS,
    INTRO3_FADE_BLACK,
    INTRO3_DONE
} intro3_phase;

typedef struct intro3 intro3_t;

/* Loads STORY3.LCZ, STORY3_1.LCZ, STORY3_T.LCZ and ANI.LCP, draws the picture and the figure,
 * and sets the palette to black in entries 0..239 (the state slide 2 leaves). `faithful_keys` is
 * accepted for the common interface and has no effect here. Returns NULL, with a message on
 * stderr, if a file is missing or has an unexpected shape. */
intro3_t *intro3_create(screen_t *scr, int faithful_keys);
void intro3_free(intro3_t *t);

intro3_phase intro3_step(intro3_t *t, screen_t *scr, unsigned tick, int skip);
intro3_phase intro3_phase_now(const intro3_t *t);

extern const slide_ops intro3_ops;

#endif
