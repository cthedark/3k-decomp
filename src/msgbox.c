#include "msgbox.h"

#include <string.h>

#include "assets.h"
#include "battle.h"

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
    return 1;
}
