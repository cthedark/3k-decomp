/*
 * The menu (the first screen of WALK0.SL), as one static frame per loop pass.
 *
 * What this reproduces, from the original's code (docs/findings.md, "WALK0.SL (the menu)"):
 *   - the base picture: GMAIN.LCZ with a 3-digit CPU-class label drawn at the top right;
 *   - the panel: a 192 x 168 backdrop at screen (64, 16) that rolls one pixel per pass, with the four
 *     labels on top at (100, 40 + 30 k), the highlighted one recoloured.
 *
 * This module only computes frames. It has no clock, no input and no SDL, so a frame can be compared
 * with the original's screen after a given number of passes. The opening animation, the input, the
 * transition after a choice and the actions come later.
 *
 * The routines named "(0x....)" are the original's helpers in WALK0.SL (linear addresses of its
 * unpacked image); they are reimplemented here, not copied.
 */
#ifndef TK_MENU_H
#define TK_MENU_H

#include <stddef.h>
#include <stdint.h>

#include "screen.h"

enum { MENU_ITEMS = 4 };

typedef struct menu menu_t;

/* What one pass decided: MENU_NONE, or the number of the item the player confirmed. */
typedef int menu_result;
enum {
    MENU_NONE = -1,
    MENU_NEW_GAME = 0,   /* the original reads the new-game files and starts WALK.SL */
    MENU_LOAD = 1,       /* the slot picker */
    MENU_CREDITS = 2,    /* starts CAST.SL */
    MENU_QUIT = 3        /* back to DOS */
};

/* Loads GMAIN.LCZ, the palette, and the pictures from RPG.LCP. The CPU label shows
 * (2 + speed class) followed by "86", where the speed class is RPG.LCP's first byte (the
 * original reads it the same way); with a byte above 6 the digit is whatever tile follows in
 * the atlas, as in the original. Shows the first frame (before any loop pass).
 * Returns NULL, with a message on stderr, if a file is missing or malformed. */
menu_t *menu_create(screen_t *scr);
void menu_free(menu_t *m);

/* The highlighted item, 0..3. */
void menu_set_highlight(menu_t *m, int item);
int menu_highlight(const menu_t *m);

/* One pass of the original's loop (0x10b91 .. 0x10e97): advance the pass counter, roll the
 * backdrop one step (one pixel to the left for counter 0..119, one row up for 120..200), build the
 * panel, read the input, and put the panel on the screen. Equivalent to menu_pass_input with no mouse
 * and the port holding no key. Returns what the pass decided. */
menu_result menu_pass(menu_t *m, screen_t *scr);

/* The same pass with the input the original reads during it. In the order it reads them:
 *   `port60`  the keyboard port 0x60: the LAST scancode the keyboard sent, make code when a key went
 *             down, make | 0x80 when it came up. It keeps its value between passes, so the caller
 *             passes the same value until the keyboard sends another. The menu ignores every make code
 *             until it has seen a break code (0x10cac..0x10cba), and accepts one key per break code:
 *             a key held from before the menu opened (the Space that left the title) does nothing,
 *             and holding a key does not repeat. Up (0x48) / Down (0x50) move the selection and wrap,
 *             Space (0x39) / Enter (0x1c) confirm.
 *   `mouse`   NULL if there is no mouse (the original's reset call failed). Otherwise the position in
 *             screen pixels and the buttons. The arrow is drawn on the panel at the position clamped
 *             to x 64..237, y 16..162. With x in 101..219 the item whose band contains y becomes the
 *             selection (items at y 41..63, 72..94, 105..127, 137..159: the gaps keep the old
 *             selection); a button down with x in that range confirms. The selection and the
 *             confirmation take effect after this pass's picture, as in the original.
 * Returns MENU_NONE, or the item confirmed (0..3, see above). A confirmed pass does not present
 * the panel (the original goes straight to its closing transition). */
typedef struct {
    int x, y;          /* screen pixels */
    unsigned buttons;  /* bit 0 left, bit 1 right */
} menu_mouse;

menu_result menu_pass_input(menu_t *m, screen_t *scr, unsigned port60, const menu_mouse *mouse);

/* The transition after a choice (WALK0.SL 0x10e9d .. 0x10fe1), 33 steps. Call menu_confirm_begin after
 * menu_pass_input returned an item, then menu_confirm_step until it returns 0. Each step rolls the
 * backdrop one pixel left, draws a check mark into the chosen label (two pixels of colour 0x24 per
 * row, two rows, moving one row and one column per step: down for 11 steps, then up for 22), builds
 * the panel as a pass does, lays the arrow over it as the pen at the head of the stroke, and shows
 * the panel. The input is not read. The mark is written into the menu's own label strip, as in the
 * original, so the menu cannot go back to passes afterwards. */
enum { MENU_CONFIRM_STEPS = 33 };
void menu_confirm_begin(menu_t *m);
int menu_confirm_step(menu_t *m, screen_t *scr);

/* Reset the keyboard latch, as when the menu starts: no break code seen yet. */
void menu_input_reset(menu_t *m);

/* Number of passes made. */
unsigned menu_passes(const menu_t *m);

/* The opening animation (WALK0.SL 0x10357 .. 0x10537), before the first pass. `n` counts completed steps:
 *   n = 0        the screen is cleared (colour 0) and the palette is set;
 *   n = 1..20    a zoom: step i shows the rectangle (2 + 16 i) x (2 + 10 i) around the picture's centre,
 *                stretched sparsely (see menu_stretch) to the whole screen, so the picture starts as a few
 *                dots and fills in;
 *   n = 21       the whole picture (GMAIN with the CPU label);
 *   n = 22..181  the 83 x 33 lettering "Ready" at (113, 81) shrinks away in 160 steps (it gets 2 pixels
 *                narrower every 5 steps and 2 rows lower every 10), with the area around it cleared;
 *   n = 182      the lettering's area (84 x 33) is cleared and the menu's first pass can start.
 * Each frame depends only on n, so they can be asked for in any order. n above the last is the last. */
enum { MENU_WIPE_STEPS = 20 };   /* the zoom; step 21 is the whole picture, then the shrink follows */
int menu_opening_steps(void);
void menu_opening_frame(menu_t *m, screen_t *scr, int n);

/* The primitives, exposed for tests. All work on "sprites": 2-byte width, 2-byte height (little
 * endian), then width * height pixels, as the original keeps them. */

/* (0x12b5d) "Stretch" `src` into a sprite of dw x dh pixels. It is not a smooth scaling: each source
 * pixel is written once, at the destination pixel where the horizontal accumulator carries, and the
 * destination pixels after it stay 0; each source row is written once and the destination rows in
 * between stay 0. The result is a sparse, dotted picture, which is how the CPU label looks.
 * `dst` must have room for 4 + dw * dh + 1 bytes; it is zeroed first. */
void menu_stretch(uint8_t *dst, const uint8_t *src, unsigned dw, unsigned dh);

/* (0x12c70) Resize `src` to dw x dh by nearest neighbour, with 16-bit fractional steps. Unlike
 * menu_stretch this fills every destination pixel. The horizontal fraction is NOT reset at the start of
 * a row: it carries over from the row before, as in the original. `dst` needs 4 + dw * dh bytes; `src`
 * is read up to one byte past its last pixel (give it a spare byte). */
void menu_resize(uint8_t *dst, const uint8_t *src, unsigned dw, unsigned dh);

/* (0x12902) Copy a w x h tile (read sequentially from `tile`) into `dst` at byte offset `off`,
 * rows `dst_w` apart, skipping zero source pixels. */
void menu_tile_copy(uint8_t *dst, size_t off, const uint8_t *tile, unsigned dst_w, unsigned w, unsigned h);

/* (0x13143) Set every non-zero byte among the n bytes at p to v. */
void menu_shade(uint8_t *p, size_t n, uint8_t v);

/* (0x13119) Change every byte equal to `from` among the n bytes at p to `to`. */
void menu_recolour(uint8_t *p, size_t n, uint8_t from, uint8_t to);

#endif
