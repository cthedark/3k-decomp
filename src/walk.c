#include "walk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"
#include "battle.h"
#include "msgbox.h"
#include "sfx.h"

struct walk {
    walk_scene_t *scene;
    int mouse_on, mouse_x, mouse_y;     /* a mouse driver (INT 33h) and what function 3 reports: pixels, buttons */
    unsigned mouse_b;
    /* the pointer (0x12608): RPG.LCP 0x300, 6 x 10 after a 4-byte header, colour 0 transparent; where it is on screen ([bp-0xc]) and
     * the 6 x 10 under it ([bp-0x84], the buffer + 0x9538, which the battle and its lists use too) */
    uint8_t mpic[60], mbg[60];
    unsigned mprev;
    int mstarted;
    unsigned v30, vc0;                  /* [bp-0x30]: the last pass's key (for a left press); [bp-0xc0]: that press was a walk */
    unsigned mx, my;                    /* where the pointer was at the pass's pointer block */
    /* F1, talking (0x14d62 .. 0x153ca, 0x19b9a): the message box's font and the shading table (the buffer's +0xfe78), the dimmed copy of
     * the palette's first 48 bytes the box's palette steps work on ([bp-0xb0]), the box, the villager talked to and his message */
    uint8_t font8[128 * 15], table[256], dimpal[48];
    msgbox_t box;
    int talking;                        /* 1 the message box waits (0x1b506), 2 the yes / no buttons (0x18c36), 3 the number (0x18fdc),
                                         * 4 a look's message box (F2) */
    uint8_t *talk_rec;
    unsigned talk_msg, talk_k;          /* the message shown, the entry used (its offset in the record) */
    uint8_t talk_under[SCREEN_W * 46];  /* [bp+6] + 0x9c40: the screen's rows 134 .. 179 under boxes that do not wait */
    unsigned talk_count, talk_mprev;    /* the two loops' palette count; where their pointer is */
    unsigned take_count;                /* F3's loop: its palette count ([bp-0x9a]) */
    battle_t *cast;                     /* F4: the picker and the magic list (battle_world_cast), while talking is 7; F5: the item list */
    int cast_items;                     /* cast is F5's (1) or F7's (2) */
    /* F7 (0x165df .. 0x1729e): set while its prompt (talking 8), list, number, yes / no or message box runs; the lit choice of the prompt
     * (0xbaa7 coins, 0xbaf7 an item; [si]), the prompt's palette count ([bp-0xc4]), the item the list gave ([bp-0x9a]) */
    int using;
    unsigned use_sel, use_count, use_item, use_member;
    /* [bp-0x96], a scratch word: written by the world pass (a kill, a spark, the rain, the shake, the pointer in the window) and by F7;
     * the other F-keys' writes are not followed. F7's name blink starts from it. */
    unsigned v96;
    void (*show)(void *ctx, screen_t *scr);   /* walk_set_show, handed to F4's picker */
    void *show_ctx;
    uint8_t talk_mbg[60];               /* the 6 x 10 under their pointer (the pointer's own + 0x40) */
    uint8_t yn_pic[3][47 * 20];         /* the buttons' place as it was, with "yes" pressed, with "no" pressed */
    unsigned yn_shown, yn_sel;          /* [bp-4], [bp-0xc]: 1 yes, 0 no (2: none shown yet) */
    uint8_t ask_art[0x284 + 94 * 16];   /* RPG.LCP 0x1643b decoded: ten 8 x 8 digits, then the 94 x 16 number box after a 4-byte header */
    uint8_t ask_under[94 * 16];
    unsigned ask_value, ask_pos, ask_latch; /* [bp-0x10], [bp-6] (1 the units .. 5), [bp-0x16] */
    screen_t *scr;                      /* the screen of the last walk_pass (a key in the pass a battle resumes uses it) */
    uint16_t drops[WALK_RAIN_DROPS];
    /* the moving world (walk_reset) */
    uint32_t rng;                       /* the game's rand() state (data segment 0x2d4) */
    uint16_t specks[WALK_SPECKS];
    uint8_t palbuf[WALK_PALBUF];        /* the palette buffer the cycle works on; the DAC takes its first 48 bytes */
    int pal_c[5];                       /* counters at data segment 0x532 .. 0x536 */
    unsigned hero_frame, ctr;           /* [bp-0x5e], [bp-0x3c] */
    unsigned v2e, v98, v48, c2;         /* [bp-0x2e], [bp-0x98], [bp-0x48], [bp-0xc2]: see walk_keys */
    unsigned vbe;                       /* [bp-0xbe]: 1 at the start (0x10bad), then the kind of the last scene record stepped on (0x1731c);
                                         * not 0, a pass that ends through 0x18b82 runs the record loop again */
    uint8_t *alt;                       /* [bp-0x9e]: byte 0 is 1 while a kind 3 record has taken the roof off, then the roof's saved part of the object map */
    char unbuilt[120];                  /* the last thing the original did that is not built, "" when none */
    int unbuilt_new;                    /* set when it changed during the current pass */
    uint8_t *buf;                       /* the world buffer, a little longer than WALK_W * WALK_H (see walk_draw_specks) */
    uint8_t *dbuf;                      /* [bp-0x20]: what the doublings write (walk_double, walk_double_dark), WALK_W wide */
    uint8_t zoom_grab[WALK_ZOOM_W * WALK_ZOOM_H];   /* [bp-0xba]: the start's view, grabbed for the zoom */
    /* [bp-0x62]: 5 shots (x, y, direction, zigzag phase; one per party member number), then [bp-0xcc]: 10 sparks
     * (frame, x, y, direction). All 0xfa at the start (0x102fe). */
    uint8_t fx[WALK_FX_BYTES];
    uint8_t art[WALK_ART_BYTES];        /* RPG.LCP at 0x9b8: ten 7 x 5 digits, the shot (4-byte header, 8 x 8), five 8 x 8 sparks */
    /* a touch (WALK_PASS_BATTLE): the member's party offset, the monster group, the record's byte 0x2f */
    unsigned battle_member, battle_group, battle_boss, battle_rec;
    unsigned battle_left;               /* the battle frame's [bp-0x84] as the last battle left it on the stack (0 before any) */
    int boss_box;                       /* a story enemy's message box waits before its battle (0x12872) */
    unsigned fm, sb;                    /* 0x2cc5:5 (the FM music) and 0x2cc5:0xc (the Sound Blaster's sounds): 0 no card, 0x81 on,
                                         * 0x80 off */
    walk_sfx_t sfx;                     /* the Sound Blaster's driver (walk_set_sfx); none: nothing plays */
    unsigned stride;                    /* the rain stride of the pass that touched (the rest of that pass runs after the battle) */
    int begin_result;                   /* what walk_begin's first pass returned */
};

enum { ART_DIGITS = 0, ART_SHOT = 0x15E, ART_SPARKS = 0x15E + 0x44 };

enum { WALK_BUF_LEN = WALK_W * WALK_H + 0x400, WALK_ALT_LEN = 1 + 256 * 256 };
/* F7's leftover from the stack: the number box's palette count when F7 opens it (INFERRED from the starts probed, docs/findings.md
 * step 32) */
enum { WALK_USE_ASK_COUNT = 0x16 };

static unsigned w16(const uint8_t *b, size_t o)
{
    return (unsigned)b[o] | ((unsigned)b[o + 1] << 8);
}

unsigned walk_rand(uint32_t *state)
{
    *state = *state * 0x343FDu + 0x269EC3u;
    return (*state >> 16) & 0x7FFF;
}

int walk_reset(walk_t *w, uint32_t seed)
{
    walk_scene_free(w->scene);
    w->scene = walk_scene_load();
    if (!w->scene)
        return -1;
    uint8_t rpg[WALK_PALBUF];
    if (assets_read_at("RPG.LCP", 0, rpg, sizeof rpg) != 0) {
        fprintf(stderr, "walk: RPG.LCP is missing or too short\n");
        return -1;
    }
    memcpy(w->palbuf, rpg, sizeof rpg);
    if (assets_read_at("RPG.LCP", 0x9B8, w->art, sizeof w->art) != 0) {
        fprintf(stderr, "walk: RPG.LCP is too short for the digits and the shot sprites\n");
        return -1;
    }
    memset(w->fx, 0xFA, sizeof w->fx);
    for (int i = 0; i < WALK_SHOTS; i++)     /* 0x11cee, just before the loop: no shot flies, no spark shows */
        w->fx[4 * i + 2] = 4;
    for (int j = 0; j < WALK_SPARKS; j++)
        w->fx[4 * WALK_SHOTS + 4 * j] = 9;
    memset(w->pal_c, 0, sizeof w->pal_c);
    w->hero_frame = 0;
    w->ctr = 0;
    w->v2e = 3;
    w->v98 = 0;
    w->v48 = w->scene->header[3];   /* 0x11233: the shake starts at the map header's byte 3 (0 in scenes 0 and 1) */
    w->c2 = 0;
    w->vbe = 1;
    memset(w->alt, 0, WALK_ALT_LEN);
    w->unbuilt[0] = 0;
    w->unbuilt_new = 0;
    w->rng = seed;
    for (int i = 0; i < WALK_RAIN_DROPS; i++)
        w->drops[i] = (uint16_t)walk_rand(&w->rng);
    for (int i = 0; i < WALK_SPECKS; i++) {   /* distinct: a repeat is drawn again (0x10641) */
        unsigned v;
        int dup;
        do {
            v = (walk_rand(&w->rng) + 0xE10) & 0xFFFF;
            dup = 0;
            for (int j = 0; j < i; j++)
                if (w->specks[j] == v)
                    dup = 1;
        } while (dup);
        w->specks[i] = (uint16_t)v;
    }
    return 0;
}

walk_t *walk_create(void)
{
    walk_t *w = calloc(1, sizeof *w);
    if (!w)
        return NULL;
    w->buf = calloc(1, WALK_BUF_LEN);
    w->dbuf = calloc(1, WALK_BUF_LEN);
    w->alt = calloc(1, WALK_ALT_LEN);
    if (!w->buf || !w->dbuf || !w->alt || walk_reset(w, 0) != 0) {
        walk_free(w);
        return NULL;
    }
    if (assets_read_at("RPG.LCP", 0x304, w->mpic, sizeof w->mpic) != 0)   /* 0x10822: the pointer, after the palette */
        memset(w->mpic, 0, sizeof w->mpic);
    if (assets_read_at("RPG.LCP", 0x1A6B7, w->font8, sizeof w->font8) != 0 || assets_read_at("RPG.LCP", 0x340, w->table, sizeof w->table) != 0) {
        memset(w->font8, 0, sizeof w->font8);
        memset(w->table, 0, sizeof w->table);
    }
    return w;
}

void walk_free(walk_t *w)
{
    if (!w)
        return;
    battle_free(w->cast);
    walk_scene_free(w->scene);
    free(w->buf);
    free(w->dbuf);
    free(w->alt);
    free(w);
}

const walk_scene_t *walk_scene(const walk_t *w)
{
    return w->scene;
}

/* ---- the ground and the objects ---- */

/* The original's art segment (0xffdc bytes allocated at 0x10057, at offset 0x16; SEEN at pass 5 and compared): 0x16 the object
 * sprites (s->sprites, 0x7f80 bytes, zero past their data), 0x7f96 the tiles (s->tiles), then the allocator's header of the next
 * block from 0xfff2, and before 0x16 this block's own. Only tile numbers that wrap past the segment's end read the sprites and the
 * headers: the words past the map's bottom do. The headers are the oracle run's (its segment numbers): a GUESS elsewhere. */
static const uint8_t art_head[0x16] = {0xEB, 0x4D, 0x03, 0x00, 0x00, 0x00, 0x14, 0x00, 0x14, 0x00, 0xFE,
                                       0xFF, 0x00, 0x00, 0xEB, 0x5D, 0x00, 0x00, 0xEB, 0x3D, 0xDC, 0xFF};
static const uint8_t art_tail[14] = {0x00, 0x00, 0x00, 0x0B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFE, 0xFF};

static unsigned art_byte(const walk_scene_t *s, unsigned off)
{
    off &= 0xFFFF;
    if (off < 0x16)
        return art_head[off];
    if (off < 0x7F96)
        return off - 0x16 < s->sprites_len ? s->sprites[off - 0x16] : 0;
    if (off < 0xFFF2)
        return s->tiles[off - 0x7F96];
    return art_tail[off - 0xFFF2];
}

void walk_draw_ground(uint8_t *buf, const walk_scene_t *s, unsigned x, unsigned y)
{
    /* row and column as bytes (bh = y / 2 - 10, bl = (x & ~1) - 0x16), the offset in 16 bits from the map's place in its segment */
    unsigned bx = ((((y >> 1) - 10) & 0xFF) << 8) | (((x & 0xFE) - 0x16) & 0xFF);
    int dx = 0xE10 - ((x & 1) ? 4 : 0) - ((y & 1) ? 0x380 : 0);
    for (int row = 0; row < 23; row++) {
        int d = dx;
        for (int col = 0; col < 25; col++) {
            unsigned off = (0x16 + bx) & 0xFFFF, word;
            if (off >= 0x16 && off + 2 <= 0x16 + WS_GMAP_BYTES)
                word = w16(s->gmap, off - 0x16);
            else
                word = walk_seg_byte(s, off) | walk_seg_byte(s, off + 1) << 8;
            unsigned t = (word << 6) & 0xFFFF;   /* the original shifts in 16 bits */
            if (t + 64 <= 0xFFF2 - 0x7F96) {
                for (int k = 0; k < 8; k++)
                    memcpy(buf + d + k * WALK_W, s->tiles + t + k * 8, 8);
            } else {   /* past the segment's end: the offset wraps (0x2c3b6) */
                for (int k = 0; k < 64; k++)
                    buf[d + (k >> 3) * WALK_W + (k & 7)] = (uint8_t)art_byte(s, 0x7F96 + t + (unsigned)k);
            }
            bx = (bx + 2) & 0xFFFF;
            d += 8;
        }
        bx = (bx + 0xCE) & 0xFFFF;
        dx += 8 * WALK_W;
    }
}

void walk_draw_objects(uint8_t *buf, const walk_scene_t *s, unsigned x, unsigned y)
{
    /* (y / 2 - 10 as a byte) << 7 plus ((x - 0x16) as a byte) / 2, in 16 bits from the object map's place (0x9300) */
    unsigned bx = (((((y >> 1) - 10) & 0xFF) << 7) + ((((x & 0xFF) - 0x16) & 0xFF) >> 1)) & 0xFFFF;
    int dx = 0xE10 - ((x & 1) ? 4 : 0) - ((y & 1) ? 0x380 : 0);
    for (int row = 0; row < 23; row++) {
        int d = dx;
        for (int col = 0; col < 25; col++) {
            unsigned off = (0x9300 + bx) & 0xFFFF;
            unsigned id = off >= 0x9300 && off < 0x9300 + WS_OMAP_BYTES ? s->omap[off - 0x9300] : walk_seg_byte(s, off);
            if (id <= 0xFE) {
                const uint8_t *spr = s->sprites + (id << 7);
                for (int k = 0; k < 8; k++)
                    for (int j = 0; j < 8; j++) {
                        uint8_t *p = buf + d + k * WALK_W + j;
                        *p = (uint8_t)((*p & spr[0x40 + k * 8 + j]) | spr[k * 8 + j]);
                    }
            }
            bx = (bx + 1) & 0xFFFF;
            d += 8;
        }
        bx = (bx + 0x67) & 0xFFFF;
        dx += 8 * WALK_W;
    }
}

/* ---- the entities ---- */

void walk_entity_order(const walk_scene_t *s, uint8_t order[WALK_ENTITIES])
{
    uint8_t ys[WALK_ENTITIES];
    for (int i = 0; i < WS_VILLAGERS; i++)
        ys[i] = s->tab_a[i * WS_VILLAGER_SIZE + 1];
    for (int i = 0; i < 5; i++)
        ys[WS_VILLAGERS + i] = s->tab_b[i * WS_PARTY_SIZE + 1];
    for (int i = 0; i < WALK_ENTITIES; i++)
        order[i] = (uint8_t)i;
    for (int pass = 0; pass < 24; pass++)
        for (int j = 0; j < 24; j++)
            if (ys[j] > ys[j + 1]) {
                uint8_t t = ys[j];
                ys[j] = ys[j + 1];
                ys[j + 1] = t;
                t = order[j];
                order[j] = order[j + 1];
                order[j + 1] = t;
            }
}

unsigned walk_screen_pos(unsigned px, unsigned py, unsigned x, unsigned y, int flag, unsigned vis)
{
    if (vis == 0 || vis >= 6 || px == 0 || py == 0)
        return WALK_HIDDEN;
    unsigned dl = (px + 4) & 0xFF, dh = (py + 4) & 0xFF;
    unsigned bl = (x - 0x16) & 0xFF, bh = (y - 0x14) & 0xFF;
    if (flag == 0) {
        if (dl < bl)
            return WALK_HIDDEN;
        if (bl <= 0xCB && dl > ((bl + 0x34) & 0xFF))
            return WALK_HIDDEN;
        if (dh < bh)
            return WALK_HIDDEN;
        if (dh > ((bh + 0x2E) & 0xFF))
            return WALK_HIDDEN;
    }
    unsigned al = ((dl - bl) << 2) & 0xFF;
    unsigned row = (dh - bh) & 0xFF;
    return (row * 0x380 + al) & 0xFFFF;
}

unsigned walk_sprite_off_villager(unsigned direction, unsigned frame, unsigned vis)
{
    if (direction == 0x61)
        return (((frame >> 2) + 1) * 0x100 + 0xE80A) & 0xFFFF;
    unsigned off = ((vis - 1) * 0x800) & 0xFFFF;
    unsigned d = direction > 3 ? direction - 4 : direction;
    off = (off + d * 0x200) & 0xFFFF;
    if (frame)
        off = (off + 0x100) & 0xFFFF;
    return off;
}

unsigned walk_sprite_off_party(unsigned direction, unsigned frame, unsigned number)
{
    unsigned f = frame + 1 < 4 ? frame : 1;
    return (direction * 0x300 + f * 0x100 + number * 0xC00) & 0xFFFF;
}

/* 16 x 16 pixels, colour 0 transparent, rows WALK_W apart. Pixels that would fall outside the buffer are dropped
 * (the original would write into whatever follows). */
static void blit16(uint8_t *buf, unsigned pos, const uint8_t *src)
{
    for (unsigned r = 0; r < 16; r++)
        for (unsigned c = 0; c < 16; c++) {
            unsigned p = pos + r * WALK_W + c;
            if (src[r * 16 + c] && p < (unsigned)(WALK_W * WALK_H))
                buf[p] = src[r * 16 + c];
        }
}

void walk_draw_entities(uint8_t *buf, const walk_scene_t *s, unsigned x, unsigned y, unsigned hero_frame)
{
    uint8_t order[WALK_ENTITIES];
    walk_entity_order(s, order);
    for (int i = 0; i < WALK_ENTITIES; i++) {
        unsigned k = order[i], pos;
        const uint8_t *src;
        if (k < WS_VILLAGERS) {
            const uint8_t *r = s->tab_a + k * WS_VILLAGER_SIZE;
            pos = walk_screen_pos(r[0], r[1], x, y, 0, r[0xB]);
            if (pos >= 0xC350)
                continue;
            unsigned off = walk_sprite_off_villager(r[2], r[0xA], r[0xB]);
            if (off >= 0xE90A && off - 0xE90A + 256 <= s->puff_len)
                src = s->puff + (off - 0xE90A);
            else
                src = off < 0x2800 ? s->bank1 + off : s->bank2 + ((off - 0x9470) & 0xFFFF);
        } else {
            const uint8_t *r = s->tab_b + (k - WS_VILLAGERS) * WS_PARTY_SIZE;
            if (r[3] >= 0x64)
                continue;   /* not in the party */
            src = s->bank2 + walk_sprite_off_party(r[2], hero_frame, r[3]);
            pos = walk_screen_pos(r[0], r[1], x, y, 1, 1);
        }
        blit16(buf, pos, src);
    }
}

/* ---- the rain ---- */

void walk_rain_initial(uint16_t drops[WALK_RAIN_DROPS])
{
    uint32_t state = 0;   /* WALK.SL calls srand(0) first thing */
    for (int i = 0; i < WALK_RAIN_DROPS; i++) {
        state = state * 0x343FDu + 0x269EC3u;
        drops[i] = (uint16_t)((state >> 16) & 0x7FFF);
    }
}

void walk_draw_rain(uint8_t *buf, uint16_t drops[WALK_RAIN_DROPS], unsigned stride)
{
    static const uint8_t shortd[3] = {0xAE, 0xAD, 0xAC};
    static const uint8_t longd[6] = {0xAD, 0xAC, 0xAB, 0xAA, 0xA9, 0xA8};
    for (int i = 0; i < WALK_RAIN_DROPS; i++) {
        int is_short = i < 40;
        unsigned n = is_short ? 3 : 6;
        for (unsigned row = 0; row < n; row++) {
            unsigned p = drops[i] + row * 0xE1;
            if (p < (unsigned)(WALK_W * WALK_H))
                buf[p] = is_short ? shortd[row] : longd[row];
        }
        unsigned p = drops[i] + (is_short ? stride >> 1 : stride);
        drops[i] = (uint16_t)(p >= 0xA100 ? p - 0xA100 : p);
    }
}

/* ---- the world in motion ---- */

unsigned walk_rain_stride(int weather, uint32_t ticks)
{
    unsigned t = ticks & 0xFFFF;
    if ((weather != 1 && weather != 2) || t >= 0x5000)
        return 0;
    unsigned n = t >> 10;
    if (n > 10)
        n = 20 - n;
    return (0xE1 * n + 0x384) & 0xFFFF;
}

void walk_animate_ground(uint8_t table[WS_BLOCK_BYTES], uint8_t *gmap)
{
    for (int e = 0; e < 30; e++) {
        uint8_t *t = table + e * 8;
        unsigned bx = w16(t, 0);
        if (bx == 0)
            return;
        unsigned counter = t[3] + 1u;
        if (counter <= t[2]) {
            t[3] = (uint8_t)counter;
            continue;
        }
        t[3] = 0;
        if (bx + 2 > WS_GMAP_BYTES)
            continue;   /* the original would change whatever lies there */
        unsigned cell = w16(gmap, bx);
        cell = cell == w16(t, 6) ? w16(t, 4) : (cell + 1) & 0xFFFF;
        gmap[bx] = (uint8_t)cell;
        gmap[bx + 1] = (uint8_t)(cell >> 8);
    }
}

void walk_animate_objects(uint8_t table[WS_BLOCK_BYTES], uint8_t *omap)
{
    for (int e = 0; e < 40; e++) {
        uint8_t *t = table + e * 6;
        unsigned bx = w16(t, 0);
        if (bx == 0)
            return;
        unsigned counter = (t[3] + 1u) & 0xFF;
        if (counter != t[2]) {
            t[3] = (uint8_t)counter;
            continue;
        }
        t[3] = 0;
        if (bx >= WS_OMAP_BYTES)
            continue;
        unsigned v = (omap[bx] + 1u) & 0xFF;
        omap[bx] = (uint8_t)(v > t[5] ? t[4] : v);
    }
}

void walk_pal_rotate(uint8_t *pal, int first, int n, int forward)
{
    uint8_t keep[3];
    uint8_t *p = pal + 3 * first;
    if (forward) {
        memcpy(keep, p, 3);
        memmove(p, p + 3, 3 * (size_t)(n - 1));
        memcpy(p + 3 * (n - 1), keep, 3);
    } else {
        memcpy(keep, p + 3 * (n - 1), 3);
        memmove(p + 3, p, 3 * (size_t)(n - 1));
        memcpy(p, keep, 3);
    }
}

void walk_pal_ramp(uint8_t *pal)
{
    unsigned red = pal[45], green = pal[46];
    if (red == 1) {
        green = (green + 7) & 0xFF;
        if (green == 0x3F)
            red = 0;
    } else {
        green = (green - 7) & 0xFF;
        if (green <= 1)
            red = 1;
    }
    pal[45] = (uint8_t)red;
    pal[46] = (uint8_t)green;
}

/* The five counters of 0x1c284, each counted up every pass and acted on when it passes its limit. */
static void palette_cycle_on(walk_t *w, screen_t *scr, uint8_t *palbuf);

static void palette_cycle(walk_t *w, screen_t *scr)
{
    palette_cycle_on(w, scr, w->palbuf);
}

/* 0x1c284 on the palette buffer it is given (the message box of a talk gives the dimmed copy); the counters are the same. */
static void palette_cycle_on(walk_t *w, screen_t *scr, uint8_t *palbuf)
{
    static const int limit[5] = {3, 10, 6, 12, 8};   /* data segment 0x532, 0x533, 0x534, 0x535, 0x536 */
    static const int order[5] = {1, 2, 3, 4, 0};     /* the order the original tests them in */
    for (int k = 0; k < 5; k++) {
        int c = order[k];
        if (++w->pal_c[c] <= limit[c])
            continue;
        w->pal_c[c] = 0;
        switch (c) {
        case 1: walk_pal_rotate(palbuf, 1, 3, 1); break;
        case 2: walk_pal_rotate(palbuf, 4, 3, 1); break;
        case 3: walk_pal_rotate(palbuf, 7, 4, 0); break;
        case 4: walk_pal_rotate(palbuf, 11, 4, 0); break;
        default: walk_pal_ramp(palbuf); break;
        }
        for (int i = 0; i < 16; i++)   /* 0x2c85e: 48 bytes to DAC entries 0 .. 15 */
            for (int ch = 0; ch < 3; ch++)
                scr->pal[i][ch] = palbuf[3 * i + ch] & 0x3F;
    }
}

void walk_draw_specks(uint8_t *buf, size_t len, uint16_t pos[WALK_SPECKS])
{
    for (int i = 0; i < WALK_SPECKS; i++) {
        unsigned p = pos[i];
        if (p < len)
            buf[p] = (uint8_t)(buf[p] - 2);
        if (i < 150) {
            p += 0xDF;
            if (p >= 0xA0F0)
                p -= 0x92E0;
        } else {
            p -= 0xDF;
            if (p <= 0xE10)
                p += 0xA0F0;
        }
        pos[i] = (uint16_t)p;
    }
}

/* repne scasw over the list of 100 walkable tile numbers: a hit in the last place leaves the count at 0, which the
 * game takes as "not found". A word outside the map reads as 0 here. */
static int tile_listed(const uint8_t *gmap, unsigned idx, const uint8_t *list)
{
    unsigned word = idx + 2 <= WS_GMAP_BYTES ? w16(gmap, idx) : 0;
    for (int j = 0; j < 99; j++)
        if (w16(list, 2 * (size_t)j) == word)
            return 1;
    return 0;
}

int walk_villager_step(uint8_t rec[WS_VILLAGER_SIZE], const uint8_t *gmap, const uint8_t *list)
{
    if (rec[0xB] == 0 || rec[0xB] >= 6)
        return -1;
    if (rec[2] >= 0x61) {
        rec[0xA]++;
        if (rec[0xA] >= 0x14) {
            rec[2] = 1;
            rec[0xA] = 1;
            return 0xC;
        }
        return 0;   /* 0x2bd15: while the smoke lasts the caller sees 0 (not the timer, which would ask for a direction at 10) */
    }
    rec[9]++;
    if (rec[9] != rec[8])
        return -1;
    rec[9] = 0;
    rec[0xA]++;
    if (rec[0xA] == 2)
        rec[0xA] = 0;
    unsigned x = rec[0], y = rec[1], nx = x, ny = y;
    switch (rec[2]) {
    case 0: if (y > rec[4]) ny--; break;
    case 1: if (y < rec[6]) ny++; break;
    case 2: if (x > rec[3]) nx--; break;
    case 3: if (x < rec[5]) nx++; break;
    default: break;
    }
    unsigned idx = (((ny >> 1) + 1) << 8) | (nx & 0xFE);
    int ok = tile_listed(gmap, idx, list) && tile_listed(gmap, idx + 2, list);
    if (ok && (nx & 1))
        ok = tile_listed(gmap, idx + 4, list);
    if (ok && (ny & 1)) {
        ok = tile_listed(gmap, idx + 0x100, list) && tile_listed(gmap, idx + 0x102, list);
        if (ok && (nx & 1))
            ok = tile_listed(gmap, idx + 0x104, list);
    }
    if (ok) {
        rec[0] = (uint8_t)nx;
        rec[1] = (uint8_t)ny;
    }
    rec[7]++;
    int ret = rec[7];
    if (rec[7] > 0xA)
        rec[7] = 0;
    return ret;
}

int walk_villager_decide(uint8_t rec[WS_VILLAGER_SIZE], uint32_t *rng, uint32_t ticks, unsigned hero_x, unsigned hero_y,
                         unsigned stride)
{
    int draws = 1;
    rec[7] = (uint8_t)(ticks & 7);
    unsigned r = walk_rand(rng);
    unsigned d = 5;
    if (r < 0x2AAA)
        d = rec[0] > hero_x ? 2 : 3;
    if (r > 0x5554)
        d = rec[1] > hero_y ? 0 : 1;
    if (d == 5 || stride != 0 || rec[0x31] == 0) {
        d = walk_rand(rng) / 0x1000;
        draws = 2;
    }
    rec[2] = (uint8_t)d;
    return draws;
}

/* ---- the hero ---- */

int walk_foot_ok(const uint8_t *gmap, const uint8_t *list, unsigned x, unsigned y)
{
    unsigned idx = ((((y >> 1) + 1) & 0xFF) << 8) | (x & 0xFE);
#define LISTED(i) ((i) + 2 <= WS_GMAP_BYTES && tile_listed(gmap, (i), list))
    if (!LISTED(idx) || !LISTED(idx + 2))
        return 0;
    if ((x & 1) && !LISTED(idx + 4))
        return 0;
    if (y & 1) {
        if (!LISTED(idx + 0x100) || !LISTED(idx + 0x102))
            return 0;
        if ((x & 1) && !LISTED(idx + 0x104))
            return 0;
    }
#undef LISTED
    return 1;
}

void walk_trail_step(uint8_t trail[WS_TRAIL_BYTES], uint8_t *party, unsigned x, unsigned y)
{
    memmove(trail + 3, trail, 12 * 3);       /* entry k + 1 gets entry k, for k = 11 down to 0 */
    trail[0] = (uint8_t)x;
    trail[1] = (uint8_t)y;
    trail[2] = party[2];
    party[0] = (uint8_t)x;
    party[1] = (uint8_t)y;
    static const int entry[4] = {3, 6, 9, 12};
    for (int i = 1; i <= 4; i++)
        memcpy(party + WS_PARTY_SIZE * i, trail + 3 * entry[i - 1], 3);
}

unsigned walk_find_record(const uint8_t *tab_a, unsigned x, unsigned y)
{
    for (unsigned bx = 0; bx < 0x2EE; bx += WS_RECORD_SIZE) {
        const uint8_t *r = tab_a + 1000 + bx;
        if (r[0] == (x & 0xFE) && r[1] == (y >> 1)) {
            if (r[2] == 5 && ((x & 1) || (y & 1)))
                return 0x320;
            return bx;
        }
    }
    return 0x2EE;
}

static void note_unbuilt(walk_t *w, const char *what, unsigned a)
{
    snprintf(w->unbuilt, sizeof w->unbuilt, "%s (scene record %u)", what, a);
    w->unbuilt_new = 1;
}

const char *walk_unbuilt(const walk_t *w)
{
    return w->unbuilt[0] ? w->unbuilt : NULL;
}

/* (0x1b634) The roof of a house: the rectangle (x0, y0) .. (x1, y1) of the object map, bytes 4 to 7 of a kind 3 record. Going in, it is saved in `alt`
 * after the flag byte and cleared to 0xff, so the roof disappears; coming out it is put back. Returns 0, or -1 if the rectangle is not
 * inside the map (the original would then change whatever follows it). */
int walk_roof(uint8_t *omap, uint8_t *alt, const uint8_t *rec, int enter)
{
    unsigned x0 = rec[4], y0 = rec[5], x1 = rec[6], y1 = rec[7];
    int wd = (int)x1 - (int)x0 + 1;
    if (y1 < y0)
        return 0;
    if (wd <= 0 || x1 >= WS_MAP_W || y1 >= 100 || 1 + (size_t)wd * (y1 - y0 + 1) > WALK_ALT_LEN)
        return -1;
    unsigned off = x0 + (y0 << 7);
    size_t idx = 1;
    for (unsigned y = y0; y <= y1; y++) {
        if (enter) {
            memcpy(alt + idx, omap + off, (size_t)wd);
            memset(omap + off, 0xFF, (size_t)wd);
        } else {
            memcpy(omap + off, alt + idx, (size_t)wd);
        }
        off += 0x80;
        idx += (size_t)wd;
    }
    return 0;
}

/* The hero stands on scene record `si` (0x172f0 .. 0x18b82). Kind 3 (a house's roof): going up through the door takes the roof off
 * and changes the weather mode by 3 (which stops the rain), coming down puts it back with a bump; the kinds the block names are
 * not built; then the record loop runs again on the live state. */
static void records_again(walk_t *w, unsigned which);

static void hit_record(walk_t *w, unsigned si)
{
    walk_scene_t *s = w->scene;
    const uint8_t *rec = s->tab_a + 1000 + si;
    unsigned kind = rec[2], d = s->tab_b[2];
    if (kind == 3) {
        if (d == 0 && rec[3] == 0 && w->alt[0] == 0) {
            if (walk_roof(s->omap, w->alt, rec, 1) != 0)
                note_unbuilt(w, "a roof outside the map", si / WS_RECORD_SIZE);
            w->alt[0] = 1;
            s->weather += 3;
            walk_sfx_play(w, SFX_EF, 0x127D);   /* 0x17b9e */
        }
        if (d == 1 && rec[3] == 1 && w->alt[0] != 0) {
            if (walk_roof(s->omap, w->alt, rec, 0) != 0)
                note_unbuilt(w, "a roof outside the map", si / WS_RECORD_SIZE);
            w->alt[0] = 0;
            w->v48 = 2;
            s->weather -= 3;
            walk_sfx_play(w, SFX_EF, 0x127D);   /* 0x17c2a */
        }
    } else if (kind == 0 || kind == 1 || kind == 4 || kind == 5 || kind == 7 || kind == 8 || kind == 0xE ||
               (kind >= 0x11 && kind <= 0x14)) {
        char what[48];
        snprintf(what, sizeof what, "scene record kind 0x%x", kind);
        note_unbuilt(w, what, si / WS_RECORD_SIZE);
    }
    w->vbe = kind;
    records_again(w, si / WS_RECORD_SIZE);
}

/* 0x18b82: the end of a pass that went through 0x172f0 (a scene record, or an F-key's action from 0x1729e): the record loop runs again
 * on the live state while [bp-0xbe] is not 0. After a talk this applies what its flags changed (a member who joins in scene 43: the
 * picture of him in the object map goes). */
static void records_again(walk_t *w, unsigned which)
{
    walk_scene_t *s = w->scene;
    if (!w->vbe)
        return;
    int events = s->events;
    if (walk_scene_apply_records(s) != 0)
        note_unbuilt(w, "the record loop failed", which);
    w->use_count = 0;   /* [bp-0xc4], which the loop writes (0x11409, 0x11b07, ...): 0 after it in every start probed (INFERRED) */
    if (s->events != events && w->scr) {
        /* 0x11b2b .. 0x11c08, for each kind 0x10 record that fires: the world drawn again (0x19680, the party standing), its window
         * grabbed into the map buffer (0x2725d), then 50 rounds of the window filled with colour 0x10 and the grab put back over it.
         * The rounds do not wait, so only the last is seen: the grab. */
        walk_zoom_keep(w);
        for (int r = 0; r < WALK_ZOOM_H; r++)
            memcpy(w->scr->pix + 0x1440 + r * SCREEN_W, w->zoom_grab + r * WALK_ZOOM_W, WALK_ZOOM_W);
    }
}

unsigned walk_shake_offset(unsigned v48, unsigned r)
{
    unsigned t = (v48 * 4) & 0xFF;
    unsigned d = (0x7FFFu / t + 1) & 0xFFFF;
    unsigned a = r / d;
    if (a > v48)
        a = ((a - v48) * 0xE0) & 0xFFFF;
    return a;
}

/* 0x1252c: after a bump the window is copied from a little way into the buffer: 0 to 2 pixels, or 1 to 5 rows. */
static unsigned shake(walk_t *w)
{
    unsigned a = 0;
    if (w->v48) {
        unsigned t = (w->v48 * 4) & 0xFF;
        w->v96 = t ? (0x7FFFu / t + 1) & 0xFFFF : w->v96;   /* 0x1255a */
        a = walk_shake_offset(w->v48, walk_rand(&w->rng));
        if (w->v48 > w->scene->header[3])   /* 0x1259b: never below the map header's byte 3 (a scene that keeps shaking) */
            w->v48--;
    }
    return a;
}

/* ---- shots and sparks ---- */

void walk_shots_move(uint8_t fx[WALK_FX_BYTES])
{
    for (int i = 0; i < WALK_SHOTS; i++) {   /* 0x2bac3: all five, used or not; a shot zigzags across its line */
        uint8_t *s = fx + 4 * i;
        uint8_t *along, *across;
        int step;
        switch (s[2]) {
        case 1: along = &s[1]; step = 2; across = &s[0]; break;
        case 2: along = &s[0]; step = -2; across = &s[1]; break;
        case 3: along = &s[0]; step = 2; across = &s[1]; break;
        default: along = &s[1]; step = -2; across = &s[0]; break;
        }
        *along = (uint8_t)(*along + step);
        if (s[3] == 1) {
            *across = (uint8_t)(*across + 2);
            s[3] = 0;
        } else {
            *across = (uint8_t)(*across - 2);
            s[3] = 1;
        }
    }
}

int walk_shot_hit(const uint8_t shot[4], uint8_t *tab_a)
{
    for (int k = 0; k < WS_VILLAGERS; k++) {   /* 0x2bb5e */
        uint8_t *r = tab_a + k * WS_VILLAGER_SIZE;
        if (r[2] == 0x61 || r[0x31] == 0)
            continue;
        uint8_t dl = r[0] & 0xFE, dh = r[1] & 0xFE, dh1 = (uint8_t)(dh + 1), dl2 = (uint8_t)(dl + 2);
        uint8_t sx = shot[0], sy = shot[1];
        if (!((sx == dl && sy == dh) || (sx == dl && sy == dh1) || (sx == dl2 && sy == dh1) || (sx == dl2 && sy == dh)))
            continue;
        if (r[0xB] != 6) {
            r[0x28]++;
            if (r[0x28] >= (uint8_t)(r[0xB] << 1)) {   /* enough hits: the enemy goes up in smoke */
                r[0x28] = 0;
                r[2] = 0x61;
                r[0xA] = 0;
                return 2;
            }
        }
        r[2] = shot[2];   /* a hit that does not destroy turns the enemy the way the shot flew */
        return 1;
    }
    return 0;
}

void walk_draw_gold(screen_t *scr, unsigned value, const uint8_t digits[350])
{
    /* 0x1bd6e: a 40 x 5 box of colour 0 at (273, 185), then the number right-aligned in 7 x 5 digits 8 pixels apart */
    unsigned at = 0xE851, div = 10000;
    if (value < 10000) { at = 0xE855; div = 1000; }
    if (value < 1000) { at = 0xE859; div = 100; }
    if (value < 100) { at = 0xE85D; div = 10; }
    if (value < 10) { at = 0xE861; div = 1; }
    for (int r = 0; r < 5; r++)
        memset(scr->pix + 0xE851 + r * SCREEN_W, 0, 40);
    while (div) {
        unsigned d = (value / div) & 0xFF;
        value -= d * div;
        div /= 10;
        for (int r = 0; r < 5; r++)
            if (at + r * SCREEN_W + 7 <= sizeof scr->pix && d < 10)
                memcpy(scr->pix + at + r * SCREEN_W, digits + d * 35 + r * 7, 7);
        at += 8;
    }
}

/* A sprite with a 4-byte header (width, height as words), colour 0 transparent (0x26ad2). */
static void blit_hdr(uint8_t *buf, unsigned pos, const uint8_t *spr)
{
    unsigned wd = w16(spr, 0), ht = w16(spr, 2);
    for (unsigned r = 0; r < ht; r++)
        for (unsigned c = 0; c < wd; c++) {
            unsigned p = pos + r * WALK_W + c;
            uint8_t v = spr[4 + r * wd + c];
            if (v && p < (unsigned)(WALK_W * WALK_H))
                buf[p] = v;
        }
}

static void blit8(uint8_t *buf, unsigned pos, const uint8_t *src)
{
    for (unsigned r = 0; r < 8; r++)
        for (unsigned c = 0; c < 8; c++) {
            unsigned p = pos + r * WALK_W + c;
            if (src[r * 8 + c] && p < (unsigned)(WALK_W * WALK_H))
                buf[p] = src[r * 8 + c];
        }
}

/* 0x1208f .. 0x122d7: move the shots, draw them, and test what each has reached. */
static void shots_pass(walk_t *w, screen_t *scr, unsigned x, unsigned y)
{
    walk_scene_t *s = w->scene;
    walk_shots_move(w->fx);
    for (int i = 0; i < WALK_SHOTS; i++) {
        uint8_t *sh = w->fx + 4 * i;
        if (sh[2] >= 4)
            continue;
        unsigned pos = walk_screen_pos(sh[0], sh[1], x, y, 0, 1);
        if (pos >= 0xC350) {
            sh[2] = 0xA;
            continue;
        }
        blit_hdr(w->buf, pos, w->art + ART_SHOT);
        int hit = walk_shot_hit(sh, s->tab_a);
        unsigned idx = ((unsigned)(sh[1] >> 1) << 8) | sh[0];   /* 0x2ba52: the tile under the shot (one row above a foot test) */
        int open = tile_listed(s->gmap, idx, s->list);
        if (!open || hit == 1) {
            for (int j = 0; j < WALK_SPARKS; j++) {
                uint8_t *sp = w->fx + 4 * WALK_SHOTS + 4 * j;
                if (sp[0] > 4) {
                    memcpy(sp + 1, sh, 3);
                    sp[0] = 0;
                    w->v48 = 4;
                    break;
                }
            }
            sh[2] = 0xA;
        }
        if (hit == 2) {   /* destroyed: one more coin, shown at once, and SND_EF.1's first sound (0x122a7) */
            unsigned g = w16(s->tab_b, WALK_GOLD_AT) + 1;
            if (g > 60000)
                g = 60000;
            w->v96 = g;   /* 0x12249 .. 0x1226e */
            s->tab_b[WALK_GOLD_AT] = (uint8_t)g;
            s->tab_b[WALK_GOLD_AT + 1] = (uint8_t)(g >> 8);
            walk_draw_gold(scr, g, w->art + ART_DIGITS);
            walk_sfx_play(w, SFX_EF, 0);
            sh[2] = 0xA;
        }
    }
}

/* 0x12314 .. 0x12464: the sparks, after the objects. */
static void sparks_pass(walk_t *w, unsigned x, unsigned y)
{
    for (int j = 0; j < WALK_SPARKS; j++) {
        uint8_t *sp = w->fx + 4 * WALK_SHOTS + 4 * j;
        if (sp[0] >= 2) {   /* 0x2ba82: from the third frame on it drifts one step */
            switch (sp[3]) {
            case 0: sp[2]--; break;
            case 1: sp[2]++; break;
            case 2: sp[1]--; break;
            default: sp[1]++; break;
            }
        }
        unsigned f = sp[0];
        unsigned pos = walk_screen_pos(sp[1], sp[2], x, y, 0, 1);
        if (f < 5 && pos < 0xC350) {
            w->v96 = f << 6;   /* 0x123c3 */
            blit8(w->buf, pos, w->art + ART_SPARKS + f * 64);
            sp[0]++;
            if (f == 2 && w->sb == 0x81 && !walk_sfx_busy(w))   /* 0x12404: frame 2 sounds, unless a sound still plays */
                walk_sfx_play(w, SFX_EF, 0x5F80);
        } else {
            sp[0] = 0xA;
        }
    }
}

/* 0x1424c: Space. Each party member that can shoot (word at +8 not 0) and whose shot slot (by member number) is free fires
 * the way he faces; the followers then face a random way. */
static void space(walk_t *w)
{
    uint8_t *tb = w->scene->tab_b;
    for (int si = 0; si < 5; si++) {
        uint8_t *p = tb + WS_PARTY_SIZE * si;
        unsigned n = p[3];
        if (n > 0x64)
            break;
        if (w16(p, 8) == 0)
            continue;
        if (4 * n + 4 > WALK_FX_BYTES) {
            note_unbuilt(w, "a shot slot outside the shot list", n);
            continue;
        }
        uint8_t *sh = w->fx + 4 * n;
        if (sh[2] <= 3)
            continue;
        memcpy(sh, p, 3);
        sh[3] = (uint8_t)(walk_rand(&w->rng) / 0x4000);
        sh[0] &= 0xFE;
        sh[1] &= 0xFE;
        if (p[2] == 0)
            sh[1] += 2;
        if (sh[3] == 0) {
            if (sh[2] < 2)
                sh[0] += 2;
            else
                sh[1] += 2;
        }
        if (si != 0)
            p[2] = (uint8_t)(walk_rand(&w->rng) / 0x2000);
    }
}

/* 0x2c119: an enemy can be put at (x, y / 2 = ry) when the two cells of row ry + 1 under it are walkable. */
static int open_place(const walk_scene_t *s, unsigned rx, unsigned ry)
{
    unsigned idx = (((ry + 1) & 0xFF) << 8) | rx;
    return tile_listed(s->gmap, idx, s->list) && tile_listed(s->gmap, idx + 2, s->list);
}

/* 0x13356: an enemy's smoke has cleared; it comes back at a random open place outside the view. The original keeps the
 * bounds in the hero's walk counter and frame ([bp-0x3c], [bp-0x5e]), which keep those values. */
static void respawn(walk_t *w, uint8_t *rec, unsigned hx, unsigned hy)
{
    walk_scene_t *s = w->scene;
    unsigned lo_x = (hx - 0x16) & 0xFF, hi_x = (hx + 0x30) & 0xFF;
    unsigned lo_y = ((hy >> 1) - 10) & 0xFF, hi_y = (lo_y + 0x15) & 0xFF;
    w->ctr = hi_x;
    w->hero_frame = hi_y;
    for (long tries = 0; tries < 1000000; tries++) {
        unsigned rx = ((((unsigned)walk_rand(&w->rng) / 0x13C) + 0xC) & 0xFF) * 2 & 0xFF;
        unsigned ry = (((unsigned)walk_rand(&w->rng) / 0x19A) + 0xA) & 0xFF;
        if (rx >= lo_x && rx <= hi_x && ry >= lo_y && ry <= hi_y)
            continue;
        int ok = open_place(s, rx, ry);
        ry = (ry * 2) & 0xFF;
        if (ok) {
            rec[0] = (uint8_t)rx;
            rec[1] = (uint8_t)ry;
            return;
        }
    }
    note_unbuilt(w, "no open place for an enemy to come back", 0);
}

/* ---- F1: talking (0x14d62 .. 0x153ca, 0x19b9a) ---- */

/* 0x19680, as the F-keys call it: the buffer's first bytes 1, x, y and the map header's byte 5, then the world drawn into the buffer
 * with every villager and party member (the party standing, frame 1) and the objects; nothing moves, no rain, no shots, no pointer. */
static void fkey_frame(walk_t *w)
{
    walk_scene_t *s = w->scene;
    unsigned x = s->tab_b[0], y = s->tab_b[1];
    w->buf[0] = 1;
    w->buf[1] = (uint8_t)x;
    w->buf[2] = (uint8_t)y;
    w->buf[3] = s->header[5];
    walk_draw_ground(w->buf, s, x, y);
    walk_draw_entities(w->buf, s, x, y, 1);
    walk_draw_objects(w->buf, s, x, y);
    if (s->header[5]) {   /* 0x19985: doubled (not darkened) into the scratch buffer, and its first 0x5080 words back over the world */
        walk_double(w->dbuf, w->buf, w->table);
        memcpy(w->buf, w->dbuf, 0x5080 * 2);
    }
}

/* 0x25dca: the buffer from row 16 (0xe00) on, 0x9300 bytes, through the shading table */
static void fkey_shade(walk_t *w)
{
    for (size_t i = 0xE00; i < 0xE00 + 0x9300 && i < WALK_BUF_LEN; i++)
        w->buf[i] = w->table[w->buf[i]];
}

/* 0x1c3ba: a button pressed or let go: the w x h picture at `at` turned round (its light and dark edges change places), the inside
 * (w - 4) x (h - 4) from two rows and columns in put back where it was. */
void walk_button_toggle(screen_t *scr, unsigned at, unsigned w, unsigned h)
{
    uint8_t outer[0x2F * 0x14], inner[0x2F * 0x14];
    if (w * h > sizeof outer)
        return;
    for (unsigned r = 0; r < h; r++)
        for (unsigned c = 0; c < w; c++) {
            unsigned o = at + r * SCREEN_W + c;
            outer[(h - 1 - r) * w + (w - 1 - c)] = o < SCREEN_W * SCREEN_H ? scr->pix[o] : 0;
        }
    for (unsigned r = 0; r < h - 4; r++)
        for (unsigned c = 0; c < w - 4; c++) {
            unsigned o = at + 0x282 + r * SCREEN_W + c;
            inner[r * (w - 4) + c] = o < SCREEN_W * SCREEN_H ? scr->pix[o] : 0;
        }
    for (unsigned r = 0; r < h - 4; r++)
        memcpy(outer + (r + 2) * w + 2, inner + r * (w - 4), w - 4);
    for (unsigned r = 0; r < h; r++)
        for (unsigned c = 0; c < w; c++) {
            unsigned o = at + r * SCREEN_W + c;
            if (o < SCREEN_W * SCREEN_H)
                scr->pix[o] = outer[r * w + c];
        }
}

static void grab(const screen_t *scr, unsigned at, unsigned bw, unsigned bh, uint8_t *out)
{
    for (unsigned r = 0; r < bh; r++)
        for (unsigned c = 0; c < bw; c++) {
            unsigned o = at + r * SCREEN_W + c;
            out[r * bw + c] = o < SCREEN_W * SCREEN_H ? scr->pix[o] : 0;
        }
}

static void put(screen_t *scr, unsigned at, const uint8_t *px, unsigned bw, unsigned bh)
{
    for (unsigned r = 0; r < bh; r++)
        for (unsigned c = 0; c < bw; c++) {
            unsigned o = at + r * SCREEN_W + c;
            if (o < SCREEN_W * SCREEN_H)
                scr->pix[o] = px[r * bw + c];
        }
}

static void fill(screen_t *scr, unsigned at, unsigned bw, unsigned bh, uint8_t c)
{
    for (unsigned r = 0; r < bh; r++)
        for (unsigned x = 0; x < bw; x++)
            if (at + r * SCREEN_W + x < SCREEN_W * SCREEN_H)
                scr->pix[at + r * SCREEN_W + x] = c;
}

/* 0x153a0 .. 0x153ca (and after a talk 0x152b4 .. 0x15387): the button let go; the pointer's corner kept again. */
static void talk_done(walk_t *w, screen_t *scr, int talked)
{
    if (talked) {
        walk_show_gold(w, scr);   /* 0x1bd6e */
        if (w->mouse_on) {
            w->mprev = 0;
            walk_mouse_grab(scr, 0, w->mbg);
        }
    }
    walk_button_toggle(scr, 0x284, 0x2F, 0x14);
    if (w->mouse_on)   /* only kept: the next pass's pointer block draws it */
        walk_mouse_grab(scr, w->mprev, w->mbg);
    w->talking = 0;
    records_again(w, 0x384 / WS_RECORD_SIZE);   /* 0x1729e: si = 0x384, no record */
}

static void talk_step(void *ctx, screen_t *scr)
{
    walk_t *w = ctx;
    palette_cycle_on(w, scr, w->dimpal);
}

/* Opens the box of message w->talk_msg (colour 0x50, the wait flag); 0 when it waits. */
static int talk_open(walk_t *w, screen_t *scr)
{
    int r = msgbox_open(&w->box, scr, w->font8, w->table, w->buf + 0x7305, w->talk_msg, 0x50, 1);
    if (r < 0) {
        note_unbuilt(w, "a message that MES_TXT.LCC does not have", w->talk_msg);
        return -1;
    }
    if (r > 0)
        note_unbuilt(w, "a character the message font does not have", w->talk_msg);
    w->talking = 1;
    return 0;
}

static void yesno_open(walk_t *w, screen_t *scr);
static void ask_open(walk_t *w, screen_t *scr);
static void look_round(walk_t *w, screen_t *scr, unsigned port60);
static void take_round(walk_t *w, screen_t *scr, unsigned port60);
static void take_box_round(walk_t *w, screen_t *scr, unsigned port60);
static void cast_round(walk_t *w, screen_t *scr, unsigned port60);
static void use_answer(walk_t *w, screen_t *scr, int yes);
static void use_coins_given(walk_t *w, screen_t *scr, unsigned value);
static void use_round(walk_t *w, screen_t *scr, unsigned port60);
static void use_done(walk_t *w, screen_t *scr);

/* The palette the yes / no buttons and the number step on: the dimmed copy in a talk, the palette itself in F7. */
static void ask_step(walk_t *w, screen_t *scr)
{
    if (w->using)
        palette_cycle(w, scr);
    else
        palette_cycle_on(w, scr, w->dimpal);
}

/* 0x19b9a: the talk itself. The world window doubled (0x2c658, as at a battle's start, then shown); which of the villager's five entries
 * (7 bytes from 0xc: a flag that must be set, the flag it sets or a member who joins, a kind, the first message) is the last one whose
 * flag is set (the first one whatever its flag); the joining or the flag; then its messages, one box each, as long as a text ends in 0x7f. */
static void talk_begin(walk_t *w, screen_t *scr, uint8_t *r)
{
    walk_scene_t *s = w->scene;
    uint8_t *party = s->tab_b;
    if (w->mouse_on)   /* 0x152b4 */
        walk_mouse_put(scr, w->mprev, w->mbg, 0);
    for (int y = 0; y < 84; y++)
        for (int x = 0; x < 96; x++) {
            uint8_t p = w->buf[0x3220 + y * WALK_W + x], q = w->table[p];
            uint8_t *d = scr->pix + (16 + 2 * y) * SCREEN_W + 64 + 2 * x;
            d[0] = p;
            d[1] = q;
            d[SCREEN_W] = q;
            d[SCREEN_W + 1] = w->table[q];
        }
    unsigned k = 0xC;
    while (k < 0x2F && w16(r, k + 5) != 0 && walk_flag_test(party, r[k], r[k + 1]))
        k += 7;
    if (k > 0xC)
        k -= 7;
    unsigned e = r[k + 2];
    if (e > 0xC8) {   /* a member joins: he takes the first free place of the party, and the villager is gone */
        r[0] = r[1] = 0;
        unsigned o = 0, q = 0;
        while (o < 0xC8 && party[o + 3] != e)
            o += 0x28;
        while (q < 0xC8 && party[q + 3] <= 5)
            q += 0x28;
        for (int i = 0; i < 0x28; i++) {   /* 0x25c8e swaps the two records */
            uint8_t t = party[o + i];
            party[o + i] = party[q + i];
            party[q + i] = t;
        }
        party[q + 3] = (uint8_t)(e - 0xC8);
        walk_flag_set(party, r[0x28], r[0x29]);
    } else if (r[k + 4] != 0xFF) {
        walk_flag_set(party, r[k + 2], r[k + 3]);
    }
    w->talk_rec = r;
    w->talk_k = k;
    w->talk_msg = w16(r, k + 5);
    if (r[0x30] == 0 && r[k + 4] != 0xFF) {   /* the wait flag: one box after the other */
        if (talk_open(w, scr) != 0)
            talk_done(w, scr, 1);
        return;
    }
    /* a villager who sells or asks: his boxes do not wait but stay, all drawn at once, over the screen's bottom kept before them */
    grab(scr, 0xA780, SCREEN_W, 46, w->talk_under);
    for (;;) {
        int rr = msgbox_open(&w->box, scr, w->font8, w->table, w->buf + 0x7305, w->talk_msg, 0x50, 0);
        if (rr < 0) {
            note_unbuilt(w, "a message that MES_TXT.LCC does not have", w->talk_msg);
            break;
        }
        if (rr > 0)
            note_unbuilt(w, "a character the message font does not have", w->talk_msg);
        if (w->box.last != 0x7F)
            break;
        w->talk_msg++;
    }
    if (r[k + 4] == 0xFF)
        ask_open(w, scr);
    else
        yesno_open(w, scr);
}

/* The two loops' pointer: at 0, what is there kept (not drawn until it moves). */
static void talk_mouse_start(walk_t *w, screen_t *scr)
{
    if (!w->mouse_on)
        return;
    w->talk_mprev = 0;
    walk_mouse_grab(scr, 0, w->talk_mbg);
}

/* The two loops' pointer moved to where the driver has it; its place and buttons in *x, *y, *b. */
static void talk_mouse_move(walk_t *w, screen_t *scr, int *x, int *y, unsigned *b)
{
    walk_mouse(w, x, y, b);
    unsigned pos = (unsigned)(*y * SCREEN_W + *x);
    if (pos != w->talk_mprev) {
        walk_mouse_put(scr, w->talk_mprev, w->talk_mbg, 0);
        walk_mouse_grab(scr, pos, w->talk_mbg);
        walk_mouse_put(scr, pos, w->mpic, 1);
        w->talk_mprev = pos;
    }
}

/* The end of a talk whose boxes did not wait: the screen's bottom put back (0x27328(0xa780, [bp+6] + 0x9c40)). */
static void talk_close(walk_t *w, screen_t *scr)
{
    put(scr, 0xA780, w->talk_under, SCREEN_W, 46);
    talk_done(w, scr, 1);
}

/* 0x18c36: the buttons "yes" and "no" at (269, 38), 47 x 20, the left half pressed for yes and the right half for no (0x1c3ba on each
 * 23 x 20 half). Its palette count starts from stack bytes the message box left: the high word of the text's file offset. */
static void yesno_open(walk_t *w, screen_t *scr)
{
    grab(scr, 0x308D, 47, 20, w->yn_pic[0]);
    walk_button_toggle(scr, 0x308D, 0x17, 0x14);
    grab(scr, 0x308D, 47, 20, w->yn_pic[1]);
    walk_button_toggle(scr, 0x308D, 0x17, 0x14);
    walk_button_toggle(scr, 0x30A5, 0x17, 0x14);
    grab(scr, 0x308D, 47, 20, w->yn_pic[2]);
    talk_mouse_start(w, scr);
    w->yn_shown = 2;
    w->yn_sel = 1;
    w->talk_count = w->box.hi8;
    w->talking = 2;
}

/* 0x19ee0 .. 0x1a02a: yes: the price (word 0x22) taken from the coins and the item (byte 0x21) put in the first empty one of the 20
 * item places (0x14 .. 0x27) of the first member with HP left; the members are looked at up to the first who is not in the party
 * (number > 4). Not enough coins or no place: nothing happens, and nothing says so. */
static void talk_buy(walk_t *w)
{
    uint8_t *r = w->talk_rec, *party = w->scene->tab_b;
    unsigned price = w16(r, 0x22), gold = w16(party, WALK_GOLD_AT);
    if (gold < price)
        return;
    for (unsigned i = 0; i < 0xC8; i += 0x28) {
        if (party[i + 3] > 4)
            return;
        if (w16(party, i + 8) == 0)
            continue;
        for (unsigned j = i + 0x14; j < i + 0x28; j++)
            if (party[j] == 0) {
                gold -= price;
                party[WALK_GOLD_AT] = (uint8_t)gold;
                party[WALK_GOLD_AT + 1] = (uint8_t)(gold >> 8);
                party[j] = r[0x21];
                return;
            }
    }
}

/* A round of 0x18d23: the palette step every 26th; the pressed half drawn again when the choice changed; the pointer (over the buttons it
 * chooses, and the left button takes the choice); Left yes, Right no, Enter or Space takes it. */
static void yesno_round(walk_t *w, screen_t *scr, unsigned port60)
{
    w->talk_count = (w->talk_count + 1) & 0xFF;
    if (w->talk_count > 0x19) {
        w->talk_count = 0;
        ask_step(w, scr);
    }
    if (w->yn_shown != w->yn_sel) {
        if (w->mouse_on)
            walk_mouse_put(scr, w->talk_mprev, w->talk_mbg, 0);
        if (w->yn_sel == 1)
            put(scr, 0x308D, w->yn_pic[1], 47, 20);
        if (w->yn_sel == 0)
            put(scr, 0x308D, w->yn_pic[2], 47, 20);
        if (w->mouse_on) {
            walk_mouse_grab(scr, w->talk_mprev, w->talk_mbg);
            walk_mouse_put(scr, w->talk_mprev, w->mpic, 1);
        }
        w->yn_shown = w->yn_sel;
    }
    int done = 0;
    if (w->mouse_on) {
        int mx, my;
        unsigned mb;
        talk_mouse_move(w, scr, &mx, &my, &mb);
        if (my > 0x25 && my < 0x3A && mx > 0x10D && mx < 0x13C) {
            w->yn_sel = mx < 0x124 ? 1 : 0;
            done = mb == 1;
        }
    }
    if (!done) {
        unsigned k = port60 & 0xFF;
        if (k == 0x4B)
            w->yn_sel = 1;
        if (k == 0x4D)
            w->yn_sel = 0;
        if (k != 0x1C && k != 0x39)
            return;
    }
    if (w->mouse_on)
        walk_mouse_put(scr, w->talk_mprev, w->talk_mbg, 0);
    put(scr, 0x308D, w->yn_pic[0], 47, 20);
    if (w->using) {
        use_answer(w, scr, w->yn_sel);
        return;
    }
    if (w->yn_sel)
        talk_buy(w);
    talk_close(w, scr);
}

/* 0x18fdc: a five-digit number to set, in a 94 x 16 box at the top of the play window (113, 0): RPG.LCP 0x1643b (0x4be bytes, decoded by
 * 0x25eeb into the world buffer). The box comes in over 128 rounds of two 0x27670 (each pixel one colour index nearer the picture's)
 * in the same pass, so it is the picture when the loop starts. A mark of colour 9 above and below the digit being set. Its palette
 * count and key latch start from stack bytes: the count from 0x19680's arguments (the low byte of a buffer's offset, 0x16, SEEN), the
 * latch from the message box's last character (never 0). */
static void ask_open(walk_t *w, screen_t *scr)
{
    size_t len = 0;
    uint8_t *art = battle_read_unpack("RPG.LCP", 0x1643B, 0x4BE, &len);
    if (!art || len < sizeof w->ask_art) {
        free(art);
        note_unbuilt(w, "the number box's picture (RPG.LCP 0x1643b) missing or too short", 0);
        if (w->using)
            use_done(w, scr);
        else
            talk_close(w, scr);
        return;
    }
    memcpy(w->ask_art, art, sizeof w->ask_art);
    free(art);
    grab(scr, 0x71, 94, 16, w->ask_under);
    put(scr, 0x71, w->ask_art + 0x284, 94, 16);
    w->ask_value = 0;
    w->ask_pos = 1;
    fill(scr, 0x46E, 8, 1, 9);
    fill(scr, 0xFAE, 8, 1, 9);
    talk_mouse_start(w, scr);
    w->talk_count = w->using ? WALK_USE_ASK_COUNT : 0x16;
    w->ask_latch = 1;
    w->talking = 3;
}

/* The marks above and below the digit being set (5 - pos digits from the left). */
static void ask_marks(walk_t *w, screen_t *scr)
{
    unsigned at = 0x44A + (5 - w->ask_pos) * 9;
    fill(scr, 0x44A, 0x2C, 1, 0);
    fill(scr, at, 8, 1, 9);
    fill(scr, 0xF8A, 0x2C, 1, 0);
    fill(scr, at + 0xB40, 8, 1, 9);
}

/* A round of 0x19166: the palette step every 23rd; the five digits drawn (8 x 8 at (138 + 9 i, 4)); a key after a release: Up adds
 * the digit's unit (at most 50,000), Down takes it away (when there is so much), Left moves to the next digit up, Right down (round),
 * Enter takes the number. The pointer: left button over the box's ends Up and Down, over its middle Enter; the right button Left. */
static void ask_round(walk_t *w, screen_t *scr, unsigned port60)
{
    static const unsigned unit[6] = {0, 1, 10, 100, 1000, 10000};
    w->talk_count = (w->talk_count + 1) & 0xFF;
    if (w->talk_count > 0x16) {
        w->talk_count = 0;
        ask_step(w, scr);
    }
    char d[8];
    snprintf(d, sizeof d, "%05u", w->ask_value % 100000u);   /* 0x26450 */
    for (unsigned i = 0; i < 5; i++)
        put(scr, 0x58A + 9 * i, w->ask_art + (unsigned)(d[i] - '0') * 64, 8, 8);
    unsigned key = port60 & 0xFF;
    int done = 0;
    if (w->mouse_on) {
        int mx, my;
        unsigned mb;
        talk_mouse_move(w, scr, &mx, &my, &mb);
        if (my < 0x10 && mb == 1) {
            if (mx > 0x71 && mx < 0x83)
                key = 0x48;
            if (mx > 0xBD && mx < 0xCF)
                key = 0x50;
            done = mx > 0x86 && mx < 0xB9;
        }
        if (!done && mb == 2)
            key = 0x4B;
    }
    if (!done) {
        if (key > 0x7E)
            w->ask_latch = 1;
        if (key >= 0x7F || !w->ask_latch)
            return;
        w->ask_latch = 0;
        if (key == 0x48) {
            w->ask_value += unit[w->ask_pos];
            if (w->ask_value > 50000)
                w->ask_value = 50000;
        }
        if (key == 0x50 && w->ask_value >= unit[w->ask_pos])
            w->ask_value -= unit[w->ask_pos];
        if (key == 0x4B) {
            w->ask_pos = w->ask_pos >= 5 ? 1 : w->ask_pos + 1;
            ask_marks(w, scr);
        }
        if (key == 0x4D) {
            w->ask_pos = w->ask_pos <= 1 ? 5 : w->ask_pos - 1;
            ask_marks(w, scr);
        }
        if (key != 0x1C)
            return;
    }
    if (w->mouse_on)
        walk_mouse_put(scr, w->talk_mprev, w->talk_mbg, 0);
    put(scr, 0x71, w->ask_under, 94, 16);
    if (w->using) {
        use_coins_given(w, scr, w->ask_value);
        return;
    }
    /* 0x1a073: the right number is the word at 0x28 + 2 x the entry's place; it sets the entry's flag and plays SND_EF.1's 0x1bf5
     * (0x1a0fd) */
    uint8_t *r = w->talk_rec;
    if (w->ask_value == w16(r, 0x28 + (w->talk_k - 0xC) / 7 * 2)) {
        walk_flag_set(w->scene->tab_b, r[w->talk_k + 2], r[w->talk_k + 3]);
        walk_sfx_play(w, SFX_EF, 0x1BF5);
    }
    talk_close(w, scr);
}

/* A round of the talk's message box: when it closes, the next message if the text ended in 0x7f, else the end of the talk. */
static void talk_round(walk_t *w, screen_t *scr, unsigned port60)
{
    if (w->talking == 2) {
        yesno_round(w, scr, port60);
        return;
    }
    if (w->talking == 3) {
        ask_round(w, scr, port60);
        return;
    }
    if (w->talking == 4) {
        look_round(w, scr, port60);
        return;
    }
    if (w->talking == 5) {
        take_round(w, scr, port60);
        return;
    }
    if (w->talking == 6) {
        take_box_round(w, scr, port60);
        return;
    }
    if (w->talking == 7) {
        cast_round(w, scr, port60);
        return;
    }
    if (w->talking == 8) {
        use_round(w, scr, port60);
        return;
    }
    int mx, my;
    unsigned mb = 0;
    int on = walk_mouse(w, &mx, &my, &mb);
    if (!msgbox_round(&w->box, scr, port60, on, mb, talk_step, w))
        return;
    if (w->box.last == 0x7F) {
        w->talk_msg++;
        if (talk_open(w, scr) == 0)
            return;
    }
    talk_done(w, scr, 1);
}

/* 0x14d62 .. 0x14e94, for every F-key (0x3b .. 0x44): the world drawn again and darkened twice (each shown), and a dimmed copy of the
 * palette's first 45 bytes (the palette buffer's 48 copied to [bp-0xb0]): what the F-keys' message boxes step their palette on. */
static void fkey_begin(walk_t *w, screen_t *scr)
{
    fkey_frame(w);
    for (int t = 0; t < 2; t++) {
        fkey_shade(w);
        walk_copy_window(scr, w->buf);   /* 0x2b9ec */
    }
    memcpy(w->dimpal, w->palbuf, sizeof w->dimpal);
    for (int i = 0; i < 0x2D; i++)
        w->dimpal[i] = w->dimpal[i] > 0x10 ? (uint8_t)(w->dimpal[i] - 0x10) : 0;
}

/* F1 (0x14e94 .. 0x153ca): the button pressed; then the cell the hero faces and the two beside it are looked at, up to three cells
 * away, for a villager who is not an enemy. One found is turned to the hero for a drawing of the world (darkened once more, not
 * shown) and the talk begins. */
static void talk_key(walk_t *w, screen_t *scr)
{
    walk_scene_t *s = w->scene;
    uint8_t *party = s->tab_b;
    if (w->mouse_on)
        walk_mouse_put(scr, w->mprev, w->mbg, 0);
    walk_button_toggle(scr, 0x284, 0x2F, 0x14);   /* 0x1c3ba: F1's button pressed */
    if (w->mouse_on)
        walk_mouse_grab(scr, w->mprev, w->mbg);
    unsigned dir = party[2];
    uint8_t px[3], py[3];
    for (int j = 0; j < 3; j++) {
        px[j] = party[0] & 0xFE;
        py[j] = party[1] >> 1;
    }
    if (dir < 2) {
        px[1] = (uint8_t)(px[1] - 2);
        px[2] = (uint8_t)(px[2] + 2);
    } else {
        py[1] = (uint8_t)(py[1] - 1);
        py[2] = (uint8_t)(py[2] + 1);
    }
    for (int step = 0; step < 3; step++) {
        for (int v = 0; v < WS_VILLAGERS; v++) {
            uint8_t *r = s->tab_a + v * WS_VILLAGER_SIZE;
            unsigned vx = r[0] & 0xFE, vy = r[1] >> 1;
            w->ctr = vx;   /* [bp-0x3c], the hero's walk counter, holds the villager's x here */
            for (int j = 0; j < 3; j++) {
                if (vx != px[j] || vy != py[j] || r[0x31] != 0)
                    continue;
                unsigned old = r[2];
                static const uint8_t facing[4] = {1, 0, 3, 2};
                if (dir < 4)
                    r[2] = facing[dir];
                fkey_frame(w);
                fkey_shade(w);
                r[2] = (uint8_t)old;
                talk_begin(w, scr, r);
                return;
            }
        }
        for (int j = 0; j < 3; j++) {
            if (dir == 0)
                py[j]--;
            else if (dir == 1)
                py[j]++;
            else if (dir == 2)
                px[j] = (uint8_t)(px[j] - 2);
            else if (dir == 3)
                px[j] = (uint8_t)(px[j] + 2);
        }
    }
    talk_done(w, scr, 0);
}

/* ---- F2: looking (0x157f5 .. 0x15a89) ---- */

/* The end of a look: F2's button let go (no pointer kept again, unlike F1); then 0x1729e, as for every F-key. */
static void look_done(walk_t *w, screen_t *scr)
{
    walk_button_toggle(scr, 0x1B84, 0x2F, 0x14);
    w->talking = 0;
    records_again(w, 0x384 / WS_RECORD_SIZE);
}

/* Opens the box of message w->talk_msg for a look (0x1afb0: colour 0xb0, the wait flag, the palette steps on [bp-0xb0]); 0 when it
 * waits. */
static int look_open(walk_t *w, screen_t *scr)
{
    int r = msgbox_open(&w->box, scr, w->font8, w->table, w->buf + 0x7305, w->talk_msg, 0xB0, 1);
    if (r < 0) {
        note_unbuilt(w, "a message that MES_TXT.LCC does not have", w->talk_msg);
        return -1;
    }
    if (r > 0)
        note_unbuilt(w, "a character the message font does not have", w->talk_msg);
    w->talking = 4;
    return 0;
}

/* 0x1c5e2: picture `pic` (1 ..) of SPIC.LCZ, 100 x 70 bytes not packed at (pic - 1) * 7000, read into the world buffer's start; a
 * frame of colour 0x28 round it, 102 x 72 at (109, 45) (0x27123), and the picture inside it (0x25b94). */
static void look_picture(walk_t *w, screen_t *scr, unsigned pic)
{
    if (assets_read_at("spic.lcz", (size_t)(pic - 1) * 7000, w->buf, 7000) != 0) {
        note_unbuilt(w, "a picture that SPIC.LCZ does not have", pic);
        return;
    }
    fill(scr, 0x38AD, 102, 1, 0x28);
    fill(scr, 0x38AD + 71 * SCREEN_W, 102, 1, 0x28);
    fill(scr, 0x38AD + SCREEN_W, 1, 70, 0x28);
    fill(scr, 0x38AD + SCREEN_W + 101, 1, 70, 0x28);
    put(scr, 0x39EE, w->buf, 100, 70);
}

/* F2: the button pressed (the pointer's corner put back first, not kept again); two cells, the hero's own and the one to its right
 * (facing up or down) or below it (facing left or right), are looked at, then each a cell further the way he faces, three times, for
 * a scene record of kind 9 (0x2c28f). The hero's walk counter and frame ([bp-0x3c], [bp-0x5e]) hold the first cell's x and y (in the
 * records' units) as they step. A record found: its picture (byte 0xe, when not 0), then its message, one box each as long as a
 * text ends in 0x7f. The message is the one before the first of its three entries (from byte 3: a flag, its bit, the message) whose
 * flag is not set, or before the first whose message is 0, or the last: before the first entry, the record's bytes 1 and 2. */
static void look_key(walk_t *w, screen_t *scr)
{
    walk_scene_t *s = w->scene;
    uint8_t *party = s->tab_b;
    if (w->mouse_on)
        walk_mouse_put(scr, w->mprev, w->mbg, 0);
    walk_button_toggle(scr, 0x1B84, 0x2F, 0x14);   /* 0x1c3ba: F2's button pressed */
    uint8_t x0 = party[0] & 0xFE, y0 = party[1] >> 1, x1 = x0, y1 = y0;
    unsigned dir = party[2];
    if (dir < 2)
        x1 = (uint8_t)(x1 + 2);
    else
        y1 = (uint8_t)(y1 + 1);
    unsigned si = 0x2EE;
    for (int step = 0; step < 3; step++) {
        si = walk_look_record(s->tab_a, x0, y0);
        if (si == 0x2EE)
            si = walk_look_record(s->tab_a, x1, y1);
        if (si < 0x2EE)
            break;
        if (dir == 0) {
            y0--;
            y1--;
        } else if (dir == 1) {
            y0++;
            y1++;
        } else if (dir == 2) {
            x0 = (uint8_t)(x0 - 2);
            x1 = (uint8_t)(x1 - 2);
        } else if (dir == 3) {
            x0 = (uint8_t)(x0 + 2);
            x1 = (uint8_t)(x1 + 2);
        }
    }
    w->ctr = x0;
    w->hero_frame = y0;
    if (si >= 0x2EE) {
        look_done(w, scr);
        return;
    }
    const uint8_t *r = s->tab_a + 1000 + si;
    if (r[0xE] != 0)
        look_picture(w, scr, r[0xE]);
    w->talk_msg = w16(r, walk_look_entry(party, r) - 2);
    if (look_open(w, scr) != 0)
        look_done(w, scr);
}

/* The palette step of a look's box: on the dimmed copy, as a talk's (the same [bp-0xb0]). */
static void look_step(void *ctx, screen_t *scr)
{
    palette_cycle_on(ctx, scr, ((walk_t *)ctx)->dimpal);   /* look */
}

/* A round of a look's message box: when it closes, the next message if the text ended in 0x7f, else the end of the look. */
static void look_round(walk_t *w, screen_t *scr, unsigned port60)
{
    int mx, my;
    unsigned mb = 0;
    int on = walk_mouse(w, &mx, &my, &mb);
    if (!msgbox_round(&w->box, scr, port60, on, mb, look_step, w))
        return;
    if (w->box.last == 0x7F) {
        w->talk_msg++;
        if (look_open(w, scr) == 0)
            return;
    }
    look_done(w, scr);
}

unsigned walk_look_record(const uint8_t *tab_a, unsigned x, unsigned y)
{
    return walk_record_of_kind(tab_a, x, y, 9);
}

unsigned walk_record_of_kind(const uint8_t *tab_a, unsigned x, unsigned y, unsigned kind)
{
    for (unsigned bx = 0; bx < 0x2EE; bx += WS_RECORD_SIZE) {
        const uint8_t *r = tab_a + 1000 + bx;
        if (r[0] == x && r[1] == y && r[2] == kind)
            return bx;
    }
    return 0x2EE;
}

/* ---- F3: taking (0x15bbd .. 0x165df) ---- */

/* The end of a take: F3's button let go (the pointer's corner not kept again), then 0x1729e. */
static void take_done(walk_t *w, screen_t *scr)
{
    walk_button_toggle(scr, 0x3484, 0x2F, 0x14);
    w->talking = 0;
    records_again(w, 0x384 / WS_RECORD_SIZE);
}

/* 0x15fea .. 0x16085 (and 0x163e7 ..): what was taken, 12 bytes of text, as a 96 x 14 picture in colour 0x10 (0x243ec) with a shadow of
 * colour 0x1e a pixel up, down, left and right of it (0x25b54) and over it (0x27371), at (112, 120); then SND_EF.1's 0x1bf5, waited
 * for when the sounds are on (0x1609e), and the loop that waits for a key. Its palette count starts from [bp-0x9a] as it is. */
static void take_strip(walk_t *w, screen_t *scr, const uint8_t text[12], unsigned count)
{
    uint8_t px[96 * 14];
    unsigned gw = 0;
    memcpy(w->zoom_grab, text, 12);   /* the map buffer [bp-0xba] holds the text */
    if (battle_text_pic(w->font8, text, 12, 0x10, px, &gw))
        note_unbuilt(w, "a character the font does not have", 0);
    static const unsigned shadow[4] = {0x9530, 0x97B0, 0x966F, 0x9671};
    for (int s = 0; s < 4; s++)
        for (unsigned i = 0; i < sizeof px; i++)
            if (px[i])
                scr->pix[shadow[s] + (i / 96) * SCREEN_W + i % 96] = 0x1E;
    for (unsigned i = 0; i < sizeof px; i++)
        if (px[i])
            scr->pix[0x9670 + (i / 96) * SCREEN_W + i % 96] = px[i];
    walk_sfx_play(w, SFX_EF, 0x1BF5);
    if (w->sb == 0x81) {   /* 0x16088: until the sound has played, then 0x6d */
        walk_sfx_wait(w);
        walk_sfx_stop(w);
    }
    w->take_count = count & 0xFF;
    w->talking = 5;
}

/* A round of the take's loop (0x160b0, 0x164ad): the palette step every 41st round; Esc, Space or Enter (no release needed) or the
 * right button alone ends it. */
static void take_round(walk_t *w, screen_t *scr, unsigned port60)
{
    w->take_count = (w->take_count + 1) & 0xFF;
    if (w->take_count > 0x28) {
        w->take_count = 0;
        palette_cycle(w, scr);
    }
    unsigned k = port60 & 0xFF;
    int mx, my;
    unsigned mb = 0;
    if (k == 1 || k == 0x39 || k == 0x1C || (walk_mouse(w, &mx, &my, &mb) && (mb & 0xFF) == 2))
        take_done(w, scr);
}

/* 0x161a3, 0x165a0: nowhere to put it: message 11 (colour 0x20, the wait flag, the palette steps on the palette itself). */
static void take_full(walk_t *w, screen_t *scr)
{
    int r = msgbox_open(&w->box, scr, w->font8, w->table, w->buf + 0x7305, 0xB, 0x20, 1);
    if (r < 0) {
        note_unbuilt(w, "a message that MES_TXT.LCC does not have", 0xB);
        take_done(w, scr);
        return;
    }
    if (r > 0)
        note_unbuilt(w, "a character the message font does not have", 0xB);
    w->talking = 6;
}

static void take_box_step(void *ctx, screen_t *scr)
{
    palette_cycle(ctx, scr);
}

static void take_box_round(walk_t *w, screen_t *scr, unsigned port60)
{
    int mx, my;
    unsigned mb = 0;
    int on = walk_mouse(w, &mx, &my, &mb);
    if (msgbox_round(&w->box, scr, port60, on, mb, take_box_step, w)) {
        if (w->using)
            use_done(w, scr);
        else
            take_done(w, scr);
    }
}

unsigned walk_take_slot(const uint8_t *party)
{
    for (unsigned di = 0; di < 0xC8; di += 0x28) {
        if (party[di + 3] >= 5 || w16(party, di + 8) == 0)
            continue;
        for (unsigned j = di + 0x14; j < di + 0x28; j++)
            if (party[j] == 0)
                return j;
    }
    return 0;
}

/* F3: the button pressed (the pointer's corner put back first); three cells, the hero's own, the one to its right and the one to its
 * left (facing up or down; below and above facing left or right), are looked at, then each a cell further the way he faces, three
 * times, for a scene record of kind 2 (something to take). The third cell's x and y are the hero's walk counter and frame ([bp-0x3c],
 * [bp-0x5e]); when nothing is found [bp-0x58] keeps his facing. A record found: its flag (bytes 3, 4) must be set. Byte 5 = 0, an item
 * (byte 0xd, not 0): into the first empty one of the 20 item places of the first member with HP left (a member's number 5 or more is
 * passed over); the record's item cleared, the flag at bytes 0xb, 0xc set, its name (OBJECT.LCC) shown. Byte 5 = 1, coins (the word at
 * 0xd, not 0): added when the total stays at most 60,000; the word cleared, the flag set, the coins drawn, the sum shown. No place or
 * too many coins: message 11. */
static void take_key(walk_t *w, screen_t *scr)
{
    walk_scene_t *s = w->scene;
    uint8_t *party = s->tab_b;
    if (w->mouse_on)
        walk_mouse_put(scr, w->mprev, w->mbg, 0);
    walk_button_toggle(scr, 0x3484, 0x2F, 0x14);   /* 0x1c3ba: F3's button pressed */
    uint8_t x[3], y[3];
    x[0] = x[1] = x[2] = party[0] & 0xFE;
    y[0] = y[1] = y[2] = party[1] >> 1;
    unsigned dir = party[2];
    if (dir < 2) {
        x[1] = (uint8_t)(x[1] + 2);
        x[2] = (uint8_t)(x[2] - 2);
    } else {
        y[1] = (uint8_t)(y[1] + 1);
        y[2] = (uint8_t)(y[2] - 1);
    }
    unsigned si = 0x2EE, ok = dir;
    for (int step = 0; step < 3; step++) {
        for (int j = 0; j < 3 && si == 0x2EE; j++)
            si = walk_record_of_kind(s->tab_a, x[j], y[j], 2);
        if (si < 0x2EE) {
            const uint8_t *r = s->tab_a + 1000 + si;
            ok = walk_flag_test(party, r[3], r[4]);
            break;
        }
        for (int j = 0; j < 3; j++) {
            if (dir == 0)
                y[j]--;
            else if (dir == 1)
                y[j]++;
            else if (dir == 2)
                x[j] = (uint8_t)(x[j] - 2);
            else if (dir == 3)
                x[j] = (uint8_t)(x[j] + 2);
        }
    }
    w->ctr = x[2];
    w->hero_frame = y[2];
    if (si >= 0x2EE || !ok) {
        take_done(w, scr);
        return;
    }
    uint8_t *r = s->tab_a + 1000 + si;
    uint8_t text[12];
    if (r[5] == 0 && r[0xD] != 0) {
        unsigned slot = walk_take_slot(party);
        if (!slot) {
            take_full(w, scr);
            return;
        }
        unsigned item = r[0xD];
        party[slot] = (uint8_t)item;
        r[0xD] = 0;
        walk_flag_set(party, r[0xB], r[0xC]);
        if (assets_read_at("OBJECT.LCC", (size_t)(item - 1) * 30, text, sizeof text) != 0) {
            note_unbuilt(w, "an item that OBJECT.LCC does not have", item);
            memset(text, ' ', sizeof text);
        }
        take_strip(w, scr, text, 1);   /* [bp-0x9a]: the item, counted down to 1 for the file offset */
        return;
    }
    unsigned coins = w16(r, 0xD);
    if (r[5] != 1 || coins == 0) {
        take_done(w, scr);
        return;
    }
    unsigned gold = w16(party, WALK_GOLD_AT);
    if (60000u - gold < coins) {
        take_full(w, scr);
        return;
    }
    gold += coins;
    party[WALK_GOLD_AT] = (uint8_t)gold;
    party[WALK_GOLD_AT + 1] = (uint8_t)(gold >> 8);
    r[0xD] = r[0xE] = 0;
    walk_show_gold(w, scr);   /* 0x1bd6e */
    walk_flag_set(party, r[0xB], r[0xC]);
    char d[8];
    snprintf(d, sizeof d, "%05u", coins % 100000u);   /* 0x26450 into 10 spaces (0x273f8) */
    memcpy(text, d, 5);
    for (int i = 0; i < 4 && text[i] <= '0'; i++)
        text[i] = ' ';
    static const uint8_t tail[7] = {' ', 0xAA, 0x54, 0xAA, 0xF7, 0xB9, 0xF4};
    memcpy(text + 5, tail, sizeof tail);
    take_strip(w, scr, text, y[0]);   /* [bp-0x9a] is still the first cell's y */
}

unsigned walk_look_entry(const uint8_t *party, const uint8_t *r)
{
    unsigned k = 3;
    for (;;) {
        if (!walk_flag_test(party, r[k], r[k + 1]))
            break;
        k += 4;
        if (k == 15 || w16(r, k + 2) == 0)
            break;
    }
    return k;
}

int walk_kind(const walk_t *w)
{
    static const int kind[9] = {0, 4, 8, 9, 4, 10, 4, 0, 12};
    if (w->boss_box)
        return 4;
    if (w->talking == 7)
        return battle_kind(w->cast);
    return kind[w->talking];
}

/* ---- F4: casting (0x154e0 .. 0x15620) ---- */

/* The end of F4 (0x155dc .. 0x15618): the button let go, the pointer's place 0 and the corner kept; then 0x1729e. F5 and F6 let both
 * their buttons go (0x1579c .. 0x157f2). */
static void use_item_chosen(walk_t *w, screen_t *scr, unsigned item, const uint8_t rec[30], unsigned member);

static void cast_done(walk_t *w, screen_t *scr)
{
    int items = w->cast_items;
    uint8_t rec[30];
    unsigned member = 0, item = items == 2 ? battle_use_item(w->cast, rec, &member) : 0;
    battle_free(w->cast);
    w->cast = NULL;
    if (items == 2) {   /* F7 (0x1691d): the pointer's place 0 and the corner kept; its button stays pressed */
        if (w->mouse_on) {
            w->mprev = 0;
            walk_mouse_grab(scr, 0, w->mbg);
        }
        use_item_chosen(w, scr, item, rec, member);
        return;
    }
    if (items) {
        walk_button_toggle(scr, 0x6684, 0x2F, 0x14);
        walk_button_toggle(scr, 0x7F84, 0x2F, 0x14);
    } else {
        walk_button_toggle(scr, 0x4D84, 0x2F, 0x14);
    }
    if (w->mouse_on) {
        w->mprev = 0;
        walk_mouse_grab(scr, 0, w->mbg);
    }
    w->talking = 0;
    records_again(w, 0x384 / WS_RECORD_SIZE);
}

/* F4: the button pressed (the pointer's corner put back first); the member picker for who casts, then his magic list
 * (battle_world_cast). The world buffer's byte 0 is set to 0 (0x15585), which tells the list it is not in a battle. */
static void cast_key(walk_t *w, screen_t *scr)
{
    if (w->mouse_on)
        walk_mouse_put(scr, w->mprev, w->mbg, 0);
    walk_button_toggle(scr, 0x4D84, 0x2F, 0x14);   /* 0x1c3ba: F4's button pressed */
    w->buf[0] = 0;
    w->cast = battle_world_cast(w, scr);
    if (!w->cast) {
        note_unbuilt(w, "the magic list's pictures", 0);
        walk_button_toggle(scr, 0x4D84, 0x2F, 0x14);
        return;
    }
    battle_set_show(w->cast, w->show, w->show_ctx);
    w->cast_items = 0;
    w->talking = 7;
}

/* F5 and F6 (0x15620 .. 0x157f5, keys 0x3f and 0x40): both buttons pressed (the pointer's corner put back first), the same picker,
 * then the member's item list (battle_world_items) or equipment screen (battle_world_equip). The world buffer's byte 0 is 0 for
 * them too. */
static void items_key(walk_t *w, screen_t *scr, int equip)
{
    if (w->mouse_on)
        walk_mouse_put(scr, w->mprev, w->mbg, 0);
    walk_button_toggle(scr, 0x6684, 0x2F, 0x14);   /* 0x1566f, 0x15684 */
    walk_button_toggle(scr, 0x7F84, 0x2F, 0x14);
    w->buf[0] = 0;
    w->cast = equip ? battle_world_equip(w, scr) : battle_world_items(w, scr);
    if (!w->cast) {
        note_unbuilt(w, "the item list's pictures", 0);
        walk_button_toggle(scr, 0x6684, 0x2F, 0x14);
        walk_button_toggle(scr, 0x7F84, 0x2F, 0x14);
        return;
    }
    w->cast_items = 1;
    w->talking = 7;
}

/* ---- F7: using (0x165df .. 0x1729e) ---- */

enum { USE_COINS = 0xBAA7, USE_ITEM = 0xBAF7 };

/* 0x17239 .. 0x1729e: the pointer off, F7's button let go, the corner kept again; then 0x172f0 with si = 0x384. */
static void use_done(walk_t *w, screen_t *scr)
{
    if (w->mouse_on)
        walk_mouse_put(scr, w->mprev, w->mbg, 0);
    walk_button_toggle(scr, 0x9884, 0x2F, 0x14);
    if (w->mouse_on)
        walk_mouse_grab(scr, w->mprev, w->mbg);
    w->using = 0;
    w->talking = 0;
    records_again(w, 0x384 / WS_RECORD_SIZE);
}

/* 0x27123: a w x h frame of colour c */
static void use_outline(screen_t *scr, unsigned at, unsigned bw, unsigned bh, uint8_t c)
{
    fill(scr, at, bw, 1, c);
    for (unsigned r = 1; r < bh; r++) {
        fill(scr, at + r * SCREEN_W, 1, 1, c);
        fill(scr, at + r * SCREEN_W + bw - 1, 1, 1, c);
    }
    fill(scr, at + (bh - 1) * SCREEN_W, bw, 1, c);
}

/* A message box of F7's (0x1afb0, colour 0x88, the palette itself): waiting (talking 6, then use_done) or not. 0 when drawn. */
static int use_box(walk_t *w, screen_t *scr, unsigned msg, int wait)
{
    int r = msgbox_open(&w->box, scr, w->font8, w->table, w->buf + 0x7305, msg, 0x88, wait);
    if (r < 0) {
        note_unbuilt(w, "a message that MES_TXT.LCC does not have", msg);
        return -1;
    }
    if (r > 0)
        note_unbuilt(w, "a character the message font does not have", msg);
    if (wait)
        w->talking = 6;
    return 0;
}

/* F7: the button pressed (the pointer's corner put back first, then kept); message 0x2e9 (金錢 或 物品, colour 0x10, not waiting) and the
 * two 33 x 17 frames under its words (colour 0x18), coins lit; then the prompt's loop (use_round). */
static void use_key(walk_t *w, screen_t *scr)
{
    if (w->mouse_on)
        walk_mouse_put(scr, w->mprev, w->mbg, 0);
    walk_button_toggle(scr, 0x9884, 0x2F, 0x14);
    if (w->mouse_on)
        walk_mouse_grab(scr, w->mprev, w->mbg);
    w->using = 1;
    int r = msgbox_open(&w->box, scr, w->font8, w->table, w->buf + 0x7305, 0x2E9, 0x10, 0);
    if (r < 0) {
        note_unbuilt(w, "a message that MES_TXT.LCC does not have", 0x2E9);
        use_done(w, scr);
        return;
    }
    if (r > 0)
        note_unbuilt(w, "a character the message font does not have", 0x2E9);
    use_outline(scr, USE_COINS, 0x21, 0x11, 0x18);
    use_outline(scr, USE_ITEM, 0x21, 0x11, 0x18);
    w->use_sel = USE_COINS;
    w->talking = 8;
}


/* The prompt's choice: none (0), an item (the picker, then his list in F7's way), or coins (the number box). The pointer goes off first. */
static void use_chosen(walk_t *w, screen_t *scr, unsigned sel)
{
    if (sel == 0) {
        use_done(w, scr);
        return;
    }
    if (w->mouse_on)
        walk_mouse_put(scr, w->mprev, w->mbg, 0);
    if (sel == USE_COINS) {
        ask_open(w, scr);
        return;
    }
    w->buf[0] = 2;   /* 0x168da: the list's buffer byte 0, read only once the picker has given a member */
    w->cast = battle_world_use(w, scr);
    if (!w->cast) {
        note_unbuilt(w, "the item list's pictures", 0);
        use_done(w, scr);
        return;
    }
    battle_set_show(w->cast, w->show, w->show_ctx);
    w->cast_items = 2;
    w->talking = 7;
}

/* A round of the prompt (0x166a8 .. 0x1685a): the palette step every 61st; the lit frame colour 0xf, the other 0x1a; the pointer (the
 * right button: none; over a word, it lights it and the left button takes it); Left coins, Right the item, Enter or Space take the lit
 * one, Esc none. No release is needed. */
static void use_round(walk_t *w, screen_t *scr, unsigned port60)
{
    w->use_count = (w->use_count + 1) & 0xFF;
    if (w->use_count > 0x3C) {
        w->use_count = 0;
        palette_cycle(w, scr);
    }
    unsigned si = w->use_sel;
    use_outline(scr, si, 0x21, 0x11, 0x0F);
    use_outline(scr, si == USE_COINS ? USE_ITEM : USE_COINS, 0x21, 0x11, 0x1A);
    if (w->mouse_on) {
        int mx, my;
        unsigned mb = 0;
        walk_mouse(w, &mx, &my, &mb);
        if ((mb & 0xFF) == 2) {
            use_chosen(w, scr, 0);
            return;
        }
        unsigned pos = (unsigned)(my * SCREEN_W + mx);
        if (pos != w->mprev) {
            walk_mouse_put(scr, w->mprev, w->mbg, 0);
            walk_mouse_grab(scr, pos, w->mbg);
            walk_mouse_put(scr, pos, w->mpic, 1);
            w->mprev = pos;
        }
        if ((unsigned)my > 0x94 && (unsigned)my < 0xA6) {
            if ((unsigned)mx > 0x66 && (unsigned)mx < 0x88) {
                w->use_sel = USE_COINS;
                if ((mb & 0xFF) == 1) {
                    use_chosen(w, scr, USE_COINS);
                    return;
                }
            }
            if ((unsigned)mx > 0xB6 && (unsigned)mx < 0xD7) {
                w->use_sel = USE_ITEM;
                if ((mb & 0xFF) == 1) {
                    use_chosen(w, scr, USE_ITEM);
                    return;
                }
            }
        }
    }
    unsigned k = port60 & 0xFF;
    if (k == 0x4B)
        w->use_sel = USE_COINS;
    if (k == 0x4D)
        w->use_sel = USE_ITEM;
    if (k == 0x1C || k == 0x39)
        use_chosen(w, scr, w->use_sel);
    else if (k == 1)
        use_chosen(w, scr, 0);
}

/* 0x1695b .. 0x16afc and 0x16f6a .. 0x170b6: the hero's own cell (x, y / 2) and the one beside it (right facing up or down, below
 * facing left or right) are looked at, then both a step further the way he faces, four times: for a kind 6 record whose byte 3 is 0
 * (an item's place) or not 0 (a price's), and for an item also a kind 0x14 record (*scene). The last record looked at is returned
 * (0x2ee: none). The first cell's x and y are left in the hero's walk counter and frame ([bp-0x3c], [bp-0x5e]). */
unsigned walk_use_find(const uint8_t *tab_a, const uint8_t *party, int coins, int *scene, uint8_t cell[3])
{
    uint8_t ax = party[0] & 0xFE, ay = party[1] >> 1, bx = ax, by = ay;
    unsigned dir = party[2], si = 0x2EE;
    if (dir < 2)
        bx = (uint8_t)(bx + 2);
    else
        by = (uint8_t)(by + 1);
    *scene = 0;
    for (int di = 0; di < 4; di++) {
        si = walk_record_of_kind(tab_a, ax, ay, 6);
        if (si < 0x2EE && (tab_a[1000 + si + 3] != 0) == coins)
            break;
        si = walk_record_of_kind(tab_a, bx, by, 6);
        if (si < 0x2EE && (tab_a[1000 + si + 3] != 0) == coins)
            break;
        if (!coins) {
            si = walk_record_of_kind(tab_a, ax, ay, 0x14);
            if (si < 0x2EE) {
                *scene = 1;
                break;
            }
            si = walk_record_of_kind(tab_a, bx, by, 0x14);
            if (si < 0x2EE) {
                *scene = 1;
                break;
            }
        }
        if (dir == 0) {
            ay--;
            by--;
        } else if (dir == 1) {
            ay++;
            by++;
        } else if (dir == 2) {
            ax = (uint8_t)(ax - 2);
            bx = (uint8_t)(bx - 2);
        } else if (dir == 3) {
            ax = (uint8_t)(ax + 2);
            bx = (uint8_t)(bx + 2);
        }
    }
    cell[0] = ax;
    cell[1] = ay;
    cell[2] = bx;
    return si;
}

static unsigned use_search(walk_t *w, int coins, int *scene)
{
    uint8_t cell[3];
    unsigned si = walk_use_find(w->scene->tab_a, w->scene->tab_b, coins, scene, cell);
    w->ctr = cell[0];
    w->hero_frame = cell[1];
    w->use_count = cell[2];   /* [bp-0xc4], the prompt's palette count, holds the second cell's x */
    return si;
}

/* The member's first item place (0x14 .. 0x27) holding the item, emptied (0x16c1a, 0x16e15). */
static void use_drop(uint8_t *party, unsigned member, unsigned item)
{
    for (unsigned j = member + 0x14; j < member + 0x28; j++)
        if (party[j] == item) {
            party[j] = 0;
            return;
        }
}

unsigned walk_use_blink(unsigned c)
{
    for (int i = 0; i < 0x4B0; i++) {   /* 0x16ce0: 1200 rounds, the name drawn while the count is 0xc8 .. 0x190 */
        c = (c + 1) & 0xFFFF;
        if (c > 0x190)
            c = 0;
    }
    return c;
}

/* After the list (0x1694c .. 0x16ea4): nothing chosen, the end. Else the cells are looked at and the world drawn again (0x19680 with
 * the buffer's bytes 1, x, y, the header's byte 5; shown, not darkened). A scene change record: 0x172f0 (not built). A kind 6 record for
 * this item: its flag (bytes 5, 6) set, the item gone from the member's places, its name (the record the list copied to the map buffer,
 * 12 bytes, colour 0x10) over (80, 93) at the end of 1200 rounds that show it or what was there by turns, SND_EF.1's 0x1bf5, a shake of
 * 4. None: when the item's byte 0x11 is 0, message 0x251 and yes / no (yes throws it away), else message 0x2b5. */
static void use_item_chosen(walk_t *w, screen_t *scr, unsigned item, const uint8_t rec[30], unsigned member)
{
    walk_scene_t *s = w->scene;
    uint8_t *party = s->tab_b;
    if (item == 0) {
        use_done(w, scr);
        return;
    }
    if (item != 0xFF)
        memcpy(w->zoom_grab, rec, 30);   /* 0x20eef: the map buffer [bp-0xba] */
    w->use_item = item;
    w->use_member = member;
    int scene;
    unsigned si = use_search(w, 0, &scene);
    fkey_frame(w);
    walk_copy_window(scr, w->buf);   /* 0x2b9ec */
    if (scene) {
        note_unbuilt(w, "F7 on a scene record of kind 0x14 (0x172f0)", si / WS_RECORD_SIZE);
        use_done(w, scr);
        return;
    }
    if (si < 0x2EE && s->tab_a[1000 + si + 4] == item) {
        const uint8_t *r = s->tab_a + 1000 + si;
        walk_flag_set(party, r[5], r[6]);
        use_drop(party, member, item);
        w->hero_frame = 1;   /* 0x16c75 .. 0x16ca0: [bp-0x5e] counts the item down to 1 (for its OBJECT.LCC offset, not read) */
        uint8_t px[96 * 14];
        unsigned gw = 0;
        if (battle_text_pic(w->font8, w->zoom_grab, 12, 0x10, px, &gw))
            note_unbuilt(w, "a character the font does not have", 0);
        if (walk_use_blink(w->v96) >= 0xC8)
            for (unsigned i = 0; i < sizeof px; i++)
                if (px[i])
                    scr->pix[0x74B0 + (i / 96) * SCREEN_W + i % 96] = px[i];
        w->v96 = walk_use_blink(w->v96);
        walk_sfx_play(w, SFX_EF, 0x1BF5);   /* 0x16d41 */
        w->v48 = 4;
        use_done(w, scr);
        return;
    }
    if (w->zoom_grab[0x11] == 0) {
        if (use_box(w, scr, 0x251, 0) != 0) {
            use_done(w, scr);
            return;
        }
        if (w->mouse_on)
            walk_mouse_put(scr, w->mprev, w->mbg, 0);
        yesno_open(w, scr);
        return;
    }
    if (use_box(w, scr, 0x2B5, 1) != 0)
        use_done(w, scr);
}

/* 0x16dd3 .. 0x16e78: the answer: the pointer's place 0 and the corner kept; yes throws the item away. */
static void use_answer(walk_t *w, screen_t *scr, int yes)
{
    w->use_count = (unsigned)yes;   /* 0x16dd3: the answer goes to [bp-0xc4] */
    if (w->mouse_on) {
        w->mprev = 0;
        walk_mouse_grab(scr, 0, w->mbg);
    }
    if (yes)
        use_drop(w->scene->tab_b, w->use_member, w->use_item);
    use_done(w, scr);
}

/* 0x16f08 .. 0x17236: the number given (the pointer's place 0 and the corner kept). 0 or more than the coins: the end. Else the cells
 * are looked at for a price and the world drawn again; a price whose byte 4 is the number's low byte: its flag set, the number taken
 * from the coins, the coins drawn, SND_EF.1's 0x1bf5. None: message 0x173. */
static void use_coins_given(walk_t *w, screen_t *scr, unsigned value)
{
    walk_scene_t *s = w->scene;
    uint8_t *party = s->tab_b;
    if (w->mouse_on) {
        w->mprev = 0;
        walk_mouse_grab(scr, 0, w->mbg);
    }
    unsigned gold = w16(party, WALK_GOLD_AT);
    w->v96 = gold;   /* 0x16f22 */
    if (value == 0 || value > gold) {
        use_done(w, scr);
        return;
    }
    int scene;
    unsigned si = use_search(w, 1, &scene);
    fkey_frame(w);
    walk_copy_window(scr, w->buf);
    if (si < 0x2EE && s->tab_a[1000 + si + 4] == (value & 0xFF)) {
        const uint8_t *r = s->tab_a + 1000 + si;
        walk_flag_set(party, r[5], r[6]);
        gold -= value;
        w->v96 = gold;   /* 0x171c4 */
        party[WALK_GOLD_AT] = (uint8_t)gold;
        party[WALK_GOLD_AT + 1] = (uint8_t)(gold >> 8);
        walk_show_gold(w, scr);   /* 0x1bd6e */
        walk_sfx_play(w, SFX_EF, 0x1BF5);
        use_done(w, scr);
        return;
    }
    if (use_box(w, scr, 0x173, 1) != 0)
        use_done(w, scr);
}

static void cast_round(walk_t *w, screen_t *scr, unsigned port60)
{
    if (battle_pass(w->cast, scr, port60) & WALK_PASS_UNBUILT)
        note_unbuilt(w, battle_unbuilt(w->cast) ? battle_unbuilt(w->cast) : "?", 0);
    if (battle_over(w->cast))
        cast_done(w, scr);
}

/* 0x2c8a0: the key a left press at (x, y) stands for. While an arrow was the last key (`last`), or over the play window, the arrow
 * towards the pointer from the screen's middle (160, 100), along the longer of the two distances (Left or Right when they are equal);
 * over the left-hand buttons F1 .. F8 (rows of 20 from y 0) and, under them, the A key; over the right-hand buttons F9, F10 and the
 * keys 1 to 4. */
unsigned walk_mouse_key(unsigned x, unsigned y, unsigned last)
{
    unsigned dx = x > 0xA0 ? x - 0xA0 : 0xA0 - x, dy = y > 0x64 ? y - 0x64 : 0x64 - y, dir;
    if (dy <= dx)
        dir = x < 0xA0 ? 0x4B : 0x4D;
    else
        dir = y > 0x64 ? 0x50 : 0x48;
    last &= 0xFF;
    if (last >= 0x48 && last < 0x51)
        return dir;
    if (x > 3 && x < 0x33) {
        if (y >= 0xAF)
            return 0x1E;
        if (y >= 0xA2)
            return dir;
        static const unsigned top[7] = {0x15, 0x29, 0x3D, 0x51, 0x65, 0x79, 0x8D};
        for (unsigned i = 0; i < 7; i++)
            if (y <= top[i])
                return 0x3B + i;
        return 0x42;
    }
    if (x > 0x10C && x < 0x13C && y > 0x39 && y < 0xB2) {
        static const unsigned top[5] = {0x4D, 0x61, 0x75, 0x89, 0x9D};
        static const unsigned key[5] = {0x43, 0x44, 2, 3, 4};
        for (unsigned i = 0; i < 5; i++)
            if (y <= top[i])
                return key[i];
        return 5;
    }
    return dir;
}

/* 0x135f0 .. 0x13ad9 and the four walking handlers. */
/* Returns 1 when an F-key's action ran: the pass then ends there (0x1729e jumps to 0x172f0 past the walk counters). */
static int walk_keys(walk_t *w, unsigned port60)
{
    walk_scene_t *s = w->scene;
    uint8_t *party = s->tab_b;
    unsigned k = port60, c2 = w->c2;
    if (k > 0xC7 && k < 0xD1 && c2 > 0x47 && c2 < 0x51) {   /* the break code of another key does not release the held arrow */
        if (c2 == 0x48 && k != 0xC8)
            k = c2;
        if (c2 == 0x50 && k != 0xD0)
            k = c2;
        if (c2 == 0x4B && k != 0xCB)
            k = c2;
        if (c2 == 0x4D && k != 0xCD)
            k = c2;
    }
    if (k == 0xE0)
        k = c2;
    int mx, my;
    unsigned mb;
    if (walk_mouse(w, &mx, &my, &mb)) {   /* 0x136d7: the left button is a key from the pointer's place, the right one Space */
        if (mb == 1) {
            k = walk_mouse_key(w->mx, w->my, w->v30);
            if (k > 0x47 && k < 0x51)
                w->vc0 = 1;
        }
        if (mb == 2)
            k = 0x39;
    }
    if (k > 0x7F)
        c2 = 0;
    if ((k == 2 || k == 3 || k == 0x1E) && c2 == k)
        k = 0x82;
    w->v30 = k;   /* 0x137d6: the key of this pass, for the next left press */
    if (k >= 0x80) {
        w->c2 = c2;
        return 0;
    }
    c2 = k;
    w->c2 = c2;
    if (w->vc0) {   /* 0x13a02: after a walk with the mouse the next pass does not take the arrow as held */
        w->vc0 = 0;
        w->c2 = 0;
    }
    int arrow = k == 0x48 || k == 0x50 || k == 0x4B || k == 0x4D;
    if (k == 0x39) {   /* Space is read on every pass, not only every third */
        space(w);
        return 0;
    }
    if (k == 0x3B && w->scr) {   /* F1: talk */
        fkey_begin(w, w->scr);
        talk_key(w, w->scr);
        return 1;
    }
    if (k == 0x3C && w->scr) {   /* F2: look */
        fkey_begin(w, w->scr);
        look_key(w, w->scr);
        return 1;
    }
    if (k == 0x3D && w->scr) {   /* F3: take */
        fkey_begin(w, w->scr);
        take_key(w, w->scr);
        return 1;
    }
    if (k == 0x3E && w->scr) {   /* F4: cast */
        fkey_begin(w, w->scr);
        cast_key(w, w->scr);
        return 1;
    }
    if (k == 0x3F && w->scr) {   /* F5: items */
        fkey_begin(w, w->scr);
        items_key(w, w->scr, 0);
        return 1;
    }
    if (k == 0x40 && w->scr) {   /* F6: equipment */
        fkey_begin(w, w->scr);
        items_key(w, w->scr, 1);
        return 1;
    }
    if (k == 0x41 && w->scr) {   /* F7: use */
        fkey_begin(w, w->scr);
        use_key(w, w->scr);
        return 1;
    }
    if (!arrow && k != 0) {
        char what[32];
        snprintf(what, sizeof what, "key 0x%x", k);
        note_unbuilt(w, what, 0);
        return 0;
    }
    if (w->v98 != 0 || k == 0)
        return 0;
    unsigned x = party[0], y = party[1], nx = x, ny = y;
    party[2] = k == 0x48 ? 0 : k == 0x50 ? 1 : k == 0x4B ? 2 : 3;
    w->v2e = 1;
    if (k == 0x48)
        ny = (y - 1) & 0xFF;
    else if (k == 0x50)
        ny = (y + 1) & 0xFF;
    else if (k == 0x4B)
        nx = (x - 1) & 0xFF;
    else
        nx = (x + 1) & 0xFF;
    if (walk_foot_ok(s->gmap, s->list, nx, ny)) {
        walk_trail_step(s->trail, party, nx, ny);
    } else {
        w->v2e = 5;
        w->v48 = 2;   /* the shake; sliding round a corner (the A key) is not built */
    }
    return 0;
}

/* 0x1275e .. 0x128ee: what a touch does before the battle. The hero's walk counter takes the member's party offset and his frame the
 * record's byte 0x2f (0x1276c, 0x1277f). Byte 0x2f = 0: the enemy goes to a random open place (the respawn's rule without the view
 * bounds) and the group is the map header's byte 1 (party table byte 518) * 5 + its sprite set - 1. Otherwise (a story enemy) the
 * group is byte 0x30, after a message box (0x1afb0: colour 0xc2, the wait flag) with the message at word 0x11 when it is not 0: the
 * battle then starts when the box closes (walk_pass, boss_round). */
static int touch(walk_t *w, unsigned member, uint8_t *rec)
{
    walk_scene_t *s = w->scene;
    w->battle_member = member;
    w->battle_boss = rec[0x2F];
    w->battle_rec = (unsigned)(rec - s->tab_a);
    w->ctr = member & 0xFF;
    w->hero_frame = rec[0x2F];
    int ret = WALK_PASS_BATTLE;
    if (rec[0x2F] == 0) {
        for (long tries = 0;; tries++) {
            if (tries >= 1000000) {
                note_unbuilt(w, "no open place for the touched enemy", 0);
                ret |= WALK_PASS_UNBUILT;
                break;
            }
            unsigned rx = ((((unsigned)walk_rand(&w->rng) / 0x13C) + 0xC) & 0xFF) * 2 & 0xFF;
            unsigned ry = (((unsigned)walk_rand(&w->rng) / 0x19A) + 0xA) & 0xFF;
            if (open_place(s, rx, ry)) {
                rec[0] = (uint8_t)rx;
                rec[1] = (uint8_t)(ry * 2);
                break;
            }
        }
        w->battle_group = (s->tab_b[518] * 5 + rec[0xB] - 1) & 0xFF;   /* [bp-0x34] = party table + 500: byte 518 */
    } else {
        w->battle_group = rec[0x30];
        unsigned msg = w16(rec, 0x11);
        if (msg != 0) {
            int r = msgbox_open(&w->box, w->scr, w->font8, w->table, w->buf + 0x7305, msg, 0xC2, 1);
            if (r < 0) {
                note_unbuilt(w, "a message that MES_TXT.LCC does not have", msg);
                return ret | WALK_PASS_UNBUILT;
            }
            if (r > 0) {
                note_unbuilt(w, "a character the message font does not have", msg);
                ret |= WALK_PASS_UNBUILT;
            }
            w->boss_box = 1;
            return ret & ~WALK_PASS_BATTLE;
        }
    }
    return ret;
}

static void boss_step(void *ctx, screen_t *scr)
{
    palette_cycle(ctx, scr);
}

/* 0x1b506 for a story enemy's box: the palette steps on the palette itself (0x1afb0's [bp-0xac]), as for the level-ups. When the box
 * closes the battle starts. */
static int boss_round(walk_t *w, screen_t *scr, unsigned port60)
{
    int mx, my;
    unsigned mb = 0;
    int on = walk_mouse(w, &mx, &my, &mb);
    if (!msgbox_round(&w->box, scr, port60, on, mb, boss_step, w))
        return 0;
    w->boss_box = 0;
    return WALK_PASS_BATTLE;
}

static int pass_tail(walk_t *w, uint32_t ticks, unsigned port60, unsigned stride);

static void mouse_start(walk_t *w, screen_t *scr);
static void mouse_pass(walk_t *w, screen_t *scr, uint8_t *view, unsigned shift);

int walk_pass(walk_t *w, screen_t *scr, uint32_t ticks, unsigned port60)
{
    walk_scene_t *s = w->scene;
    unsigned x = s->tab_b[0], y = s->tab_b[1];
    w->unbuilt_new = 0;
    w->scr = scr;
    if (w->talking) {   /* a talk's message box waits (0x1b506) */
        talk_round(w, scr, port60);
        return w->unbuilt_new ? WALK_PASS_UNBUILT : 0;
    }
    if (w->boss_box) {
        int got = boss_round(w, scr, port60);
        return got | (w->unbuilt_new ? WALK_PASS_UNBUILT : 0);
    }
    mouse_start(w, scr);

    walk_animate_ground(s->block_x, s->gmap);
    walk_animate_objects(s->block_y, s->omap);
    palette_cycle(w, scr);

    memset(w->buf, 0, WALK_BUF_LEN);
    walk_draw_ground(w->buf, s, x, y);
    walk_draw_entities(w->buf, s, x, y, w->hero_frame);
    if (w->v2e == 1) {   /* the hero took a step in the last pass: is there a scene record under his feet? */
        w->v2e = 5;
        unsigned si = walk_find_record(s->tab_a, x, y);
        if (si < 0x2EE) {
            hit_record(w, si);
            return w->unbuilt_new ? WALK_PASS_UNBUILT : 0;   /* the rest of the pass, window copy and counters included, is skipped */
        }
    }
    shots_pass(w, scr, x, y);
    walk_draw_objects(w->buf, s, x, y);
    sparks_pass(w, x, y);
    int rainy = s->weather == 1 || s->weather == 2;
    if (rainy && (ticks & 0xFFFF) > 0xA000)
        walk_draw_specks(w->buf, WALK_BUF_LEN, w->specks);
    unsigned stride = walk_rain_stride(s->weather, ticks);
    if (rainy)   /* 0x1248d, 0x124d3: the clock, shifted down 10 when below 0x5000 */
        w->v96 = (ticks & 0xFFFF) < 0x5000 ? (ticks & 0xFFFF) >> 10 : ticks & 0xFFFF;
    if (stride && s->weather == 1)   /* mode 2 is read from the code but never seen: nothing is drawn for it */
        walk_draw_rain(w->buf, w->drops, stride);
    unsigned sh = shake(w);   /* 0x1259b: the shake moves where the buffer starts, for the pointer too */
    uint8_t *view = w->buf;
    if (s->header[5]) {   /* 0x125c6: a dark scene shows the doubled, darkened middle; the shake moved what is doubled */
        walk_double_dark(w->dbuf, w->buf + sh, w->table, s->dark_mask);
        view = w->dbuf;
        sh = 0;
    }
    mouse_pass(w, scr, view, sh);
    walk_copy_window_at(scr, view, sh);

    /* 0x12703: a party member standing on a live enemy (0x2c0da: same position with the low bits cleared, byte 0x31 set, not in
     * smoke) starts a battle; the loop is left here, before the villagers' steps. */
    for (int i = 0; i < 5; i++) {
        const uint8_t *p = s->tab_b + WS_PARTY_SIZE * i;
        if (p[3] > 0x64)
            break;
        for (int k = 0; k < WS_VILLAGERS; k++) {
            uint8_t *r = s->tab_a + k * WS_VILLAGER_SIZE;
            if (r[2] != 0x61 && (r[0] & 0xFE) == (p[0] & 0xFE) && (r[1] & 0xFE) == (p[1] & 0xFE) && r[0x31] > 0) {
                w->stride = stride;
                return touch(w, (unsigned)(WS_PARTY_SIZE * i), r);
            }
        }
    }
    return pass_tail(w, ticks, port60, stride);
}

/* 0x13318 on: the villagers' steps, the key handlers and the hero's walk frame; after a battle the pass that touched ends here. */
static int pass_tail(walk_t *w, uint32_t ticks, unsigned port60, unsigned stride)
{
    walk_scene_t *s = w->scene;
    for (int k = 0; k < WS_VILLAGERS; k++) {
        uint8_t *rec = s->tab_a + k * WS_VILLAGER_SIZE;
        int ret = walk_villager_step(rec, s->gmap, s->list);
        if (ret == 0xC)
            respawn(w, rec, s->tab_b[0], s->tab_b[1]);
        else if (ret == 0xA)
            walk_villager_decide(rec, &w->rng, ticks, s->tab_b[0], s->tab_b[1], stride);
    }
    if (++w->v98 > (unsigned)s->speed_class)
        w->v98 = 0;
    if (walk_keys(w, port60))
        return w->unbuilt_new ? WALK_PASS_UNBUILT : 0;
    w->ctr = (w->ctr + 1) & 0xFF;
    if (w->ctr > (unsigned)s->speed_class + w->v2e) {
        w->ctr = 0;
        if (++w->hero_frame > 3)   /* 0x172d7 (a respawn can leave any value here) */
            w->hero_frame = 0;
    }
    return w->unbuilt_new ? WALK_PASS_UNBUILT : 0;
}

void walk_state_dump(const walk_t *w, uint8_t out[WALK_STATE_BYTES])
{
    const walk_scene_t *s = w->scene;
    size_t o = 0;
    memcpy(out + o, s->tab_a, 1000);
    o += 1000;
    for (int i = 0; i < 4; i++)
        out[o++] = (uint8_t)(w->rng >> (8 * i));
    for (int i = 0; i < WALK_RAIN_DROPS; i++) {
        out[o++] = (uint8_t)w->drops[i];
        out[o++] = (uint8_t)(w->drops[i] >> 8);
    }
    for (int i = 0; i < WALK_SPECKS; i++) {
        out[o++] = (uint8_t)w->specks[i];
        out[o++] = (uint8_t)(w->specks[i] >> 8);
    }
    memcpy(out + o, w->palbuf, 48);
    o += 48;
    memcpy(out + o, s->block_x, WS_BLOCK_BYTES);
    o += WS_BLOCK_BYTES;
    memcpy(out + o, s->block_y, WS_BLOCK_BYTES);
    o += WS_BLOCK_BYTES;
    memcpy(out + o, s->tab_b, 200);          /* the party table's records */
    o += 200;
    memcpy(out + o, s->trail, WS_TRAIL_BYTES);
    o += WS_TRAIL_BYTES;
    out[o++] = (uint8_t)w->v2e;
    out[o++] = (uint8_t)w->v98;
    out[o++] = (uint8_t)w->ctr;
    out[o++] = (uint8_t)w->hero_frame;
    out[o++] = (uint8_t)w->v48;
    out[o++] = (uint8_t)w->c2;
    out[o++] = w->alt[0];
    out[o++] = (uint8_t)s->weather;
    memcpy(out + o, w->fx, WALK_FX_BYTES);
    o += WALK_FX_BYTES;
    out[o++] = s->tab_b[WALK_GOLD_AT];
    out[o++] = s->tab_b[WALK_GOLD_AT + 1];
}

/* ---- the window ---- */

enum { DOUBLE_SRC = 0x3220, DOUBLE_DST = 0xE10, DARK_MASK_AT = 0x48A0 };

void walk_double(uint8_t *dst, const uint8_t *src, const uint8_t table[256])
{
    for (int r = 0; r < 84; r++)
        for (int c = 0; c < 96; c++) {
            uint8_t p = src[DOUBLE_SRC + r * WALK_W + c], q = table[p];
            uint8_t *d = dst + DOUBLE_DST + 2 * r * WALK_W + 2 * c;
            d[0] = p;
            d[1] = q;
            d[WALK_W] = q;
            d[WALK_W + 1] = table[q];
        }
}

void walk_double_dark(uint8_t *dst, const uint8_t *src, const uint8_t table[256], const uint8_t *mask)
{
    for (int r = 0; r < 84; r++)
        for (int c = 0; c < 96; c++) {
            uint8_t p = src[DOUBLE_SRC + r * WALK_W + c];
            uint8_t *d = dst + DOUBLE_DST + 2 * r * WALK_W + 2 * c;
            d[0] = p;
            if (r >= 34 && r < 34 + 27) {
                uint8_t q = table[p];
                d[1] = q;
                d[WALK_W] = q;
                d[WALK_W + 1] = table[q];
            } else {
                d[1] = 0;
                d[WALK_W] = 0;
                d[WALK_W + 1] = 0;
            }
        }
    for (int i = 0; i < WS_DARK_MASK_BYTES; i++)
        dst[DARK_MASK_AT + i] &= mask[i];
}

void walk_copy_window_at(screen_t *scr, const uint8_t *buf, unsigned shift)
{
    for (int r = 0; r < 168; r++)
        memcpy(scr->pix + (16 + r) * SCREEN_W + 64, buf + shift + (16 + r) * WALK_W + 16, 192);
}

void walk_copy_window(screen_t *scr, const uint8_t *buf)
{
    walk_copy_window_at(scr, buf, 0);
}

/* ---- the interface ---- */

size_t walk_glyph_offset(unsigned hi, unsigned lo)
{
    if (hi < 0xA4)
        return (size_t)-1;
    return 0x3B20 + 30 * ((size_t)(hi - 0xA4) * 157 + (lo <= 0x7E ? lo - 0x40 : lo - 0x62));
}

int walk_label_sprite(const uint8_t *big5, int chars, uint8_t *out)
{
    int w = 16 * chars;
    memset(out, 0, (size_t)w * 14);
    for (int k = 0; k < chars; k++) {
        uint8_t g[28];
        size_t off = walk_glyph_offset(big5[2 * k], big5[2 * k + 1]);
        if (off == (size_t)-1 || assets_read_at("NUMBER.LCZ", off, g, sizeof g) != 0)
            return -1;
        for (int y = 0; y < 14; y++) {
            unsigned v = (unsigned)g[2 * y] << 8 | g[2 * y + 1];
            uint8_t colour = y < 5 ? 0x89 : y < 10 ? 0x8A : 0x88;
            for (int x = 0; x < 16; x++)
                if (v >> (15 - x) & 1)
                    out[y * w + 16 * k + x] = colour;
        }
    }
    if (chars == 2) {   /* three overlapping recolouring passes (0x2741b), in this order */
        static const struct { int lo, hi; uint8_t c; } pass[3] = {{120, 270, 0x89}, {240, 390, 0x8A}, {360, 510, 0x8B}};
        for (int p = 0; p < 3; p++)
            for (int i = pass[p].lo; i < pass[p].hi && i < w * 14; i++)
                if (out[i] == 0x88)
                    out[i] = pass[p].c;
    }
    return 0;
}

static void blit_label(screen_t *scr, int x, int y, const uint8_t *px, int w, int h, int masked, uint8_t colour)
{
    for (int r = 0; r < h; r++)
        for (int c = 0; c < w; c++) {
            int sx = x + c, sy = y + r;
            if (px[r * w + c] && sx >= 0 && sx < SCREEN_W && sy >= 0 && sy < SCREEN_H)
                scr->pix[sy * SCREEN_W + sx] = masked ? px[r * w + c] : colour;
        }
}

int walk_draw_interface(screen_t *scr)
{
    /* The fourteen two-character labels are in MES_TXT.LCC at 0x386 (Big5, 4 bytes each); the lone 是 and 否 are
     * not read from there (the source of these two was not found), so their Big5 codes are written here. */
    static const struct { int x, y; } at[16] = {
        {17, 4}, {17, 24}, {17, 44}, {17, 64}, {17, 84}, {17, 104}, {17, 124}, {17, 144},
        {282, 60}, {282, 80}, {282, 100}, {282, 120}, {282, 140}, {282, 160}, {273, 40}, {297, 40}};
    static const uint8_t yes_no[4] = {0xAC, 0x4F, 0xA7, 0x5F};   /* 是 否 */
    uint8_t text[56];
    uint8_t sprites[16][32 * 14];
    int widths[16];
    if (assets_read_at("MES_TXT.LCC", 0x386, text, sizeof text) != 0)
        return -1;
    for (int i = 0; i < 16; i++) {
        const uint8_t *b = i < 14 ? text + 4 * i : yes_no + 2 * (i - 14);
        int chars = i < 14 ? 2 : 1;
        widths[i] = 16 * chars;
        if (walk_label_sprite(b, chars, sprites[i]) != 0)
            return -1;
    }
    /* Bottom right (BIOS tick 9): a box of colour 0 at (273, 185), 40 x 5, and a 7 x 5 piece of RPG.LCP at (289, 185). */
    uint8_t piece[35];
    if (assets_read_at("RPG.LCP", 0x9B8, piece, sizeof piece) != 0)
        return -1;
    for (int i = 0; i < 16; i++) {
        blit_label(scr, at[i].x, at[i].y + 2, sprites[i], widths[i], 14, 0, 0x8E);
        blit_label(scr, at[i].x, at[i].y + 1, sprites[i], widths[i], 14, 0, 0x8F);
        blit_label(scr, at[i].x, at[i].y, sprites[i], widths[i], 14, 1, 0);
    }
    for (int r = 0; r < 5; r++) {
        memset(scr->pix + (185 + r) * SCREEN_W + 273, 0, 40);
        memcpy(scr->pix + (185 + r) * SCREEN_W + 289, piece + r * 7, 7);
    }
    return 0;
}

int walk_first_screen(walk_t *w, screen_t *scr)
{
    return walk_begin(w, scr, 0, 0);
}

int walk_begin(walk_t *w, screen_t *scr, uint32_t seed, uint32_t ticks)
{
    return walk_begin_shown(w, scr, seed, ticks, NULL, NULL);
}

int walk_begin_shown(walk_t *w, screen_t *scr, uint32_t seed, uint32_t ticks, void (*show)(void *ctx, screen_t *scr), void *ctx)
{
    if (walk_draw_interface(scr) != 0)
        return -1;
    walk_sound_buttons(w, scr);
    if (walk_reset(w, seed) != 0)
        return -1;
    for (int k = 0; k < WALK_ZOOM_STEPS; k++) {   /* 0x11c25 .. 0x11ce6: the view comes in before the first pass */
        walk_zoom_step(w, scr, k);
        if (show)
            show(ctx, scr);
    }
    w->begin_result = walk_pass(w, scr, ticks, 0);   /* the first pass: its picture is the first screen */
    return 0;
}

/* ---- the start's zoom (0x11c25 .. 0x11ce6, 0x19ab2) ---- */

void walk_zoom_scale(uint8_t *dst, const uint8_t *src, unsigned sw, unsigned sh)
{
    enum { W = WALK_ZOOM_W, H = WALK_ZOOM_H };
    memset(dst, 0, W * H);
    unsigned xstep = (0xFFFFu * sw / W) & 0xFFFF, ystep = (0xFFFFu * sh / H) & 0xFFFF;
    size_t di = 0;
    unsigned dy = 1;
    for (unsigned row = 0; row < sh; row++) {
        size_t si = (size_t)row * sw;
        unsigned bx = 1;
        uint8_t al = src[si];
        for (unsigned x = 0; x < W; x++) {   /* a source pixel is written once, where the step carries; the others stay 0 */
            if (di < W * H)
                dst[di] = al;
            di++;
            al = 0;
            bx += xstep;
            if (bx > 0xFFFF) {
                bx &= 0xFFFF;
                al = src[++si];
            }
        }
        for (;;) {   /* rows the step does not carry into stay 0 */
            dy += ystep;
            if (dy > 0xFFFF) {
                dy &= 0xFFFF;
                break;
            }
            di += W;
        }
    }
}

void walk_zoom_keep(walk_t *w)
{
    fkey_frame(w);
    for (int r = 0; r < WALK_ZOOM_H; r++)
        memcpy(w->zoom_grab + r * WALK_ZOOM_W, w->buf + 0xE10 + r * WALK_W, WALK_ZOOM_W);
    w->scene->view_head = w->zoom_grab;   /* the map buffer's start: what the view reads above the map's top */
}

void walk_zoom_step(walk_t *w, screen_t *scr, int k)
{
    static uint8_t crop[WALK_ZOOM_W * WALK_ZOOM_H + 1], out[WALK_ZOOM_W * WALK_ZOOM_H];
    if (k == 0)   /* the world drawn as the F-keys draw it (the party standing; doubled in a dark scene), its window grabbed */
        walk_zoom_keep(w);
    /* step k: the w x h middle of the grab (24 x 24 growing by 14 x 12, the corner 0x3654 - 0x487 k) scaled to the window */
    unsigned cw = 24 + 14 * (unsigned)k, ch = 24 + 12 * (unsigned)k, off = 0x3654 - 0x487 * (unsigned)k;
    for (unsigned r = 0; r < ch; r++)
        memcpy(crop + r * cw, w->zoom_grab + off + r * WALK_ZOOM_W, cw);
    crop[cw * ch] = 0;   /* the scaling may read the byte after the crop, never to draw it */
    walk_zoom_scale(out, crop, cw, ch);
    for (int r = 0; r < WALK_ZOOM_H; r++)   /* 0x27328: shown at (64, 16), its zeros too */
        memcpy(scr->pix + (16 + r) * SCREEN_W + 64, out + r * WALK_ZOOM_W, WALK_ZOOM_W);
}

int walk_begin_result(const walk_t *w)
{
    return w->begin_result;
}

/* ---- for battle.c ---- */

uint32_t *walk_rng(walk_t *w)
{
    return &w->rng;
}

void walk_set_mouse(walk_t *w, int on, int x, int y, unsigned buttons)
{
    w->mouse_on = on;
    w->mouse_x = x;
    w->mouse_y = y;
    w->mouse_b = buttons;
}

int walk_mouse(const walk_t *w, int *x, int *y, unsigned *buttons)
{
    /* 0x260c9: function 3, x kept below 0x13b and y below 0xc3 */
    *x = w->mouse_x > 0x13A ? 0x13A : w->mouse_x < 0 ? 0 : w->mouse_x;
    *y = w->mouse_y > 0xC2 ? 0xC2 : w->mouse_y < 0 ? 0 : w->mouse_y;
    *buttons = w->mouse_b;
    return w->mouse_on;
}

void walk_mouse_grab(const screen_t *scr, unsigned at, uint8_t out[60])
{
    for (unsigned r = 0; r < 10; r++)
        for (unsigned c = 0; c < 6; c++) {
            unsigned o = at + r * SCREEN_W + c;
            out[r * 6 + c] = o < SCREEN_W * SCREEN_H ? scr->pix[o] : 0;
        }
}

void walk_mouse_put(screen_t *scr, unsigned at, const uint8_t px[60], int clear)
{
    for (unsigned r = 0; r < 10; r++)
        for (unsigned c = 0; c < 6; c++) {
            unsigned o = at + r * SCREEN_W + c;
            if (o < SCREEN_W * SCREEN_H && (!clear || px[r * 6 + c]))
                scr->pix[o] = px[r * 6 + c];
        }
}

uint8_t *walk_mouse_under(walk_t *w)
{
    return w->mbg;
}

const uint8_t *walk_mouse_pic(const walk_t *w)
{
    return w->mpic;
}

void walk_mouse_hide(walk_t *w, screen_t *scr)
{
    if (w->mouse_on)
        walk_mouse_put(scr, w->mprev, w->mbg, 0);
}

void walk_mouse_reset(walk_t *w, screen_t *scr)
{
    if (w->mouse_on) {
        w->mprev = 0;
        walk_mouse_grab(scr, 0, w->mbg);
    }
}

/* 0x10b87 (once, mouse or not): the pointer is at 0 and the 6 x 10 there is kept; 0x12608 (each pass, with a mouse): the pointer moved
 * on the screen (what was under it put back, the new place kept, the pointer drawn), and over the play window also into the world
 * buffer, so the window copy shows it there. */
static void mouse_start(walk_t *w, screen_t *scr)
{
    if (w->mstarted)
        return;
    w->mstarted = 1;
    w->mprev = 0;
    walk_mouse_grab(scr, 0, w->mbg);
    walk_show_gold(w, scr);   /* 0x10bc9 */
}

static void mouse_pass(walk_t *w, screen_t *scr, uint8_t *view, unsigned shift)
{
    int x, y;
    unsigned b;
    if (!walk_mouse(w, &x, &y, &b))
        return;
    w->mx = (unsigned)x;   /* [bp-0x46], [bp-0x70]: also the place a left press is taken at (walk_keys), even in the pass a battle resumes */
    w->my = (unsigned)y;
    unsigned pos = (unsigned)(y * SCREEN_W + x);
    if (pos != w->mprev) {
        walk_mouse_put(scr, w->mprev, w->mbg, 0);
        walk_mouse_grab(scr, pos, w->mbg);
        walk_mouse_put(scr, pos, w->mpic, 1);
        w->mprev = pos;
    }
    if (x > 0x3A && y > 6 && x < 0x100 && y < 0xB8) {   /* 0x26ad2 into the buffer, 0xe0 wide */
        w->v96 = (unsigned)(x - 0x30);   /* 0x126ba */
        for (unsigned r = 0; r < 10; r++)
            for (unsigned c = 0; c < 6; c++) {
                size_t o = shift + (size_t)(x - 0x30) + (size_t)y * WALK_W + r * WALK_W + c;
                if (w->mpic[r * 6 + c] && o < WALK_BUF_LEN)
                    view[o] = w->mpic[r * 6 + c];
            }
    }
}

void walk_palette_step(walk_t *w, screen_t *scr)
{
    palette_cycle(w, scr);
}

void walk_palette_step_dim(walk_t *w, screen_t *scr)
{
    palette_cycle_on(w, scr, w->dimpal);
}

void walk_set_show(walk_t *w, void (*show)(void *ctx, screen_t *scr), void *ctx)
{
    w->show = show;
    w->show_ctx = ctx;
}

battle_t *walk_cast(const walk_t *w)
{
    return w->talking == 7 ? w->cast : NULL;
}

void walk_show_gold(walk_t *w, screen_t *scr)
{
    walk_draw_gold(scr, w16(w->scene->tab_b, WALK_GOLD_AT), w->art + ART_DIGITS);
}

int walk_resume(walk_t *w, uint32_t ticks, unsigned port60, unsigned result)
{
    /* the touch left the enemy record's byte 0x2f in the hero's frame ([bp-0x5e]); the battle's result lands in [bp-0x3c] */
    w->hero_frame = w->battle_boss & 0xFF;
    w->ctr = result & 0xFF;
    w->unbuilt_new = 0;
    if (w->battle_boss && w->ctr == 5) {   /* 0x13300: di = 0xffff, the pass ends at 0x172f0 with si = 0xc8, through 0x18b82 */
        records_again(w, 0xC8 / WS_RECORD_SIZE);
        return w->unbuilt_new ? WALK_PASS_UNBUILT : 0;
    }
    return pass_tail(w, ticks, port60, w->stride);
}

void walk_boss_after(walk_t *w, unsigned result)
{    walk_scene_t *s = w->scene;
    if (!w->battle_boss)
        return;
    uint8_t *rec = s->tab_a + w->battle_rec;
    if ((result & 0xFF) == 5) {   /* 0x12fd1: all five killed: the enemy is gone and its flag set */
        rec[0] = rec[1] = 0;
        walk_flag_set(s->tab_b, rec[0xC], rec[0xD]);
        return;
    }
    uint8_t x = rec[0x28], y = rec[0x29];   /* 0x1302e: the party is put back where the record says, the trail too */
    for (int i = 0; i < 5; i++) {
        s->tab_b[WS_PARTY_SIZE * i] = x;
        s->tab_b[WS_PARTY_SIZE * i + 1] = y;
    }
    for (int i = 0; i < 13; i++) {
        s->trail[3 * i] = x;
        s->trail[3 * i + 1] = y;
        s->trail[3 * i + 2] = 1;
    }
}

unsigned *walk_battle_left(walk_t *w)
{
    return &w->battle_left;
}

void walk_set_sound(walk_t *w, unsigned fm, unsigned sb)
{
    w->fm = fm;
    w->sb = sb;
}

void walk_sound_buttons(const walk_t *w, screen_t *scr)
{
    if (w->fm == 0x81)
        walk_button_toggle(scr, 0x7B8D, 0x2F, 0x14);
    if (w->sb == 0x81)
        walk_button_toggle(scr, 0x948D, 0x2F, 0x14);
}

unsigned *walk_fm(walk_t *w)
{
    return &w->fm;
}

unsigned *walk_sb(walk_t *w)
{
    return &w->sb;
}

void walk_set_sfx(walk_t *w, const walk_sfx_t *sfx)
{
    if (sfx)
        w->sfx = *sfx;
    else
        memset(&w->sfx, 0, sizeof w->sfx);
}

void walk_sfx_play(walk_t *w, unsigned file, unsigned off)
{
    if (w->sb == 0x81 && w->sfx.play)
        w->sfx.play(w->sfx.ctx, file, off);
}

int walk_sfx_busy(walk_t *w)
{
    return w->sfx.busy ? w->sfx.busy(w->sfx.ctx) : 0;
}

void walk_sfx_stop(walk_t *w)
{
    if (w->sb == 0x81 && w->sfx.stop)
        w->sfx.stop(w->sfx.ctx);
}

void walk_sfx_wait(walk_t *w)
{
    if (w->sb == 0x81 && w->sfx.wait)
        w->sfx.wait(w->sfx.ctx);
}

void walk_battle_info(const walk_t *w, unsigned *member, unsigned *group, unsigned *boss, unsigned *rec)
{
    *member = w->battle_member;
    *group = w->battle_group;
    *boss = w->battle_boss;
    *rec = w->battle_rec;
}

void walk_battle_view(walk_t *w, const uint8_t table[256], uint8_t window[192 * 168])
{
    /* 0x1296e: the frame routine 0x19680 (not the main loop's own drawing) with the buffer's bytes 0..3 = 0, x, y, 1: byte 0 = 0
     * leaves out every villager and party member, byte 3 = 1 draws the ground and objects for 14 x 13 cells from buffer (64, 56)
     * only, which holds every pixel the doubling reads, so the full drawing gives the same picture there. 0x2c658 doubles the 96 x 84
     * pixels from (64, 57): p, t[p] / t[p], t[t[p]]; the window copy 0x2728d then takes its 192 x 168 from (16, 16). */
    walk_scene_t *s = w->scene;
    unsigned x = s->tab_b[0], y = s->tab_b[1];
    memset(w->buf, 0, WALK_BUF_LEN);
    walk_draw_ground(w->buf, s, x, y);
    walk_draw_objects(w->buf, s, x, y);   /* no entities: byte 0 of the buffer is 0, and 0x19713 then skips them */
    for (int r = 0; r < 84; r++)
        for (int c = 0; c < 96; c++) {
            uint8_t p = w->buf[0x3220 + r * WALK_W + c], q = table[p];
            uint8_t *d = window + (2 * r) * 192 + 2 * c;
            d[0] = p;
            d[1] = q;
            d[192] = q;
            d[193] = table[q];
        }
}
