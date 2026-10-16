#include "title.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"
#include "gfx.h"
#include "lcz.h"

/* Layout facts, each checked against the original's code (addresses in MARK.EXE):
 *  - strip   : TITLE1 chunk 0, a 67 x 200 sprite (13404 bytes), scrolled vertically.
 *  - banner  : chunk 1, a 260 x 24 sprite; the right 112 columns are shown.
 *  - masks   : chunks 2, 3, 4, headerless 67 x 67 images laid over the three windows.
 *  - text    : chunk 5, a headerless 112 x 24 image laid over the banner slice. */
enum {
    STRIP_W = 67, STRIP_H = 200, STRIP_BYTES = SPRITE_HDR + STRIP_W * STRIP_H,
    BANNER_W = 260, BANNER_H = 24, BANNER_BYTES = SPRITE_HDR + BANNER_W * BANNER_H,
    WIN = 67, WIN_PIXELS = WIN * WIN,
    LOGO_W = 112, LOGO_H = 24, LOGO_PIXELS = LOGO_W * LOGO_H,
    LOGO_SRC_COL = BANNER_W - LOGO_W, /* 148 */
    FADE_STEPS = 64,
    TIMEOUT_PASS = 0x2bb,             /* 699 */
    BAR_PASSES = 0x18,
    FADE_IDLE = 0x41                  /* value of the fade variable while no fade-out has begun */
};

/* Screen positions as linear offsets (row * 320 + column). */
static const size_t WIN_DST[3] = {0x1a5d, 0x1abd, 0x1b1d};      /* row 21; columns 29, 125, 221 */
/* Offset of each window's source rows inside the strip sprite (header included).
 * The third starts at row 133 so that it ends exactly at row 200. */
static const size_t WIN_SRC[3] = {SPRITE_HDR, 0x118d, 0x22d3};
static const size_t LOGO_DST = 0x70e8;                           /* row 90, column 104 */
static const size_t BAR_DST[2] = {0x823a, 0x82de};               /* row 104; columns 58 and 222 */
enum { BAR_W = 40 };

struct title {
    uint8_t strip[STRIP_BYTES];
    uint8_t banner[BANNER_BYTES + 1];      /* +1: the rotate routine's spare byte (see gfx.h) */
    uint8_t mask[3][WIN_PIXELS];
    uint8_t text[LOGO_PIXELS];
    uint8_t win[SPRITE_HDR + WIN_PIXELS];
    uint8_t logo[SPRITE_HDR + LOGO_PIXELS];
    uint8_t pal_cur[768];                  /* the working palette, uploaded after every step */
    uint8_t pal_in[768];                   /* target while fading in: ANI.LCP palette 0 */
    uint8_t pal_black[768];                /* target while fading out */

    unsigned passes;      /* pass counter, saturates at 0x2bc */
    unsigned fade_out;    /* FADE_IDLE until a fade-out begins, then counts up to 64 */
    unsigned fade_in;     /* 0..64 */
    unsigned bar_step;    /* 0..24 */
    uint8_t bar_colour;
    unsigned last_tick;
    int to_menu;
};

static int load_chunks(title_t *t)
{
    size_t len = 0;
    uint8_t *data = assets_read("title1.lcz", &len);
    if (!data) {
        fprintf(stderr, "title: cannot read title1.lcz (data dir: %s)\n", assets_dir());
        return -1;
    }
    size_t pos = 0;
    int rc = -1;
    for (int i = 0; i < 6; i++) {
        lcz_chunk c;
        if (lcz_next_chunk(data, len, &pos, &c) != LCZ_OK) {
            fprintf(stderr, "title: title1.lcz has no chunk %d\n", i);
            goto out;
        }
        size_t want = i == 0 ? STRIP_BYTES : i == 1 ? BANNER_BYTES : i <= 4 ? WIN_PIXELS : LOGO_PIXELS;
        if (c.len != want) {
            fprintf(stderr, "title: chunk %d is %zu bytes, expected %zu\n", i, c.len, want);
            lcz_chunk_free(&c);
            goto out;
        }
        uint8_t *dst = i == 0 ? t->strip : i == 1 ? t->banner : i <= 4 ? t->mask[i - 2] : t->text;
        memcpy(dst, c.pixels, want);
        lcz_chunk_free(&c);
    }
    if (sprite_w(t->strip) != STRIP_W || sprite_h(t->strip) != STRIP_H ||
        sprite_w(t->banner) != BANNER_W || sprite_h(t->banner) != BANNER_H) {
        fprintf(stderr, "title: unexpected sprite sizes in title1.lcz\n");
        goto out;
    }
    rc = 0;
out:
    free(data);
    return rc;
}

title_t *title_create(screen_t *scr, unsigned tick0)
{
    title_t *t = calloc(1, sizeof *t);
    if (!t)
        return NULL;
    if (load_chunks(t) != 0)
        goto fail;
    if (assets_read_at("ani.lcp", 0, t->pal_in, 768) != 0) {
        fprintf(stderr, "title: cannot read ani.lcp\n");
        goto fail;
    }
    for (int i = 0; i < 768; i++)
        if (t->pal_in[i] > 63) {
            fprintf(stderr, "title: ani.lcp does not start with a 6-bit palette\n");
            goto fail;
        }
    sprite_set_size(t->win, WIN, WIN);
    sprite_set_size(t->logo, LOGO_W, LOGO_H);

    t->fade_out = FADE_IDLE;
    t->bar_colour = 0x1f;
    t->last_tick = tick0 & 0xffff; /* the BIOS word is 16 bits wide here */

    memset(scr->pix, 0, SCREEN_PIXELS);
    screen_set_palette(scr, t->pal_cur, 0, 256); /* all black */
    return t;
fail:
    free(t);
    return NULL;
}

void title_free(title_t *t)
{
    free(t);
}

title_result title_pass(title_t *t, screen_t *scr, unsigned tick, int pressed)
{
    /* 1. count the pass; the timeout starts the fade-out toward the intro. */
    if (t->passes < 0x2bc)
        t->passes++;
    if (t->passes == TIMEOUT_PASS) {
        t->fade_out = 0;
        t->to_menu = 0;
    }

    /* 2. fade-out step. */
    if (t->fade_out < FADE_STEPS) {
        t->fade_out++;
        if (t->fade_out == FADE_STEPS)
            return t->to_menu ? TITLE_TO_MENU : TITLE_TO_INTRO;
        gfx_palette_step(t->pal_cur, t->pal_black, 768);
        screen_set_palette(scr, t->pal_cur, 0, 256);
    }

    /* 3. fade-in, or the banner rotation and bar ramp once the fade-in is complete. */
    if (t->fade_in < FADE_STEPS) {
        gfx_palette_step(t->pal_cur, t->pal_in, 768);
        screen_set_palette(scr, t->pal_cur, 0, 256);
        t->fade_in++;
    } else {
        gfx_rotate_rows(t->banner);
        if (t->bar_step < BAR_PASSES) {
            gfx_fill_rect(scr, BAR_DST[0], BAR_W, 1, t->bar_colour);
            gfx_fill_rect(scr, BAR_DST[1], BAR_W, 1, t->bar_colour);
            if (t->bar_step < 0xf)
                t->bar_colour--;
            else
                t->bar_colour++;
            t->bar_step++;
        }
    }

    /* 4. when the clock has ticked, scroll the strip up one row; the old top row
     *    becomes the new bottom row. */
    tick &= 0xffff;
    if (tick != t->last_tick) {
        uint8_t first_row[STRIP_W];
        memcpy(first_row, t->strip + SPRITE_HDR, STRIP_W);
        gfx_scroll_up(t->strip, 1);
        memcpy(t->strip + STRIP_BYTES - STRIP_W, first_row, STRIP_W);
        t->last_tick = tick;
    }

    /* 5. the three windows: a slice of the strip with a mask laid over it. */
    for (int i = 0; i < 3; i++) {
        memcpy(t->win + SPRITE_HDR, t->strip + WIN_SRC[i], WIN_PIXELS);
        gfx_overlay(t->win + SPRITE_HDR, t->mask[i], WIN_PIXELS);
        gfx_draw_sprite(scr, WIN_DST[i], t->win);
    }
    /* the logo: the right part of the banner with the text laid over it. */
    gfx_copy_rect(t->logo + SPRITE_HDR, t->banner + SPRITE_HDR + LOGO_SRC_COL, BANNER_W, LOGO_W, LOGO_H);
    gfx_overlay(t->logo + SPRITE_HDR, t->text, LOGO_PIXELS);
    gfx_draw_sprite(scr, LOGO_DST, t->logo);

    /* 6. input: the first press starts a fade-out as long as the fade-in had progressed. */
    if (pressed && t->fade_out == FADE_IDLE) {
        t->fade_out = FADE_STEPS - t->fade_in;
        t->fade_in = FADE_STEPS;
        t->to_menu = 1;
    }
    return TITLE_RUNNING;
}
