/*
 * Drawing primitives used by the title and other scenes.
 *
 * Each function reproduces the behaviour of a small routine in the original programs
 * (addresses are in MARK.EXE, see docs/code.md, "Title screen"). They work on plain
 * byte buffers and on the linear 320x200 screen.
 *
 * A "sprite" is: [width : u16 LE][height : u16 LE] followed by width*height pixel bytes.
 */
#ifndef TK_GFX_H
#define TK_GFX_H

#include <stddef.h>
#include <stdint.h>

#include "screen.h"

#define SPRITE_HDR 4

static inline unsigned sprite_w(const uint8_t *s) { return (unsigned)s[0] | ((unsigned)s[1] << 8); }
static inline unsigned sprite_h(const uint8_t *s) { return (unsigned)s[2] | ((unsigned)s[3] << 8); }
static inline void sprite_set_size(uint8_t *s, unsigned w, unsigned h)
{
    s[0] = (uint8_t)w;
    s[1] = (uint8_t)(w >> 8);
    s[2] = (uint8_t)h;
    s[3] = (uint8_t)(h >> 8);
}

/* Draw a sprite onto the screen with its top-left pixel at linear offset `dst`
 * (row * 320 + column). Opaque. Rows are not clipped horizontally: a sprite that is too
 * wide wraps into the next row, as the original's linear writes do. Pixels that would
 * fall outside the 64000-byte screen are dropped. (MARK 0x11e2c) */
void gfx_draw_sprite(screen_t *s, size_t dst, const uint8_t *sprite);

/* Copy n bytes from src to dst, skipping source bytes equal to 0 (colour 0 is
 * transparent). (MARK 0x11856) */
void gfx_overlay(uint8_t *dst, const uint8_t *src, size_t n);

/* Copy a width x rows rectangle out of a wider image into a packed buffer.
 * `src` points at the rectangle's top-left pixel; `src_stride` is the source row length. (MARK 0x11eb7) */
void gfx_copy_rect(uint8_t *dst, const uint8_t *src, unsigned src_stride, unsigned width, unsigned rows);

/* Fill a width x rows rectangle of the screen at linear offset `dst`. (MARK 0x11be4) */
void gfx_fill_rect(screen_t *s, size_t dst, unsigned width, unsigned rows, uint8_t colour);

/* Scroll a sprite's pixels up by n rows; the bottom n rows keep their old contents. (MARK 0x11077) */
void gfx_scroll_up(uint8_t *sprite, unsigned n);

/* Rotate the sprite's rows right by one pixel, reproducing the original routine's
 * one-byte offset: its row windows are the true rows shifted by +1, and the first window
 * wraps in the byte just past the end of the sprite. That byte is persistent state: the
 * routine reads it and also overwrites it (with the last pixel), so the next call sees a
 * different value. The buffer therefore must have room for one spare byte at index
 * SPRITE_HDR + w*h, and the caller must leave it alone between calls. (MARK 0x12124) */
void gfx_rotate_rows(uint8_t *sprite);

/* Move each of n bytes of `cur` one step toward `target`. One step of a palette fade. (MARK 0x12174) */
void gfx_palette_step(uint8_t *cur, const uint8_t *target, size_t n);

/* Routines of STORY2.SL (addresses are in that program, see docs/code.md). */

/* Expand a bitmap of 1 bit per pixel, most significant bit first, into `src_bytes * 8` bytes:
 * a set bit becomes `on`, a clear bit becomes 0. (STORY2.SL 0x12984) */
void gfx_expand_bits(uint8_t *dst, const uint8_t *src, size_t src_bytes, uint8_t on);

/* Rotate every row of a sprite left by one pixel; the first pixel of a row becomes its
 * last. (STORY2.SL 0x13b19, direction 1) */
void gfx_rotate_rows_left(uint8_t *sprite);

/* Write `colour` into `dst` wherever the sprite has a non-zero pixel. `dst` points at the
 * place for the sprite's top-left pixel and rows of `dst` are `stride` bytes apart; `x` is
 * added to the starting position (the original draws text twice, at x = 0 and x = 1, to make
 * it bolder). Nothing is clipped: a pixel past the end of a row lands in the next row, as in the
 * original's linear writes. (STORY2.SL 0x134f1) */
void gfx_stamp(uint8_t *dst, unsigned stride, unsigned x, const uint8_t *sprite, uint8_t colour);

/* Copy the sprite's non-zero pixels into `dst`, whose rows are `stride` bytes apart; `dst`
 * points at the place for the sprite's top-left pixel. Colour 0 is transparent.
 * (STORY2.SL 0x134b2) */
void gfx_blit_masked(uint8_t *dst, unsigned stride, const uint8_t *sprite);

/* Copy a headerless w x h block of pixels to the screen at linear offset `dst` (row stride
 * 320); pixels past the end of the screen are dropped. (STORY2.SL 0x1293e) */
void gfx_blit_rect(screen_t *s, size_t dst, const uint8_t *src, unsigned w, unsigned h);

#endif
