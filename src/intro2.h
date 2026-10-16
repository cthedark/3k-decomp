/*
 * Slide 2 of the intro (the third part of STORY2.SL): the road through the cliffs at sunset,
 * with a small figure walking and a woman's hair moving in the wind, and three lines of
 * narration.
 *
 * Reproduces STORY2.SL main(), 0x10e61 .. 0x1114f and the call of the subtitle player at
 * 0x111e2 (see docs/code.md, "Slide 2"). Same shape as slide 1 (intro1.h): fade to the dim and
 * then the bright palette, an animation loop of 500 passes, fade to dim, subtitles, fade to black.
 *
 * Not built: the voice-over (see intro1.h).
 */
#ifndef TK_INTRO2_H
#define TK_INTRO2_H
#include "screen.h"
#include "slide.h"
#include "subs.h"

typedef enum {
    INTRO2_FADE_DIM,
    INTRO2_FADE_BRIGHT,
    INTRO2_ANIM,
    INTRO2_FADE_DIM2,
    INTRO2_SUBS,
    INTRO2_FADE_BLACK,
    INTRO2_DONE
} intro2_phase;

typedef struct intro2 intro2_t;

/* Loads STORY2.LCZ, STORY2_1.LCZ, STORY2_T.LCZ and ANI.LCP, draws the picture with the first
 * hair and figure frames, and sets the palette to black in entries 0..239 (the state slide 1
 * leaves). `faithful_keys` is accepted for the common interface and has no effect here. Returns
 * NULL, with a message on stderr, if a file is missing or has an unexpected shape. */
intro2_t *intro2_create(screen_t *scr, int faithful_keys);
void intro2_free(intro2_t *t);

/* One step of the current phase (see intro1_step). */
intro2_phase intro2_step(intro2_t *t, screen_t *scr, unsigned tick, int skip);
intro2_phase intro2_phase_now(const intro2_t *t);

extern const slide_ops intro2_ops;

#endif
