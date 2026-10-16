#include "gfx.h"

#include <string.h>

void gfx_draw_sprite(screen_t *s, size_t dst, const uint8_t *sprite)
{
    size_t w = sprite_w(sprite), h = sprite_h(sprite);
    const uint8_t *px = sprite + SPRITE_HDR;
    for (size_t r = 0; r < h; r++)
        for (size_t c = 0; c < w; c++) {
            size_t o = dst + r * SCREEN_W + c;
            if (o < SCREEN_PIXELS)
                s->pix[o] = px[r * w + c];
        }
}

void gfx_overlay(uint8_t *dst, const uint8_t *src, size_t n)
{
    for (size_t i = 0; i < n; i++)
        if (src[i] != 0)
            dst[i] = src[i];
}

void gfx_copy_rect(uint8_t *dst, const uint8_t *src, unsigned src_stride, unsigned width, unsigned rows)
{
    for (unsigned r = 0; r < rows; r++)
        memcpy(dst + (size_t)r * width, src + (size_t)r * src_stride, width);
}

void gfx_fill_rect(screen_t *s, size_t dst, unsigned width, unsigned rows, uint8_t colour)
{
    for (size_t r = 0; r < rows; r++)
        for (size_t c = 0; c < width; c++) {
            size_t o = dst + r * SCREEN_W + c;
            if (o < SCREEN_PIXELS)
                s->pix[o] = colour;
        }
}

void gfx_scroll_up(uint8_t *sprite, unsigned n)
{
    unsigned w = sprite_w(sprite), h = sprite_h(sprite);
    if (n == 0 || n >= h)
        return;
    uint8_t *px = sprite + SPRITE_HDR;
    memmove(px, px + (size_t)n * w, (size_t)(h - n) * w);
}

void gfx_rotate_rows(uint8_t *sprite)
{
    size_t w = sprite_w(sprite), h = sprite_h(sprite);
    if (w < 2 || h == 0)
        return;
    size_t end = SPRITE_HDR + w * h; /* index of the spare byte just past the last pixel */
    size_t si = end, di = end;
    for (size_t row = 0; row < h; row++) {
        uint8_t wrap = sprite[si];
        si--;
        for (size_t k = 0; k + 1 < w; k++) {
            sprite[di] = sprite[si];
            si--;
            di--;
        }
        sprite[di] = wrap;
        di--;
    }
}

void gfx_palette_step(uint8_t *cur, const uint8_t *target, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (cur[i] > target[i])
            cur[i]--;
        else if (cur[i] < target[i])
            cur[i]++;
    }
}

void gfx_expand_bits(uint8_t *dst, const uint8_t *src, size_t src_bytes, uint8_t on)
{
    for (size_t i = 0; i < src_bytes; i++)
        for (int b = 0; b < 8; b++)
            dst[i * 8 + (size_t)b] = (src[i] & (0x80u >> b)) ? on : 0;
}

void gfx_rotate_rows_left(uint8_t *sprite)
{
    size_t w = sprite_w(sprite), h = sprite_h(sprite);
    if (w < 2)
        return;
    uint8_t *px = sprite + SPRITE_HDR;
    for (size_t r = 0; r < h; r++) {
        uint8_t *row = px + r * w;
        uint8_t first = row[0];
        memmove(row, row + 1, w - 1);
        row[w - 1] = first;
    }
}

void gfx_stamp(uint8_t *dst, unsigned stride, unsigned x, const uint8_t *sprite, uint8_t colour)
{
    size_t w = sprite_w(sprite), h = sprite_h(sprite);
    const uint8_t *px = sprite + SPRITE_HDR;
    for (size_t r = 0; r < h; r++)
        for (size_t c = 0; c < w; c++)
            if (px[r * w + c] != 0)
                dst[r * stride + c + x] = colour;
}

void gfx_blit_masked(uint8_t *dst, unsigned stride, const uint8_t *sprite)
{
    size_t w = sprite_w(sprite), h = sprite_h(sprite);
    const uint8_t *px = sprite + SPRITE_HDR;
    for (size_t r = 0; r < h; r++)
        for (size_t c = 0; c < w; c++)
            if (px[r * w + c] != 0)
                dst[r * stride + c] = px[r * w + c];
}

void gfx_blit_rect(screen_t *s, size_t dst, const uint8_t *src, unsigned w, unsigned h)
{
    for (size_t r = 0; r < h; r++)
        for (size_t c = 0; c < w; c++) {
            size_t o = dst + r * SCREEN_W + c;
            if (o < SCREEN_PIXELS)
                s->pix[o] = src[r * w + c];
        }
}
