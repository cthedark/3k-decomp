/* WALK.SL's message box (0x1afb0 with the wait flag, and its wait loop 0x1b506), used by the battle's level-ups and by the world's
 * talk (docs/findings.md "WALK.SL step 16", "step 20"). */
#ifndef TK_MSGBOX_H
#define TK_MSGBOX_H

#include <stdint.h>

#include "screen.h"

typedef struct {
    uint8_t under[SCREEN_W * 46];   /* what the box covers (screen rows 134 .. 179) */
    unsigned count;                 /* [bp-0x16]: a palette step when it passes 0x28 */
    unsigned wait;                  /* [bp-0x18]: a key release must come first */
    unsigned mlatch;                /* [bp-0xe]: a mouse button must be up first */
    unsigned last;                  /* what 0x1afb0 returns: the text's last byte before '#' (0x7f: another message follows) */
    unsigned hi8;                   /* [bp-8]: the low byte of the text's file offset's high word (a talk's yes / no loop starts its
                                     * palette count with it, from the same stack bytes) */
} msgbox_t;

/* Message `msg` of MES_TXT.LCC (0x78 bytes at (msg - 1) * 0x80 + 6, up to '#') in `colour` over the darkened bottom of the screen;
 * `font8` is RPG.LCP 0x1a6b7 (128 one-bit 8 x 15 characters), `table` the shading table (RPG.LCP 0x340), `tail` the 4 bytes the box's
 * last 4 pixels come from (the world buffer's 0x7305 .. 0x7308; the box's two shadings run over them there, so they are left
 * darkened). With `wait` the box is the screen's rows 134 .. 179 and msgbox_round waits on it; without, it is only as big as the text
 * needs (320 x 16 narrowed by the centring, 320 x 31 or 320 x 46, one row above the text) and stays, the caller puts the screen back.
 * Returns 0, -1 when the text cannot be read (nothing drawn), 1 when
 * the text was drawn with a character the font does not have (drawn blank). */
int msgbox_open(msgbox_t *m, screen_t *scr, const uint8_t *font8, const uint8_t table[256], uint8_t tail[4], unsigned msg,
                uint8_t colour, int wait);

/* One round of the wait: `step` is the palette step (0x1c284 on the caller's palette) for every 41st round; the keyboard port `port60`;
 * with a mouse (`mouse` set) its buttons. Returns 1 when the box closes (what it covered is put back), else 0. */
int msgbox_round(msgbox_t *m, screen_t *scr, unsigned port60, int mouse, unsigned buttons, void (*step)(void *ctx, screen_t *scr),
                 void *ctx);

#endif
