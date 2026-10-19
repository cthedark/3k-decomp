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
    TAB_AT = 0x4501,                   /* screen (65, 55): the first command tab, 0x30 apart */
    SHOUTS = 12,                       /* shout pictures read (the party's are 2 .. 6, the monsters' 7 .. 11) */
    BMEM_LEN = 0x43A2 - 0x4028,
    GRAB_AT = 0x4078 - 0x4028,         /* [bp-0x18] in bmem */
    COPY_AT = GRAB_AT + 0x194,         /* the face's copy for the hit animation */
    CLOSE_W = 120, CLOSE_H = 110,
    FX_N = 20
};

struct battle {
    walk_t *w;
    uint8_t table[256];                /* RPG.LCP 0x340: the shading table (the world buffer's +0xfe78) */
    uint8_t font[150];                 /* RPG.LCP 0xd30: ten 3 x 5 digits (the battle buffer's +0xd44c) */
    uint8_t font2[150];                /* RPG.LCP 0xc9a: the floating numbers' digits (+0xd3b6) */
    uint8_t sparks[5 * 64];            /* RPG.LCP 0xb5a: five 8 x 8 sparks (+0xd24e), the world's */
    uint8_t hits[5 * 400];             /* RPG.LCP 0x135e4 block 1: five 20 x 20 hit pictures (+0x3858) */
    uint8_t *atk;                      /* block 2 (+0x7be6): each member's attack picture, with a header */
    size_t atk_len;
    uint8_t masks[6 * MON_W * MON_H];  /* RPG.LCP 0x14891: six 60 x 60 masks a dying monster is cut away with (world buffer + 0) */
    uint8_t back[BATTLE_BACK_W * BATTLE_FIELD_H];   /* the backdrop ([bp-0x50] = world buffer + 0x5460) */
    uint8_t field[BATTLE_FIELD_W * BATTLE_FIELD_H]; /* [bp-0x90] + 4: what is copied to (64, 74) each pass */
    uint8_t pics[MON_PICS][MON_W * MON_H];
    /* the battle buffer from +0x4028 to +0x43a2: the monster table (0x3c), what the world's window left (+0x4064), the 12 x 3 / 20 x 20
     * grab of [bp-0x18] (+0x4078) and its copy (+0x420c). The hit animation's sixth frame reads from +0x4028, past the five pictures. */
    uint8_t bmem[BMEM_LEN];
    uint8_t *mon;                      /* [bp-0x7c] = bmem */
    uint8_t closeup[CLOSE_W * CLOSE_H];   /* [bp-0x24] (+0x43a2): a monster doubled, 120 x 110 */
    /* +0xd816: the 20 floating numbers' 12 x 5 pictures, 64 bytes apart with headers, and right after them (+0xdd16) RPG.LCP
     * 0x198d8: 136 x 14 one-bit pictures of the shouts, 0xff apart (a number's recolouring runs 40 bytes past its picture) */
    uint8_t fxmem[FX_N * 64 + SHOUTS * 0xFF];
    uint16_t fxpos[FX_N];              /* +0xd4e2: their positions (0 = none) and passes left */
    uint8_t fxcnt[FX_N];
    uint8_t shout[0x88 * 0x0E];        /* C + 0xf85e: the shout's picture, shout_w x shout_h */
    unsigned shout_w, shout_h;
    unsigned v44;                      /* the party offset of the member who acts (0xf0 while the monsters do) */
    unsigned v5e;                      /* 0x1e: the party's turn; below 0x14: the monsters' (4 per slot) */
    unsigned v28, v82;                 /* the command tab drawn last, and the one chosen */
    unsigned choosing;                 /* [bp-0x72]: a command was confirmed (with command 0, a target is being chosen) */
    unsigned count, dir;               /* [bp-0x52], [bp-0x70]: the sway */
    unsigned v36, v30;                 /* the shout: passes left (it is cleared at 1), its place in the field */
    unsigned latch;                    /* [bp-0x38]: a key's release has been seen (also reused, see the attack) */
    unsigned v84;                      /* what the monster routine returned last: 0 calm, 1 one finished dying, 2 busy */
    unsigned v42, v8c, v8a, v92, v32;  /* the party's attack: frame (5 = none), its pass counter, hit size, attack picture, hit place */
    unsigned atk_pic;                  /* offset in atk of the attacking member's picture */
    unsigned v10, v78;                 /* the monster's close-up: pass counter, frame */
    unsigned vc, v58, v54;             /* the hit face: frame (5 = none), pass counter, its screen place */
    unsigned v14;                      /* the field's shaking after a monster's blow */
    int over;                          /* won or lost: not built, the battle stops */
    char unbuilt[120];
    int unbuilt_new;
};

static unsigned w16(const uint8_t *b, size_t o)
{
    return b[o] | (unsigned)b[o + 1] << 8;
}

void battle_free(battle_t *b);

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

int battle_over(const battle_t *b)
{
    return b->over;
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
    b->mon = b->bmem;
    if (assets_read_at("RPG.LCP", 0x340, b->table, sizeof b->table) != 0 ||
        assets_read_at("RPG.LCP", 0xD30, b->font, sizeof b->font) != 0 ||
        assets_read_at("RPG.LCP", 0xC9A, b->font2, sizeof b->font2) != 0 ||
        assets_read_at("RPG.LCP", 0xB5A, b->sparks, sizeof b->sparks) != 0 ||
        assets_read_at("RPG.LCP", 0x198D8, b->fxmem + FX_N * 64, SHOUTS * 0xFF) != 0) {
        fprintf(stderr, "battle: RPG.LCP is too short\n");
        free(b);
        return NULL;
    }
    {   /* RPG.LCP 0x135e4: the hit pictures, then (at +0x25e) the members' attack pictures; 0x14891: the six dying masks */
        uint8_t *raw = NULL;
        size_t len = 0, mlen = 0;
        uint8_t *hits = read_unpack("RPG.LCP", 0x135E4, 0x12AD, &len, &raw);
        uint8_t *masks = read_unpack("RPG.LCP", 0x14891, 0x1BAA, &mlen, NULL);
        if (hits && raw)
            b->atk = unpack(raw + 0x25E, 0x12AD - 0x25E, &b->atk_len);
        int ok = hits && b->atk && masks && len >= sizeof b->hits && mlen >= sizeof b->masks;
        if (ok) {
            memcpy(b->hits, hits, sizeof b->hits);
            memcpy(b->masks, masks, sizeof b->masks);
        }
        free(hits);
        free(masks);
        free(raw);
        if (!ok) {
            fprintf(stderr, "battle: the battle's pictures in RPG.LCP are missing or too short\n");
            battle_free(b);
            return NULL;
        }
    }
    unsigned member, group, boss, rec;
    walk_battle_info(w, &member, &group, &boss, &rec);
    uint32_t *rng = walk_rng(w);

    make_backdrop(b, scr);
    if (draw_strip(b, scr) != 0) {
        battle_free(b);
        return NULL;
    }

    /* 0x1ce03: each slot draws rand() / 0x199a (0 .. 4); above 2 it is empty (9); an enemy with byte 0x2f set brings frame 1 in every
     * slot. Again until one slot is filled. Then empty front slots (2 .. 4) take the first filled back one. */
    memset(b->mon, 0, BATTLE_MON_BYTES);
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
        battle_free(b);
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
    /* [bp-0x38] is never set before the loop: it holds what the stack had there, the SI the world pushed for its copy at 0x199a8,
     * the touched record's offset in the villager table (low byte). Not 0 means a held key counts at once. */
    b->latch = rec & 0xFF;
    b->choosing = 0;
    b->v42 = 5;   /* 0x1d3e4, 0x1d3e8: no hit picture, no hit face running */
    b->vc = 5;
    b->v30 = 0;
    b->shout_w = b->shout_h = 1;
    return b;
}

void battle_free(battle_t *b)
{
    if (b)
        free(b->atk);
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

/* 0x26ad2 into the field: colour 0 transparent. `at` counts from the field's first pixel; pixels outside the field are dropped. */
static void blit_masked(uint8_t *field, unsigned at, const uint8_t *px, unsigned w, unsigned h)
{
    for (unsigned r = 0; r < h; r++)
        for (unsigned x = 0; x < w; x++) {
            unsigned d = (at + r * BATTLE_FIELD_W + x) & 0xFFFF;
            if (px[r * w + x] && d < BATTLE_FIELD_W * BATTLE_FIELD_H)
                field[d] = px[r * w + x];
        }
}

/* The same for the routines that pass the field picture's header as the target (the floating numbers, the shout): their place
 * counts from the header, 4 bytes before the first pixel. */
static void blit_hdr(uint8_t *field, unsigned at, const uint8_t *px, unsigned w, unsigned h)
{
    blit_masked(field, (at - 4) & 0xFFFF, px, w, h);
}

/* 0x271d3: a w x h rectangle's outline in a picture `pw` wide, from (x, y). */
static void box_outline(uint8_t *pic, unsigned pw, unsigned x, unsigned y, unsigned w, unsigned h, uint8_t c)
{
    uint8_t *p = pic + y * pw + x;
    memset(p, c, w);
    for (unsigned r = 1; r < h; r++)
        p[r * pw] = p[r * pw + w - 1] = c;
    memset(p + (h - 1) * pw, c, w);
}

/* 0x2723c + 0x2741b + 0x27328: the 12 x 3 pixels at the top of a member's box, colour `from` turned `to`. The grab goes through
 * [bp-0x18], as in the original. */
static void mark(battle_t *b, screen_t *scr, unsigned at, uint8_t from, uint8_t to)
{
    uint8_t *g = b->bmem + GRAB_AT;
    g[0] = 12, g[1] = 0, g[2] = 3, g[3] = 0;
    for (int r = 0; r < 3; r++)
        memcpy(g + 4 + 12 * r, scr->pix + at + r * SCREEN_W, 12);
    for (int i = 0; i < 0x28; i++)
        if (g[i] == from)
            g[i] = to;
    put(scr, at, g + 4, 12, 3);
}

static unsigned box_at(unsigned member)
{
    return (member / 0x28) * 0x26 + MARK_AT;
}

/* 0x1c47c, mode 1: a 12 x 5 number of three digits into `img` (with its header), then the ink recoloured over 0x68 bytes from the
 * header on, which runs into whatever follows (`room` bytes are there). */
static void number12(uint8_t *img, size_t room, unsigned value, const uint8_t font[150], uint8_t colour)
{
    img[0] = 12, img[1] = 0, img[2] = 5, img[3] = 0;
    memset(img + 4, 0, 60);
    unsigned div = 100, x = 0;
    int shown = 0;
    while (div != 0) {
        unsigned d = value / div;
        value -= d * div;
        div /= 10;
        if (div == 0)
            shown = 1;
        if (d != 0 || shown) {
            const uint8_t *g = font + (d < 10 ? d * 15 : 0);
            for (int r = 0; r < 5; r++)
                for (int c = 0; c < 3; c++)
                    if (g[r * 3 + c])
                        img[4 + r * 12 + x + c] = g[r * 3 + c];
            shown = 1;
        }
        x += 4;
    }
    for (size_t i = 0; i < 0x68 && i < room; i++)
        if (img[i] == 0x10)
            img[i] = colour;
}

/* 0x202e6: a floating number in the first free one of the 20 places: it rises a row a pass for `count` passes. At most 999. */
static void add_number(battle_t *b, unsigned at, unsigned value, unsigned count, uint8_t colour)
{
    if (value > 999)
        value = 999;
    for (int i = 0; i < FX_N; i++)
        if (b->fxpos[i] == 0) {
            b->fxcnt[i] = (uint8_t)count;
            b->fxpos[i] = (uint16_t)at;
            number12(b->fxmem + 64 * i, sizeof b->fxmem - 64 * (size_t)i, value, b->font2, colour);
            return;
        }
}

/* 0x203b4 + 0x25c58: a shout's one-bit 136 x 14 picture in `colour`; drawn every pass at v30 until v36 runs out. */
static void set_shout(battle_t *b, unsigned r, uint8_t colour, unsigned at, unsigned passes)
{
    const uint8_t *src = b->fxmem + FX_N * 64 + r * 0xFF;
    b->shout_w = 0x88;
    b->shout_h = 0x0E;
    for (int i = 0; i < 0xEE; i++)
        for (int k = 0; k < 8; k++)
            b->shout[8 * i + k] = (src[i] << k) & 0x80 ? colour : 0;
    b->v30 = at;
    b->v36 = passes;
}

/* 0x26de8: a picture stretched to dw x dh: 16-bit steps 0xffff * size / new size; across, each row starts at 1; down, at 0. */
static void stretch(const uint8_t *src, unsigned sw, unsigned sh, uint8_t *dst, unsigned dw, unsigned dh)
{
    unsigned xs = (unsigned)((0xFFFFul * sw / dw) & 0xFFFF), ys = (unsigned)((0xFFFFul * sh / dh) & 0xFFFF);
    unsigned row = 0, yacc = 0;
    for (unsigned r = 0; r < dh; r++) {
        unsigned si = row, acc = 1;
        for (unsigned i = 0; i < dw; i++) {
            *dst++ = src[si < sw * sh ? si : 0];
            acc += xs;
            if (acc > 0xFFFF) {
                acc &= 0xFFFF;
                si++;
            }
        }
        yacc += ys;
        if (yacc > 0xFFFF) {
            yacc &= 0xFFFF;
            row += sw;
        }
    }
}

/* 0x25cdc: a monster's picture doubled for the close-up, 0x37 source rows: p q / below p. */
static void double_up(const uint8_t *pic, uint8_t *out)
{
    for (int r = 0; r < CLOSE_H / 2; r++)
        for (int c = 0; c < MON_W; c++) {
            const uint8_t *s = pic + r * MON_W + c;
            uint8_t *d = out + (2 * r) * CLOSE_W + 2 * c;
            d[0] = s[0];
            d[1] = s[1];
            d[CLOSE_W] = s[MON_W];
            d[CLOSE_W + 1] = s[0];
        }
}

/* 0x1fd06: the monsters, slot by slot. Each live one jitters a pixel (rand() below 0xbb8 left, above 0x7447 right), the two at the back at
 * half size and shaded twice, the front ones at full size, in the state byte 3 says: 0 or 0x32 plain, 1 .. 0x14 a silhouette of colour
 * state + 0xf (a hit flashing; at its end a monster with no HP starts dying, 0x3d), 0x3d .. 0x53 cut away by the six masks (at 0x54 the
 * slot is dead, frame 5), 0x96 the chosen target; any state but 0 and 0x96 adds falling sparks. Then a second rand() may change the
 * frame by one (below 0x2ff down, above 0x7d00 up, within 0 .. 2). Returns 0, 1 when a monster finished dying, 2 while one is busy. */
static unsigned draw_monsters(battle_t *b)
{
    uint32_t *rng = walk_rng(b->w);
    unsigned ret = 0;
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
            unsigned f = m[3];
            if (f == 0 || f == 0x32)
                blit_masked(b->field, at, b->pics[t], MON_W, MON_H);
            if (f != 0 && f < 0x15) {
                for (int y = 0; y < MON_H; y++)
                    for (int x = 0; x < MON_W; x++) {
                        unsigned d = (at + y * BATTLE_FIELD_W + x) & 0xFFFF;
                        if (b->pics[t][y * MON_W + x] && d < sizeof b->field)
                            b->field[d] = (uint8_t)(f + 0xF);
                    }
                f--;
                m[3] = (uint8_t)f;
                ret = 2;
                if (f == 0) {
                    if (b->mon[0x32 + sl] == 0) {   /* no HP left: it dies, with sparks from just under its middle */
                        m[3] = 0x3D;
                        b->mon[0x14 + 4 * sl] = (uint8_t)(at + 0x169E);
                        b->mon[0x15 + 4 * sl] = (uint8_t)((at + 0x169E) >> 8);
                        b->mon[0x16 + 4 * sl] = 0;
                    }
                    f = 0;
                }
            }
            if (f > 0x3C && f < 0x55) {
                uint8_t cut[MON_W * MON_H];
                const uint8_t *mask = b->masks + ((f - 0x3C) >> 2) * MON_W * MON_H;
                for (int i = 0; i < MON_W * MON_H; i++)
                    cut[i] = b->pics[t][i] & mask[i];
                blit_masked(b->field, at, cut, MON_W, MON_H);
                if (++f == 0x54) {
                    f = 0;
                    t = 5;
                    ret = 1;
                }
                m[3] = (uint8_t)f;
            }
            if (f == 0x96) {   /* 0x20066: the chosen target, in a box of three outlines (0xf4, 0xff, 0xf8) */
                uint8_t boxed[MON_W * MON_H];
                memcpy(boxed, b->pics[t], sizeof boxed);
                static const uint8_t colour[3] = {0xF4, 0xFF, 0xF8};
                for (int i = 0; i < 3; i++)
                    box_outline(boxed, MON_W, i, i, MON_W - 2 * i, MON_H - 2 * i, colour[i]);
                blit_masked(b->field, at, boxed, MON_W, MON_H);
            }
            if (f != 0 && f != 0x96) {   /* 0x2010c: the sparks, frames 0 .. 4 two rows apart every third pass, then a new place */
                uint8_t *e = b->mon + 0x14 + 4 * sl;
                unsigned pos = w16(e, 0), fr = e[2];
                if (ret == 0)
                    ret = 2;
                blit_masked(b->field, pos, b->sparks + (fr << 6) % sizeof b->sparks, 8, 8);
                if (++e[3] > 2) {
                    pos = (pos + 0x180) & 0xFFFF;
                    e[0] = (uint8_t)pos;
                    e[1] = (uint8_t)(pos >> 8);
                    fr++;
                    e[3] = 0;
                    if (fr > 4) {
                        fr = 0;
                        unsigned p2 = at + (walk_rand(rng) / 0x2D8) * 0xC0;
                        p2 += walk_rand(rng) / 0x28F;
                        e[0] = (uint8_t)p2;
                        e[1] = (uint8_t)(p2 >> 8);
                    }
                }
                e[2] = (uint8_t)fr;
            }
        }
        r = walk_rand(rng);
        if (r < 0x2FF && t > 0)
            t--;
        if (r > 0x7D00 && t < 2)
            t++;
        m[2] = (uint8_t)t;
    }
    return ret;
}

/* The party's blow: experience / 1200, plus (byte 0x13 / 5) * 4 (half for the last two members), at least 1; less (blow / t) when
 * t = (2000 - word 0x10) / 128 + 2 is below 17. */
unsigned battle_blow(unsigned exp, unsigned stat13, unsigned word10, int back)
{
    unsigned d = (exp / 0x4B0) & 0xFF;
    unsigned s = ((stat13 / 5) * 4) & 0xFF;
    if (back)
        s >>= 1;
    d = (d + s) & 0xFF;
    if (d == 0)
        d = 1;
    unsigned t = ((((0x7D0 - word10) & 0xFFFF) >> 7) + 2) & 0xFF;
    if (t < 0x11 && t != 0)
        d = (d - d / t) & 0xFF;
    return d;
}

/* A monster's blow: its attack word, halved from the back row, less the member's defence byte, not below 0. */
unsigned battle_monster_blow(unsigned atk, int back, unsigned def)
{
    if (back)
        atk >>= 1;
    return atk > def ? atk - def : 0;
}

/* 0x1dfc1: the front slot (2, 3, 4) under a hit picture's column (0 .. 0xbf). */
unsigned battle_hit_slot(unsigned col)
{
    unsigned k = 2;
    if (col > 0x1E)
        k = 3;
    if (col > 0x60)
        k = 4;
    return k;
}

static uint8_t *party_of(battle_t *b)
{
    return (uint8_t *)walk_scene(b->w)->tab_b;
}

/* 0x1ec8f .. 0x1f18d: the member who acts hits the chosen target. All of it is worked out at once; the pictures follow over the next
 * passes (the hit animation, the target's flashing, its dying). */
static void party_attack(battle_t *b, screen_t *scr)
{
    uint8_t *party = party_of(b);
    unsigned m = b->v44;
    mark(b, scr, box_at(m), 0x0F, 0x1F);
    b->v42 = 0;
    unsigned i = 8, front = 0;
    while (i < 0x14 && b->mon[i + 3] != 0x96) {
        front++;
        i += 4;
    }
    b->mon[i + 3] = 0;
    i = (front * 4 + 8) & 0xFF;
    b->mon[i + 3] = 0x32;
    unsigned n = party[m + 3];
    static const unsigned pic_at[5] = {0x7BE6, 0x80FE, 0x859C, 0x8B13, 0x8FBB};   /* written at +0x93be by the set-up */
    b->atk_pic = n < 5 ? pic_at[n] - 0x7BE6 : 0;
    unsigned ph = b->atk_pic + 4 <= b->atk_len ? w16(b->atk, b->atk_pic + 2) : 0;
    b->v92 = (front * 0x3E + ((0x6E - ph) * 0xC0 + 6)) & 0xFFFF;
    b->v32 = (front * 0x3E + 0x3A9F) & 0xFFFF;
    unsigned hp = b->mon[0x34 + front];
    unsigned d = battle_blow(w16(party, m + 4), party[m + 0x13], w16(party, m + 0x10), m > 0x50);
    if (d >= hp) {   /* killed: the monsters' experience stat goes to the member, at most 60,000 */
        hp = 0;
        unsigned x = (w16(party, m + 4) + b->mon[0x2C]) & 0xFFFF;
        if (x > 0xEA60)
            x = 0xEA60;
        party[m + 4] = (uint8_t)x;
        party[m + 5] = (uint8_t)(x >> 8);
    } else {
        hp -= d;
    }
    b->mon[0x34 + front] = (uint8_t)hp;
    b->latch = hp;   /* [bp-0x38] holds the HP here, and stays the key latch */
    add_number(b, b->v32, hp, 0x41, 0x23);
    add_number(b, (b->v32 + 0x254) & 0xFFFF, d, 0x32, 0xF0);
    b->choosing = 0;
    b->v8a = d / 7 + 0x14;
    b->v32 = (b->v32 - (b->v8a >> 1) * 0xC1) & 0xFFFF;
    /* the next member who can act gets the mark; none left: the monsters' turn comes (F) */
    unsigned at = box_at(b->v44) + 0x26;
    b->v44 += 0x28;
    while (b->v44 < 0xC8) {
        if (w16(party, b->v44 + 8) != 0 && party[b->v44 + 3] < 5) {
            mark(b, scr, at, 0x1F, 0x0F);
            break;
        }
        at += 0x26;
        b->v44 += 0x28;
    }
    unsigned r = walk_rand(walk_rng(b->w)) / 0x199A + 2;
    set_shout(b, r, 0xF0, 0x79C, 0x2D);
}

/* 0x1d70f .. 0x1dbf0: a live monster's turn, after its close-up has shown its three frames four passes each: the field flashes, a member
 * is hit (the back row half as hard), the strip shows his HP, the floating numbers show the blow and what his defence learnt. */
static void monster_blow(battle_t *b, screen_t *scr)
{
    uint8_t *party = party_of(b);
    uint32_t *rng = walk_rng(b->w);
    memset(b->field, 0x28, sizeof b->field);
    unsigned r;
    if (b->v5e < 8 || b->v5e == 0xC) {   /* one of the first three */
        do
            r = walk_rand(rng) / 0x2AAB;
        while (party[0x28 * r + 3] >= 0xA);
    } else {
        unsigned tries = 0;
        do {
            r = walk_rand(rng) / 0x4000;
            if (b->v5e == 0x10 && r != 0)
                r = 2;
            if (++tries > 0xA)
                r = 0;
        } while (party[0x28 * r + 3] >= 0xA);
    }
    if (walk_rand(rng) < 0x666 && b->v5e > 4 && party[0x7B] < 0xA)   /* now and then one of the last two */
        do
            r = walk_rand(rng) / 0x4000 + 3;
        while (party[0x28 * r + 3] >= 0xA);
    b->v54 = 0x26 * r + FACE_AT;
    unsigned m = 0x28 * r;
    b->v14 = 7;
    unsigned dmg = battle_monster_blow(w16(b->mon, 0x29), b->v5e < 8, party[m + 0x12]);
    unsigned hp = w16(party, m + 8);
    int dead = 0;
    if (hp != 0) {
        if (hp <= dmg) {
            hp = 0;
            dead = 1;
        } else {
            hp -= dmg;
        }
    }
    uint8_t num[100];
    battle_number(hp, b->font, num, 0xF0);
    put(scr, b->v54 + 0x1400, num, 20, 5);
    uint8_t *g = b->bmem + GRAB_AT;   /* his face, for the hit animation (darkened seven times when he falls) */
    g[0] = 20, g[1] = 0, g[2] = 20, g[3] = 0;
    for (int y = 0; y < 20; y++)
        memcpy(g + 4 + 20 * y, scr->pix + b->v54 + y * SCREEN_W, 20);
    if (dead)
        for (int k = 0; k < 7; k++)
            for (int j = 0; j < 400; j++)
                g[4 + j] = b->table[g[4 + j]];
    party[m + 8] = (uint8_t)hp;
    party[m + 9] = (uint8_t)(hp >> 8);
    unsigned at = (w16(b->mon, b->v5e) + 0x1E1E) & 0xFFFF;
    add_number(b, at, dmg, 0x50, 0x78);
    unsigned gain = b->mon[0x2D];
    if (b->v5e < 8)
        gain >>= 1;
    unsigned dx = w16(party, m + 0x10) + gain;
    if (dx > 0x7D0)
        dx = 0x7D0;
    party[m + 0x10] = (uint8_t)dx;
    party[m + 0x11] = (uint8_t)(dx >> 8);
    if (gain != 0) {
        memset(b->field, 0x30, sizeof b->field);
        add_number(b, (at + 0x606) & 0xFFFF, gain, 0x57, 0x69);
    }
    b->v5e++;
    b->vc = 0;
    unsigned o = 3;
    while (o < 0xC8 && !(w16(party, o + 5) != 0 && party[o] < 0xA))
        o += 0x28;
    if (o == 0xCB) {
        b->over = 2;
        note(b, "the party has lost (the end of a battle)");
        return;
    }
    r = walk_rand(rng) / 0x199A + 7;
    set_shout(b, r, 0xFF, 0x475C, 0x2E);
}

/* The front index under the hit picture: 0x1dfc1 keeps the column of v32 and picks slot 2, 3 or 4 by it. */
static void hit_lands(battle_t *b)
{
    b->v32 %= 0xC0;
    unsigned k = battle_hit_slot(b->v32);
    if (b->mon[k * 4 + 3] == 0x32)
        b->mon[k * 4 + 3] = 0x14;
}

/* The front slots' byte 3 (state): 0x96 marks the chosen target. Slots 2 .. 4 are table offsets 8, 12, 16. */
static unsigned target_slot(const battle_t *b)
{
    unsigned i = 8;
    while (i < 0x14 && b->mon[i + 3] != 0x96)
        i += 4;
    return i;
}

static int can_target(const battle_t *b, unsigned i)
{
    return b->mon[i + 2] < 3 && b->mon[i + 3] == 0;
}

/* 0x1e81c .. 0x1f1c7: a key in the party's turn (the latch is then cleared, whatever the key). */
static void keys(battle_t *b, screen_t *scr, unsigned k)
{
    b->latch = 0;
    if (k == 2 || k == 3)   /* music and sound on / off, with the right-hand button lit (0x1c3ba); no FM card or Sound Blaster in the
                             * oracle, where the key then does nothing */
        note(b, "keys 2 and 3 in the battle (music and sound on or off)");
    if (!b->choosing) {
        if (k == 0x4B) {          /* Left: the tab before, round from the first to the last */
            if (b->v82 == 0)
                b->v82 = 4;
            b->v82--;
        }
        if (k == 0x4D && ++b->v82 > 3)   /* Right */
            b->v82 = 0;
        if (k == 0x39 || k == 0x1C) {    /* Space or Enter: the command; attack needs a target, the first live front monster */
            b->choosing = 1;
            k = 0;
            if (b->v82 == 0) {
                unsigned i = 8;
                while (i < 0x14 && !can_target(b, i))
                    i += 4;
                if (i < 0x14)
                    b->mon[i + 3] = 0x96;
                else
                    b->choosing = 0;
            }
        }
    }
    if (!b->choosing)
        return;
    if (b->v82 != 0) {
        note(b, "the magic, item and fourth commands");
        return;
    }
    if (k == 0x4B) {   /* 0x1eabf: the target moves to the nearest live monster on the left, if any */
        unsigned i = target_slot(b);
        if (i > 8) {
            b->mon[i + 3] = 0;
            unsigned j = i - 4;
            while (j > 7 && !can_target(b, j))
                j -= 4;
            b->mon[(j > 7 ? j : i) + 3] = 0x96;
        }
    }
    if (k == 0x4D) {   /* 0x1eba0: the same to the right */
        unsigned i = target_slot(b);
        if (i < 0x10) {
            b->mon[i + 3] = 0;
            unsigned j = i + 4;
            while (j < 0x11 && !can_target(b, j))
                j += 4;
            b->mon[(j < 0x11 ? j : i) + 3] = 0x96;
        }
    }
    if (k == 0x39 || k == 0x1C) {
        party_attack(b, scr);
        return;
    }
    if (k == 1) {   /* Esc: back to the commands */
        b->choosing = 0;
        for (unsigned i = 8; i < 0x14; i += 4)
            b->mon[i + 3] = 0;
    }
}

int battle_pass(battle_t *b, screen_t *scr, unsigned port60)
{
    b->unbuilt_new = 0;
    if (b->over)   /* won or lost: what follows is not built, the battle stands still */
        return WALK_PASS_UNBUILT;
    uint8_t *party = party_of(b);
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
    /* 0x1d5de: the monsters' turn walks over the slots, 4 counts each, one count a pass while the sway moves down (dir set). At a slot's
     * first count a live monster is taken out of the row (frame 4) and shown doubled instead. */
    if (b->v5e < 0x14 && b->v5e % 4 != 0 && b->dir != 0)
        b->v5e++;
    unsigned v5a = b->v5e % 4, t = 9;
    if (b->v5e < 0x14 && v5a == 0) {
        t = b->mon[b->v5e + 2];
        if (t < 3) {
            b->mon[b->v5e + 2] = 4;
            double_up(b->pics[b->v78 < MON_PICS ? b->v78 : 0], b->closeup);
        }
    }
    b->v84 = draw_monsters(b);
    if (b->v5e < 0x14 && v5a == 0) {
        if (t >= 3) {
            b->v5e++;   /* 0x1dbf3: an empty slot is passed over */
        } else {
            blit_masked(b->field, 0x24, b->closeup, CLOSE_W, CLOSE_H);
            b->mon[b->v5e + 2] = 1;
            if (++b->v10 > 3) {
                b->v10 = 0;
                if (++b->v78 > 2) {
                    b->v78 = 0;
                    monster_blow(b, scr);
                    if (b->over)
                        return WALK_PASS_UNBUILT;
                }
            }
        }
    }
    if (b->v84 == 1) {   /* 0x1dbf7: a monster has just died */
        int left = 0;
        for (int i = 0x32; i < 0x37; i++)
            if (b->mon[i])
                left = 1;
        if (!left) {
            b->over = 1;
            note(b, "the party has won (the end of a battle)");
            return WALK_PASS_UNBUILT;
        }
        unsigned fh = 0x34;   /* an empty front slot takes the first live one from the back, with full HP */
        for (unsigned o = 0xA; o < 0x14; o += 4, fh++) {
            if (b->mon[o] <= 2)
                continue;
            unsigned bh = 0x32, j = 2;
            while (j < 7 && b->mon[j] >= 3) {
                bh++;
                j += 4;
            }
            if (j < 7) {
                b->mon[fh] = b->mon[0x28];
                b->mon[bh] = 0;
                b->mon[o] = 1;
                b->mon[j] = 4;
            }
        }
    }
    if (b->vc < 5) {   /* 0x1dd19: the hit member's face, with the five hit pictures over it, two passes each */
        uint8_t *g = b->bmem + GRAB_AT, *c = b->bmem + COPY_AT;
        memcpy(c, g, 404);
        for (int j = 0; j < 400; j++)
            if (b->hits[b->vc * 400 + j])
                c[4 + j] = b->hits[b->vc * 400 + j];
        put(scr, b->v54, c + 4, 20, 20);
        if (++b->v58 > 1) {
            b->v58 = 0;
            if (++b->vc == 5)
                put(scr, b->v54, g + 4, 20, 20);
        }
    }
    if (b->v84 == 0) {   /* 0x1ddc8: whose turn */
        if (b->v44 == 0xC8) {
            b->v5e = 0;
            b->v44 = 0xF0;
        }
        if (b->v5e == 0x14 && b->vc == 5) {
            b->v44 = 0;
            b->v5e = 0x1E;
            for (int tries = 0; tries < 6; tries++) {
                if (w16(party, b->v44 + 8) != 0) {
                    mark(b, scr, box_at(b->v44), 0x1F, 0x0F);
                    break;
                }
                b->v44 += 0x28;
                if (b->v44 > 0xA0)
                    b->v44 = 0;
            }
        }
    }
    if (b->v42 < 5) {   /* 0x1deb0: the party's hit: the hit pictures grown to v8a, the member's attack picture under them */
        const uint8_t *src = b->hits + b->v42 * 400;
        if (++b->v8c > 2) {
            b->v8c = 0;
            b->v42++;
        }
        static uint8_t big[64 * 64];
        unsigned sz = b->v8a < 64 ? b->v8a : 64;
        stretch(src, 20, 20, big, sz, sz);
        blit_masked(b->field, b->v32, big, sz, sz);
        if (b->atk_pic + 4 <= b->atk_len) {
            unsigned aw = w16(b->atk, b->atk_pic), ah = w16(b->atk, b->atk_pic + 2);
            if (b->atk_pic + 4 + (size_t)aw * ah <= b->atk_len)
                blit_masked(b->field, b->v92, b->atk + b->atk_pic + 4, aw, ah);
        }
        if (b->v42 == 5)
            hit_lands(b);
    }
    if (b->v14 != 0) {   /* 0x1e2a0: after a monster's blow the field jumps up 0 .. 19 rows, the bottom rows black */
        b->v14--;
        unsigned r = walk_rand(walk_rng(b->w)) / 0x667;
        if (r != 0 && r < BATTLE_FIELD_H) {
            memmove(b->field, b->field + r * BATTLE_FIELD_W, (BATTLE_FIELD_H - r) * BATTLE_FIELD_W);
            memset(b->field + (BATTLE_FIELD_H - r) * BATTLE_FIELD_W, 0, r * BATTLE_FIELD_W);
        }
    }
    for (int i = 0; i < FX_N; i++) {   /* 0x1e300: the floating numbers */
        if (b->fxpos[i] == 0)
            continue;
        blit_hdr(b->field, b->fxpos[i], b->fxmem + 64 * i + 4, 12, 5);
        b->fxpos[i] = (uint16_t)(b->fxpos[i] - 0xC0);
        if (--b->fxcnt[i] == 0)
            b->fxpos[i] = 0;
    }
    if (b->v36 != 0 && --b->v36 == 1) {   /* the shout is cleared to a 1 x 1 picture */
        b->shout_w = b->shout_h = 1;
        b->shout[0] = 0;
    }
    blit_hdr(b->field, b->v30, b->shout, b->shout_w, b->shout_h);
    put(scr, FIELD_AT, b->field, BATTLE_FIELD_W, BATTLE_FIELD_H);
    unsigned k = port60 & 0xFF;
    if (k > 0x7F)
        b->latch = 1;
    else if (b->v44 < 0xC8 && b->latch && b->v84 == 0)   /* 0x1e7e3: the party's turn, a release seen, nothing moving */
        keys(b, scr, k);
    return b->unbuilt_new ? WALK_PASS_UNBUILT : 0;
}

void battle_state_dump(const battle_t *b, uint8_t out[BATTLE_STATE_BYTES])
{
    uint32_t r = *walk_rng(b->w);
    for (int i = 0; i < 4; i++)
        out[i] = (uint8_t)(r >> (8 * i));
    memcpy(out + 4, b->mon, BATTLE_MON_BYTES);
    uint8_t *o = out + 4 + BATTLE_MON_BYTES;
    o[0] = (uint8_t)b->count;
    o[1] = (uint8_t)b->dir;
    o[2] = (uint8_t)b->v44;
    o[3] = (uint8_t)b->v5e;
    o[4] = (uint8_t)b->v82;
    o[5] = (uint8_t)b->choosing;
    o[6] = (uint8_t)b->latch;
    o[7] = (uint8_t)b->v42;
    o[8] = (uint8_t)b->v8c;
    o[9] = (uint8_t)b->vc;
    o[10] = (uint8_t)b->v58;
    o[11] = (uint8_t)b->v14;
    o[12] = (uint8_t)b->v84;
    o[13] = (uint8_t)b->v36;
    o[14] = (uint8_t)b->v10;
    o[15] = (uint8_t)b->v78;
    memcpy(o + 16, walk_scene(b->w)->tab_b, 200);
    for (int i = 0; i < FX_N; i++) {
        o[216 + 3 * i] = (uint8_t)b->fxpos[i];
        o[217 + 3 * i] = (uint8_t)(b->fxpos[i] >> 8);
        o[218 + 3 * i] = b->fxcnt[i];
    }
}
