/*
 * The credits (CAST.SL), reached from the menu's third item.
 *
 * What this reproduces, from the original's code (docs/findings.md, "CAST.SL ..."):
 *   - a wipe of 200 steps: the picture the previous program left (the menu) rolls up and off the top
 *     of the screen while the lower half of the credits' background rolls in from the bottom edge;
 *   - the main loop (0x103fa .. 0x106af): staff names, a 1-bit bitmap of 120 pixels per row, scroll up one row
 *     per pass through a 120 x 100 window at (100, 0), faded in colour at its top and bottom; the same
 *     rows, stretched row by row to a growing width and squeezed in height, form their reflection on a floor
 *     in the lower half of the screen;
 *   - the end: after 1,790 passes, or at once when the last scancode read at the end of a pass is
 *     Space (0x39) or Enter (0x1c). The original then starts WALK0.SL (the menu) again.
 *
 * Like menu.h, this module has no clock, no SDL and no sound: it computes frames, so they can be compared
 * with the original's screen after a given number of steps. The palette is never touched here: the original
 * never sets it either (it keeps the interface palette the menu left), so `scr` must hold it.
 *
 * The routines named "(0x....)" are the original's helpers in CAST.SL (linear addresses of its image); they
 * are reimplemented here, not copied.
 */
#ifndef TK_CREDITS_H
#define TK_CREDITS_H

#include <stddef.h>
#include <stdint.h>

#include "screen.h"

typedef struct credits credits_t;

/* How a pass ended. */
typedef int credits_status;
enum {
    CREDITS_RUNNING = 1,
    CREDITS_END_TIME = 0,   /* the scroll finished and the last picture was held long enough */
    CREDITS_END_KEY = -1    /* Space or Enter was the last scancode when the pass ended */
};

enum {
    CREDITS_WIPE_STEPS = 200,
    CREDITS_TEXT_BYTES = 25000,     /* the text bitmap's buffer; the file decodes to 24,900 of them */
    CREDITS_SCROLL_MAX = 0x6144,    /* byte offset in the bitmap where the scroll stops (24,900) */
    CREDITS_HOLD_PASSES = 0x82      /* passes at the end of the scroll: the loop ends on the next one */
};

/* Reads CAST1.LCZ and CAST2.LCZ, and keeps a copy of what is on `scr` now (the picture the wipe rolls
 * away). Returns NULL, with a message on stderr, if a file is missing or malformed. */
credits_t *credits_create(const screen_t *scr);
void credits_free(credits_t *c);

/* One step of the wipe (0x101c3 .. 0x1023d). Returns 1 while more steps follow, 0 after the last (the 200th). */
int credits_wipe_step(credits_t *c, screen_t *scr);

/* One pass of the main loop. `port60` is the keyboard port 0x60 as the original reads it at the end of the
 * pass (0x116f0): the last scancode the keyboard sent, with no release latch. The pass that finds the scroll
 * finished draws nothing and returns CREDITS_END_TIME; a key ends the loop after the pass has drawn. */
credits_status credits_pass(credits_t *c, screen_t *scr, unsigned port60);

/* Passes that ran to the end of their drawing. */
unsigned credits_passes(const credits_t *c);

/* The primitives, exposed for tests. */

/* (0x10748) Expand `nbytes` bytes of a bitmap, most significant bit first, to one pixel per bit:
 * a set bit becomes `colour`, a clear bit 0. Writes 8 * nbytes pixels. */
void credits_expand_bits(uint8_t *dst, const uint8_t *src, unsigned nbytes, uint8_t colour);

/* (0x114e2) Resize one line of `srclen` pixels to `dstlen` pixels, nearest neighbour, with a 16-bit
 * fractional step of 0xffff * srclen / dstlen and an accumulator that starts at 1. `src` is read up to
 * `srclen` pixels, and one beyond when the accumulator carries late. */
void credits_resize_line(uint8_t *dst, const uint8_t *src, unsigned srclen, unsigned dstlen);

/* (0x10b6f) Scroll a sprite (2-byte width, 2-byte height, then the pixels) up by one row inside itself:
 * the rows 1.. move to 0.., the last row stays as it was. */
void credits_scroll_up(uint8_t *sprite);

#endif
