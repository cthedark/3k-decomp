#include "walk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"

struct walk {
    walk_scene_t *scene;
    uint16_t drops[WALK_RAIN_DROPS];
    /* the moving world (walk_reset) */
    uint32_t rng;                       /* the game's rand() state (data segment 0x2d4) */
    uint16_t specks[WALK_SPECKS];
    uint8_t palbuf[WALK_PALBUF];        /* the palette buffer the cycle works on; the DAC takes its first 48 bytes */
    int pal_c[5];                       /* counters at data segment 0x532 .. 0x536 */
    unsigned hero_frame, ctr;           /* [bp-0x5e], [bp-0x3c] */
    unsigned v2e, v98, v48, c2;         /* [bp-0x2e], [bp-0x98], [bp-0x48], [bp-0xc2]: see walk_keys */
    int indoors;                        /* the byte [bp-0x9e] points to: set while a kind 3 record has taken the roof off */
    char unbuilt[120];                  /* the last thing the original did that is not built, "" when none */
    int unbuilt_new;                    /* set when it changed during the current pass */
    uint8_t *buf;                       /* the world buffer, a little longer than WALK_W * WALK_H (see walk_draw_specks) */
};

enum { WALK_BUF_LEN = WALK_W * WALK_H + 0x400 };

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
    memset(w->pal_c, 0, sizeof w->pal_c);
    w->hero_frame = 0;
    w->ctr = 0;
    w->v2e = 3;
    w->v98 = 0;
    w->v48 = 0;
    w->c2 = 0;
    w->indoors = 0;
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
    if (!w->buf || walk_reset(w, 0) != 0) {
        walk_free(w);
        return NULL;
    }
    return w;
}

void walk_free(walk_t *w)
{
    if (!w)
        return;
    walk_scene_free(w->scene);
    free(w->buf);
    free(w);
}

const walk_scene_t *walk_scene(const walk_t *w)
{
    return w->scene;
}

/* ---- the ground and the objects ---- */

void walk_draw_ground(uint8_t *buf, const walk_scene_t *s, unsigned x, unsigned y)
{
    int bx = ((int)(y >> 1) - 10) * 256 + (int)(x & 0xFE) - 0x16;
    int dx = 0xE10 - ((x & 1) ? 4 : 0) - ((y & 1) ? 0x380 : 0);
    for (int row = 0; row < 23; row++) {
        int b = bx, d = dx;
        for (int col = 0; col < 25; col++) {
            unsigned word = (b >= 0 && b + 2 <= WS_GMAP_BYTES) ? w16(s->gmap, (size_t)b) : 0;
            unsigned off = (word << 6) & 0xFFFF;   /* the original shifts in 16 bits */
            for (int k = 0; k < 8; k++)
                memcpy(buf + d + k * WALK_W, s->tiles + off + k * 8, 8);
            b += 2;
            d += 8;
        }
        bx += 256;
        dx += 8 * WALK_W;
    }
}

static int half_floor(int v)
{
    return v >= 0 ? v / 2 : -((-v + 1) / 2);
}

void walk_draw_objects(uint8_t *buf, const walk_scene_t *s, unsigned x, unsigned y)
{
    int bx = ((int)(y >> 1) - 10) * 128 + half_floor((int)(x & 0xFF) - 0x16);
    int dx = 0xE10 - ((x & 1) ? 4 : 0) - ((y & 1) ? 0x380 : 0);
    for (int row = 0; row < 23; row++) {
        int b = bx, d = dx;
        for (int col = 0; col < 25; col++) {
            unsigned id = (b >= 0 && b < WS_OMAP_BYTES) ? s->omap[b] : 0xFF;
            if (id <= 0xFE) {
                const uint8_t *spr = s->sprites + (id << 7);
                for (int k = 0; k < 8; k++)
                    for (int j = 0; j < 8; j++) {
                        uint8_t *p = buf + d + k * WALK_W + j;
                        *p = (uint8_t)((*p & spr[0x40 + k * 8 + j]) | spr[k * 8 + j]);
                    }
            }
            b++;
            d += 8;
        }
        bx += 128;
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
static void palette_cycle(walk_t *w, screen_t *scr)
{
    static const int limit[5] = {3, 10, 6, 12, 8};   /* data segment 0x532, 0x533, 0x534, 0x535, 0x536 */
    static const int order[5] = {1, 2, 3, 4, 0};     /* the order the original tests them in */
    for (int k = 0; k < 5; k++) {
        int c = order[k];
        if (++w->pal_c[c] <= limit[c])
            continue;
        w->pal_c[c] = 0;
        switch (c) {
        case 1: walk_pal_rotate(w->palbuf, 1, 3, 1); break;
        case 2: walk_pal_rotate(w->palbuf, 4, 3, 1); break;
        case 3: walk_pal_rotate(w->palbuf, 7, 4, 0); break;
        case 4: walk_pal_rotate(w->palbuf, 11, 4, 0); break;
        default: walk_pal_ramp(w->palbuf); break;
        }
        for (int i = 0; i < 16; i++)   /* 0x2c85e: 48 bytes to DAC entries 0 .. 15 */
            for (int ch = 0; ch < 3; ch++)
                scr->pal[i][ch] = w->palbuf[3 * i + ch] & 0x3F;
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
        return rec[0xA];
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

/* The hero stands on scene record `si` (0x172f0 .. 0x18b82). Kind 3 (a house's roof) does nothing unless the hero is
 * going through the door; the kinds the block names are not built; then the record loop runs again on the live state. */
static void hit_record(walk_t *w, unsigned si)
{
    walk_scene_t *s = w->scene;
    const uint8_t *rec = s->tab_a + 1000 + si;
    unsigned kind = rec[2], d = s->tab_b[2];
    if (kind == 3) {
        if ((d == 0 && rec[3] == 0 && !w->indoors) || (d == 1 && rec[3] == 1 && w->indoors))
            note_unbuilt(w, "a roof comes off or goes back", si / WS_RECORD_SIZE);
    } else if (kind == 0 || kind == 1 || kind == 4 || kind == 5 || kind == 7 || kind == 8 || kind == 0xE ||
               (kind >= 0x11 && kind <= 0x14)) {
        char what[48];
        snprintf(what, sizeof what, "scene record kind 0x%x", kind);
        note_unbuilt(w, what, si / WS_RECORD_SIZE);
    }
    int events = s->events;
    if (walk_scene_apply_records(s) != 0)
        note_unbuilt(w, "the record loop failed", si / WS_RECORD_SIZE);
    if (s->events != events)
        note_unbuilt(w, "a cut scene starts", si / WS_RECORD_SIZE);
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
        a = walk_shake_offset(w->v48, walk_rand(&w->rng));
        w->v48--;
    }
    return a;
}

/* 0x135f0 .. 0x13ad9 and the four walking handlers. */
static void walk_keys(walk_t *w, unsigned port60)
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
    if (k > 0x7F)
        c2 = 0;
    if ((k == 2 || k == 3 || k == 0x1E) && c2 == k)
        k = 0x82;
    if (k >= 0x80) {
        w->c2 = c2;
        return;
    }
    c2 = k;
    w->c2 = c2;
    int arrow = k == 0x48 || k == 0x50 || k == 0x4B || k == 0x4D;
    if (!arrow && k != 0) {
        char what[32];
        snprintf(what, sizeof what, "key 0x%x", k);
        note_unbuilt(w, what, 0);
        return;
    }
    if (w->v98 != 0 || k == 0)
        return;
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
}

int walk_pass(walk_t *w, screen_t *scr, uint32_t ticks, unsigned port60)
{
    walk_scene_t *s = w->scene;
    unsigned x = s->tab_b[0], y = s->tab_b[1];
    int event = 0;
    w->unbuilt_new = 0;

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
    walk_draw_objects(w->buf, s, x, y);
    int rainy = s->weather == 1 || s->weather == 2;
    if (rainy && (ticks & 0xFFFF) > 0xA000)
        walk_draw_specks(w->buf, WALK_BUF_LEN, w->specks);
    unsigned stride = walk_rain_stride(s->weather, ticks);
    if (stride && s->weather == 1)   /* mode 2 is read from the code but never seen: nothing is drawn for it */
        walk_draw_rain(w->buf, w->drops, stride);
    walk_copy_window_at(scr, w->buf, shake(w));

    for (int k = 0; k < WS_VILLAGERS; k++) {
        uint8_t *rec = s->tab_a + k * WS_VILLAGER_SIZE;
        int ret = walk_villager_step(rec, s->gmap, s->list);
        if (ret == 0xC)
            event = 1;
        else if (ret == 0xA)
            walk_villager_decide(rec, &w->rng, ticks, s->tab_b[0], s->tab_b[1], stride);
    }
    if (++w->v98 > (unsigned)s->speed_class)
        w->v98 = 0;
    walk_keys(w, port60);
    if (++w->ctr > (unsigned)s->speed_class + w->v2e) {
        w->ctr = 0;
        w->hero_frame = (w->hero_frame + 1) & 3;
    }
    return (event ? WALK_PASS_VILLAGER_EVENT : 0) | (w->unbuilt_new ? WALK_PASS_UNBUILT : 0);
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
}

/* ---- the window ---- */

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
    if (walk_draw_interface(scr) != 0)
        return -1;
    if (walk_reset(w, seed) != 0)
        return -1;
    walk_pass(w, scr, ticks, 0);   /* the first pass: its picture is the first screen */
    return 0;
}
