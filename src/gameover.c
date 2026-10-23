#include "gameover.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"
#include "battle.h"
#include "lcz.h"

enum { W = SCREEN_W, H = SCREEN_H, TILE_W = 80, TILE_H = 50, WORDS_AT = 0x6D36 - 4 }   /* 0x11626 is given the picture with its 4-byte header: row 87, column 114 */;

struct gameover {
    uint8_t pic[W * H];        /* [bp-0x10]: the tiled screen that rolls */
    uint8_t *word;             /* the lettering's pixels */
    unsigned word_w, word_h;
};

gameover_t *gameover_begin(const screen_t *scr)
{
    gameover_t *g = calloc(1, sizeof *g);
    uint8_t table[256], raw[0x5B8] = {0};
    if (!g)
        return NULL;
    /* the original asks for 0x5b8 bytes at 0x3e73, one more than the file has: the block itself is shorter */
    if (assets_read_at("RPG.LCP", 0x340, table, sizeof table) != 0 || assets_read_at("TITLE1.LCZ", 0x3E73, raw, sizeof raw - 1) != 0) {
        fprintf(stderr, "gameover: RPG.LCP or TITLE1.LCZ is missing or too short\n");
        free(g);
        return NULL;
    }
    size_t pos = 0;
    lcz_chunk c;
    if (lcz_next_chunk(raw, sizeof raw, &pos, &c) != LCZ_OK || c.len < 4) {
        fprintf(stderr, "gameover: bad lettering block at 0x3e73 of TITLE1.LCZ\n");
        free(g);
        return NULL;
    }
    g->word_w = c.pixels[0] | (unsigned)c.pixels[1] << 8;
    g->word_h = c.pixels[2] | (unsigned)c.pixels[3] << 8;
    if (g->word_w > W || g->word_h > H || c.len < 4 + (size_t)g->word_w * g->word_h || !(g->word = malloc(c.len))) {
        fprintf(stderr, "gameover: the lettering is too short\n");
        lcz_chunk_free(&c);
        free(g);
        return NULL;
    }
    memcpy(g->word, c.pixels + 4, (size_t)g->word_w * g->word_h);
    lcz_chunk_free(&c);

    /* 0x102af .. 0x1031f: the screen four times through the table, then 0x11a76 to 80 x 50 and 0x115dc into each of the 4 x 4 places */
    memcpy(g->pic, scr->pix, sizeof g->pic);
    for (int t = 0; t < 4; t++)
        for (size_t i = 0; i < sizeof g->pic; i++)
            g->pic[i] = table[g->pic[i]];
    uint8_t tile[TILE_W * TILE_H];
    battle_shrink(g->pic, W, H, tile, TILE_W, TILE_H);
    for (unsigned ty = 0; ty < H; ty += TILE_H)
        for (unsigned tx = 0; tx < W; tx += TILE_W)
            for (unsigned r = 0; r < TILE_H; r++)
                memcpy(g->pic + (ty + r) * W + tx, tile + r * TILE_W, TILE_W);
    return g;
}

void gameover_pass(gameover_t *g, screen_t *scr)
{
    /* 0x1040a: row 0 to the bottom, the rest up one (0x11077); each row's first pixel to its end (0x12091) */
    uint8_t row[W];
    memcpy(row, g->pic, W);
    memmove(g->pic, g->pic + W, (size_t)W * (H - 1));
    memcpy(g->pic + (size_t)W * (H - 1), row, W);
    for (unsigned r = 0; r < H; r++) {
        uint8_t *p = g->pic + r * W, first = p[0];
        memmove(p, p + 1, W - 1);
        p[W - 1] = first;
    }
    /* the lettering at 0x6d36 (colour 0 transparent, 0x11626) for the copy to the screen only: the place under it is put back */
    memcpy(scr->pix, g->pic, sizeof g->pic);
    for (unsigned r = 0; r < g->word_h; r++)
        for (unsigned x = 0; x < g->word_w; x++) {
            uint8_t v = g->word[r * g->word_w + x];
            if (v && WORDS_AT + r * W + x < sizeof scr->pix)
                scr->pix[WORDS_AT + r * W + x] = v;
        }
}

int gameover_key(unsigned port60)
{
    port60 &= 0xFF;
    return port60 == 0x39 || port60 == 0x1C;
}

int gameover_mouse(unsigned buttons)
{
    return (buttons & 0xFF) != 0;   /* INT 33h function 3's BX, low byte */
}

void gameover_free(gameover_t *g)
{
    if (g)
        free(g->word);
    free(g);
}
