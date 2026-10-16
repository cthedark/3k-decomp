/*
 * Slide 6 of the intro (the last part of STORY2.SL): a woman in a red robe, her hair blowing in
 * the wind; then eleven lines of narration. After it the original starts MARK.EXE again (the
 * title), which is milestone 4 here.
 *
 * Reproduces STORY2.SL main(), 0x11e69 .. 0x12068 and the call of the subtitle player at
 * 0x120f5 (see docs/code.md, "Slide 6"). Same shape as slides 1-5 (intro1.h).
 *
 * Not built: the voice-over (see intro1.h).
 */
#ifndef TK_INTRO6_H
#define TK_INTRO6_H
#include "screen.h"
#include "slide.h"
#include "subs.h"

typedef enum {
    INTRO6_FADE_DIM,
    INTRO6_FADE_BRIGHT,
    INTRO6_ANIM,
    INTRO6_FADE_DIM2,
    INTRO6_SUBS,
    INTRO6_FADE_BLACK,
    INTRO6_DONE
} intro6_phase;

typedef struct intro6 intro6_t;

/* Loads STORY6.LCZ, STORY6_1.LCZ, STORY6_T.LCZ and ANI.LCP, draws the picture and the first hair
 * frame, and sets the palette to black in entries 0..239 (the state slide 5 leaves).
 * `faithful_keys` is accepted for the common interface and has no effect. Returns NULL, with a
 * message on stderr, if a file is missing or has an unexpected shape. */
intro6_t *intro6_create(screen_t *scr, int faithful_keys);
void intro6_free(intro6_t *t);

intro6_phase intro6_step(intro6_t *t, screen_t *scr, unsigned tick, int skip);
intro6_phase intro6_phase_now(const intro6_t *t);

extern const slide_ops intro6_ops;

#endif
