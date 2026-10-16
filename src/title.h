/*
 * The title screen (the first scene of MARK.EXE).
 *
 * Reproduces one pass of MARK.EXE's title loop at 0x107a7..0x10b06 (see docs/code.md,
 * "Title screen"). The scene advances one pass per call and takes the clock and the input
 * as arguments, so it can run in real time or be stepped deterministically for comparison
 * with the original.
 *
 * What the original does, in order, on every pass:
 *   1. count the pass; at pass 699 begin the fade-out and choose "intro";
 *   2. fade-out step (palette toward black), leaving when it completes;
 *   3. fade-in step (palette toward the title palette) for the first 64 passes,
 *      afterwards: rotate the banner one pixel and run the bar-colour ramp;
 *   4. if the BIOS clock changed since the last pass, scroll the texture strip up one row
 *      (wrapping);
 *   5. build and draw the three character windows and the logo;
 *   6. if Space, Enter or a mouse button is down and no fade-out has begun: start the
 *      fade-out and choose "menu".
 */
#ifndef TK_TITLE_H
#define TK_TITLE_H

#include "screen.h"

typedef enum {
    TITLE_RUNNING = 0,
    TITLE_TO_INTRO, /* timed out: the original starts STORY2.SL (the intro slideshow) */
    TITLE_TO_MENU   /* key or click: the original starts WALK0.SL (the menu) */
} title_result;

typedef struct title title_t;

/* Loads TITLE1.LCZ and the palette from ANI.LCP, clears the screen and sets an all-black
 * palette, as the original's mode switch does. `tick0` is the BIOS clock at this moment:
 * the original stores it as the "last tick seen" before the first pass (MARK 0x10743).
 * Returns NULL (with a message on stderr) if a file is missing or has an unexpected shape. */
title_t *title_create(screen_t *scr, unsigned tick0);
void title_free(title_t *t);

/* Run one pass. `tick` is the BIOS clock (18.2 per second; only changes matter) and
 * `pressed` is non-zero while Space, Enter or a mouse button is down. */
title_result title_pass(title_t *t, screen_t *scr, unsigned tick, int pressed);

#endif
