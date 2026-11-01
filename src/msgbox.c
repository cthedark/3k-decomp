#include "msgbox.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"
#include "battle.h"
#include "l10n.h"

/* The message box's character: `bits` (MSB first, `w` / 8 bytes a row, 14 rows) in `colour`, recoloured from data byte `split` on to
 * colour + 1 and from 2 * split - 2 (the loop's 0x25 / 0x50 steps) to colour + 2; a shadow of colour + 6 one row down, at x and x + 1. */
static void message_char(screen_t *scr, unsigned at, const uint8_t *bits, unsigned w, uint8_t colour)
{
    uint8_t px[16 * 14];
    unsigned n = w * 14, bw = w / 8;
    for (unsigned r = 0; r < 14; r++)
        for (unsigned x = 0; x < w; x++)
            px[r * w + x] = (bits[r * bw + x / 8] << (x % 8)) & 0x80 ? colour : 0;
    /* 0x2741b twice: 8 wide from 0x26 (37 bytes each), 16 wide from 0x50 (80 bytes each) */
    unsigned from = w == 8 ? 0x26 : 0x50, step = w == 8 ? 0x25 : 0x50;
    for (unsigned k = 0; k < 2; k++)
        for (unsigned i = from + k * step; i < from + (k + 1) * step && i < n; i++)
            if (px[i] == colour)
                px[i] = (uint8_t)(colour + 1 + k);
    static const unsigned shadow[2] = {SCREEN_W + 1, SCREEN_W};
    for (int s = 0; s < 2; s++)   /* 0x25b54 */
        for (unsigned i = 0; i < n; i++)
            if (px[i] && at + shadow[s] + (i / w) * SCREEN_W + i % w < SCREEN_W * SCREEN_H)
                scr->pix[at + shadow[s] + (i / w) * SCREEN_W + i % w] = (uint8_t)(colour + 6);
    for (unsigned i = 0; i < n; i++)   /* 0x27371 */
        if (px[i] && at + (i / w) * SCREEN_W + i % w < SCREEN_W * SCREEN_H)
            scr->pix[at + (i / w) * SCREEN_W + i % w] = px[i];
}

/* The box over the screen at `at`, `bw` x `bh`, shaded as msgbox_open shades it (the original's four shadings and the tail). */
static void shade_box(screen_t *scr, const uint8_t table[256], uint8_t tail[4], unsigned at, unsigned bw, unsigned bh)
{
    uint8_t box[SCREEN_W * 46];
    for (unsigned r = 0; r < bh; r++)
        for (unsigned c = 0; c < bw; c++)
            box[r * bw + c] = at + r * SCREEN_W + c < SCREEN_W * SCREEN_H ? scr->pix[at + r * SCREEN_W + c] : 0;
    unsigned len = bw * bh;
    if (len > sizeof box - 4)
        memcpy(box + sizeof box - 4, tail, 4);
    for (int t = 0; t < 2; t++) {
        for (unsigned i = 0; i < len; i++)
            box[i] = table[box[i]];
        for (unsigned i = 0; i < 4; i++)
            tail[i] = table[tail[i]];
    }
    for (int t = 0; t < 2; t++)
        for (unsigned i = SCREEN_W; i < SCREEN_W * 45 && i < len; i++)
            box[i] = table[box[i]];
    for (unsigned r = 0; r < bh; r++)
        for (unsigned c = 0; c < bw; c++)
            if (at + r * SCREEN_W + c < SCREEN_W * SCREEN_H)
                scr->pix[at + r * SCREEN_W + c] = box[r * bw + c];
}

/* The next box of a translation, from m->more: up to three rows, placed as the original places one, two or three rows of text. */
static int translated_page(msgbox_t *m, screen_t *scr, int wait)
{
    l10n_line lines[3];
    unsigned all = l10n_wrap(m->more, SCREEN_W, L10N_FONT_NORMAL, lines, 3), n = all < 3 ? all : 3;
    if (n == 0)
        lines[0].start = m->more, lines[0].len = 0, n = 1;
    /* the original's three layouts (msgbox_open below): the first row and the box's height for one, two or three rows; one row is
     * centred, in 8-pixel columns, the box narrowed by twice the padding */
    static const unsigned first_row[3] = {0x87 + 15, 0x87 + 7, 0x87}, height[3] = {0x10, 0x1F, 0x2E};
    unsigned w0 = l10n_width(lines[0].start, lines[0].len, L10N_FONT_NORMAL);
    unsigned pad = n == 1 ? (SCREEN_W - w0) / 16 : 0, x = 8 * pad, row = first_row[n - 1];
    unsigned box_at = (row - 1) * SCREEN_W + x, box_w = SCREEN_W - 16 * pad, box_h = height[n - 1];
    if (wait)   /* the wait flag: row 134, 320 x 46 */
        box_at = 0xA780, box_w = SCREEN_W, box_h = 0x2E;
    shade_box(scr, m->table, m->tail, box_at, box_w, box_h);
    int ret = 0;
    for (unsigned l = 0; l < n; l++) {
        const char *s = lines[l].start, *end = s + lines[l].len;
        unsigned cx = l == 0 ? x : 0;
        while (s < end) {
            l10n_glyph g;
            if (l10n_glyph_of(l10n_next(&s), L10N_FONT_NORMAL, &g) != 0)
                ret = 1;
            message_char(scr, SCREEN_W * (row + 15 * l) + cx, g.bits, g.w, m->colour);
            cx += g.w;
        }
    }
    /* what is left after these rows */
    m->more = NULL;
    if (all > 3) {
        const char *rest = lines[2].start + lines[2].len;
        while (*rest == ' ' || (rest[0] == '\\' && rest[1] == 'n'))
            rest += *rest == ' ' ? 1 : 2;
        if (*rest && wait)
            m->more = rest;
    }
    m->pages++;
    return ret;
}

int msgbox_open(msgbox_t *m, screen_t *scr, const uint8_t *font8, const uint8_t table[256], uint8_t tail[4], unsigned msg,
                uint8_t colour, int wait)
{
    uint8_t text[0x78];
    if (msg == 0 || assets_read_at("MES_TXT.LCC", (size_t)(msg - 1) * 0x80 + 6, text, sizeof text) != 0)
        return -1;
    int ret = 0;
    unsigned n = 0;
    while (n < sizeof text && text[n] != '#')
        n++;
    m->more = NULL;
    m->pages = 0;
    if (getenv("TK_TRACE_MESSAGES"))   /* which message a box shows, for translating and play-testing (localization/README.md) */
        fprintf(stderr, "message %u\n", msg);
    const char *tr = l10n_message(msg);
    if (tr) {   /* the translated modes: the translation's first box (msgbox_round shows the others) */
        memcpy(m->under, scr->pix + 0xA780, sizeof m->under);
        m->table = table;
        m->tail = tail;
        memcpy(m->tail0, tail, 4);
        m->colour = colour;
        m->more = tr;
        ret = translated_page(m, scr, wait);
        m->last = n ? text[n - 1] : 0;   /* what the game reads from the box stays the original's */
        m->hi8 = (((msg - 1) * 0x80u + 6) >> 16) & 0xFF;
        m->count = colour + 3u;
        m->wait = m->mlatch = 1;
        return ret;
    }
    unsigned x = 0, row = 0x87, bw = SCREEN_W, bh = 0x2E;
    if (n < 0x28) {
        row += 15;
        bh = 0x10;
        x += 8 * ((0x28 - n) / 2);
        bw -= 16 * ((0x28 - n) / 2);
    } else if (n < 0x50) {
        row += 7;
        bh = 0x1F;
    }
    unsigned at = SCREEN_W * row + x - SCREEN_W;
    if (wait) {   /* the wait flag puts it at row 134, 320 x 46 whatever the text's length */
        at = 0xA780;
        bw = SCREEN_W;
        bh = 0x2E;
    }
    /* the box: 0x2723c takes the screen into the world buffer (after a 4-byte header), 0x27503 copies 0x3980 bytes of that 5 bytes on
     * whatever the box's size, and two shadings run over the copy's 0x3980 pixels, two more over its pixels 0x140 .. 0x383f; so the
     * copy's first 320 pixels are shaded twice, the rest four times, and a 320 x 46 box's last 4 pixels are the world buffer's bytes
     * 0x7305 .. 0x7308, which the first two shadings leave darkened where they are */
    memcpy(m->under, scr->pix + 0xA780, sizeof m->under);
    shade_box(scr, table, tail, at, bw, bh);
    for (unsigned i = 0; i < n; i++) {
        unsigned at = SCREEN_W * row + x;
        if (text[i] < 0x80) {
            message_char(scr, at, font8 + text[i] * 15, 8, colour);
            x += 8;
        } else {
            unsigned hi = text[i], lo = ++i < n ? text[i] : 0;
            uint8_t g[28];
            size_t off = battle_glyph_offset(hi, lo);
            if (off == (size_t)-1 || assets_read_at("NUMBER.LCZ", off, g, sizeof g) != 0) {
                ret = 1;
                memset(g, 0, sizeof g);
            }
            if (x == 0x138) {
                x = 0;
                row += 15;
            }
            message_char(scr, SCREEN_W * row + x, g, 16, colour);
            x += 16;
        }
        if (x == 0x140) {
            x = 0;
            row += 15;
        }
    }
    m->last = n ? text[n - 1] : 0;
    m->hi8 = (((msg - 1) * 0x80u + 6) >> 16) & 0xFF;
    m->count = colour + 3u;   /* [bp-0x16] after the last recolouring loop */
    m->wait = 1;
    m->mlatch = 1;
    return ret;
}

int msgbox_round(msgbox_t *m, screen_t *scr, unsigned port60, int mouse, unsigned buttons, void (*step)(void *ctx, screen_t *scr),
                 void *ctx)
{
    if (++m->count > 0x28) {
        m->count = 0;
        step(ctx, scr);
    }
    unsigned k = port60 & 0xFF;
    if (k > 0x7F)
        m->wait = 0;
    if (m->wait)
        k = 0x80;
    if (k >= 0x80) {   /* 0x1b5b5: the right button closes it, once all were up after it opened */
        if (!mouse)
            return 0;
        if (buttons == 0)
            m->mlatch = 0;
        if (m->mlatch || buttons != 2)
            return 0;
    }
    memcpy(scr->pix + 0xA780, m->under, sizeof m->under);   /* 0x27328(0xa780, ...) */
    if (m->more) {   /* a translation's next box in place of closing; the key or button must be let go again first */
        memcpy(m->tail, m->tail0, 4);
        translated_page(m, scr, 1);
        m->wait = m->mlatch = 1;
        return 0;
    }
    return 1;
}
