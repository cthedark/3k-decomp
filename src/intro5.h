/*
 * Slide 5 of the intro (the sixth part of STORY2.SL): a woman in red, whose face changes slowly
 * between three expressions; then eight lines of narration.
 *
 * Reproduces STORY2.SL main(), 0x11c85 .. 0x11dc8 and the call of the subtitle player at
 * 0x11e55 (see docs/code.md, "Slide 5"). Same shape as slides 1-4 (intro1.h). The simplest slide:
 * one block of frames, drawn in a fixed cycle.
 *
 * Not built: the voice-over (see intro1.h).
 */
#ifndef TK_INTRO5_H
#define TK_INTRO5_H
#include "screen.h"
#include "slide.h"
#include "subs.h"

typedef enum {
    INTRO5_FADE_DIM,
    INTRO5_FADE_BRIGHT,
    INTRO5_ANIM,
    INTRO5_FADE_DIM2,
    INTRO5_SUBS,
    INTRO5_FADE_BLACK,
    INTRO5_DONE
} intro5_phase;

typedef struct intro5 intro5_t;

/* Loads STORY5_1.LCZ, STORY5_T.LCZ and ANI.LCP, draws the picture, and sets the palette to
 * black in entries 0..239 (the state slide 4 leaves). `faithful_keys` is accepted for the common
 * interface and has no effect. Returns NULL, with a message on stderr, if a file is missing or
 * has an unexpected shape. */
intro5_t *intro5_create(screen_t *scr, int faithful_keys);
void intro5_free(intro5_t *t);

intro5_phase intro5_step(intro5_t *t, screen_t *scr, unsigned tick, int skip);
intro5_phase intro5_phase_now(const intro5_t *t);

extern const slide_ops intro5_ops;

#endif
