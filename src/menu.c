#include "menu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"
#include "gfx.h"
#include "lcz.h"

/* Layout facts, all observed in the original (docs/findings.md, "WALK0.SL (the menu)"). */
enum {
    PANEL_X = 64, PANEL_Y = 16, PANEL_W = 192, PANEL_H = 168,   /* the panel on the screen */
    LABEL_W = 122, LABEL_H = 30,                                /* one label of the strip */
    LABEL_X = 100 - PANEL_X, LABEL_Y = 40 - PANEL_Y,            /* labels inside the panel */
    DIGITS_AT = 12 * 320 + 268,                                 /* top-left of the CPU label (0x100c) */
    DIGITS_SHADOW = 0x141,                                      /* its shadow is one row and column on */
    STRIP_W = 27, STRIP_H = 8, LABEL_W2 = 53, LABEL_H2 = 15,    /* the label before and after stretching */
    COUNTER_TURN = 0x78, COUNTER_MAX = 0xc8,                    /* roll left below, up from here; wraps after */
    HIGHLIGHT_FROM = 0x17, HIGHLIGHT_TO = 0x10,                 /* the selected label turns white */
    CURSOR_BYTES = 382, CURSOR_AT = 0x3255,                     /* the arrow, read raw from RPG.LCP */
    MOUSE_X_MIN = 0x40, MOUSE_X_MAX = 0xed, MOUSE_Y_MIN = 0x10, MOUSE_Y_MAX = 0xa2,
    HOVER_X_ABOVE = 0x64, HOVER_X_BELOW = 0xdc,                 /* x must lie strictly between */
    SC_ENTER = 0x1c, SC_SPACE = 0x39, SC_UP = 0x48, SC_DOWN = 0x50
};

/* The y bands that select an item (strictly between the two values), 0x10e19 .. 0x10e73. */
static const unsigned char k_band_above[MENU_ITEMS] = {0x28, 0x47, 0x68, 0x88};
static const unsigned char k_band_below[MENU_ITEMS] = {0x40, 0x5f, 0x80, 0xa0};

#define SPRITE_BYTES(w, h) (4u + (size_t)(w) * (h))

struct menu {
    uint8_t base[SCREEN_PIXELS];          /* GMAIN.LCZ with the CPU label drawn: what is behind the panel */
    uint8_t palette[768];
    uint8_t *backdrop;                    /* 192 x 168 sprite, rolled in place */
    uint8_t *labels;                      /* 122 x 120 sprite: four labels one above the other */
    uint8_t *cursor;                      /* 18 x 21 arrow sprite (RPG.LCP 0x3255), raw: not LCZ coded */
    unsigned counter;                     /* [bp-0x12]: 0..0xc8 */
    unsigned passes;
    int highlight;
    int key_latch;                        /* [bp-0x32]: a break code has been seen */
    int confirm_step;                     /* [bp-0x3e] during the transition after a choice */
    size_t mark_at;                       /* [bp-0x20]: where the check mark is drawn in the label strip */
    int arrow_at;                         /* [bp-0x30]: where the arrow is drawn in the panel */
};

/* Put a panel (sprite) on the screen at (64, 16): the original's 0x13026(0x1440, panel). */
static void present_panel(screen_t *scr, const uint8_t *panel)
{
    for (int y = 0; y < PANEL_H; y++)
        memcpy(scr->pix + (size_t)(PANEL_Y + y) * SCREEN_W + PANEL_X, panel + 4 + (size_t)y * PANEL_W, PANEL_W);
}

/* ---- the original's helpers ---------------------------------------------------------------- */

void menu_stretch(uint8_t *dst, const uint8_t *src, unsigned dw, unsigned dh)
{
    unsigned sw = sprite_w(src), sh = sprite_h(src);
    memset(dst, 0, ((size_t)dw * dh + 5) / 2 * 2);
    sprite_set_size(dst, dw, dh);
    unsigned xstep = (unsigned)(((uint32_t)0xffff * sw) / dw) & 0xffff;
    unsigned ystep = (unsigned)(((uint32_t)0xffff * sh) / dh) & 0xffff;
    size_t di = 4, row = 4;
    unsigned acc = 1;
    for (unsigned r = 0; r < sh; r++) {
        size_t si = row;
        unsigned bx = 1;
        uint8_t al = src[si];   /* 0x12bc6: loaded once per row, then only when the add carries */
        for (unsigned c = 0; c < dw; c++) {
            dst[di++] = al;
            al = 0;             /* so a source pixel is written once and the pixels after it stay 0 */
            bx += xstep;
            if (bx > 0xffff) {   /* the add carried: next source pixel */
                bx &= 0xffff;
                si++;
                al = src[si];
            }
        }
        /* Skip destination rows (left zero) until the vertical accumulator overflows. */
        for (;;) {
            acc += ystep;
            if (acc > 0xffff) {
                acc &= 0xffff;
                break;
            }
            di += dw;
        }
        row += sw;
    }
}

void menu_tile_copy(uint8_t *dst, size_t off, const uint8_t *tile, unsigned dst_w, unsigned w, unsigned h)
{
    size_t di = off;
    for (unsigned y = 0; y < h; y++) {
        for (unsigned x = 0; x < w; x++) {
            uint8_t v = *tile++;
            if (v)
                dst[di] = v;
            di++;
        }
        di += dst_w - w;
    }
}

void menu_shade(uint8_t *p, size_t n, uint8_t v)
{
    for (size_t i = 0; i < n; i++)
        if (p[i])
            p[i] = v;
}

void menu_recolour(uint8_t *p, size_t n, uint8_t from, uint8_t to)
{
    for (size_t i = 0; i < n; i++)
        if (p[i] == from)
            p[i] = to;
}

void menu_resize(uint8_t *dst, const uint8_t *src, unsigned dw, unsigned dh)
{
    unsigned sw = sprite_w(src), sh = sprite_h(src);
    sprite_set_size(dst, dw, dh);
    /* Steps, all 16-bit as in the original (0x12c93 .. 0x12ccf): per destination row the source
     * pointer moves by yq whole rows plus one more whenever the fraction yf carries; per destination
     * column by xq pixels plus one when xf carries. */
    unsigned yunit = 0xffffu / dh;
    unsigned row_adv = ((sh / dh) * sw) & 0xffff;
    unsigned yf = ((sh % dh) * yunit) & 0xffff;
    unsigned xunit = 0xffffu / dw;
    unsigned xq = sw / dw;
    unsigned xf = ((sw % dw) * xunit) & 0xffff;
    unsigned row = 4, xacc = 0, yacc = 0;
    size_t di = 4;
    for (unsigned r = 0; r < dh; r++) {
        unsigned si = row;
        for (unsigned c = 0; c < dw; c++) {
            dst[di++] = src[si];
            si = (si + xq) & 0xffff;
            xacc += xf;
            if (xacc > 0xffff) {   /* add [mem], bx; adc si, 0 */
                xacc &= 0xffff;
                si = (si + 1) & 0xffff;
            }
        }
        row = (row + row_adv) & 0xffff;
        yacc += yf;
        if (yacc > 0xffff) {
            yacc &= 0xffff;
            row = (row + sw) & 0xffff;
        }
    }
}

/* ---- loading ------------------------------------------------------------------------------- */

/* Decode the first LCZ chunk of the `len` bytes at `off` of a data file. Returns a malloc'ed
 * buffer of exactly `want` bytes, or NULL (with a message) if the chunk has another size. */
static uint8_t *load_block(const char *file, size_t off, size_t len, size_t want)
{
    uint8_t *raw = malloc(len);
    if (!raw)
        return NULL;
    if (assets_read_at(file, off, raw, len) != 0) {
        fprintf(stderr, "menu: cannot read %zu bytes at 0x%zx of %s\n", len, off, file);
        free(raw);
        return NULL;
    }
    size_t pos = 0;
    lcz_chunk c;
    uint8_t *out = NULL;
    if (lcz_next_chunk(raw, len, &pos, &c) != LCZ_OK) {
        fprintf(stderr, "menu: %s at 0x%zx is not an LCZ chunk\n", file, off);
    } else if (c.len != want) {
        fprintf(stderr, "menu: %s at 0x%zx decodes to %zu bytes, expected %zu\n", file, off, c.len, want);
        lcz_chunk_free(&c);
    } else {
        out = c.pixels;   /* ownership moves to the caller */
    }
    free(raw);
    return out;
}

/* Build the CPU-class label (0x10258 .. 0x10357) and draw it, with its shadow, onto `frame`. */
static void draw_cpu_label(uint8_t *frame, const uint8_t *atlas, size_t atlas_len, unsigned speed_class)
{
    uint8_t strip[SPRITE_BYTES(STRIP_W, STRIP_H) + 24];      /* the original clears 244 bytes */
    uint8_t stretched[SPRITE_BYTES(LABEL_W2, LABEL_H2) + 8];
    memset(strip, 0, sizeof strip);
    sprite_set_size(strip, STRIP_W, STRIP_H);

    /* Three 8 x 8 tiles of the digit atlas, 64 bytes each: tile (speed class + 2), then 8 and 6. */
    const size_t tiles[3] = {(size_t)speed_class * 64 + 0x80, 0x200, 0x180};
    const size_t at[3] = {4, 13, 22};
    for (int i = 0; i < 3; i++) {
        uint8_t tile[64];
        for (size_t k = 0; k < 64; k++)
            tile[k] = tiles[i] + k < atlas_len ? atlas[tiles[i] + k] : 0;   /* the original reads on */
        menu_tile_copy(strip, at[i], tile, STRIP_W, 8, 8);
    }
    /* A colour ramp: twelve bands of 20 bytes, 0xf0 upward (0x102c1 .. 0x102f8). */
    for (unsigned k = 0; k < 12; k++)
        menu_shade(strip + 4 + 20 * k, 20, (uint8_t)(0xf0 + k));

    menu_stretch(stretched, strip, LABEL_W2, LABEL_H2);
    gfx_blit_masked(frame + DIGITS_AT, SCREEN_W, stretched);
    gfx_stamp(frame + DIGITS_AT + DIGITS_SHADOW, SCREEN_W, 0, stretched, 0);
}

/* ---- the opening --------------------------------------------------------------------------- */

enum {
    WIPE_STEPS = 20,
    WIPE_FIRST_AT = 0x7c5f,                    /* the first zoom rectangle's top-left: (159, 99) */
    WIPE_AT_STEP = 0x648,                      /* each step starts 5 rows and 8 columns earlier */
    READY_AT = 0x65b1, READY_W = 83, READY_H = 33,   /* the lettering: (113, 81) */
    READY_CLEAR_W = 84                         /* the final clear is one column wider */
};

/* Step k of the lettering's shrink: the size it is drawn at and where it sits in the 83 x 33 frame
 * (an offset including the 4 header bytes). The loop runs while the height is above 1. Returns 0 past
 * the end. (0x10410 .. 0x104c7) */
static int shrink_params(int k, unsigned *w, unsigned *h, size_t *off)
{
    unsigned cw = READY_W, ch = READY_H;
    size_t si = 4;
    int flag = 0, cnt = 0;
    for (int step = 0; ch > 1; step++) {
        if (step == k) {
            *w = cw;
            *h = ch;
            *off = si;
            return 1;
        }
        if (++cnt > 4) {   /* every 5th step: 2 narrower, one column over */
            cnt = 0;
            cw -= 2;
            si += 1;
            if (flag) {    /* every 2nd of those: 2 lower, one row down */
                flag = 0;
                ch -= 2;
                si += READY_W;
            } else {
                flag = 1;
            }
        }
    }
    return 0;
}

static int shrink_steps(void)
{
    unsigned w, h;
    size_t off;
    int n = 0;
    while (shrink_params(n, &w, &h, &off))
        n++;
    return n;
}

int menu_opening_steps(void) { return WIPE_STEPS + 1 + shrink_steps() + 1; }

/* Copy a w x h rectangle out of a 320-wide picture into a sprite (0x12f8b). */
static void grab_rect(uint8_t *sprite, const uint8_t *pic, size_t off, unsigned w, unsigned h)
{
    sprite_set_size(sprite, w, h);
    gfx_copy_rect(sprite + SPRITE_HDR, pic + off, SCREEN_W, w, h);
}

void menu_opening_frame(menu_t *m, screen_t *scr, int n)
{
    const int total = menu_opening_steps();
    if (n > total)
        n = total;
    uint8_t *pix = scr->pix;
    if (n <= 0) {
        memset(pix, 0, SCREEN_PIXELS);
        return;
    }
    if (n <= WIPE_STEPS) {
        int i = n - 1;
        unsigned w = 2 + 16 * i, h = 2 + 10 * i;
        size_t off = WIPE_FIRST_AT - (size_t)WIPE_AT_STEP * i;
        uint8_t *rect = calloc(1, SPRITE_BYTES(SCREEN_W, SCREEN_H) + 8);
        uint8_t *big = calloc(1, SPRITE_BYTES(SCREEN_W, SCREEN_H) + 8);
        if (!rect || !big) {
            free(rect);
            free(big);
            return;
        }
        grab_rect(rect, m->base, off, w, h);
        menu_stretch(big, rect, SCREEN_W, SCREEN_H);
        memcpy(pix, big + SPRITE_HDR, SCREEN_PIXELS);
        free(rect);
        free(big);
        return;
    }
    memcpy(pix, m->base, SCREEN_PIXELS);
    if (n == WIPE_STEPS + 1)
        return;
    if (n == total) {
        for (unsigned y = 0; y < READY_H; y++)
            memset(pix + READY_AT + (size_t)y * SCREEN_W, 0, READY_CLEAR_W);
        return;
    }
    /* A shrink step: the lettering, resized, set into a cleared 83 x 33 frame, shown at (113, 81). */
    unsigned w, h;
    size_t off;
    if (!shrink_params(n - (WIPE_STEPS + 2), &w, &h, &off))
        return;
    uint8_t rect[SPRITE_BYTES(READY_W, READY_H) + 8];
    uint8_t small[SPRITE_BYTES(READY_W, READY_H) + 8];
    uint8_t frame[SPRITE_BYTES(READY_W, READY_H) + 8];
    memset(rect, 0, sizeof rect);
    memset(small, 0, sizeof small);
    memset(frame, 0, sizeof frame);
    grab_rect(rect, m->base, READY_AT, READY_W, READY_H);
    menu_resize(small, rect, w, h);
    for (unsigned r = 0; r < h; r++)
        memcpy(frame + off + (size_t)r * READY_W, small + SPRITE_HDR + (size_t)r * w, w);
    for (unsigned y = 0; y < READY_H; y++)
        memcpy(pix + READY_AT + (size_t)y * SCREEN_W, frame + SPRITE_HDR + (size_t)y * READY_W, READY_W);
}

menu_t *menu_create(screen_t *scr)
{
    menu_t *m = calloc(1, sizeof *m);
    if (!m)
        return NULL;

    size_t gsize = 0;
    uint8_t *gfile = assets_read("GMAIN.LCZ", &gsize);
    uint8_t *atlas = NULL;
    int ok = 0;
    if (gfile) {
        size_t pos = 0;
        lcz_chunk c;
        if (lcz_next_chunk(gfile, gsize, &pos, &c) == LCZ_OK) {
            if (c.len == SCREEN_PIXELS) {
                memcpy(m->base, c.pixels, SCREEN_PIXELS);
                ok = 1;
            } else {
                fprintf(stderr, "menu: GMAIN.LCZ chunk 0 is not a 64000-byte screen\n");
            }
            lcz_chunk_free(&c);
        } else {
            fprintf(stderr, "menu: GMAIN.LCZ is not an LCZ file\n");
        }
    } else {
        fprintf(stderr, "menu: cannot read GMAIN.LCZ\n");
    }
    free(gfile);
    if (!ok || assets_read_at("RPG.LCP", 0, m->palette, sizeof m->palette) != 0) {
        menu_free(m);
        return NULL;
    }
    for (int i = 0; i < 768; i++)
        if (m->palette[i] > 63) {
            fprintf(stderr, "menu: RPG.LCP does not start with a 6-bit palette\n");
            menu_free(m);
            return NULL;
        }
    /* RPG.LCP byte 0 is the red value of colour 0 and, for this program, the speed class. */
    unsigned speed_class = m->palette[0];

    m->backdrop = load_block("RPG.LCP", 0x11d3e, 0x1b58, SPRITE_BYTES(PANEL_W, PANEL_H));
    m->labels = load_block("RPG.LCP", 0x1384, 0x1ed1, SPRITE_BYTES(LABEL_W, 4 * LABEL_H));
    atlas = load_block("RPG.LCP", 0x1643b, 1214, 2148);
    if (!m->backdrop || !m->labels || !atlas || sprite_w(m->backdrop) != PANEL_W ||
        sprite_h(m->backdrop) != PANEL_H || sprite_w(m->labels) != LABEL_W || sprite_h(m->labels) != 4 * LABEL_H) {
        fprintf(stderr, "menu: RPG.LCP pictures are missing or have an unexpected shape\n");
        free(atlas);
        menu_free(m);
        return NULL;
    }
    draw_cpu_label(m->base, atlas, 2148, speed_class);
    free(atlas);

    /* The arrow cursor follows the labels in the file; the original hands the raw bytes to its blit. */
    m->cursor = malloc(CURSOR_BYTES);
    if (!m->cursor || assets_read_at("RPG.LCP", CURSOR_AT, m->cursor, CURSOR_BYTES) != 0 ||
        sprite_w(m->cursor) * sprite_h(m->cursor) + 4 != CURSOR_BYTES) {
        fprintf(stderr, "menu: cannot read the cursor sprite from RPG.LCP\n");
        menu_free(m);
        return NULL;
    }

    /* The original sets the counter to 0 and jumps to the loop's test (0x10b89), whose first act is to count:
     * the first pass runs with counter 1. */
    m->counter = 0;

    screen_set_palette(scr, m->palette, 0, 256);
    memcpy(scr->pix, m->base, SCREEN_PIXELS);
    return m;
}

void menu_free(menu_t *m)
{
    if (m) {
        free(m->backdrop);
        free(m->labels);
        free(m->cursor);
    }
    free(m);
}

void menu_set_highlight(menu_t *m, int item)
{
    if (item >= 0 && item < MENU_ITEMS)
        m->highlight = item;
}

int menu_highlight(const menu_t *m) { return m->highlight; }
unsigned menu_passes(const menu_t *m) { return m->passes; }

void menu_input_reset(menu_t *m) { m->key_latch = 0; }

menu_result menu_pass_input(menu_t *m, screen_t *scr, unsigned port60, const menu_mouse *mouse)
{
    /* 0x10b91: count the pass; it wraps to 0 after 0xc8. */
    if (++m->counter > COUNTER_MAX)
        m->counter = 0;
    m->passes++;

    if (m->counter < COUNTER_TURN) {
        gfx_rotate_rows_left(m->backdrop);   /* 0x1328b: one pixel left, the first pixel wraps */
    } else {
        /* 0x10bd8 .. 0x10c1f: save the top row, scroll up one row, put the row at the bottom. */
        uint8_t row0[PANEL_W];
        memcpy(row0, m->backdrop + 4, PANEL_W);
        gfx_scroll_up(m->backdrop, 1);
        memcpy(m->backdrop + 4 + (size_t)(PANEL_H - 1) * PANEL_W, row0, PANEL_W);
    }

    /* 0x10c22 ..: the panel is a copy of the backdrop; the labels are a copy of the strip with the
     * selected one recoloured, laid over the panel with colour 0 transparent. */
    uint8_t panel[SPRITE_BYTES(PANEL_W, PANEL_H)];
    uint8_t labels[SPRITE_BYTES(LABEL_W, 4 * LABEL_H)];
    memcpy(panel, m->backdrop, sizeof panel);
    memcpy(labels, m->labels, sizeof labels);
    menu_recolour(labels + 4 + (size_t)m->highlight * LABEL_W * LABEL_H, (size_t)LABEL_W * LABEL_H,
                  HIGHLIGHT_FROM, HIGHLIGHT_TO);
    gfx_blit_masked(panel + 4 + (size_t)LABEL_Y * PANEL_W + LABEL_X, PANEL_W, labels);

    /* 0x10c8d .. 0x10d65: the keyboard. A break code (>= 0x80) opens the latch; until it is open a make
     * code is replaced by 0x82 and ignored. An accepted key closes the latch again. */
    unsigned key = port60 & 0xff;
    if (key > 0x7f)
        m->key_latch = 1;
    if (!m->key_latch)
        key = 0x82;
    if (key < 0x80) {
        m->key_latch = 0;
        if (key == SC_SPACE || key == SC_ENTER)
            return m->highlight;   /* 0x10e9d: no present on this pass */
        if (key == SC_UP)
            m->highlight = m->highlight > 0 ? m->highlight - 1 : MENU_ITEMS - 1;
        else if (key == SC_DOWN)
            m->highlight = m->highlight < MENU_ITEMS - 1 ? m->highlight + 1 : 0;
    }

    /* 0x10d69 .. 0x10e86: the mouse. */
    menu_result result = MENU_NONE;
    if (mouse) {
        int x = mouse->x, y = mouse->y;
        if (x < MOUSE_X_MIN) x = MOUSE_X_MIN;
        if (x > MOUSE_X_MAX) x = MOUSE_X_MAX;
        if (y < MOUSE_Y_MIN) y = MOUSE_Y_MIN;
        if (y > MOUSE_Y_MAX) y = MOUSE_Y_MAX;
        /* The arrow goes on top, at the position inside the panel (0x10dcc .. 0x10dfe). Its tip is the
         * top-left pixel; nothing is clipped by the original, so an arrow near the right or bottom edge
         * spills into the next row / is dropped at the end of the buffer. */
        gfx_blit_masked(panel + 4 + (size_t)(y - PANEL_Y) * PANEL_W + (x - PANEL_X), PANEL_W, m->cursor);
        if (x > HOVER_X_ABOVE && x < HOVER_X_BELOW) {
            for (int k = 0; k < MENU_ITEMS; k++)
                if (y > k_band_above[k] && y < k_band_below[k])
                    m->highlight = k;
            if (mouse->buttons & 0xff)
                result = m->highlight;   /* 0x10e86 -> 0x10e9d */
        }
    }
    if (result != MENU_NONE)
        return result;

    /* 0x10e89: put the panel on the screen at (64, 16). */
    present_panel(scr, panel);
    return MENU_NONE;
}

void menu_confirm_begin(menu_t *m)
{
    /* 0x10e9d .. 0x10ecc. hv = 4 + 3660 k is the chosen label's offset in the strip (header included). */
    size_t hv = 4 + (size_t)m->highlight * LABEL_W * LABEL_H;
    m->confirm_step = 0;
    m->mark_at = hv + 0x4c4;                                   /* row 10, column 0 of the label */
    m->arrow_at = 0xc0 * (m->highlight * LABEL_H + 0x21) + 0x25;   /* panel row 33 + 30 k, column 37 */
}

int menu_confirm_step(menu_t *m, screen_t *scr)
{
    if (m->confirm_step >= MENU_CONFIRM_STEPS)
        return 0;
    gfx_rotate_rows_left(m->backdrop);   /* 0x1328b: always the horizontal roll here */

    uint8_t panel[SPRITE_BYTES(PANEL_W, PANEL_H)];
    uint8_t labels[SPRITE_BYTES(LABEL_W, 4 * LABEL_H)];
    memcpy(panel, m->backdrop, sizeof panel);

    /* Two words of 0x0024 (colour 0x24, then a 0 that erases the pixel beside it), one label row apart. */
    for (int row = 0; row < 2; row++) {
        size_t at = m->mark_at + (size_t)row * LABEL_W;
        m->labels[at] = 0x24;
        m->labels[at + 1] = 0x00;
    }
    memcpy(labels, m->labels, sizeof labels);
    menu_recolour(labels + 4 + (size_t)m->highlight * LABEL_W * LABEL_H, (size_t)LABEL_W * LABEL_H,
                  HIGHLIGHT_FROM, HIGHLIGHT_TO);
    gfx_blit_masked(panel + 4 + (size_t)LABEL_Y * PANEL_W + LABEL_X, PANEL_W, labels);
    gfx_blit_masked(panel + 4 + m->arrow_at, PANEL_W, m->cursor);   /* the pen */
    present_panel(scr, panel);

    m->confirm_step++;
    if (m->confirm_step < 12) {   /* the counter is tested after it is incremented (0x10fb9) */
        m->mark_at += 0x7b;       /* one row down, one column right */
        m->arrow_at += 0xc1;
    } else {
        m->mark_at -= 0x79;       /* one row up, one column right */
        m->arrow_at -= 0xbf;
    }
    return m->confirm_step < MENU_CONFIRM_STEPS;
}

menu_result menu_pass(menu_t *m, screen_t *scr)
{
    return menu_pass_input(m, scr, 0x80, NULL);   /* a break code on the port: the latch is open, no key */
}
