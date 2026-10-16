/*
 * Slide 1 of the intro (the second part of STORY2.SL): the woman with the wounded man on the
 * cliff. Clouds drift across the sky, her face changes between four frames, then the palette
 * darkens and eight lines of narration cross-fade at the bottom.
 *
 * Reproduces STORY2.SL main(), 0x10a59 .. 0x10f9a, and the subtitle player 0x1225a .. 0x1293c
 * (see docs/code.md, "Slide 1"). Like intro0 the scene advances one step per call and takes the
 * BIOS clock as an argument, so it can be run in real time or stepped deterministically.
 *
 * Phases:
 *   FADE_DIM     64 steps: palette entries 0..239 move to the dim palette (ANI.LCP bytes 768..1535).
 *   FADE_BRIGHT  64 steps to the normal palette (ANI.LCP bytes 0..767).
 *   ANIM         320 passes (fewer if Space is hit): clouds and face.
 *   FADE_DIM2    64 steps back to the dim palette.
 *   SUB_PASS     one pass of a line's cross-fade (14 passes per line, 8 lines).
 *   SUB_WAIT     the line stays for 74 BIOS ticks, or until Space.
 *   OUT_PASS     the last line fades out (14 passes), then the strip is restored.
 *   FADE_BLACK   64 steps to black.
 *
 * Not built: the voice-over. The original starts a clip per line when a sound card is present and
 * then waits for the clip; without one it waits the 74 ticks modelled here.
 */
#ifndef TK_INTRO1_H
#define TK_INTRO1_H
#include "screen.h"
#include "slide.h"
#include "subs.h"

typedef enum {
    INTRO1_FADE_DIM,
    INTRO1_FADE_BRIGHT,
    INTRO1_ANIM,
    INTRO1_FADE_DIM2,
    INTRO1_SUBS,
    INTRO1_FADE_BLACK,
    INTRO1_DONE
} intro1_phase;

typedef struct intro1 intro1_t;

/* Loads STORY1.LCZ, STORY1_2.LCZ, STORY1_T.LCZ and ANI.LCP, draws the picture with its first
 * cloud composite, and sets the palette to black in entries 0..239 (the state slide 0 leaves).
 * `faithful_keys` selects how the second cloud strip is paced; see intro1.c. Returns NULL, with a
 * message on stderr, if a file is missing or has an unexpected shape. */
intro1_t *intro1_create(screen_t *scr, int faithful_keys);
void intro1_free(intro1_t *t);

/* One step of the current phase. `tick` is the BIOS clock (low 16 bits used); `skip` is non-zero
 * if Space was hit since the last step. Returns the phase that is current after the step. */
intro1_phase intro1_step(intro1_t *t, screen_t *scr, unsigned tick, int skip);
intro1_phase intro1_phase_now(const intro1_t *t);

/* Passes completed so far in the animation loop; the subtitle player (phase INTRO1_SUBS) for its
 * own counters. Used to place snapshots when comparing with the original. */
unsigned intro1_anim_done(const intro1_t *t);
const subs_t *intro1_subs(const intro1_t *t);

extern const slide_ops intro1_ops;   /* the same scene through the uniform interface (slide.h) */

#endif
