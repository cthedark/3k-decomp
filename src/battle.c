/* WALK.SL's battle, stage 1: the hand-over from the world, the first screen and the idle passes. See battle.h. */
#include "battle.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"
#include "lcz.h"

enum {
    WIN_W = 192, WIN_H = 168,          /* the doubled world window */
    MON_W = 60, MON_H = 60,            /* a monster's picture */
    MON_PICS = 3,                      /* frames 0 .. 2; a slot's frame >= 3 means empty (9) */
    MON_STRIDE = 0xE14,                /* 4-byte header + 60 x 60 in the decoded group */
    SMALL = 30,                        /* the back row is drawn at half size */
    STRIP_AT = 0x1440,                 /* screen (64, 16) */
    FIELD_AT = 0x5CC0,                 /* screen (64, 74) */
    FACE_AT = 0x2483,                  /* screen (67, 29), then 0x26 per member */
    MARK_AT = 0x1A87,                  /* screen (71, 21): the mark on the member who acts */
    TAB_AT = 0x4501                    /* screen (65, 55): the first command tab, 0x30 apart */
};

struct battle {
    walk_t *w;
    uint8_t table[256];                /* RPG.LCP 0x340: the shading table (the world buffer's +0xfe78) */
    uint8_t font[150];                 /* RPG.LCP 0xd30: ten 3 x 5 digits (the battle buffer's +0xd44c) */
    uint8_t back[BATTLE_BACK_W * BATTLE_FIELD_H];   /* the backdrop ([bp-0x50] = world buffer + 0x5460) */
    uint8_t field[BATTLE_FIELD_W * BATTLE_FIELD_H]; /* [bp-0x90] + 4: what is copied to (64, 74) each pass */
    uint8_t pics[MON_PICS][MON_W * MON_H];
    uint8_t mon[BATTLE_MON_BYTES];     /* [bp-0x7c] */
    unsigned v44;                      /* the party offset of the member who acts (0xf0 while the monsters do) */
    unsigned v5e;                      /* 0x1e: the party's turn; below 0x14: the monsters' (4 per slot) */
    unsigned v28, v82;                 /* the command tab drawn last, and the one chosen */
    unsigned count, dir;               /* [bp-0x52], [bp-0x70]: the sway */
    unsigned v36;                      /* counts 2 down to 0 (at 1 it clears a 1 x 1 picture that is drawn every pass) */
    unsigned latch;                    /* [bp-0x38]: a key's release has been seen */
    char unbuilt[120];
    int unbuilt_new;
};

static unsigned w16(const uint8_t *b, size_t o)
{
    return b[o] | (unsigned)b[o + 1] << 8;
}

static void note(battle_t *b, const char *what)
{
    if (strcmp(b->unbuilt, what) != 0) {
        snprintf(b->unbuilt, sizeof b->unbuilt, "%s", what);
        fprintf(stderr, "battle: not built: %s\n", what);
    }
    b->unbuilt_new = 1;
}

const char *battle_unbuilt(const battle_t *b)
{
    return b->unbuilt[0] ? b->unbuilt : NULL;
}

/* ---- screen pieces (0x270e0, 0x27123, 0x27328) ---- */

static void fill(screen_t *scr, unsigned at, unsigned w, unsigned h, uint8_t c)
{
    for (unsigned r = 0; r < h; r++)
        for (unsigned x = 0; x < w; x++)
            if (at + r * SCREEN_W + x < SCREEN_W * SCREEN_H)
                scr->pix[at + r * SCREEN_W + x] = c;
}

static void outline(screen_t *scr, unsigned at, unsigned w, unsigned h, uint8_t c)
{
    fill(scr, at, w, 1, c);
    for (unsigned r = 1; r < h; r++) {
        fill(scr, at + r * SCREEN_W, 1, 1, c);
        fill(scr, at + r * SCREEN_W + w - 1, 1, 1, c);
    }
    fill(scr, at + (h - 1) * SCREEN_W, w, 1, c);
}

static void put(screen_t *scr, unsigned at, const uint8_t *px, unsigned w, unsigned h)
{
    for (unsigned r = 0; r < h; r++)
        for (unsigned x = 0; x < w; x++)
            if (at + r * SCREEN_W + x < SCREEN_W * SCREEN_H)
                scr->pix[at + r * SCREEN_W + x] = px[r * w + x];
}

/* ---- the pieces ---- */

void battle_stretch_row(const uint8_t *src, unsigned sw, uint8_t *dst, unsigned dw)
{
    unsigned step = (unsigned)((0xFFFFul * sw / dw) & 0xFFFF), acc = 1, si = 0;
    for (unsigned i = 0; i < dw; i++) {
        dst[i] = src[si];
        acc += step;
        if (acc > 0xFFFF) {
            acc &= 0xFFFF;
            si++;
        }
    }
}

void battle_shrink(const uint8_t *src, unsigned sw, unsigned sh, uint8_t *dst, unsigned dw, unsigned dh)
{
    unsigned cy = 0xFFFF / dh, rowskip = ((sh / dh) * sw) & 0xFFFF, yfrac = ((sh % dh) * cy) & 0xFFFF;
    unsigned cx = 0xFFFF / dw, xint = sw / dw, xfrac = ((sw % dw) * cx) & 0xFFFF;
    unsigned row = 0, xacc = 0, yacc = 0;
    for (unsigned r = 0; r < dh; r++) {
        unsigned si = row;
        for (unsigned i = 0; i < dw; i++) {
            *dst++ = src[si < sw * sh ? si : 0];
            si += xint;
            xacc += xfrac;
            if (xacc > 0xFFFF) {
                xacc &= 0xFFFF;
                si++;
            }
        }
        row += rowskip;
        yacc += yfrac;
        if (yacc > 0xFFFF) {
            yacc &= 0xFFFF;
            row += sw;
        }
    }
}

void battle_sway(const uint8_t *back, uint8_t *field, unsigned count, unsigned dir)
{
    unsigned ax = count, dl = dir;
    for (unsigned r = 0; r < BATTLE_FIELD_H; r++) {
        memcpy(field + r * BATTLE_FIELD_W, back + r * BATTLE_BACK_W + (ax & 3), BATTLE_FIELD_W);
        if (dl == 0) {
            if (--ax == 0)
                dl = 1;
        } else if (++ax == 3) {
            dl = 0;
        }
    }
}

void battle_number(unsigned value, const uint8_t font[150], uint8_t out[100], uint8_t colour)
{
    memset(out, 0, 100);
    unsigned div = 10000, x = 0;
    int shown = 0;
    while (div != 0) {
        unsigned d = (value / div) & 0xFFFF;
        value -= d * div;
        div /= 10;
        if (div == 0)
            shown = 1;
        if (d != 0 || shown) {
            const uint8_t *g = font + (d * 15 < 150 ? d * 15 : 0);
            for (int r = 0; r < 5; r++)
                for (int c = 0; c < 3; c++)
                    if (g[r * 3 + c] && x + c < 20)
                        out[r * 20 + x + c] = g[r * 3 + c];
            shown = 1;
        }
        x += 4;
    }
    for (int i = 0; i < 100; i++)   /* 0x2741b over the picture's 0x68 bytes: digit ink 0x10 takes the colour */
        if (out[i] == 0x10)
            out[i] = colour;
}

/* Decode the run-length block that starts at `p`; NULL on error. */
static uint8_t *unpack(const uint8_t *p, size_t avail, size_t *len)
{
    size_t pos = 0;
    lcz_chunk c;
    if (lcz_next_chunk(p, avail, &pos, &c) != LCZ_OK)
        return NULL;
    uint8_t *out = malloc(c.len ? c.len : 1);
    if (out)
        memcpy(out, c.pixels, c.len);
    *len = c.len;
    lcz_chunk_free(&c);
    return out;
}

static uint8_t *read_unpack(const char *name, size_t pos, size_t n, size_t *len, uint8_t **raw_out)
{
    uint8_t *raw = malloc(n ? n : 1);
    if (!raw || assets_read_at(name, pos, raw, n) != 0) {
        free(raw);
        fprintf(stderr, "battle: cannot read %zu bytes at %#zx of %s\n", n, pos, name);
        return NULL;
    }
    uint8_t *d = unpack(raw, n, len);
    if (!d)
        fprintf(stderr, "battle: bad run-length block at %#zx of %s\n", pos, name);
    if (raw_out && d)
        *raw_out = raw;
    else
        free(raw);
    return d;
}

/* ---- the hand-over (0x1296e .. 0x12e4e) ---- */

static void make_backdrop(battle_t *b, screen_t *scr)
{
    static uint8_t window[WIN_W * WIN_H], row[BATTLE_BACK_W + 6 * BATTLE_FIELD_H];
    walk_battle_view(b->w, b->table, window);
    /* 0x129b5: row k of the window stretched to 195 + 6k pixels, the middle 195 kept: the ground seems to lie back. Every 36 rows a
     * shadowed square of the progress row (screen (80 + 10 i, 50)). */
    unsigned sq = 0x50, c = 0;
    for (unsigned k = 0; k < BATTLE_FIELD_H; k++) {
        unsigned dw = BATTLE_BACK_W + 6 * k;
        battle_stretch_row(window + k * WIN_W, WIN_W, row, dw);
        memcpy(b->back + k * BATTLE_BACK_W, row + 3 * k, BATTLE_BACK_W);
        if (++c > 0x23) {
            c = 0;
            fill(scr, 0x4102 + sq, 6, 6, 0x1F);
            fill(scr, 0x3E80 + sq, 6, 6, 0x10);
            sq += 10;
        }
    }
    /* 0x2b8f6: in every second byte of the first 0x5284, the colours below 16 (the ones the palette cycle changes) are replaced */
    static const uint8_t fixed[16] = {0, 0x57, 0x59, 0x5A, 0xD2, 0xC7, 0x26, 0x2D, 0x2B, 0x26, 0x2B, 0x54, 0x53, 0x54, 0x55, 0x36};
    for (unsigned j = 0; j < 0x5284; j += 2)
        if (b->back[j] < 16)
            b->back[j] = fixed[b->back[j]];
    /* 0x12af5: shaded four times over shrinking ranges from the top, the far rows most */
    static const unsigned from[4] = {0, 1, 0, 0}, n[4] = {0x3E0F, 0x2E89, 0x1F06, 0xF83};
    for (int i = 0; i < 4; i++)
        for (unsigned j = from[i]; j < from[i] + n[i]; j++)
            b->back[j] = b->table[b->back[j]];
}

static int draw_strip(battle_t *b, screen_t *scr)
{
    size_t len;
    uint8_t *strip = read_unpack("RPG.LCP", 0x18691, 0x1247, &len, NULL);
    if (!strip)
        return -1;
    unsigned w = w16(strip, 0), h = w16(strip, 2);
    if (len < 4 + (size_t)w * h) {
        free(strip);
        fprintf(stderr, "battle: the party strip is too short\n");
        return -1;
    }
    put(scr, STRIP_AT, strip + 4, w, h);
    free(strip);

    const walk_scene_t *s = walk_scene(b->w);
    const uint8_t *party = s->tab_b;
    for (unsigned k = 0; k < 5; k++) {
        const uint8_t *m = party + WS_PARTY_SIZE * k;
        if (m[3] > 4)
            break;
        unsigned at = FACE_AT + 0x26 * k;
        fill(scr, at, 20, 20, 0);
        put(scr, at, s->bank2 + m[3] * 0xC00 + 0x400, 16, 16);
        if (m[8] == 0 && m[9] == 0)   /* no HP: the face darkened seven times */
            for (int t = 0; t < 7; t++)
                for (int r = 0; r < 16; r++)
                    for (int x = 0; x < 16; x++) {
                        uint8_t *p = scr->pix + at + r * SCREEN_W + x;
                        *p = b->table[*p];
                    }
        uint8_t num[100];
        battle_number(w16(m, 8), b->font, num, 0xF0);     /* HP */
        put(scr, at + 0x1400, num, 20, 5);
        battle_number(w16(m, 0xD), b->font, num, 0xB0);   /* MP */
        put(scr, at + 0x10, num, 20, 5);
    }
    return 0;
}

/* ---- the battle's set-up (0x1cbb6 .. 0x1d415) ---- */

static int load_group(battle_t *b, unsigned group)
{
    uint8_t ent[5];
    if (assets_read_at("TAB.TAB", 0x19F + 5 * group, ent, 5) != 0) {
        fprintf(stderr, "battle: TAB.TAB has no entry for monster group %u\n", group);
        return -1;
    }
    size_t pos = ((size_t)ent[2] << 16) + w16(ent, 0), n = w16(ent, 3), len;
    uint8_t *raw = NULL;
    uint8_t *d = n >= 10 ? read_unpack("MON.ALL", pos, n, &len, &raw) : NULL;
    if (!d || len < MON_PICS * MON_STRIDE) {
        free(d);
        free(raw);
        fprintf(stderr, "battle: monster group %u in MON.ALL is missing or too short\n", group);
        return -1;
    }
    for (int t = 0; t < MON_PICS; t++)   /* the headers are overwritten with 60 x 60 */
        memcpy(b->pics[t], d + t * MON_STRIDE + 4, MON_W * MON_H);
    memcpy(b->mon + 0x28, raw + n - 10, 10);   /* the group's last 10 bytes as read: its stats */
    free(d);
    free(raw);
    return 0;
}

battle_t *battle_begin(walk_t *w, screen_t *scr)
{
    battle_t *b = calloc(1, sizeof *b);
    if (!b)
        return NULL;
    b->w = w;
    if (assets_read_at("RPG.LCP", 0x340, b->table, sizeof b->table) != 0 ||
        assets_read_at("RPG.LCP", 0xD30, b->font, sizeof b->font) != 0) {
        fprintf(stderr, "battle: RPG.LCP is too short\n");
        free(b);
        return NULL;
    }
    unsigned member, group, boss;
    walk_battle_info(w, &member, &group, &boss);
    uint32_t *rng = walk_rng(w);

    make_backdrop(b, scr);
    if (draw_strip(b, scr) != 0) {
        free(b);
        return NULL;
    }

    /* 0x1ce03: each slot draws rand() / 0x199a (0 .. 4); above 2 it is empty (9); an enemy with byte 0x2f set brings frame 1 in every
     * slot. Again until one slot is filled. Then empty front slots (2 .. 4) take the first filled back one. */
    memset(b->mon, 0, sizeof b->mon);
    int none;
    do {
        none = 1;
        for (int sl = 0; sl < BATTLE_SLOTS; sl++) {
            unsigned t = walk_rand(rng) / 0x199A;
            if (t > 2)
                t = 9;
            if (boss)
                t = 1;
            b->mon[4 * sl + 2] = (uint8_t)t;
            if (t < 3)
                none = 0;
        }
    } while (none);
    for (int sl = 2; sl < BATTLE_SLOTS; sl++) {
        if (b->mon[4 * sl + 2] <= 2)
            continue;
        for (int j = 0; j < 2; j++)
            if (b->mon[4 * j + 2] < 3) {
                b->mon[4 * sl + 2] = b->mon[4 * j + 2];
                b->mon[4 * j + 2] = 9;
                break;
            }
    }
    static const unsigned at[BATTLE_SLOTS] = {0x21F6, 0x222C, 0x2101, 0x2142, 0x2183};   /* in the 192-wide field */
    for (int sl = 0; sl < BATTLE_SLOTS; sl++) {
        b->mon[4 * sl] = (uint8_t)at[sl];
        b->mon[4 * sl + 1] = (uint8_t)(at[sl] >> 8);
    }
    if (load_group(b, group) != 0) {
        free(b);
        return NULL;
    }
    for (int sl = 0; sl < BATTLE_SLOTS; sl++)   /* 0x1d238: each filled slot's HP from the first stat */
        if (b->mon[4 * sl + 2] < 3)
            b->mon[0x32 + sl] = b->mon[0x28];

    /* 0x1d2f8: who starts. Below 0x3e80 the monsters; otherwise the first member from the one who touched with HP and a status below 5
     * gets the mark (colour 0x1f of the 12 x 3 pixels at the top of his box turns 0x0f). */
    if (walk_rand(rng) < 0x3E80) {
        b->v5e = 0;
        b->v44 = 0xF0;
    } else {
        b->v5e = 0x1E;
        b->v44 = member;
        const uint8_t *party = walk_scene(w)->tab_b;
        int found = 0;
        for (int tries = 0; tries < 6 && !found; tries++) {
            const uint8_t *m = party + b->v44;
            if (w16(m, 8) != 0 && m[3] < 5) {
                unsigned p = (b->v44 / 0x28) * 0x26 + MARK_AT;
                for (int r = 0; r < 3; r++)
                    for (int x = 0; x < 12; x++)
                        if (scr->pix[p + r * SCREEN_W + x] == 0x1F)
                            scr->pix[p + r * SCREEN_W + x] = 0x0F;
                found = 1;
            } else {
                b->v44 += 0x28;
                if (b->v44 == 0xC8)
                    b->v44 = 0;
            }
        }
        if (!found)
            note(b, "nobody in the party can act (the original would loop for ever)");
    }
    b->v82 = 0;
    b->v28 = 1;
    b->count = 0;
    b->dir = 1;
    b->v36 = 2;
    b->latch = 0;
    return b;
}

void battle_free(battle_t *b)
{
    free(b);
}

/* ---- one pass ---- */

static void draw_tabs(screen_t *scr, unsigned sel)
{
    for (unsigned p = TAB_AT; p < 0x4593; p += 0x30) {
        outline(scr, p - 0x141, 0x30, 6, 0x95);
        outline(scr, p, 0x2E, 4, 0x94);
        fill(scr, p + 0x141, 0x2C, 2, 0x93);
    }
    unsigned q = TAB_AT + 0x30 * sel;
    outline(scr, q - 0x141, 0x30, 6, 0x92);
    outline(scr, q, 0x2E, 4, 0x90);
    fill(scr, q + 0x141, 0x2C, 2, 0x10);
}

static void blit_masked(uint8_t *field, unsigned at, const uint8_t *px, unsigned w, unsigned h)
{
    for (unsigned r = 0; r < h; r++)
        for (unsigned x = 0; x < w; x++) {
            unsigned d = at + r * BATTLE_FIELD_W + x;
            if (px[r * w + x] && d < sizeof ((battle_t *)0)->field)
                field[d] = px[r * w + x];
        }
}

/* 0x1fd06: the monsters, slot by slot. Each live one jitters a pixel (rand() below 0xbb8 left, above 0x7447 right), the two at the back at
 * half size and shaded twice, the front ones at full size; then a second rand() may change its frame by one (below 0x2ff down, above
 * 0x7d00 up, within 0 .. 2). */
static void draw_monsters(battle_t *b)
{
    uint32_t *rng = walk_rng(b->w);
    for (int sl = 0; sl < BATTLE_SLOTS; sl++) {
        uint8_t *m = b->mon + 4 * sl;
        unsigned t = m[2];
        if (t >= 3)
            continue;
        unsigned at = w16(m, 0), r = walk_rand(rng);
        if (r < 0xBB8)
            at = (at - 1) & 0xFFFF;
        if (r > 0x7447)
            at = (at + 1) & 0xFFFF;
        if (sl < 2) {
            uint8_t small[SMALL * SMALL];
            battle_shrink(b->pics[t], MON_W, MON_H, small, SMALL, SMALL);
            for (int i = 0; i < SMALL * SMALL; i++)
                small[i] = b->table[b->table[small[i]]];
            blit_masked(b->field, at, small, SMALL, SMALL);
        } else {
            if (m[3] == 0 || m[3] == 0x32)
                blit_masked(b->field, at, b->pics[t], MON_W, MON_H);
            if (m[3] != 0)
                note(b, "a monster's action state (slot byte 3)");
        }
        r = walk_rand(rng);
        if (r < 0x2FF && t > 0)
            t--;
        if (r > 0x7D00 && t < 2)
            t++;
        m[2] = (uint8_t)t;
    }
}

int battle_pass(battle_t *b, screen_t *scr, unsigned port60)
{
    b->unbuilt_new = 0;
    walk_palette_step(b->w, scr);
    if (b->v28 != b->v82) {
        draw_tabs(scr, b->v82);
        b->v28 = b->v82;
    }
    if (b->dir) {
        if (++b->count == 3)
            b->dir = 0;
    } else if (--b->count == 0) {
        b->dir = 1;
    }
    battle_sway(b->back, b->field, b->count, b->dir);
    /* 0x1d5de: the monsters' turn walks over the slots, 4 counts each, one count a pass while the sway moves down (dir set). An
     * empty slot is passed over (0x1dbf3); a live monster's action (from 0x1d641) is not built. */
    int empty_turn = 0;
    if (b->v5e < 0x14) {
        if (b->v5e % 4 != 0 && b->dir != 0)
            b->v5e++;
        if (b->v5e < 0x14 && b->v5e % 4 == 0) {
            if (b->mon[b->v5e + 2] < 3)
                note(b, "a monster's action (the monsters' turn)");
            else
                empty_turn = 1;
        }
    }
    draw_monsters(b);
    if (empty_turn)
        b->v5e++;
    if (b->v36 != 0)
        b->v36--;
    put(scr, FIELD_AT, b->field, BATTLE_FIELD_W, BATTLE_FIELD_H);
    unsigned k = port60 & 0xFF;
    if (k > 0x7F)
        b->latch = 1;
    else if (b->v44 < 0xC8 && b->latch) {
        b->latch = 0;
        if (k != 0)
            note(b, "the battle's keys (commands and targets)");
    }
    return b->unbuilt_new ? WALK_PASS_UNBUILT : 0;
}

void battle_state_dump(const battle_t *b, uint8_t out[BATTLE_STATE_BYTES])
{
    uint32_t r = *walk_rng(b->w);
    for (int i = 0; i < 4; i++)
        out[i] = (uint8_t)(r >> (8 * i));
    memcpy(out + 4, b->mon, BATTLE_MON_BYTES);
    out[4 + BATTLE_MON_BYTES] = (uint8_t)b->count;
    out[5 + BATTLE_MON_BYTES] = (uint8_t)b->dir;
    out[6 + BATTLE_MON_BYTES] = (uint8_t)b->v44;
    out[7 + BATTLE_MON_BYTES] = (uint8_t)b->v5e;
}
