/*
 * The credits (CAST.SL). See credits.h and docs/findings.md ("CAST.SL ...") for what is reproduced and how it
 * was checked against the original's own buffers and screens.
 */
#include "credits.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"
#include "lcz.h"

enum {
    W = 120,                  /* width of the text window and of the sprites, pixels */
    HA = 30,                  /* rows of the sprite the newest text enters (its header says 120 x 30) */
    HB = 100,                 /* rows of the older-text sprite, and of the window on the screen */
    NEW_ROW = 26,             /* row of A where the next line of the bitmap is written (A + 0xc34) */
    BITMAP_ROW_BYTES = 15,    /* 120 pixels of 1 bit */
    BG_BYTES = 32000,         /* CAST1.LCZ decodes to the lower half of the screen */
    SPRITE_A_BYTES = 4 + W * HA,
    SPRITE_B_BYTES = 4 + W * HB,
    L_BYTES = 0x2000,         /* the floor's source rows (the original keeps them in a 65,500 byte buffer) */
    F_BYTES = 0x8000,         /* the floor image: 320 x 100 pixels, 101 rows are written */
    L_COPY = 0x654 * 2,       /* bytes of A copied into L: rows 0..26 (0x1173b counts words) */
    C_COPY = 0x1770 * 2       /* bytes of B copied into C each pass: header and all but the last 4 pixels */
};

struct credits {
    uint8_t saved[SCREEN_PIXELS];       /* the previous program's picture, what the wipe rolls away */
    uint8_t bg[BG_BYTES];               /* CAST1.LCZ, decoded */
    uint8_t text[CREDITS_TEXT_BYTES];   /* CAST2.LCZ, decoded, zero padded */
    uint8_t a[SPRITE_A_BYTES];          /* newest text: rows enter at NEW_ROW and move up */
    uint8_t b[SPRITE_B_BYTES];          /* older text: A's top row enters at the bottom and moves up */
    uint8_t c[SPRITE_B_BYTES];          /* B with the colour ramps, what is shown in the window */
    uint8_t l[L_BYTES];                 /* A's top 27 rows with a colour ramp: the floor's source */
    uint8_t f[F_BYTES];                 /* the floor image */
    unsigned wipe_done;
    unsigned scroll;                    /* byte offset into `text` of the next row (the original's [bp-0x14]) */
    unsigned hold;                      /* passes since the scroll stopped (the original's [bp-0x58]) */
    unsigned passes;
};

static void put16(uint8_t *p, unsigned v)
{
    p[0] = (uint8_t)(v & 0xff);
    p[1] = (uint8_t)(v >> 8);
}

/* Decode a file that is a single LCZ chunk into `dst` (capacity `cap`), leaving the rest of dst alone. */
static int load_chunk(const char *name, uint8_t *dst, size_t cap)
{
    size_t size = 0, pos = 0;
    uint8_t *file = assets_read(name, &size);
    if (!file) {
        fprintf(stderr, "credits: cannot read %s\n", name);
        return -1;
    }
    lcz_chunk ch;
    int rc = lcz_next_chunk(file, size, &pos, &ch);
    free(file);
    if (rc != LCZ_OK) {
        fprintf(stderr, "credits: %s is not a valid LCZ file\n", name);
        return -1;
    }
    if (ch.len > cap) {
        fprintf(stderr, "credits: %s decodes to %zu bytes, more than the %zu expected\n", name, ch.len, cap);
        lcz_chunk_free(&ch);
        return -1;
    }
    memcpy(dst, ch.pixels, ch.len);
    lcz_chunk_free(&ch);
    return 0;
}

credits_t *credits_create(const screen_t *scr)
{
    credits_t *c = calloc(1, sizeof *c);
    if (!c)
        return NULL;
    if (load_chunk("cast1.lcz", c->bg, sizeof c->bg) != 0 ||
        load_chunk("cast2.lcz", c->text, sizeof c->text) != 0) {
        free(c);
        return NULL;
    }
    memcpy(c->saved, scr->pix, SCREEN_PIXELS);
    /* The headers the original writes before its loop (0x1035d .. 0x103e5): the sprites A and B, and the floor.
     * C has none until the first copy of B. */
    put16(c->a, W);
    put16(c->a + 2, HA);
    put16(c->b, W);
    put16(c->b + 2, HB);
    put16(c->f, 320);
    put16(c->f + 2, 80);
    return c;
}

void credits_free(credits_t *c)
{
    free(c);
}

unsigned credits_passes(const credits_t *c)
{
    return c->passes;
}

int credits_wipe_step(credits_t *c, screen_t *scr)
{
    if (c->wipe_done >= CREDITS_WIPE_STEPS)
        return 0;
    c->wipe_done++;
    const unsigned k = c->wipe_done;
    const unsigned rem = SCREEN_PIXELS - 320u * k;   /* bytes of the old picture still on the screen */
    const unsigned off = 320u * k;
    unsigned si = SCREEN_PIXELS - 320u * k;          /* where the new picture's top currently is */
    unsigned di = 320u * k;                          /* how much of the new picture has come in */
    /* the old picture, shifted up by k rows */
    memcpy(scr->pix, c->saved + off, rem);
    if (si < 0x7d00) {                               /* past the middle: the new half stays whole, */
        si = di = 0x7d00;
        memset(scr->pix + rem, 0, 320);              /* and the old picture's last row goes dark */
    }
    memcpy(scr->pix + si, c->bg, di);                /* the new picture, rolling in from below */
    return c->wipe_done < CREDITS_WIPE_STEPS;
}

void credits_expand_bits(uint8_t *dst, const uint8_t *src, unsigned nbytes, uint8_t colour)
{
    for (unsigned i = 0; i < nbytes; i++)
        for (int bit = 7; bit >= 0; bit--)
            *dst++ = (src[i] >> bit) & 1 ? colour : 0;
}

void credits_resize_line(uint8_t *dst, const uint8_t *src, unsigned srclen, unsigned dstlen)
{
    const uint32_t step = (0xffffu * srclen) / dstlen;
    uint32_t acc = 1;
    unsigned pos = 0;
    for (unsigned i = 0; i < dstlen; i++) {
        dst[i] = src[pos];
        acc += step;
        if (acc > 0xffff) {   /* the carry of the original's 16-bit add */
            acc &= 0xffff;
            pos++;
        }
    }
}

void credits_scroll_up(uint8_t *sprite)
{
    const unsigned w = sprite[0] | (unsigned)sprite[1] << 8;
    const unsigned h = sprite[2] | (unsigned)sprite[3] << 8;
    if (h > 1)
        memmove(sprite + 4, sprite + 4 + w, (size_t)(h - 1) * w);
}

static void recolour(uint8_t *p, size_t n, uint8_t from, uint8_t to)
{
    for (size_t i = 0; i < n; i++)
        if (p[i] == from)
            p[i] = to;
}

credits_status credits_pass(credits_t *c, screen_t *scr, unsigned port60)
{
    /* The text moves up: A's top row passes into B's bottom row, both sprites scroll up one row, and the
     * next row of the bitmap enters at the bottom of A's used part. */
    memcpy(c->b + 4 + (size_t)(HB - 1) * W, c->a + 4, W);
    credits_scroll_up(c->a);
    credits_scroll_up(c->b);
    credits_expand_bits(c->a + 4 + NEW_ROW * W, c->text + c->scroll, BITMAP_ROW_BYTES, 0x10);
    c->scroll += BITMAP_ROW_BYTES;
    if (c->scroll > CREDITS_SCROLL_MAX) {
        c->scroll = CREDITS_SCROLL_MAX;
        if (++c->hold > CREDITS_HOLD_PASSES)
            return CREDITS_END_TIME;   /* the original leaves here, before drawing */
    }

    /* The window: B with colour 0x10 ramped to 0xdf..0xd1 over the top 30 rows and 0xd1..0xdf over the bottom 30. */
    memcpy(c->c, c->b, C_COPY);
    size_t off = 4;
    for (unsigned col = 0xdf; col > 0xd0; col--, off += 0xf0)
        recolour(c->c + off, 0xf0, 0x10, (uint8_t)col);
    off = 0x20d4;
    for (unsigned col = 0xd1; col < 0xe0; col++, off += 0xf0)
        recolour(c->c + off, 0xf0, 0x10, (uint8_t)col);
    for (int y = 0; y < HB; y++)   /* opaque, at x = 100, y = 0 (0x11560) */
        memcpy(scr->pix + (size_t)y * SCREEN_W + 100, c->c + 4 + (size_t)y * W, W);

    /* The floor's source: A's top 27 rows, the first 16 recoloured one by one 0xdf .. 0xd0. */
    memcpy(c->l, c->a + 4, L_COPY);
    off = 0;
    for (unsigned col = 0xdf; col > 0xcf; col--, off += W)
        recolour(c->l + off, W, 0x10, (uint8_t)col);

    /* The floor: row i is one line of L resized to 120 + 2 i pixels, written 319 bytes further on than the
     * row before (so each row starts one pixel further left). The source line changes only after `rep + 1`
     * rows, and `rep` grows by one every four changes. F is not cleared between passes. */
    {
        unsigned row = 0, cnt = 0, rep = 0, sub = 0, si = 0x68;
        for (unsigned di = 0x78; di < 0x142; di += 2) {
            credits_resize_line(c->f + si, c->l + row, W, di);
            si += 0x13f;
            if (++cnt > rep) {
                row += W;
                cnt = 0;
                if (++sub > 3) {
                    sub = 0;
                    rep++;
                }
            }
        }
    }

    /* The lower half of the screen: the background with the floor's non-zero pixels over it (0x111d2). */
    uint8_t *low = scr->pix + BG_BYTES;
    memcpy(low, c->bg, BG_BYTES);
    for (size_t i = 0; i < BG_BYTES; i++)
        if (c->f[4 + i])
            low[i] = c->f[4 + i];

    c->passes++;
    if (port60 == 0x39 || port60 == 0x1c)
        return CREDITS_END_KEY;
    return CREDITS_RUNNING;
}
