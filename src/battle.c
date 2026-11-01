/* WALK.SL's battle, stage 1: the hand-over from the world, the first screen and the idle passes. See battle.h. */
#include "battle.h"
#include "msgbox.h"
#include "sfx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"
#include "l10n.h"
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
    int over;                          /* 0 while fighting; BATTLE_LOST, or BATTLE_BACK once the world takes over */
    /* what follows a win (0x1f900 .. 0x1fd01, then the world's 0x12e56 .. 0x132f1) */
    int phase;                         /* BATTLE_KIND_FIGHT, _REWARDS or _DIALOG */
    unsigned kills;                    /* [bp-0x66]: what the battle returns, the slots whose monster died */
    uint8_t glyphs[12 * 169];          /* NUMBER.LCZ 0x359b: 13 x 13 pictures of x, = and the digits 0 .. 9 ([bp-0x4c]) */
    uint8_t bar[BATTLE_FIELD_W * 13];  /* [bp-0x24]: "kills x per monster = coins", drawn over the field */
    unsigned bar_at, bar_step;         /* [bp-0x56] (from the field picture's header), [bp-0x64] */
    unsigned lu;                       /* the next party member whose level is looked at */
    uint8_t font8[128 * 15];           /* RPG.LCP 0x1a6b7: the message box's 8 x 15 one-bit ASCII characters (font buffer + 0x223e) */
    msgbox_t box;                      /* a level-up's message box (msgbox.h) */
    /* command 1, the magic list (0x21486), and the member picker it opens (0x1a8a4) */
    uint8_t mg_bar[0x8A8];             /* RPG.LCP 0xe95 decoded: the 96 x 15 row with its header, then the icons, 48 wide */
    uint8_t mg_tab[100];               /* the field + 0x7d0: per spell MAGICT.LCC bytes 0xc .. 0x13 (level, heal, defence, revive, result) */
    unsigned mg_cur, mg_prev, mg_idx;  /* [bp-0xa], [bp-0x10]: the highlighted row's screen place; [bp-0x22]: 10 x the spell */
    unsigned mg_latch, mg_count;       /* [bp-0x1e]; [bp-0x1c] (a palette step when it passes 0x28) */
    unsigned pk_sel, pk_prev, pk_m;    /* the picker's box [bp-0x14], the box drawn last, the member [bp-0x1a] */
    unsigned pk_latch, pk_count, pk_faces, pk_dead;   /* [bp-0x1c], [bp-0x16]; its two flags (faces drawn, a fallen one may be chosen) */
    unsigned v66;                      /* [bp-0x66] in the fight: a spell's flashes are running */
    unsigned last_gw;                  /* the width of the last character 0x243ec drew (an argument it left on the stack) */
    /* command 2, the item list (0x20412) */
    uint8_t it_tab[26 * 30];           /* the field + 0x7d0: OBJECT.LCC records of the member's 20 items, then of his 6 worn things */
    unsigned it_cur, it_prev, it_idx;  /* [bp-0xc], [bp-0x12], [bp-0x24] (0 .. 9) */
    unsigned it_page, it_redraw;       /* [bp-4]: 0x14 the first ten items, 0x1e the next ten; [bp-0x26]: the rows are drawn again */
    unsigned it_latch, it_count;       /* [bp-0x20]; [bp-0x1e] (a palette step when it passes 0x28) */
    /* the mouse (only with a driver): the pointer's place on screen in the fight ([bp-0x7e]), the lists ([bp-0x24], [bp-0x28]) and the
     * picker ([bp-0x18]), all keeping what is under it in the world's buffer (walk_mouse_under); what the fight's first read gave
     * ([bp-0xe], [bp-0x2a]); and each loop's "a button is still down" flag */
    unsigned mptr, mx, my, mlatch;
    unsigned mg_ptr, mg_mlatch, pk_ptr, pk_mlatch, it_ptr, it_mlatch;
    char unbuilt[120];
    int unbuilt_new;
    void (*show)(void *ctx, screen_t *scr);   /* battle_set_show: the steps of the picker face's flash on screen */
    void *show_ctx;
    /* F4 in the world (battle_world_cast): the list's buffer byte 0 is 0 there ([bp-0xe] of 0x21486); 0 while the caster is being
     * picked, 1 from his list on */
    int world, world_stage;
    int world_items;                   /* F5 (battle_world_items, 1) or F6 (battle_world_equip, 2): after the picker, not the magic list;
                                        * 3: a shop's picker alone (battle_world_pick) */
    int pal_live;                      /* the picker steps the palette itself, not the dimmed copy (a shop's) */
    int use;                           /* F7 (battle_world_use): the list's buffer byte 0 is 2 ([bp-0x10] of 0x20412) */
    unsigned use_item;                 /* what the list returns there ([bp-8]) */
    uint8_t use_rec[30];               /* the chosen item's record, copied to the map buffer (0x20eef) */
    /* the equipment screen (0x221d2), F5's and F6's; its frame is the item list's (both get 13 words from F5's handler), so each starts
     * from what the other left: its [bp-0x1c] (eq_count) is the list's [bp-0x1c] (frame_1c), its [bp-0x1e] the list's it_count */
    unsigned eq_cur, eq_prev, eq_count, eq_latch, eq_mlatch, eq_ptr, frame_1c;
    uint8_t eq_tab[6 * 20];            /* buffer + 0x2710: 16 bytes from byte 0xe of each worn thing's record, 20 apart */
    uint8_t digits[350];               /* the map buffer + 0xd070: RPG.LCP 0x9b8, ten 7 x 5 digits */
};

void battle_set_show(battle_t *b, void (*show)(void *ctx, screen_t *scr), void *ctx)
{
    b->show = show;
    b->show_ctx = ctx;
}

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

int battle_kind(const battle_t *b)
{
    return b->phase;
}

unsigned battle_result(const battle_t *b)
{
    return b->kills;
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

uint8_t *battle_read_unpack(const char *name, size_t pos, size_t n, size_t *len)
{
    return read_unpack(name, pos, n, len, NULL);
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
    walk_mouse_hide(w, scr);   /* 0x1287a: the world's pointer comes off the screen */
    battle_t *b = calloc(1, sizeof *b);
    if (!b)
        return NULL;
    b->w = w;
    b->mon = b->bmem;
    if (assets_read_at("RPG.LCP", 0x340, b->table, sizeof b->table) != 0 ||
        assets_read_at("RPG.LCP", 0xD30, b->font, sizeof b->font) != 0 ||
        assets_read_at("RPG.LCP", 0xC9A, b->font2, sizeof b->font2) != 0 ||
        assets_read_at("RPG.LCP", 0xB5A, b->sparks, sizeof b->sparks) != 0 ||
        assets_read_at("RPG.LCP", 0x198D8, b->fxmem + FX_N * 64, SHOUTS * 0xFF) != 0 ||
        assets_read_at("RPG.LCP", 0x1A6B7, b->font8, sizeof b->font8) != 0) {
        fprintf(stderr, "battle: RPG.LCP is too short\n");
        free(b);
        return NULL;
    }
    for (unsigned k = 0; k < SHOUTS; k++) {   /* the translated modes: the shouts (one-bit 136 x 14, 17 bytes a row) redrawn */
        char id[16];
        snprintf(id, sizeof id, "shout.%u", k);
        l10n_bitmap(id, b->fxmem + FX_N * 64 + 0xFF * k, 136, 14, 17, 0);
    }
    {   /* NUMBER.LCZ 0x359b (0x25eeb at the win): x, = and the ten digits, 13 x 13 each, no headers */
        uint8_t head[3];
        size_t len = 0;
        uint8_t *g = assets_read_at("NUMBER.LCZ", 0x359B, head, 3) == 0 ? read_unpack("NUMBER.LCZ", 0x359B, w16(head, 1), &len, NULL) : NULL;
        int ok = g && len >= sizeof b->glyphs;
        if (ok)
            memcpy(b->glyphs, g, sizeof b->glyphs);
        free(g);
        if (!ok) {
            fprintf(stderr, "battle: the reward digits in NUMBER.LCZ are missing or too short\n");
            free(b);
            return NULL;
        }
    }
    b->phase = BATTLE_KIND_FIGHT;
    {   /* RPG.LCP 0xe95 (0x264 bytes): the magic list's row and icons, decoded by 0x25eeb into the field picture when the list opens */
        size_t len = 0;
        uint8_t *g = read_unpack("RPG.LCP", 0xE95, 0x264, &len, NULL);
        int ok = g && len >= sizeof b->mg_bar;
        if (ok)
            memcpy(b->mg_bar, g, sizeof b->mg_bar);
        free(g);
        if (!ok) {
            fprintf(stderr, "battle: the magic list's pictures in RPG.LCP are missing or too short\n");
            free(b);
            return NULL;
        }
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
    walk_sfx_play(w, SFX_EF, 0);   /* 0x12df4 */
    /* 0x1cbfa (mouse or not): the pointer's place is 0 and the 6 x 10 at the corner is kept under it. [bp-0x5c] (a button still down)
     * is never set: a release sets it, and a button held from the start counts only if the stack happened to hold a non-zero there
     * (taken as 0 here, not seen) */
    b->mptr = 0;
    walk_mouse_grab(scr, 0, walk_mouse_under(w));

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
    /* [bp-0x58] (the hit face's pass counter) is never set before it counts. Without an FM card it is 0 there. With one, the FM driver's
     * timer interrupt (0x2aa52) pushes its CX onto the stack below the world's frame and that is left there: 1, 3, 4, 5 in the oracle,
     * by when the interrupt came. The counter only asks "above 1" after adding 1, so every value 1 .. 0xfe acts as 1. */
    b->v58 = *walk_fm(w) ? 1 : 0;
    b->v84 = *walk_battle_left(w);   /* seen 1 after a won battle (2026-10-19, walk at clock 20300); never read before it is set */
    b->choosing = 0;
    b->v42 = 5;   /* 0x1d3e4, 0x1d3e8: no hit picture, no hit face running */
    b->vc = 5;
    b->v30 = 0;
    b->shout_w = b->shout_h = 1;
    return b;
}

void battle_free(battle_t *b)
{
    if (b) {
        if (!b->world)
            *walk_battle_left(b->w) = b->v84;
        free(b->atk);
    }
    free(b);
}

static void pick_begin(battle_t *b, screen_t *scr, int faces, int dead);

battle_t *battle_world_cast(walk_t *w, screen_t *scr)
{
    battle_t *b = calloc(1, sizeof *b);
    if (!b)
        return NULL;
    b->w = w;
    b->mon = b->bmem;
    b->world = 1;
    size_t len = 0;
    uint8_t *g = read_unpack("RPG.LCP", 0xE95, 0x264, &len, NULL);   /* 0x214f5: the map buffer + 0xd5b1 holds the same bytes */
    int ok = g && len >= sizeof b->mg_bar && assets_read_at("RPG.LCP", 0x1A6B7, b->font8, sizeof b->font8) == 0 &&
             assets_read_at("RPG.LCP", 0x9B8, b->digits, sizeof b->digits) == 0;
    if (ok)
        memcpy(b->mg_bar, g, sizeof b->mg_bar);
    free(g);
    if (!ok) {
        fprintf(stderr, "walk: the magic list's pictures or font in RPG.LCP are missing or too short\n");
        free(b);
        return NULL;
    }
    pick_begin(b, scr, 1, 0);   /* 0x15549: faces, a fallen one not to be chosen */
    /* [bp-0x16] is never set: 0x2d in every F4 tried (INFERRED constant; any value from 0x29 on gives a palette step at once) */
    b->pk_count = 0x2D;
    return b;
}

battle_t *battle_world_pick(walk_t *w, screen_t *scr, int dead, unsigned count)
{
    battle_t *b = calloc(1, sizeof *b);
    if (!b)
        return NULL;
    b->w = w;
    b->mon = b->bmem;
    b->world = 1;
    b->world_items = 3;
    b->pal_live = 1;
    pick_begin(b, scr, 1, dead);
    b->pk_count = count;
    return b;
}

unsigned battle_picked(const battle_t *b)
{
    return b->v44;
}

unsigned battle_pick_count(const battle_t *b)
{
    return b->last_gw;
}

battle_t *battle_world_items(walk_t *w, screen_t *scr)
{
    battle_t *b = battle_world_cast(w, scr);   /* the same picker (0x156c7), its count 0x2d in F5 too */
    if (b)
        b->world_items = 1;
    return b;
}

battle_t *battle_world_equip(walk_t *w, screen_t *scr)
{
    battle_t *b = battle_world_cast(w, scr);
    if (b)
        b->world_items = 2;
    return b;
}

battle_t *battle_world_use(walk_t *w, screen_t *scr)
{
    battle_t *b = battle_world_cast(w, scr);
    if (b) {
        b->world_items = 1;
        b->use = 1;
        b->pk_count = 0x1E;   /* [bp-0x16] is never set: 0x1e after F7's prompt in every start probed (INFERRED) */
    }
    return b;
}

unsigned battle_use_item(const battle_t *b, uint8_t rec[30], unsigned *member)
{
    *member = b->v44;
    if (b->use_item && b->use_item != 0xFF)
        memcpy(rec, b->use_rec, 30);
    return b->use_item;
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
                        walk_sfx_play(b->w, SFX_LOC, 0);   /* 0x1ff7c */
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
/* The next member who can act gets the mark; none left: the monsters' turn comes (v44 reaches 0xc8). */
/* (0x1cd79) _1_.LOC's sound for each of the five members, by his number: his shout when he attacks or casts. */
static void member_sound(battle_t *b, unsigned n)
{
    static const unsigned at[5] = {0x6771, 0x7742, 0x8849, 0x8E01, 0xA1BB};
    if (n < 5)
        walk_sfx_play(b->w, SFX_LOC, at[n]);
}

static void next_member(battle_t *b, screen_t *scr)
{
    uint8_t *party = party_of(b);
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
}

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
    member_sound(b, n);   /* 0x1f083 */
    next_member(b, scr);
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
    walk_sfx_play(b->w, SFX_LOC, 0xBC69);   /* 0x1d8f1 */
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
    if (o == 0xCB) {   /* 0x1dba1: nobody left: the battle returns 0 and the world sends the game to MARK.EXE's game-over screen */
        b->over = BATTLE_LOST;
        b->kills = 0;
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

/* ---- the mouse pointer (with a driver only; the world keeps the pointer's picture and what is under it) ---- */

static int mouse_on(const battle_t *b)
{
    int x, y;
    unsigned bt;
    return walk_mouse(b->w, &x, &y, &bt);
}

/* 0x27328: what is under the pointer back at `at` */
static void ptr_hide(battle_t *b, screen_t *scr, unsigned at)
{
    if (mouse_on(b))
        walk_mouse_put(scr, at, walk_mouse_under(b->w), 0);
}

/* 0x2723c + 0x27371: keep what is at `at`, draw the pointer there */
static void ptr_show(battle_t *b, screen_t *scr, unsigned at)
{
    if (mouse_on(b)) {
        walk_mouse_grab(scr, at, walk_mouse_under(b->w));
        walk_mouse_put(scr, at, walk_mouse_pic(b->w), 1);
    }
}

/* the place set to 0 and the corner kept (after a list, at a list's start) */
static void ptr_corner(battle_t *b, screen_t *scr, unsigned *place)
{
    if (mouse_on(b)) {
        *place = 0;
        walk_mouse_grab(scr, 0, walk_mouse_under(b->w));
    }
}

/* the pointer to `pos` if it is not there; returns 1 when it moved */
static int ptr_move(battle_t *b, screen_t *scr, unsigned *place, unsigned pos)
{
    if (pos == *place)
        return 0;
    ptr_hide(b, scr, *place);
    ptr_show(b, scr, pos);
    *place = pos;
    return 1;
}

/* A loop's buttons with its "still down" flag: a press counts once, until all are released. */
static unsigned press(unsigned bt, unsigned *down)
{
    if (bt == 0)
        *down = 0;
    if (*down)
        bt = 0;
    if (bt)
        *down = 1;
    return bt;
}

/* ---- command 1: the magic list (0x21486) and the member picker (0x1a8a4) ---- */

/* 0x25b54: a picture's pixels that are not 0, all in colour c. */
static void silhouette(screen_t *scr, unsigned at, const uint8_t *px, unsigned w, unsigned h, uint8_t c)
{
    for (unsigned r = 0; r < h; r++)
        for (unsigned x = 0; x < w; x++)
            if (px[r * w + x] && at + r * SCREEN_W + x < SCREEN_W * SCREEN_H)
                scr->pix[at + r * SCREEN_W + x] = c;
}

/* 0x27371: a picture with colour 0 transparent. */
static void put_clear(screen_t *scr, unsigned at, const uint8_t *px, unsigned w, unsigned h)
{
    for (unsigned r = 0; r < h; r++)
        for (unsigned x = 0; x < w; x++)
            if (px[r * w + x] && at + r * SCREEN_W + x < SCREEN_W * SCREEN_H)
                scr->pix[at + r * SCREEN_W + x] = px[r * w + x];
}

/* 0x243ec: `n` bytes of text as an (8 n) x 14 picture in `colour`. A byte from 0x20 to 0x7f is an 8 x 15 character of font8; any other
 * byte starts a Big5 pair (16 x 14, NUMBER.LCZ). In each character the colour turns colour + 1 in the first 0x25 (Big5: 0x50) pixels and
 * colour + 2 in the next as many. The 15th row of a character falls past the picture. */
static void text_pic(battle_t *b, const uint8_t *s, unsigned n, uint8_t colour, uint8_t *px)
{
    if (battle_text_pic(b->font8, s, n, colour, px, &b->last_gw))
        note(b, "a character the font does not have");
}

/* The translated modes' colouring of a battle_text_pic character: colour + 1 in the first `split` pixels of the cell, colour + 2
 * in the next as many (0x25 for 8 wide, 0x50 for 16, as the original; 0x1c for 6, the same rows) */
static uint8_t text_cell(void *ctx, unsigned gw, unsigned index)
{
    uint8_t colour = *(const uint8_t *)ctx;
    unsigned split = gw == 16 ? 0x50 : gw == 8 ? 0x25 : 0x1C;
    return index < 2 * split ? (uint8_t)(colour + 1 + index / split) : colour;
}

/* The translation of the `n` bytes at `s`: by the bytes themselves, or, for a string the game puts together (a number and a word,
 * as "  10 枚金幣"), its ASCII as it is and each run of Big5 characters by its own translation (none when a run has none). Into
 * `built` (cap bytes) when put together. NULL in Chinese or when there is none. */
static const char *text_translation(const uint8_t *s, unsigned n, char *built, size_t cap)
{
    const char *t = l10n_big5(s, n);
    if (t || l10n_current() == L10N_ZH)
        return t;
    size_t o = 0;
    unsigned i = 0;
    int ok = 0;
    while (i < n && o < cap - 1) {
        if (s[i] < 0x80) {
            built[o++] = s[i] >= 0x20 && s[i] < 0x7F ? (char)s[i] : ' ';
            i++;
            continue;
        }
        unsigned j = i;
        while (j + 1 < n && s[j] >= 0x80)
            j += 2;
        const char *r = l10n_big5(s + i, j - i);
        if (!r || o + strlen(r) >= cap - 1)
            return NULL;
        memcpy(built + o, r, strlen(r));
        o += strlen(r);
        ok = 1;
        i = j;
    }
    built[o] = 0;
    while (o && built[o - 1] == ' ')   /* trailing spaces, as the lookup ignores them */
        built[--o] = 0;
    return ok ? built : NULL;
}

unsigned battle_text_cols(const uint8_t *s, unsigned n, unsigned max)
{
    char built[256];
    const char *t = text_translation(s, n, built, sizeof built);
    if (!t)
        return n;
    size_t len = strlen(t);
    unsigned w = l10n_width(t, len, L10N_FONT_NORMAL);
    if (w > 8 * max)
        w = l10n_width(t, len, L10N_FONT_NARROW);
    unsigned cols = (w + 7) / 8;
    return cols > max ? max : cols ? cols : 1;
}

int battle_text_pic_cols(const uint8_t *font8, const uint8_t *s, unsigned n, unsigned cols, uint8_t colour, uint8_t *px,
                         unsigned *last_gw)
{
    char built[256];
    const char *t = text_translation(s, n, built, sizeof built);
    if (t) {   /* a translation in a 8 cols x 14 picture: the narrow font when the normal one does not fit */
        unsigned w = 8 * cols;
        size_t len = strlen(t);
        int font = l10n_width(t, len, L10N_FONT_NORMAL) <= w ? L10N_FONT_NORMAL : L10N_FONT_NARROW;
        memset(px, 0, (size_t)w * 14);
        int r = l10n_draw(t, len, font, px, w, 0, text_cell, &colour, NULL);
        *last_gw = font == L10N_FONT_NARROW ? 6 : 8;
        return r != 0;
    }
    return battle_text_pic(font8, s, n, colour, px, last_gw);
}

int battle_text_pic(const uint8_t *font8, const uint8_t *s, unsigned n, uint8_t colour, uint8_t *px, unsigned *last_gw)
{
    unsigned w = 8 * n, x = 0;
    int missing = 0;
    if (l10n_current() != L10N_ZH) {
        char built[256];
        if (text_translation(s, n, built, sizeof built))
            return battle_text_pic_cols(font8, s, n, n, colour, px, last_gw);
    }
    memset(px, 0, (size_t)w * 14);
    for (unsigned i = 0; i < n; i++) {
        uint8_t g[16 * 15];
        unsigned gw, split;
        memset(g, 0, sizeof g);
        if (s[i] > 0x1F && s[i] < 0x80) {
            gw = 8, split = 0x25;
            for (unsigned r = 0; r < 15; r++)
                for (unsigned k = 0; k < 8; k++)
                    g[r * 8 + k] = (font8[s[i] * 15 + r] << k) & 0x80 ? colour : 0;
        } else {
            gw = 16, split = 0x50;
            unsigned hi = s[i], lo = i + 1 < n ? s[i + 1] : 0;
            i++;
            uint8_t bits[28];
            size_t off = battle_glyph_offset(hi, lo);
            if (off == (size_t)-1 || assets_read_at("NUMBER.LCZ", off, bits, sizeof bits) != 0) {
                missing = 1;
                memset(bits, 0, sizeof bits);
            }
            for (unsigned r = 0; r < 14; r++)
                for (unsigned k = 0; k < 16; k++)
                    g[r * 16 + k] = (bits[r * 2 + k / 8] << (k % 8)) & 0x80 ? colour : 0;
        }
        for (unsigned j = 0; j < 2 * split; j++)
            if (g[j] == colour)
                g[j] = (uint8_t)(colour + 1 + j / split);
        for (unsigned r = 0; r < 14; r++)   /* 0x26c04, colour 0 transparent, rows of the picture's width */
            for (unsigned k = 0; k < gw; k++) {
                size_t at = (size_t)r * w + x + k;
                if (g[r * gw + k] && at < (size_t)w * 14)
                    px[at] = g[r * gw + k];
            }
        x += gw;
        *last_gw = gw;
    }
    return missing;
}

/* 0x2723c, 0x2741b, 0x27328: the 94 x 13 pixels inside a row of the list with its colours 7 .. 10 turned 0x74 .. 0x77 (or back). */
static void list_row(screen_t *scr, unsigned at, int lit)
{
    for (unsigned r = 0; r < 0xD; r++)
        for (unsigned x = 0; x < 0x5E; x++) {
            if (at + r * SCREEN_W + x >= SCREEN_W * SCREEN_H)
                continue;
            uint8_t *p = scr->pix + at + r * SCREEN_W + x;
            if (!lit && *p >= 7 && *p <= 0xA)
                *p = (uint8_t)(*p + 0x6D);
            else if (lit && *p >= 0x74 && *p <= 0x77)
                *p = (uint8_t)(*p - 0x6D);
        }
}

/* The picker's mark over a face: eight 2 x 2 squares down each diagonal of the 16 x 16 face at `at`, colours c, c + 1, ...; with `wrap`
 * the colours go 7 .. 10 round. */
static void pick_cross(screen_t *scr, unsigned at, unsigned c, int wrap)
{
    unsigned p = at, q = at + 0xE;
    for (int s = 0; s < 8; s++) {
        fill(scr, p, 2, 2, (uint8_t)c);
        p += 0x282;
        fill(scr, q, 2, 2, (uint8_t)c);
        q += 0x27E;
        if (++c > 0xA && wrap)
            c = 7;
    }
}

/* 0x1f60b / 0x1f780: each member's HP and MP in the strip again. */
static void strip_numbers(battle_t *b, screen_t *scr)
{
    const uint8_t *party = party_of(b);
    for (unsigned o = 0, at = FACE_AT; o < 0xC8 && party[o + 3] <= 4; o += 0x28, at += 0x26) {
        uint8_t num[100];
        battle_number(w16(party, o + 8), b->font, num, 0xF0);
        put(scr, at + 0x1400, num, 20, 5);
        battle_number(w16(party, o + 0xD), b->font, num, 0xB0);
        put(scr, at + 0x10, num, 20, 5);
    }
}

static void magic_end(battle_t *b, screen_t *scr, unsigned r);
static void magic_picked(battle_t *b, screen_t *scr, unsigned m);
static void items_begin(battle_t *b, screen_t *scr);
static void item_given(battle_t *b, screen_t *scr, unsigned m);
static void equip_begin(battle_t *b, screen_t *scr);

/* The lists' and the picker's palette step (0x1c284 on their palette argument): the palette itself in a battle, F4's dimmed copy
 * ([bp-0xb0]) in the world. */
static void step_palette(battle_t *b, screen_t *scr)
{
    if (b->world && !b->pal_live)
        walk_palette_step_dim(b->w, scr);
    else
        walk_palette_step(b->w, scr);
}

/* 0x1a8a4 .. 0x1aa2d: the picker's boxes (18 x 18, from (70, 74), 18 apart), each member's face in his with `faces` (a fallen one
 * crossed out in colours 0x10 .. 0x17), the first box lit. */
static void pick_begin(battle_t *b, screen_t *scr, int faces, int dead)
{
    uint8_t *party = party_of(b);
    const walk_scene_t *s = walk_scene(b->w);
    unsigned pos = 0x5D06;
    for (unsigned o = 0; o < 0xC8 && party[o + 3] <= 4; o += 0x28, pos += 0x12) {
        if (faces) {
            put(scr, pos + 0x141, s->bank2 + party[o + 3] * 0xC00 + 0x400, 16, 16);
            if (w16(party, o + 8) == 0)
                pick_cross(scr, pos + 0x141, 0x10, 0);
        }
        outline(scr, pos, 0x12, 0x12, 0x4E);
    }
    outline(scr, 0x5D06, 0x12, 0x12, 0x0F);
    ptr_corner(b, scr, &b->pk_ptr);   /* 0x1a9f9 */
    b->pk_mlatch = 0;
    b->pk_sel = b->pk_prev = 0x5D06;
    b->pk_m = 0;
    b->pk_latch = 0;
    /* [bp-0x16] is never set: the picker's frame has there the width argument (8 or 16) of the last character the list drew (0x26c04
     * called from 0x243ec at the same depth). Found 2026-10-19 after two starts gave 8 and 0x10. */
    b->pk_count = b->last_gw;
    b->pk_faces = faces;
    b->pk_dead = dead;
    b->phase = BATTLE_KIND_PICK;
}

/* 0x1aa30 .. 0x1af67: a round of the picker. Left and Right go to the member before or after (round), Enter chooses, Esc gives up
 * (0xc8). Only after a release. */
static void pick_round(battle_t *b, screen_t *scr, unsigned port60)
{
    uint8_t *party = party_of(b);
    if (++b->pk_count > 0x28) {
        b->pk_count = 0;
        step_palette(b, scr);
    }
    if (b->pk_prev != b->pk_sel) {
        for (unsigned o = 3, pos = 0x5D06; pos < 0x5D60 && party[o] <= 4; o += 0x28, pos += 0x12)
            if (pos != b->pk_sel)
                outline(scr, pos, 0x12, 0x12, 0x4E);
        outline(scr, b->pk_sel, 0x12, 0x12, 0x0F);
        b->pk_prev = b->pk_sel;
    }
    int done = 0;
    int mx, my;
    unsigned mb;
    if (walk_mouse(b->w, &mx, &my, &mb)) {   /* 0x1ab18: the pointer over a box chooses it, a left press takes it, the right gives up */
        unsigned bt = press(mb, &b->pk_mlatch), x = (unsigned)mx, y = (unsigned)my;
        unsigned pos = y * SCREEN_W + x;
        if (pos != b->pk_ptr) {   /* the boxes again; the loop's place variable is [bp-0xa], the box drawn last */
            ptr_hide(b, scr, b->pk_ptr);
            unsigned o = 3, p = 0x5D06;
            for (; p < 0x5D60 && party[o] <= 4; o += 0x28, p += 0x12)
                if (p != b->pk_sel)
                    outline(scr, p, 0x12, 0x12, 0x4E);
            outline(scr, b->pk_sel, 0x12, 0x12, 0x0F);
            b->pk_prev = p;
            ptr_show(b, scr, pos);
            b->pk_ptr = pos;
        }
        if (y > 0x49 && y < 0x5C) {
            static const unsigned lo[5] = {0x85, 0x97, 0xA9, 0xBB, 0xCD};
            for (unsigned i = 0; i < 5 && !done; i++)
                if (x > lo[i] && x < lo[i] + 0x13 && (i == 0 || party[0x28 * i + 3] < 5)) {
                    b->pk_m = 0x28 * i;
                    b->pk_sel = 0x5D06 + 0x12 * i;
                    if (bt == 1)
                        done = 1;
                }
        }
        if (!done && bt == 2) {
            b->pk_m = 0xC8;
            done = 1;
        }
    }
    unsigned k = port60 & 0xFF;
    if (!done) {
        if (k > 0x7F)
            b->pk_latch = 1;
        if (k >= 0x80 || !b->pk_latch)
            return;
        b->pk_latch = 0;
    }
    if (done) {
    } else if (k == 1) {
        b->pk_m = 0xC8;
        done = 1;
    } else if (k == 0x4B) {
        b->pk_sel -= 0x12;
        b->pk_m = (b->pk_m - 0x28) & 0xFF;
        if (b->pk_sel < 0x5D06) {
            b->pk_sel = 0x5D4E;
            b->pk_m = 0xA0;
        }
        while (party[b->pk_m + 3] > 4) {
            b->pk_m -= 0x28;
            b->pk_sel -= 0x12;
        }
    } else if (k == 0x4D) {
        b->pk_sel += 0x12;
        b->pk_m += 0x28;
        if (b->pk_sel > 0x5D4E) {
            b->pk_sel = 0x5D06;
            b->pk_m = 0;
        }
        if (party[b->pk_m + 3] > 4) {
            b->pk_m = 0;
            b->pk_sel = 0x5D06;
        }
    } else if (k == 0x1C) {
        done = 1;
    }
    if (!done)
        return;
    /* the picker's [bp-0x16] stays on the stack: a picker opened again before anything draws text there (the item list's key 3 a
     * second time) starts from it (found 2026-10-24, a palette step one pass early otherwise) */
    b->last_gw = b->pk_count;
    ptr_hide(b, scr, b->pk_ptr);   /* 0x1ae98 */
    outline(scr, b->pk_sel, 0x12, 0x12, 0x4E);
    if (b->pk_m < 0xC8 && b->pk_faces)
        pick_cross(scr, b->pk_sel + 0x141, 7, 1);
    if (b->pk_m < 0xC8 && w16(party, b->pk_m + 8) == 0 && !b->pk_dead)
        b->pk_m = 0xC8;
    magic_picked(b, scr, b->pk_m);
}

/* 0x21486 .. 0x217a3 (in a battle): the list over the field: an icon at (84, 76), ten rows (two columns of five, 96 x 15, from
 * (64, 102)), and in each the name of a spell the member's level (byte 0xc) has reached, as MAGICT.LCC lists them (20 bytes each, 200 a
 * member): with a shadow of colour 0xfa, greyed (colour 0x16) and not to be cast when his MP is below twice the level it needs. */
static void magic_begin(battle_t *b, screen_t *scr)
{
    uint8_t *party = party_of(b);
    unsigned m = b->v44;
    ptr_corner(b, scr, &b->mg_ptr);   /* 0x214a0 */
    b->mg_mlatch = 0;
    uint8_t icon[16 * 16];
    for (unsigned r = 0; r < 16; r++)
        memcpy(icon + 16 * r, b->mg_bar + 0x5A8 + 0x30 * r, 16);
    put(scr, 0x5F54, icon, 16, 16);
    unsigned bw = w16(b->mg_bar, 0), bh = w16(b->mg_bar, 2), at = 0x7FC0;
    for (int k = 0; k < 10; k++) {
        put(scr, at, b->mg_bar + 4, bw, bh);
        at = k == 4 ? 0x8020 : at + 0x12C0;
    }
    unsigned level = party[m + 0xC], mp = w16(party, m + 0xD);
    at = 0x7FC0;
    for (unsigned i = 0; i < 10; i++) {
        uint8_t rec[20], *t = b->mg_tab + 10 * i;
        if (assets_read_at("MAGICT.LCC", (size_t)party[m + 3] * 200 + 20 * i, rec, sizeof rec) != 0) {
            note(b, "a spell that MAGICT.LCC does not have");
            memset(rec, 0, sizeof rec);
        }
        memcpy(t, rec + 0xC, 8);   /* 0x27503 copies five words: bytes 0x14 and 0x15 come from past the record, and nothing reads them */
        t[8] = t[9] = 0;
        if (level >= t[0]) {
            uint8_t px[96 * 14];
            text_pic(b, rec, 12, 0xF0, px);
            silhouette(scr, at + 0x141, px, 96, 14, 0xFA);
            silhouette(scr, at + 0x140, px, 96, 14, 0xFA);
            if (mp >= t[0] * 2u) {
                put_clear(scr, at, px, 96, 14);
            } else {
                silhouette(scr, at, px, 96, 14, 0x16);
                t[0] = 0;
            }
        } else {
            t[0] = 0;
        }
        at = i == 4 ? 0x8020 : at + 0x12C0;
    }
    b->mg_prev = 0x93C1;
    b->mg_cur = 0x8101;
    b->mg_idx = 0;
    b->mg_latch = 0;
    b->mg_count = b->world ? 0xC3 : 3;   /* never set: what the stack holds there, 3 in every battle tried and 0xc3 in every F4 (INFERRED
                                          * constants; any value from 0x29 on gives a palette step in the first round) */
    b->phase = BATTLE_KIND_MAGIC;
}

/* 0x217a6 .. 0x22199: a round of the list. Up, Down, Left and Right move the lit row, Esc gives up, key 1 (scan code 2) casts the lit
 * spell if it can be: one that heals or helps opens the picker, the others are cast at once on the monsters. Only after a release. */
static void magic_round(battle_t *b, screen_t *scr, unsigned port60)
{
    if (++b->mg_count > 0x28) {
        b->mg_count = 0;
        step_palette(b, scr);
    }
    if (b->mg_prev != b->mg_cur) {
        ptr_hide(b, scr, b->mg_ptr);
        list_row(scr, b->mg_prev, 0);
        list_row(scr, b->mg_cur, 1);
        b->mg_prev = b->mg_cur;
        ptr_show(b, scr, b->mg_ptr);
    }
    unsigned k = port60 & 0xFF;
    int mx, my;
    unsigned mb;
    if (walk_mouse(b->w, &mx, &my, &mb)) {   /* 0x2197b: a press on a row lights it, on the icon casts; the right button leaves */
        unsigned bt = press(mb, &b->mg_mlatch), x = (unsigned)mx, y = (unsigned)my;
        unsigned pos = y * SCREEN_W + x;
        if (pos != b->mg_ptr) {   /* the lit row lit again; its loop leaves [bp-0x1c], the palette counter, at 0x78 */
            ptr_hide(b, scr, b->mg_ptr);
            list_row(scr, b->mg_cur, 1);
            b->mg_count = 0x78;
            ptr_show(b, scr, pos);
            b->mg_ptr = pos;
        }
        if (bt == 1 && x > 0x3F && x < 0x100 && y > 0x65 && y < 0xB1) {
            unsigned idx = (((x - 0x40) / 0x60) * 0x32 + ((y - 0x66) / 0xF) * 10) & 0xFF, i = 0;
            b->mg_idx = idx;
            b->mg_cur = 0x8101;
            while (i < idx) {   /* its counter is the buttons' variable, so the button tests below see the row instead */
                i += 10;
                b->mg_cur += 0x12C0;
                if (i == 0x32)
                    b->mg_cur = 0x8161;
            }
            bt = i;
        }
        if (bt == 1 && x > 0x53 && x < 0x64 && y > 0x4B && y < 0x5C)
            k = 2;
        if (bt == 2) {
            magic_end(b, scr, 0);
            return;
        }
    }
    if (k > 0x7F)
        b->mg_latch = 1;
    if (k >= 0x80 || !b->mg_latch)
        return;
    b->mg_latch = 0;
    if (k == 1) {
        magic_end(b, scr, 0);
        return;
    }
    if (k == 0x48 && b->mg_idx > 0) {
        b->mg_idx -= 10;
        b->mg_cur -= 0x12C0;
        if (b->mg_idx == 0x28)
            b->mg_cur = 0xCC01;
    }
    if (k == 0x50 && b->mg_idx < 0x5A) {
        b->mg_idx += 10;
        b->mg_cur += 0x12C0;
        if (b->mg_idx == 0x32)
            b->mg_cur = 0x8161;
    }
    if (k == 0x4B && b->mg_idx > 0x28) {
        b->mg_idx -= 0x32;
        b->mg_cur -= 0x60;
    }
    if (k == 0x4D && b->mg_idx < 0x32) {
        b->mg_idx += 0x32;
        b->mg_cur += 0x60;
    }
    if (k != 2)
        return;
    const uint8_t *t = b->mg_tab + b->mg_idx;
    if (t[0] == 0)
        return;
    if (t[1] | t[2] | t[3] | t[4] | t[5] | t[6]) {
        ptr_hide(b, scr, b->mg_ptr);   /* 0x21d22 */
        pick_begin(b, scr, 1, 1);
        return;
    }
    if (b->world)   /* 0x22107: a spell on the monsters does nothing in the world; the list goes on */
        return;
    uint8_t *party = party_of(b);   /* 0x22107: in a battle, the MP and the spell's strength (byte 0x13) */
    unsigned mp = (w16(party, b->v44 + 0xD) - t[0] * 2u) & 0xFFFF;
    party[b->v44 + 0xD] = (uint8_t)mp;
    party[b->v44 + 0xE] = (uint8_t)(mp >> 8);
    magic_end(b, scr, t[7]);
}

/* 0x21d71 .. 0x22101: the picker has given member m (0xc8: none, back to the list). The caster's MP goes down; the spell heals (the
 * HP, not a fallen member's, up to word 0xa), lowers word 0x10, revives (HP up even from 0); the face in the picker flashes in the colour
 * of the last of those (0x9f, 0x2f, 0x3f, 0x1f) counting down 16 steps, three times (each step shown through b->show when set); a revived one's
 * face is drawn again in the strip. */
static void magic_picked(battle_t *b, screen_t *scr, unsigned m)
{
    if (b->world_items == 3) {   /* a shop's picker alone (battle_world_pick): the member it gave */
        b->v44 = m;
        b->over = BATTLE_BACK;
        return;
    }
    if (b->world && b->world_stage == 0) {   /* F4's caster (0x15551 .. 0x155d4): one with HP left opens his list */
        if (m >= 0xC8 || w16(party_of(b), m + 8) == 0) {
            b->over = BATTLE_BACK;
            return;
        }
        b->v44 = m;
        b->world_stage = 1;
        if (b->world_items == 2) {   /* F6 (0x15748 .. 0x1577b): his equipment */
            b->eq_count = 0xEB;   /* [bp-0x1c] is never set: 0xeb after the picker in every F6 tried (INFERRED) */
            equip_begin(b, scr);
        } else if (b->world_items) {   /* F5 (0x156cf .. 0x1572d): his item list */
            b->it_count = 0x70;   /* never set: 0x70 after the picker in every F5 tried (INFERRED) */
            items_begin(b, scr);
        } else {
            magic_begin(b, scr);
        }
        return;
    }
    if (b->world && b->world_stage == 3) {   /* the item list's key 3 (0x2132d .. 0x21423): to whom */
        item_given(b, scr, m);
        return;
    }
    ptr_corner(b, scr, &b->mg_ptr);   /* 0x21d74: the list's pointer starts again from the corner */
    if (m >= 0xC8) {
        b->phase = BATTLE_KIND_MAGIC;
        return;
    }
    uint8_t *party = party_of(b);
    const uint8_t *t = b->mg_tab + b->mg_idx;
    unsigned mp = (w16(party, b->v44 + 0xD) - t[0] * 2u) & 0xFFFF;
    party[b->v44 + 0xD] = (uint8_t)mp;
    party[b->v44 + 0xE] = (uint8_t)(mp >> 8);
    unsigned res = 2, v;
    if ((v = w16(t, 1)) != 0) {
        res = 0x9F;
        unsigned hp = w16(party, m + 8);
        if (hp != 0) {
            hp = (hp + v) & 0xFFFF;
            if (hp > w16(party, m + 0xA))
                hp = w16(party, m + 0xA);
            party[m + 8] = (uint8_t)hp;
            party[m + 9] = (uint8_t)(hp >> 8);
            res = 0x2F;
        }
    }
    if ((v = w16(t, 3)) != 0) {
        unsigned d = w16(party, m + 0x10);
        d = d > v ? d - v : 0;
        party[m + 0x10] = (uint8_t)d;
        party[m + 0x11] = (uint8_t)(d >> 8);
        res = 0x3F;
    }
    if ((v = w16(t, 5)) != 0) {
        unsigned hp = (w16(party, m + 8) + v) & 0xFFFF;
        if (hp > w16(party, m + 0xA))
            hp = w16(party, m + 0xA);
        party[m + 8] = (uint8_t)hp;
        party[m + 9] = (uint8_t)(hp >> 8);
        res = 0x1F;
    }
    const uint8_t *face = walk_scene(b->w)->bank2 + party[m + 3] * 0xC00 + 0x400;
    unsigned k = m / 0x28;
    uint8_t sil[16 * 16];
    memcpy(sil, face, sizeof sil);
    for (int rep = 0; rep < 3; rep++)   /* 0x22041 .. 0x220a2: three times 16 colours down, each drawn 100 times (0x25c18) */
        for (unsigned s = 0; s < 16; s++) {
            silhouette(scr, k * 0x12 + 0x5E47, sil, 16, 16, (uint8_t)(res - s));
            if (b->show)
                b->show(b->show_ctx, scr);
        }
    if (res == 0x1F && !b->world)   /* 0x220b3: only in a battle (the list's buffer byte 0) */
        put(scr, FACE_AT + k * 0x26, face, 16, 16);
    magic_end(b, scr, 1);
}

/* 0x1f247 .. 0x1f6f2: back from the list with r (0 nothing cast, 1 a member helped, else the strength of a spell on the monsters), which
 * is also the key latch. The member's mark goes back; a spell on the monsters takes r HP from each live front one (a kill gives the
 * caster the monsters' experience stat, with no cap), with the floating numbers, a shout, and the flashes (v66) over the next passes;
 * then the next member's turn. The strip's HP and MP are drawn again. */
static void magic_end(battle_t *b, screen_t *scr, unsigned r)
{
    uint8_t *party = party_of(b);
    unsigned m = b->v44;
    ptr_hide(b, scr, b->mg_ptr);    /* 0x2219c: the list's pointer off */
    if (b->world) {   /* back to F4 (0x155dc), which lets its button go */
        b->over = BATTLE_BACK;
        return;
    }
    ptr_corner(b, scr, &b->mptr);   /* 0x1f24a: the fight's starts again from the corner */
    b->phase = BATTLE_KIND_FIGHT;
    b->latch = r & 0xFF;
    if (r != 0)
        mark(b, scr, box_at(m), 0x0F, 0x1F);
    if (r > 1) {
        unsigned n = party[m + 3];
        static const unsigned pic_at[5] = {0x7BE6, 0x80FE, 0x859C, 0x8B13, 0x8FBB};
        b->atk_pic = n < 5 ? pic_at[n] - 0x7BE6 : 0;
        unsigned ph = b->atk_pic + 4 <= b->atk_len ? w16(b->atk, b->atk_pic + 2) : 0;
        b->v92 = ((0x6E - ph) * 0xC0 + 0x47) & 0xFFFF;
        unsigned pos = 0x30C5;
        for (unsigned k = 0; k < 3; k++, pos += 0x3C) {
            unsigned hp = b->mon[0x34 + k];
            if (hp == 0)
                continue;
            if (hp > r) {
                hp -= r;
            } else {
                hp = 0;
                unsigned x = (w16(party, m + 4) + b->mon[0x2C]) & 0xFFFF;
                party[m + 4] = (uint8_t)x;
                party[m + 5] = (uint8_t)(x >> 8);
            }
            b->mon[0x34 + k] = (uint8_t)hp;
            b->mon[8 + 4 * k + 3] = 0x32;
            add_number(b, pos, r, 0x32, 0xF0);
            add_number(b, pos + 0x3E8, hp, 0x41, 0x22);
        }
        b->v66 = 1;
        b->v8a = 0x13;
        b->v8c = 1;
        member_sound(b, n);   /* 0x1f4f4 */
        set_shout(b, walk_rand(walk_rng(b->w)) / 0x4000, 0x30, 0x79C, 0x2D);
    }
    if (r != 0)
        next_member(b, scr);
    strip_numbers(b, scr);
    b->choosing = 0;
}

/* ---- command 2: the item list (0x20412) ---- */

/* 0x2723c, 0x2741b, 0x27328: a row of the item list lit (colours 0x74 .. 0x77 turned 0xb .. 0xe) or not; the grab is 94 x 15 but the
 * recolouring stops 0x4ca bytes in, so its last two rows keep their colours. */
static void item_row(screen_t *scr, unsigned at, int lit)
{
    for (unsigned i = 0; i < 0x4CA - 4; i++) {
        unsigned o = at + (i / 0x5E) * SCREEN_W + i % 0x5E;
        if (o >= SCREEN_W * SCREEN_H)
            continue;
        uint8_t *p = scr->pix + o;
        if (lit && *p >= 0x74 && *p <= 0x77)
            *p = (uint8_t)(*p - 0x69);
        else if (!lit && *p >= 0xB && *p <= 0xE)
            *p = (uint8_t)(*p + 0x69);
    }
}

static void read_object(battle_t *b, unsigned id, uint8_t *rec)
{
    memset(rec, 0, 30);
    if (assets_read_at("OBJECT.LCC", (size_t)(id - 1) * 30, rec, 30) != 0)
        note(b, "an item that OBJECT.LCC does not have");
}

/* 0x20412 .. 0x207af (in a battle): the member's 20 item bytes (party record 0x14 ..) are packed to the front, the two icons at
 * (84, 76), and the records of his items and of the six things he wears (party table 0x12c + 6 x his number) read from OBJECT.LCC
 * (30 bytes each, item n at (n - 1) x 30). The rows are drawn in the first round. The caller first puts the mouse pointer's background
 * back at the screen's corner (0x1f716, with no mouse too); it holds the corner as it is, so nothing changes there (seen once). */
static void items_begin(battle_t *b, screen_t *scr)
{
    uint8_t *party = party_of(b);
    unsigned m = b->v44, n = party[m + 3];
    ptr_corner(b, scr, &b->it_ptr);   /* 0x204d4 */
    b->it_mlatch = 0;
    for (unsigned i = m + 0x14; i < m + 0x28; i++) {
        unsigned v = party[i];
        if (v == 0)
            continue;
        for (unsigned j = m + 0x14; j < i; j++)
            if (party[j] == 0) {
                party[i] = 0;
                party[j] = (uint8_t)v;
                break;
            }
    }
    unsigned iw = b->world && !b->use ? 48 : 32;   /* 0x2054f: all three icons in F5 (key 3 gives an item away), two in a battle and in F7 */
    uint8_t icon[48 * 16];
    for (unsigned r = 0; r < 16; r++)
        memcpy(icon + iw * r, b->mg_bar + 0x5A8 + 0x30 * r, iw);
    put(scr, 0x5F54, icon, iw, 16);
    memset(b->it_tab, 0, sizeof b->it_tab);
    for (unsigned i = 0; i < 20; i++)
        if (party[m + 0x14 + i])
            read_object(b, party[m + 0x14 + i], b->it_tab + 30 * i);
    for (unsigned k = 0; k < 6; k++)
        if (party[0x12C + n * 6 + k])
            read_object(b, party[0x12C + n * 6 + k], b->it_tab + 30 * (20 + k));
    b->it_prev = 0x93C1;
    b->it_cur = 0x8101;
    b->it_idx = 0;
    b->it_page = 0x14;
    b->it_redraw = 1;
    b->it_latch = 0;
    if (!b->world)   /* never set: what the stack holds there, 0x98 in every battle tried (after the magic list too; INFERRED); in the
                      * world it is set by whoever opens the list */
        b->it_count = 0x98;
    b->phase = BATTLE_KIND_ITEMS;
}

/* 0x20e8f .. 0x2127b: item `slot` (party offset) used by the member. One worn in slot k (byte 0x10, 1 .. 6) changes places with what
 * he wears there, list records included (0x25c8e swaps them), and his attack (0x13) and defence (0x12) become the sums of byte 0xe and
 * byte 0xf / 5 of the six worn records, each halved when byte 0x16 is not his number. Then, from the record now in the list: word 0xc
 * heals him (up to word 0xa), word 0x14 lowers word 0x10, word 0x17 adds MP (up to 4000); each of these uses the item up. */
static void item_use(battle_t *b, unsigned slot)
{
    uint8_t *party = party_of(b);
    unsigned m = b->v44, id = party[slot];
    uint8_t *t = b->it_tab + (b->it_page == 0x1E ? 300 : 0) + 30 * b->it_idx;
    if (t[0x10]) {
        unsigned n = party[m + 3], e = n * 6 + 0x12B + t[0x10];
        party[slot] = party[e];
        party[e] = (uint8_t)id;
        uint8_t *w = b->it_tab + 0x23A + ((t[0x10] * 30) & 0xFF);
        for (int i = 0; i < 30; i++) {
            uint8_t x = t[i];
            t[i] = w[i];
            w[i] = x;
        }
        uint8_t a = 0, d = 0;
        for (unsigned k = 20; k < 26; k++) {
            const uint8_t *r = b->it_tab + 30 * k;
            unsigned ra = r[0xE], rd = r[0xF] / 5;
            if (r[0x16] != n) {
                ra >>= 1;
                rd >>= 1;
            }
            a = (uint8_t)(a + ra);
            d = (uint8_t)(d + rd);
        }
        party[m + 0x12] = d;
        party[m + 0x13] = a;
        b->it_redraw = 1;
    }
    unsigned v;
    if ((v = w16(t, 0xC)) != 0) {
        party[slot] = 0;
        unsigned hp = (w16(party, m + 8) + v) & 0xFFFF, top = w16(party, m + 0xA);
        if (hp > top)
            hp = top;
        party[m + 8] = (uint8_t)hp;
        party[m + 9] = (uint8_t)(hp >> 8);
        b->it_redraw = 1;
    }
    if ((v = w16(t, 0x14)) != 0) {
        party[slot] = 0;
        unsigned x = w16(party, m + 0x10);
        x = x > v ? x - v : 0;
        party[m + 0x10] = (uint8_t)x;
        party[m + 0x11] = (uint8_t)(x >> 8);
        b->it_redraw = 1;
    }
    if ((v = w16(t, 0x17)) != 0) {
        party[slot] = 0;
        unsigned mp = (w16(party, m + 0xD) + v) & 0xFFFF;
        if (mp > 0xFA0)
            mp = 0xFA0;
        party[m + 0xD] = (uint8_t)mp;
        party[m + 0xE] = (uint8_t)(mp >> 8);
        b->it_redraw = 1;
    }
}

/* 0x2143f, then the caller's 0x1f753: the list's pointer off, the fight's from the corner, the strip's numbers again. In the world
 * (F5) only the pointer: back to F5. */
static void items_end(battle_t *b, screen_t *scr)
{
    ptr_hide(b, scr, b->it_ptr);
    if (b->world) {
        b->over = BATTLE_BACK;
        return;
    }
    ptr_corner(b, scr, &b->mptr);
    b->phase = BATTLE_KIND_FIGHT;
    strip_numbers(b, scr);
    b->choosing = 0;
}

/* 0x21330 .. 0x21423: back from the picker of the item list's key 3 with member m: the pointer's place 0 and the corner kept; when m is
 * another member (below 0xc8) with an empty one of his 20 item places, the lit item goes to the first of them. */
static void item_given(battle_t *b, screen_t *scr, unsigned m)
{
    uint8_t *party = party_of(b);
    ptr_corner(b, scr, &b->it_ptr);
    b->world_stage = 1;
    b->phase = BATTLE_KIND_ITEMS;
    unsigned slot = b->v44 + b->it_page + b->it_idx;
    if (m == b->v44 || m >= 0xC8)
        return;
    for (unsigned j = 0x14; j < 0x28; j++)
        if (party[m + j] == 0) {
            party[m + j] = party[slot];
            party[slot] = 0;
            b->it_redraw = 1;
            return;
        }
}

/* 0x207b2 .. 0x2143c: a round of the item list. When asked, the ten rows of the page with the names of the items there (colour 0x10,
 * shadow 0x16) and the lit row. Up, Down, Left, Right move; key 1 (scan code 2) uses the lit item, key 2 (scan 3) turns the page; Esc
 * goes back (0x1f753: the strip's numbers again). Using things does not end the member's turn. Only after a release. */
static void items_round(battle_t *b, screen_t *scr, unsigned port60)
{
    uint8_t *party = party_of(b);
    unsigned m = b->v44;
    b->it_count = (b->it_count + 1) & 0xFF;
    if (b->it_count > 0x28) {
        b->it_count = 0;
        step_palette(b, scr);
    }
    if (b->it_redraw) {
        unsigned at = 0x7FC0, toff = b->it_page == 0x1E ? 300 : 0, slot = m + b->it_page;
        unsigned bw = w16(b->mg_bar, 0), bh = w16(b->mg_bar, 2);
        for (unsigned i = 0; i < 10; i++, toff += 30, slot++) {
            put(scr, at, b->mg_bar + 4, bw, bh);
            if (party[slot]) {
                uint8_t px[96 * 14];
                text_pic(b, b->it_tab + toff, 12, 0x10, px);
                silhouette(scr, at + 0x141, px, 96, 14, 0x16);
                silhouette(scr, at + 0x140, px, 96, 14, 0x16);
                put_clear(scr, at, px, 96, 14);
            }
            at = i == 4 ? 0x8020 : at + 0x12C0;
        }
        item_row(scr, b->it_cur, 1);
        b->it_redraw = 0;
    }
    if (b->it_prev != b->it_cur) {
        ptr_hide(b, scr, b->it_ptr);
        item_row(scr, b->it_prev, 0);
        item_row(scr, b->it_cur, 1);
        b->it_prev = b->it_cur;
        ptr_show(b, scr, b->it_ptr);
    }
    unsigned k = port60 & 0xFF;
    int mx, my;
    unsigned mb;
    if (walk_mouse(b->w, &mx, &my, &mb)) {   /* 0x20ae3: a left press on a row lights it, on an icon is key 1 to 3, on the
                                              * left-hand buttons' corner leaves (after a release); the right button leaves */
        unsigned bt = press(mb, &b->it_mlatch), x = (unsigned)mx, y = (unsigned)my;
        unsigned pos = y * SCREEN_W + x;
        if (pos != b->it_ptr) {   /* the lit row lit again; its loop leaves [bp-0x1e], the palette counter, at 0x78 */
            ptr_hide(b, scr, b->it_ptr);
            item_row(scr, b->it_cur, 1);
            b->it_count = 0x78;
            ptr_show(b, scr, pos);
            b->it_ptr = pos;
        }
        if (bt == 1 && x > 0x3F && x < 0x100 && y > 0x65 && y < 0xB1) {
            unsigned idx = (((x - 0x40) / 0x60) * 5 + (y - 0x66) / 0xF) & 0xFF, i = 0;
            b->it_idx = idx;
            x = y = 0;
            b->it_cur = 0x8101;
            while (i < idx) {   /* the palette counter is this loop's counter */
                i++;
                b->it_cur += 0x12C0;
                if (i == 5)
                    b->it_cur = 0x8161;
            }
            b->it_count = i;
        }
        if (bt == 1 && x > 4 && x < 0x32 && y > 0x66 && y < 0x79)
            k = 0x40;
        if (bt == 1 && x > 0x53 && x < 0x84 && y > 0x4B && y < 0x5C)
            k = ((x - 0x54) >> 4) + 2;
        if (bt == 2) {
            items_end(b, scr);
            return;
        }
    }
    if (k > 0x7F)
        b->it_latch = 1;
    if (k >= 0x80 || !b->it_latch)
        return;
    b->it_latch = 0;
    if (k == 1) {
        items_end(b, scr);
        return;
    }
    if (k == 0x48 && b->it_idx > 0) {
        b->it_idx--;
        b->it_cur -= 0x12C0;
        if (b->it_idx == 4)
            b->it_cur = 0xCC01;
    }
    if (k == 0x50 && b->it_idx < 9) {
        b->it_idx++;
        b->it_cur += 0x12C0;
        if (b->it_idx == 5)
            b->it_cur = 0x8161;
    }
    if (k == 0x4B && b->it_idx > 4) {
        b->it_idx -= 5;
        b->it_cur -= 0x60;
    }
    if (k == 0x4D && b->it_idx < 5) {
        b->it_idx += 5;
        b->it_cur += 0x60;
    }
    unsigned slot = m + b->it_page + b->it_idx;
    b->frame_1c = slot & 0xFF;   /* 0x20e63, [bp-0x1c]: the equipment screen's palette count if it opens now */
    if (k == 2 && party[slot] && b->use) {   /* 0x20eda: F7's list ends with the lit item, its record copied to the map buffer */
        b->use_item = party[slot];
        memcpy(b->use_rec, b->it_tab + (b->it_page == 0x1E ? 300 : 0) + 30 * b->it_idx, 30);
        items_end(b, scr);
    } else if (k == 2 && party[slot]) {
        item_use(b, slot);
    } else if (k == 3) {
        b->it_redraw = 1;
        b->it_page += 10;
        if (b->it_page > 0x1E)
            b->it_page = 0x14;
    } else if (k == 4 && party[slot] && b->world && !b->use) {   /* 0x212b5: key 3, in F5 only: to whom (a picker without faces) */
        ptr_hide(b, scr, b->it_ptr);
        pick_begin(b, scr, 0, 0);
        b->world_stage = 3;
    } else if (k == 0x40) {   /* 0x21427: F6 (or its button); in the world the list returns 0xff, the equipment screen (F7: an item 0xff) */
        if (b->use) {
            b->use_item = 0xFF;
            items_end(b, scr);
            return;
        }
        if (b->world) {
            ptr_hide(b, scr, b->it_ptr);
            b->eq_count = b->frame_1c;
            equip_begin(b, scr);
            return;
        }
        items_end(b, scr);
    }
}

static void start_rewards(battle_t *b, screen_t *scr);

/* ---- F6: the equipment screen (0x221d2 .. 0x22c41) ---- */

static const unsigned EQ_NUM_AT[6] = {0x7622, 0x87A2, 0x9A62, 0xAD22, 0xBFE2, 0xD162};   /* 0x22335 .. 0x223b7 */

/* 0x24302 with its flag set: a number below 1000 in three 7 x 5 digits 8 apart (0x25bda, colour 0 clear), leading zeros left out. */
static void eq_number(battle_t *b, screen_t *scr, unsigned at, unsigned v)
{
    int started = 0;
    for (unsigned div = 100; div; at += 8) {
        unsigned d = (v / div) & 0xFF;
        v -= d * div;
        div /= 10;
        if (div && d == 0 && !started)
            continue;
        if (d)
            started = 1;
        if (d < 10)
            put_clear(scr, at, b->digits + d * 35, 7, 5);
        if (!div)
            break;
    }
}

/* 0x2723c, 0x2741b, 0x27328: the 96 x 15 row at `at` with its colour `from` turned `to` */
static void eq_recolour(screen_t *scr, unsigned at, uint8_t from, uint8_t to)
{
    for (unsigned r = 0; r < 15; r++)
        for (unsigned x = 0; x < 96; x++) {
            unsigned o = at + r * SCREEN_W + x;
            if (o < SCREEN_W * SCREEN_H && scr->pix[o] == from)
                scr->pix[o] = to;
        }
}

/* 0x221d2 .. 0x22567: a box of colour 0x5c at (64, 92), 96 x 90; RPG.LCP 0x168f9 (a 96 x 90 picture, a body with six boxes) at
 * (160, 93), colour 0 clear; then each of the six things the member wears (party table 0x12c + 6 x his number) from OBJECT.LCC: its
 * name (12 bytes) in a row of the box in colour 0xa0, 0x27 when byte 0x16 is not his number, over a shadow of that colour + 5 one pixel
 * down and down right; byte 0xe of the record (byte 0xf when 0) in three digits in the picture's box for it. The first row is lit in
 * the first round. The pointer's place is 0 and the corner kept. */
static void equip_begin(battle_t *b, screen_t *scr)
{
    uint8_t *party = party_of(b);
    unsigned m = b->v44, n = party[m + 3];
    fill(scr, 0x7340, 0x60, 0x5A, 0x5C);
    ptr_corner(b, scr, &b->eq_ptr);
    size_t len = 0;
    uint8_t *pic = read_unpack("RPG.LCP", 0x168F9, 0x1D98, &len, NULL);
    if (pic && len >= 4 + 96 * 90)
        put_clear(scr, 0x74E0, pic + 4, w16(pic, 0), w16(pic, 2));
    else
        note(b, "the equipment screen's picture in RPG.LCP");
    free(pic);
    memset(b->eq_tab, 0, sizeof b->eq_tab);
    for (unsigned i = 0, at = 0x7480; i < 6; i++, at += 0x12C0) {
        unsigned id = party[0x12C + n * 6 + i];
        if (!id)
            continue;
        uint8_t rec[30];
        read_object(b, id, rec);
        memcpy(b->eq_tab + 20 * i, rec + 0xE, 16);   /* 0x27503, eight words */
        unsigned v = rec[0xE] ? rec[0xE] : rec[0xF];
        b->it_count = v;   /* [bp-0x1e]: what the item list starts from if F5 follows */
        uint8_t c = n == rec[0x16] ? 0xA0 : 0x27;
        uint8_t px[96 * 14];
        text_pic(b, rec, 12, c, px);
        silhouette(scr, at, px, 96, 14, (uint8_t)(c + 5));
        silhouette(scr, at + 1, px, 96, 14, (uint8_t)(c + 5));
        put_clear(scr, at - 0x140, px, 96, 14);
        eq_number(b, scr, EQ_NUM_AT[i], v);
        b->eq_count = c;
    }
    b->eq_cur = 0x7340;
    b->eq_prev = 0x8600;
    b->eq_mlatch = 0;
    b->eq_latch = 0;
    b->phase = BATTLE_KIND_EQUIP;
}

/* 0x22c0d: the pointer off; the screen returns its key: 0x3f opens the item list (0x20412 again), anything else ends F5 or F6 */
static void equip_end(battle_t *b, screen_t *scr, unsigned k)
{
    ptr_hide(b, scr, b->eq_ptr);
    if (k == 0x3F) {
        items_begin(b, scr);
        return;
    }
    b->over = BATTLE_BACK;
}

/* 0x2256a .. 0x22c0a: a round of the equipment screen. A new lit row: its colour 0x5c turned 0xd, the old one's back. Up and Down
 * move; Esc leaves; F5 (or its button) goes to the item list; Enter or Space takes the lit thing off into his first empty item place
 * (his attack and defence summed again as an item worn changes them, item_use); with no room the thing stays, but its number is wiped
 * all the same. Keys only after a release, presses only after all buttons are up; the pointer over a row lights it. */
static void equip_round(battle_t *b, screen_t *scr, unsigned port60)
{
    uint8_t *party = party_of(b);
    unsigned m = b->v44;
    if (b->eq_cur != b->eq_prev) {
        ptr_hide(b, scr, b->eq_ptr);
        eq_recolour(scr, b->eq_cur, 0x5C, 0xD);
        eq_recolour(scr, b->eq_prev, 0xD, 0x5C);
        b->eq_prev = b->eq_cur;
        ptr_show(b, scr, b->eq_ptr);
    }
    b->eq_count = (b->eq_count + 1) & 0xFF;
    if (b->eq_count > 0x28) {
        b->eq_count = 0;
        step_palette(b, scr);
    }
    unsigned k = port60 & 0xFF;
    if (k > 0x7F)
        b->eq_latch = 1;
    if (!b->eq_latch)
        k = 0x82;
    int mx, my;
    unsigned mb;
    if (walk_mouse(b->w, &mx, &my, &mb)) {   /* 0x2270d */
        unsigned x = (unsigned)mx, y = (unsigned)my;
        if (mb == 0)
            b->eq_mlatch = 1;
        if (!b->eq_mlatch)
            mb = 0;
        if (mb)
            b->eq_mlatch = 0;
        b->it_count = y & 0xFF;
        ptr_move(b, scr, &b->eq_ptr, y * SCREEN_W + x);
        if (x > 0x3F && x < 0xA0 && y > 0x5B && y < 0xB6) {
            b->eq_cur = ((y - 0x5C) / 0xF) * 0x12C0 + 0x7340;
            if (mb == 1)
                k = 0x1C;
        }
        if (x > 4 && x < 0x32 && y > 0x52 && y < 0x65 && mb == 1)
            k = 0x3F;
        if (mb == 2) {
            equip_end(b, scr, k);
            return;
        }
    }
    if (k >= 0x7F)
        return;
    b->eq_latch = 0;
    if (k == 1) {
        equip_end(b, scr, 1);
        return;
    }
    if (k == 0x48 && b->eq_cur > 0x7340)
        b->eq_cur -= 0x12C0;
    if (k == 0x50 && b->eq_cur < 0xD100)
        b->eq_cur += 0x12C0;
    if (k == 0x3F) {
        equip_end(b, scr, 0x3F);
        return;
    }
    if (k != 0x1C && k != 0x39)
        return;
    unsigned slot = ((b->eq_cur + 0x8CC0) & 0xFFFF) / 0x12C0, n = party[m + 3], e = 0x12C + n * 6 + slot;
    b->it_count = e & 0xFF;
    unsigned id = party[e];
    b->eq_count = id;
    if (!id)
        return;
    unsigned j = m + 0x14;
    while (j < m + 0x28 && party[j])
        j++;
    if (j < m + 0x28) {
        party[e] = 0;
        party[j] = (uint8_t)id;
        memset(b->eq_tab + 20 * slot, 0, 20);
        ptr_hide(b, scr, b->eq_ptr);
        fill(scr, b->eq_cur, 0x60, 0xF, 0x5C);
        ptr_show(b, scr, b->eq_ptr);
        b->eq_count = n;
        uint8_t a = 0, d = 0;   /* the sums are kept in the two latches' bytes ([bp-0x1a], [bp-8]) */
        for (unsigned i = 0; i < 6; i++) {
            const uint8_t *t = b->eq_tab + 20 * i;
            unsigned ra = t[0], rd = t[1] / 5;
            if (t[8] != n) {
                ra >>= 1;
                rd >>= 1;
            }
            a = (uint8_t)(a + ra);
            d = (uint8_t)(d + rd);
        }
        party[m + 0x12] = d;
        party[m + 0x13] = a;
        b->eq_latch = a;
        b->eq_mlatch = d;
    }
    if (slot < 6)
        fill(scr, EQ_NUM_AT[slot], 0x18, 6, 0);
}

/* 0x1e82e .. 0x1e8df, 0x1e8f1 .. 0x1e98f: key 1 or 2 with its card there: the flag switched, the right-hand button (47 x 20 at `at`)
 * pressed or let go with the pointer taken off first and only kept again after (the next move draws it). */
static void sound_button(battle_t *b, screen_t *scr, unsigned at)
{
    if (mouse_on(b))
        walk_mouse_put(scr, b->mptr, walk_mouse_under(b->w), 0);
    walk_button_toggle(scr, at, 0x2F, 0x14);
    if (mouse_on(b))
        walk_mouse_grab(scr, b->mptr, walk_mouse_under(b->w));
}

/* 0x1e81c .. 0x1f1c7: a key in the party's turn (the latch is then cleared, whatever the key). */
static void keys(battle_t *b, screen_t *scr, unsigned k)
{
    b->latch = 0;
    unsigned *fm = walk_fm(b->w), *sb = walk_sb(b->w);
    if (k == 2 && *fm) {   /* music: stopped (0x80) or the battle's song from its start (0x81); the caller plays it */
        *fm = *fm == 0x81 ? 0x80 : 0x81;
        sound_button(b, scr, 0x7B8D);
        k = 0;
    }
    if (k == 3 && *sb) {   /* the Sound Blaster's sounds */
        *sb = *sb == 0x81 ? 0x80 : 0x81;
        sound_button(b, scr, 0x948D);
        k = 0;
    }
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
    if (b->v82 == 3) {   /* 0x1f86b: flee. The mark goes back and the turn passes to the monsters; one time in two the party gets
                          * away, to the rewards bar as after a win (0x1f8ec) */
        mark(b, scr, box_at(b->v44), 0x0F, 0x1F);
        b->v44 = 0xC8;
        b->choosing = 0;
        if (walk_rand(walk_rng(b->w)) < 0x4000)
            start_rewards(b, scr);
        return;
    }
    if (b->v82 == 1) {   /* 0x1f1ca: the magic list, at once (with a mouse the fight's pointer comes off first) */
        ptr_hide(b, scr, b->mptr);
        magic_begin(b, scr);
        return;
    }
    if (b->v82 == 2) {   /* 0x1f6f6: the item list, at once (0x1f716 puts back what is under the fight's pointer, mouse or not) */
        ptr_hide(b, scr, b->mptr);
        items_begin(b, scr);
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

/* ---- what follows a win ---- */

/* 0x25d82: a 13 x 13 picture into the bar at column x. */
static void bar_glyph(battle_t *b, unsigned x, unsigned g)
{
    for (unsigned r = 0; r < 13; r++)
        memcpy(b->bar + r * BATTLE_FIELD_W + x, b->glyphs + g * 169 + r * 13, 13);
}

/* 0x1f900 .. 0x1fb7a: the bar "kills x coins per monster = coins" (glyph 0 is x, 1 is =, 2 + d the digit d; numbers through the
 * five-digit 0x26450, of which the last three and the last four are shown), the coins added (at most 50,000) and shown at once. */
static void start_rewards(battle_t *b, screen_t *scr)
{
    unsigned kills = 0;
    for (unsigned i = 2; i < 0x14; i += 4)
        if (b->mon[i] > 2 && b->mon[i] < 9)
            kills++;
    unsigned per = b->mon[0x2B], total = (kills * per) & 0xFFFF;
    b->kills = kills;
    memset(b->bar, 0, sizeof b->bar);
    bar_glyph(b, 3, 2 + kills);
    bar_glyph(b, 0x16, 0);
    char d[6];
    snprintf(d, sizeof d, "%05u", per);
    for (int i = 2; i < 5; i++)
        bar_glyph(b, 0x29 + 0x13 * (unsigned)(i - 2), 2 + (unsigned)(d[i] - '0'));
    bar_glyph(b, 0x62, 1);
    snprintf(d, sizeof d, "%05u", total);
    for (int i = 1; i < 5; i++)
        bar_glyph(b, 0x75 + 0x13 * (unsigned)(i - 1), 2 + (unsigned)(d[i] - '0'));
    uint8_t *party = party_of(b);
    unsigned g = battle_add_coins(w16(party, WALK_GOLD_AT), kills, per);
    party[WALK_GOLD_AT] = (uint8_t)g;
    party[WALK_GOLD_AT + 1] = (uint8_t)(g >> 8);
    walk_show_gold(b->w, scr);
    b->bar_at = 0x1744;
    b->bar_step = 0x180;
    b->phase = BATTLE_KIND_REWARDS;
}

static void level_ups(battle_t *b, screen_t *scr);

/* 0x1fb7d: the field sways on, the palette cycles, the bar bobs between rows 31 and 56; Space, Esc or Enter ends the battle. */
static void rewards_pass(battle_t *b, screen_t *scr, unsigned port60)
{
    if (b->dir) {
        if (++b->count == 3)
            b->dir = 0;
    } else if (--b->count == 0) {
        b->dir = 1;
    }
    battle_sway(b->back, b->field, b->count, b->dir);
    walk_palette_step(b->w, scr);
    b->bar_at = (b->bar_at + b->bar_step - 0xC0) & 0xFFFF;
    if (b->bar_at > 0x2944 || b->bar_at < 0x1804) {
        b->bar_step = (b->bar_step + 0x180) & 0xFFFF;
        if (b->bar_step > 0x180)
            b->bar_step = 0;
    }
    for (unsigned i = 0; i < sizeof b->bar; i++)   /* 0x26d52: colour 0 is transparent */
        if (b->bar[i] && b->bar_at - 4 + i < sizeof b->field)
            b->field[b->bar_at - 4 + i] = b->bar[i];
    put(scr, FIELD_AT, b->field, BATTLE_FIELD_W, BATTLE_FIELD_H);
    unsigned k = port60 & 0xFF;
    int mx, my;
    unsigned mb;
    int leave = k == 0x39 || k == 1 || k == 0x1C;
    if (!leave && walk_mouse(b->w, &mx, &my, &mb) && mb != 0)   /* 0x1fc75: any button */
        leave = 1;
    if (leave) {
        walk_sfx_stop(b->w);   /* 0x1fcc8: the sound is cut */
        ptr_hide(b, scr, b->mptr);   /* 0x1fcd7 */
        b->lu = 0;
        level_ups(b, scr);
    }
}

/* 0x2464a: a Big5 character's place in NUMBER.LCZ (30-byte cells from 0x3b20; lead bytes 0xa4 .. 0xc6, then 0xc9 on after 0x1519
 * cells). (size_t)-1 for a lead byte outside both. */
size_t battle_glyph_offset(unsigned hi, unsigned lo)
{
    if (lo >= 0x40 && lo <= 0x7E)
        lo -= 0x40;
    else if (lo >= 0xA1 && lo <= 0xFE)
        lo -= 0x62;
    size_t idx;
    if (hi >= 0xA4 && hi <= 0xC6)
        idx = (size_t)(hi - 0xA4) * 0x9D + lo;
    else if (hi >= 0xC9)
        idx = (size_t)(hi - 0xC9) * 0x9D + lo + 0x1519;
    else
        return (size_t)-1;
    return 0x3B20 + 0x1E * idx;
}

/* 0x1afb0 with the wait flag: message `msg` of MES_TXT.LCC (0x78 bytes at (msg - 1) * 0x80 + 6, up to '#') over the darkened bottom of
 * the screen (rows 134 .. 179: shaded twice, rows 135 .. 178 twice more), in colour 0x70; then the box waits (dialog_pass). Returns -1 if
 * the text cannot be read. */
static int start_message(battle_t *b, screen_t *scr, unsigned msg)
{
    /* in a battle the world buffer's 0x7305 .. 0x7308 are the backdrop's 0x1ea5 .. 0x1ea8 */
    int r = msgbox_open(&b->box, scr, b->font8, b->table, b->back + 0x1EA5, msg, 0x70, 1);
    if (r < 0) {
        note(b, "a message that MES_TXT.LCC does not have");
        return -1;
    }
    if (r > 0)
        note(b, "a character the message font does not have");
    b->phase = BATTLE_KIND_DIALOG;
    return 0;
}

static void box_step(void *ctx, screen_t *scr)
{
    walk_palette_step(((battle_t *)ctx)->w, scr);
}

/* 0x1b506: one round of the message box's wait: the palette steps every 41st; a key counts after a release has been seen. */
static void dialog_pass(battle_t *b, screen_t *scr, unsigned port60)
{
    int mx, my;
    unsigned mb = 0;
    int on = walk_mouse(b->w, &mx, &my, &mb);
    if (msgbox_round(&b->box, scr, port60, on, mb, box_step, b))
        level_ups(b, scr);
}

/* 0x130e2 .. 0x13291: each member whose experience (word 4) has reached the next level's (word 6, below 60,000) gets it as his
 * experience, the next one 500 higher, his most HP (word 0xa) a twelfth more and full HP (each at most 60,000), and the message
 * 0x2e2 + his number. Then the world goes on (BATTLE_BACK). After a story enemy (byte 0x2f set) the world first takes it away or puts
 * the party back (walk_boss_after, 0x12fb5). */
static void level_ups(battle_t *b, screen_t *scr)
{
    uint8_t *party = party_of(b);
    if (b->lu == 0)
        walk_boss_after(b->w, battle_result(b));
    for (; b->lu < 5; b->lu++) {
        uint8_t *m = party + 0x28 * b->lu;
        if (!battle_level_up(m))
            continue;
        unsigned msg = m[3] + 0x2E2u;
        b->lu++;
        if (start_message(b, scr, msg) == 0)
            return;
    }
    b->over = BATTLE_BACK;   /* the music is the caller's (0x132cb .. 0x132f1) */
    walk_mouse_reset(b->w, scr);   /* 0x1329d: the world's pointer starts again from the corner */
}

int battle_level_up(uint8_t m[0x28])
{
    unsigned e = w16(m, 4), nx = w16(m, 6);
    if (e < nx || e >= 0xEA60)
        return 0;
    m[4] = (uint8_t)nx;
    m[5] = (uint8_t)(nx >> 8);
    nx = (nx + 0x1F4) & 0xFFFF;
    if (nx > 0xEA60)
        nx = 0xEA60;
    m[6] = (uint8_t)nx;
    m[7] = (uint8_t)(nx >> 8);
    unsigned hp = w16(m, 0xA);
    hp = (hp + hp / 12) & 0xFFFF;
    if (hp > 0xEA60)
        hp = 0xEA60;
    m[0xA] = m[8] = (uint8_t)hp;
    m[0xB] = m[9] = (uint8_t)(hp >> 8);
    return 1;
}

unsigned battle_add_coins(unsigned coins, unsigned kills, unsigned per)
{
    unsigned g = (coins + ((kills * per) & 0xFFFF)) & 0xFFFF;
    return g > 0xC350 ? 0xC350 : g;
}

/* 0x1e472 .. 0x1e7e3: the buttons in the fight (read again; the places are the first read's). The right button is Esc. A press on the
 * music or sound button is key 1 or 2 (scan 2, 3). With no command confirmed, the pointer over the tabs chooses the tab and a left press
 * confirms it (Space); while a target is chosen for an attack, the pointer over a live front monster makes it the target and a left
 * press attacks. A press counts once ([bp-0x5c]). Returns the key the fight then takes. */
static unsigned fight_mouse(battle_t *b, unsigned mb, unsigned k)
{
    unsigned x = b->mx, y = b->my;
    if (mb == 0)
        b->mlatch = 1;
    if (mb == 2) {
        b->latch = 1;
        k = 1;
    }
    if (x > 0x10C && x < 0x13C && y > 0x62 && y < 0x8A && mb != 0 && b->mlatch) {
        b->mlatch = 0;
        k = y < 0x76 ? 2 : 3;
    }
    if (!b->choosing && y > 0x3B && y < 0x4A && x > 0x3F && x < 0x100) {
        b->v82 = (x - 0x40) / 0x30;
        if (mb == 1 && b->mlatch) {
            b->mlatch = 0;
            k = 0x39;
            b->latch = 1;
        }
    }
    if (b->choosing && b->v82 == 0 && y > 0x75 && y < 0xB2) {
        static const unsigned lo[3] = {0x40, 0x82, 0xC2}, hi[3] = {0x7D, 0xBF, 0xFF};
        for (unsigned s = 0; s < 3; s++) {
            unsigned o = 8 + 4 * s;
            if (!(x > lo[s] && x < hi[s] && b->mon[o + 2] < 3 && (b->mon[o + 3] == 0 || b->mon[o + 3] == 0x96)))
                continue;
            for (unsigned i = 0xB; i < 0x14; i += 4)
                if (b->mon[i] == 0x96)
                    b->mon[i] = 0;
            b->mon[o + 3] = 0x96;
            if (mb == 1 && b->mlatch) {
                b->mlatch = 0;
                k = 0x39;
                b->latch = 1;
            }
        }
    }
    return k;
}

int battle_pass(battle_t *b, screen_t *scr, unsigned port60)
{
    b->unbuilt_new = 0;
    if (b->over)   /* lost, or back in the world */
        return 0;
    if (b->phase == BATTLE_KIND_REWARDS) {
        rewards_pass(b, scr, port60);
        return b->unbuilt_new ? WALK_PASS_UNBUILT : 0;
    }
    if (b->phase == BATTLE_KIND_DIALOG) {
        dialog_pass(b, scr, port60);
        return b->unbuilt_new ? WALK_PASS_UNBUILT : 0;
    }
    if (b->phase == BATTLE_KIND_MAGIC) {
        magic_round(b, scr, port60);
        return b->unbuilt_new ? WALK_PASS_UNBUILT : 0;
    }
    if (b->phase == BATTLE_KIND_PICK) {
        pick_round(b, scr, port60);
        return b->unbuilt_new ? WALK_PASS_UNBUILT : 0;
    }
    if (b->phase == BATTLE_KIND_ITEMS) {
        items_round(b, scr, port60);
        return b->unbuilt_new ? WALK_PASS_UNBUILT : 0;
    }
    if (b->phase == BATTLE_KIND_EQUIP) {
        equip_round(b, scr, port60);
        return b->unbuilt_new ? WALK_PASS_UNBUILT : 0;
    }
    uint8_t *party = party_of(b);
    walk_palette_step(b->w, scr);
    if (b->v28 != b->v82) {
        ptr_hide(b, scr, b->mptr);   /* 0x1d48b: the pointer off, the tabs, the pointer on again */
        draw_tabs(scr, b->v82);
        ptr_show(b, scr, b->mptr);
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
                        return 0;
                }
            }
        }
    }
    if (b->v84 == 1) {   /* 0x1dbf7: a monster has just died */
        int left = 0;
        for (int i = 0x32; i < 0x37; i++)
            if (b->mon[i])
                left = 1;
        if (!left) {   /* 0x1dc42: won */
            start_rewards(b, scr);
            return b->unbuilt_new ? WALK_PASS_UNBUILT : 0;
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
        if (b->v42 == 5) {
            walk_sfx_play(b->w, SFX_LOC, 0x37A3);   /* 0x1dfb9 */
            hit_lands(b);
        }
    }
    if (b->v66) {   /* 0x1e034: a spell on the monsters: the field flashes colour 0x10 every other pass under the member's attack picture */
        if (b->v8c) {
            b->v8c = 0;
            memset(b->field, 0x10, sizeof b->field);
        } else {
            b->v8c = 1;
        }
        if (b->atk_pic + 4 <= b->atk_len) {
            unsigned aw = w16(b->atk, b->atk_pic), ah = w16(b->atk, b->atk_pic + 2);
            if (b->atk_pic + 4 + (size_t)aw * ah <= b->atk_len)
                blit_masked(b->field, b->v92, b->atk + b->atk_pic + 4, aw, ah);
        }
        b->v8a = (b->v8a - 1) & 0xFFFF;
        if (b->v8a < 1) {   /* the end: the hit monsters flash and die as after an attack */
            walk_sfx_play(b->w, SFX_LOC, 0x37A3);   /* 0x1e0ba */
            b->v66 = 0;
            for (unsigned i = 8; i < 0x14; i += 4)
                if (b->mon[i + 3] == 0x32)
                    b->mon[i + 3] = 0x14;
        }
        b->v32 = (b->v32 + 0x241) & 0xFFFF;
    }
    int mx, my;
    unsigned mb;
    if (walk_mouse(b->w, &mx, &my, &mb)) {   /* 0x1e10e: the pointer on the screen, and over the field also into the field */
        b->mx = (unsigned)mx;
        b->my = (unsigned)my;
        ptr_move(b, scr, &b->mptr, (unsigned)(my * SCREEN_W + mx));
        if (mx > 0x3A && my > 0x40 && mx < 0x100 && my < 0xB3) {   /* 0x26b97: rows r0 .. r1, columns c0 .. c1 of the 6 x 10 */
            unsigned r0 = 0, r1 = 9, c0 = 0, c1 = 5, frow = 0, fcol = 0;
            if (my > 0x49)
                frow = (unsigned)my - 0x4A;
            else
                r0 = 0x4A - (unsigned)my;
            if (my > 0xA9)
                r1 = 0xB2 - (unsigned)my;
            if (mx > 0x3F)
                fcol = (unsigned)mx - 0x40;
            else
                c0 = 0x40 - (unsigned)mx;
            if (mx > 0xFA)
                c1 = 0xFF - (unsigned)mx;
            const uint8_t *pic = walk_mouse_pic(b->w);
            for (unsigned r = r0; r <= r1; r++)
                for (unsigned c = c0; c <= c1; c++) {
                    size_t o = (size_t)(frow + r - r0) * BATTLE_FIELD_W + fcol + c - c0;
                    if (pic[r * 6 + c] && o < sizeof b->field)
                        b->field[o] = pic[r * 6 + c];
                }
        }
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
    if (walk_mouse(b->w, &mx, &my, &mb))   /* 0x1e472: buttons on the screen's places */
        k = fight_mouse(b, mb, k);
    if (k < 0x80 && b->v44 < 0xC8 && b->latch && b->v84 == 0)   /* 0x1e7e3: the party's turn, a release seen, nothing moving */
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
