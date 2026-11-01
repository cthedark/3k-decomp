#include "walk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"
#include "battle.h"
#include "l10n.h"
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
                                         * 4 a look's message box (F2), 5 F3's wait, 6 F3's message box, 7 F4 to F7's lists, 8 F7's
                                         * prompt, 9 F8's map, 10 F9's order, 11 F10's data, 12 a shop, 13 its picker, 14 the
                                         * inn's night, 15 a number lock's or a scripted talk's message box */
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
    /* F7 at a sell counter (kind 0x14, 0x1864b): set while its yes / no or message box runs; half the item's price ([bp-0xbc]) */
    int use_selling;
    unsigned use_half;
    /* [bp-0x96], a scratch word: written by the world pass (a kill, a spark, the rain, the shake, the pointer in the window) and by F7;
     * the other F-keys' writes are not followed. F7's name blink starts from it. */
    unsigned v96;
    /* F8 (talking 9): the screen's rows 49 .. 150 under the map (the world buffer + 0x32c8), where the mark goes ([bp-4]) and the
     * blink's count ([bp-0xe]) */
    uint8_t map_under[SCREEN_W * 102];
    unsigned map_at, map_count;
    /* F9 (talking 10): the lit row's record offset ([bp-0x1c], 0x28 a member) and the last's ([bp-0x10]); the row picked up
     * ([bp-0x22]), the button let go ([bp-0x16]), the walk frame ([bp-0x20]), the frame's count ([bp-0x18]), the blink's
     * ([bp-0xe]), the key latch ([bp-0x24]), the pointer's last y ([bp-0xa]); the picture (RPG.LCP 0x7b5a) is in the world buffer */
    unsigned pos_at, pos_last, pos_sel, pos_rel, pos_frame, pos_ctr, pos_blink, pos_latch, pos_my;
    /* F10 (talking 11): the palette count ([bp-0x14]), the pictures' count ([bp-0x16]) and the picture shown ([bp-0x18]) */
    unsigned data_ctr, data_anim, data_frame;
    /* Keys 3 and 4 (0x14647 .. 0x14d46): 1 while the save's number box runs, 2 while the leave's yes / no buttons run (talking 3 or 2);
     * left: yes was chosen to leave, the program would now exec MARK.EXE; slide: [bp-0x76], key A's sliding round corners */
    int sys, left, slide;
    int ended;                          /* a kind 0x13 record has ended WALK.SL: the program would now exec ENDING.SL */
    void (*show)(void *ctx, screen_t *scr);   /* walk_set_show, handed to F4's picker */
    void *show_ctx;
    uint8_t talk_mbg[60];               /* the 6 x 10 under their pointer (the pointer's own + 0x40) */
    /* a shop (kind 5, 0x22c42): its record's bytes 3 .. 14 ([bp-0x80]), the shopkeeper's three 70 x 75 pictures, the strip at (0, 152)
     * as it was and its two shaded copies with their texts (the scratch buffer + 0x7d0, 0x1954, 0x2ad8), the picker's place kept
     * (+0x64); the items and prices (the map buffer + 0xbb8); [bp-0xa], [bp-0x10] (the lit row, the one lit last), [bp-0xe] (the
     * picture shown), [bp-0x18] (the pass counter), [bp-0x16] (the palette count), [bp-0x1c] and [bp-8] (the key and button latches) */
    uint8_t shop_rec[12];
    uint8_t shop_pics[3 * 0x1482];
    uint8_t shop_strip[320 * 14], shop_a[320 * 14], shop_b[320 * 14], shop_pk[90 * 18];
    unsigned shop_items[3], shop_prices[3];
    unsigned shop_cur, shop_prev, shop_pic, shop_count, shop_pal, shop_latch, shop_mlatch, shop_gw, shop_si;
    unsigned shop_pkc;                  /* what the next picker's count ([bp-0x16]) starts from, a stack leftover */
    /* the inn's night: the scratch buffer's pictures (RPG.LCP 0x7777 unpacked, the Zzz's places and counts at its start), the
     * sleepers drawn ([bp-0x1c]), the round counter ([bp-0xc]), and how many nights stopped the music (walk_inn_nights) */
    uint8_t inn_scr[0x798];
    unsigned inn_n, inn_c, inn_nights;
    unsigned shop_c;                    /* the shop's [bp-0xc]: the latched buttons, or the member a picker gave; the night counts from it */
    /* a number lock (kind 7) or a scripted talk (kind 0x11) while its number box or message boxes run: the kind, the record, the
     * talk's next entry in the map buffer ([bp-0x96]) */
    unsigned rec_kind, rec_si, rec_k;
    /* the card game of a kind 0xe record (0x24962, talking 16 its betting loop, 17 its wait, 18 its last message box): the deck
     * ([bp-0x14], one byte more that rand / 0xccd can reach), the bet ([bp-0x1e]), the coins while betting ([bp-6]), the lit card
     * ([bp-0xe]) and the one lit last ([bp-0x18]; in the wait the two dealer pictures), the dealer's card ([bp-0x24]), the way to win
     * ([bp-0x2e]), the card picked ([bp-4]), the key and button latches ([bp-0x22], [bp-0xc]), the repeat counters ([bp-8],
     * [bp-0x1c]), the palette count ([bp-0x1a]), the strip shown ([bp-0x16]) */
    uint8_t cg_deck[11];
    unsigned cg_bet, cg_coins, cg_cur, cg_last, cg_shown, cg_dir, cg_pick, cg_latch, cg_mlatch, cg_rep, cg_repc, cg_pal, cg_strip;
    unsigned cg_si;
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
    void (*zoom_show)(void *ctx, screen_t *scr);    /* walk_begin_shown's: called after each zoom step (a door's too) */
    void *zoom_ctx;
    unsigned scene_changes;                         /* doors gone through (walk_scene_changes) */
    unsigned port60;                                /* the keyboard port as the current pass reads it */
    unsigned (*live_port)(void);                    /* the port now, for a door's picture (walk_set_live_port), or NULL */
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
/* Key 3's number box: its palette count's leftover (0xe8 in the save probed, docs/findings.md step 37; any value from 0x16 up acts the
 * same) */
enum { WALK_SAVE_ASK_COUNT = 0xE8 };
/* A number lock's (kind 7): 0xee in scene 4's lock (SEEN, docs/findings.md step 43; any value from 0x16 up acts the same) */
enum { WALK_LOCK_ASK_COUNT = 0xEE };
/* The card game's palette count ([bp-0x1a], never set before its first use): 0x19 at the first round (SEEN, docs/findings.md step 45) */
enum { WALK_CARDS_PAL = 0x19 };
/* F8's map: the rows it keeps; the byte of its frame that says whether the first row widens ([bp-0xc], never set there: 0xc6, the low
 * byte of a segment number, in every start probed, docs/findings.md step 33) */
enum { WALK_MAP_UNDER_H = 102, WALK_MAP_TOGGLE = 0xC6 };

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
    w->sys = w->left = w->slide = w->ended = 0;   /* 0x10ba3: no sliding at the start */
    w->talking = 0;
    w->using = w->use_selling = 0;
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

/* 0x2c236 .. 0x2c28e, weather mode 2: snow on the rain's places and steps. The first 40 are 2 x 2 flakes (colours 0x14 over 0x16) at half
 * the stride, the other 35 crosses (0x10 on top, 0x11 0x11 0x12 across, 0x11 below) at the full stride. */
static void walk_draw_snow(uint8_t *buf, uint16_t drops[WALK_RAIN_DROPS], unsigned stride)
{
    static const struct { uint16_t off; uint8_t c; } flake[4] = {{0, 0x14}, {1, 0x14}, {0xE0, 0x16}, {0xE1, 0x16}},
        cross[5] = {{1, 0x10}, {0xE0, 0x11}, {0xE1, 0x11}, {0xE2, 0x12}, {0x1C1, 0x11}};
    for (int i = 0; i < WALK_RAIN_DROPS; i++) {
        int small = i < 40;
        for (unsigned k = 0; k < (small ? 4u : 5u); k++) {
            unsigned p = drops[i] + (small ? flake[k].off : cross[k].off);
            if (p < (unsigned)(WALK_W * WALK_H))
                buf[p] = small ? flake[k].c : cross[k].c;
        }
        unsigned p = drops[i] + (small ? stride >> 1 : stride);
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

int walk_slide_ok(const uint8_t *gmap, const uint8_t *list, unsigned x, unsigned y)
{
    unsigned idx = (((y & 0xFF) >> 1) << 8) | (x & 0xFE);
    return tile_listed(gmap, idx, list) && tile_listed(gmap, idx + 2, list);
}

size_t walk_rle_pack(const uint8_t *in, size_t n, uint8_t *out)
{
    uint16_t count[256] = {0};
    for (size_t i = 0; i < n; i++)
        count[in[i]]++;
    unsigned least = 0xFFFF, mark = 0;
    for (unsigned b = 0; b < 256; b++)
        if (count[b] < least) {
            least = count[b];
            mark = b;
        }
    out[0] = (uint8_t)mark;
    size_t o = 3, si = 0;
    while (si < n) {
        uint8_t al = in[si];
        unsigned dl;
        if (al == mark)
            dl = 0;   /* the marker byte itself: one fewer than the run is counted (the unpacker then drops one) */
        else if (si + 2 < n && in[si + 1] == al && in[si + 2] == al)
            dl = 1;
        else {
            out[o++] = al;
            si++;
            continue;
        }
        int ended = 0;
        for (;;) {
            si++;
            if (si == n) {
                ended = 1;
                break;
            }
            if (in[si] != al) {
                si--;
                break;
            }
            if (++dl == 0xFF)
                break;
        }
        out[o++] = (uint8_t)mark;
        out[o++] = (uint8_t)dl;
        out[o++] = al;
        if (ended || ++si == n)
            break;
    }
    out[1] = (uint8_t)o;
    out[2] = (uint8_t)(o >> 8);
    return o;
}

size_t walk_rle_unpack(const uint8_t *in, size_t n, uint8_t *out, size_t cap)
{
    if (n < 3)
        return 0;
    size_t end = (size_t)in[1] | (size_t)in[2] << 8, si = 3, o = 0;
    if (end > n)
        end = n;
    while (si < end) {
        if (in[si] != in[0]) {
            if (o < cap)
                out[o] = in[si];
            o++;
            si++;
            continue;
        }
        if (si + 2 >= n)
            break;
        for (unsigned k = 0; k < in[si + 1]; k++, o++)
            if (o < cap)
                out[o] = in[si + 2];
        si += 3;
    }
    return o < cap ? o : cap;
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

static void door(walk_t *w, unsigned si);
static void shop_open(walk_t *w, screen_t *scr, unsigned si);
static void boat(walk_t *w, unsigned si);
static void warp(walk_t *w, unsigned si);
static void cards_open(walk_t *w, screen_t *scr, unsigned si);
static void use_sell(walk_t *w, screen_t *scr);
static void frame_at(walk_t *w, unsigned x, unsigned y, int people);
static int lock_open(walk_t *w, screen_t *scr, unsigned si);
static void rec_talk_open(walk_t *w, screen_t *scr, unsigned si);

void walk_record_kind4(uint8_t *party, const uint8_t *rec)
{
    unsigned di = 3;
    while (di < 0xC8 && party[di] != rec[3])
        di += WS_PARTY_SIZE;
    if (di >= 0xC8 || (party[di + 5] | party[di + 6] << 8) == 0)
        return;
    if (party[di + 9] < rec[4])
        party[di + 9] = rec[4];
    walk_flag_set(party, rec[5], rec[6]);
}

static void hit_record(walk_t *w, unsigned si)
{
    walk_scene_t *s = w->scene;
    const uint8_t *rec = s->tab_a + 1000 + si;
    unsigned kind = rec[2], d = s->tab_b[2];
    if (kind == 1) {
        door(w, si);
        return;
    }
    if (kind == 8 && w->scr) {
        boat(w, si);
        return;
    }
    /* Kind 0x13, the ending (0x18548 .. 0x18639): when its flag (byte 3, bit byte 4) is set, the Sound Blaster driver ended (0x190e:0x76),
     * the music stopped (0x165d:0x126, 0x165d:0x14e) when it is on, six buffers freed and execl("ending.sl") (walk_ended). */
    if (kind == 0x13 && w->scr) {
        if (walk_flag_test(s->tab_b, rec[3], rec[4])) {
            w->ended = 1;
            return;
        }
        w->vbe = kind;
        records_again(w, si / WS_RECORD_SIZE);
        return;
    }
    /* Kind 0x14 stepped on (0x1864b): the sale of F7's item path, on whatever the map buffer holds (its name, byte 0x11, its price)
     * and F7's member and item as F7 last left them; at its end F7's button is turned and [bp-0xbe] = 1, as after F7. No shipped
     * counter can be stepped on (each blocks the hero on all four sides, and a hero placed on one cannot move), so this is read, not
     * compared (docs/findings.md step 47). */
    if (kind == 0x14 && w->scr) {
        w->using = 1;   /* the boxes step the palette itself ([bp-0xac]), as in F7 */
        use_sell(w, w->scr);
        return;
    }
    if (kind == 0xE && w->scr) {   /* the card game: its loops run until it ends (cg_done) */
        cards_open(w, w->scr, si);
        return;
    }
    if (kind == 0 && w->scr) {
        warp(w, si);
        w->vbe = 0;
        return;
    }
    if (kind == 7 && w->scr && lock_open(w, w->scr, si))   /* a number lock: the rest when its boxes are done (rec_end) */
        return;
    if (kind == 0x11 && w->scr) {   /* a scripted talk (0x18305) */
        rec_talk_open(w, w->scr, si);
        return;
    }
    if (kind == 5 && w->scr) {   /* a shop (0x17d6c): its loop runs until it is left, then the record loop */
        shop_open(w, w->scr, si);
        return;
    }
    if (kind == 4)
        walk_record_kind4(s->tab_b, rec);
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
    } else if (!w->scr && (kind == 0 || kind == 7 || kind == 8 || kind == 0xE || kind == 0x11 || kind == 0x13 || kind == 0x14)) {
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
/* 0x11b2b .. 0x11c08, where a kind 0x10 record fires in the loop (the records after it not applied yet): the world drawn again
 * (0x19680, the party standing), its window grabbed into the map buffer (0x2725d), then 50 rounds of the window filled with colour 0x10
 * and the grab put back over it. The rounds do not wait, so only the last is seen: the grab. */
static void cut_flash(void *ctx)
{
    walk_t *w = ctx;
    walk_zoom_keep(w);
    for (int r = 0; r < WALK_ZOOM_H; r++)
        memcpy(w->scr->pix + 0x1440 + r * SCREEN_W, w->zoom_grab + r * WALK_ZOOM_W, WALK_ZOOM_W);
}

static void records_again(walk_t *w, unsigned which)
{
    walk_scene_t *s = w->scene;
    if (!w->vbe)
        return;
    if (walk_scene_apply_records_cb(s, w->scr ? cut_flash : NULL, w) != 0)
        note_unbuilt(w, "the record loop failed", which);
    w->use_count = 0;   /* [bp-0xc4], which the loop writes (0x11409, 0x11b07, ...): 0 after it in every start probed (INFERRED) */
}

/* (0x199b4) A door's zoom out: the window (192 x 168 at (64, 16)) grabbed with its header into the buffer [bp-0x8c], then 13 steps:
 * its middle w x h (192 x 168 shrinking by 14 x 12, the corner 0x487 k on) scaled to the window in the map buffer and shown, a
 * retrace wait before each. The steps are the start's zoom in backwards. */
static void zoom_out(walk_t *w, screen_t *scr)
{
    static uint8_t grab[WALK_ZOOM_W * WALK_ZOOM_H], crop[WALK_ZOOM_W * WALK_ZOOM_H + 1];
    for (int r = 0; r < WALK_ZOOM_H; r++)
        memcpy(grab + r * WALK_ZOOM_W, scr->pix + (16 + r) * SCREEN_W + 64, WALK_ZOOM_W);
    for (unsigned k = 0; k < 13; k++) {
        unsigned cw = WALK_ZOOM_W - 14 * k, ch = WALK_ZOOM_H - 12 * k, off = 0x487 * k;
        for (unsigned r = 0; r < ch; r++)
            memcpy(crop + r * cw, grab + off + r * WALK_ZOOM_W, cw);
        crop[cw * ch] = 0;
        walk_zoom_scale(w->zoom_grab, crop, cw, ch);
        for (int r = 0; r < WALK_ZOOM_H; r++)
            memcpy(scr->pix + (16 + r) * SCREEN_W + 64, w->zoom_grab + r * WALK_ZOOM_W, WALK_ZOOM_W);
        if (w->zoom_show)
            w->zoom_show(w->zoom_ctx, scr);
    }
}

/* 0x10bf9 .. 0x11ce6 after a door: the loader on the party table in memory, then the zoom in. */
static int scene_enter(walk_t *w, screen_t *scr)
{
    walk_scene_t *n = walk_scene_load_party(w->scene->tab_b);
    if (!n)
        return -1;
    walk_scene_free(w->scene);
    w->scene = n;
    w->scene_changes++;
    memset(w->fx, 0xFA, sizeof w->fx);
    for (int i = 0; i < WALK_SHOTS; i++)
        w->fx[4 * i + 2] = 4;
    for (int j = 0; j < WALK_SPARKS; j++)
        w->fx[4 * WALK_SHOTS + 4 * j] = 9;
    w->v48 = n->header[3];   /* 0x11233 */
    for (int k = 0; k < WALK_ZOOM_STEPS; k++) {
        walk_zoom_step(w, scr, k);
        if (w->zoom_show)
            w->zoom_show(w->zoom_ctx, scr);
    }
    return 0;
}

/* Kind 1, a door (0x1768a .. 0x17b0b): the first 100 words of [bp-0x8c] set to 10000, 10050, .. (not kept here: the zoom out's grab
 * covers them, and the DAC does not change); each villager's byte 8 put back from its copy; with byte 0xe set the door's picture
 * (door_picture) and the sound at SND_EF 0x5571, else the sound at 0x4086; the zoom out; the scene's 1750 bytes written into
 * __1.NOW at 1750 x the scene (party byte 510); party bytes 510 .. 513 = the record's bytes 3 .. 6 (the new scene, its byte 511, x and
 * y / 2); [bp-0x74] set, so 0x18b82 goes on to the loader (0x10bf9) for the new scene. */
static void fkey_frame(walk_t *w);

void walk_spin(uint8_t *party)
{
    static const uint8_t next[4] = {3, 2, 0, 1};
    for (unsigned i = 0; i < 0xC8; i += WS_PARTY_SIZE)
        if (party[i + 2] < 4)
            party[i + 2] = next[party[i + 2]];
}

/* 0x1772a .. 0x17a2b, a door with byte 0xe set: RPG.LCP 0x1236 (0x14e bytes) read into the world buffer and unpacked (0x25eeb) into
 * [bp-0x20]; its two 256-byte halves are map rows 1 and 4, and 7 and 10, of the map buffer [bp-0xba], whose rows 0 .. 11 are then copied
 * three times below (the other rows keep what the map buffer held). Then up to 60 steps: a round counter ([bp-0x10], 0 here in every
 * start probed) counts up and, once past [bp-0x78] (3, then 0 from step 20), goes back to 0, the party turns (0x2489a) and a step is
 * counted; the world drawn as the F-keys draw it (0x19680), on the map buffer's map once past step 20; the window copied to the
 * screen (0x2b9ec). A make code on the keyboard port ends it (0x1732:0x198). */
static void door_picture(walk_t *w)
{
    walk_scene_t *s = w->scene;
    uint8_t *mb = w->zoom_grab;
    uint8_t packed[0x14E];
    if (assets_read_at("RPG.LCP", 0x1236, packed, sizeof packed) != 0) {
        note_unbuilt(w, "RPG.LCP without the door's picture (0x1236)", 0);
        return;
    }
    memcpy(w->buf, packed, sizeof packed);
    walk_rle_unpack(packed, sizeof packed, w->dbuf, WALK_BUF_LEN);
    memcpy(mb + 0x100, w->dbuf, 0x100);
    memcpy(mb + 0x400, w->dbuf, 0x100);
    memcpy(mb + 0x700, w->dbuf + 0x100, 0x100);
    memcpy(mb + 0xA00, w->dbuf + 0x100, 0x100);
    for (unsigned k = 1; k <= 4; k++)
        memcpy(mb + 0xC00 * k, mb, 0xC00);
    /* past the map buffer's 192 x 168 the port has zeros (a villager's sprite read from the party bank's far end, GUESS) */
    static uint8_t alt_bank[WS_BANK2_PAD];
    unsigned v10 = 0, v78 = 3, c4 = 0;
    while (c4 < 0x3C) {
        v10 = (v10 + 1) & 0xFF;
        if (v10 > v78) {
            v10 = 0;
            walk_spin(s->tab_b);
            c4++;
        }
        if (c4 == 0x14)
            v78 = 0;
        if (c4 > 0x14) {   /* the map buffer as the party's sprite bank ([bp-0x38]): the party drawn as the picture's ball */
            uint8_t *bank = s->bank2;
            memcpy(alt_bank, mb, sizeof w->zoom_grab);
            s->bank2 = alt_bank;
            fkey_frame(w);
            s->bank2 = bank;
        } else {
            fkey_frame(w);
        }
        if (w->scr) {
            walk_copy_window(w->scr, w->buf);
            if (w->zoom_show)
                w->zoom_show(w->zoom_ctx, w->scr);
        }
        unsigned key = (w->live_port ? w->live_port() : w->port60) & 0xFF;
        if (key < 0x7F)
            break;
    }
}

/* The end of a door or a boat: the scene's 1750 bytes written into __1.NOW at 1750 x the scene (party byte 510), then party bytes
 * 510 .. 513 = the record's bytes 3 .. 6. */
static void scene_leave(walk_t *w, const uint8_t *rec)
{
    uint8_t *ta = w->scene->tab_a, *tb = w->scene->tab_b;
    if (assets_write_at("__1.now", (size_t)WS_STATE * tb[510], ta, WS_STATE) != 0)
        note_unbuilt(w, "__1.NOW that cannot be written", tb[510]);
    memcpy(tb + 510, rec + 3, 4);
}

/* Kind 0, a warp (0x1732f .. 0x17678), when its flag (byte 3, bit byte 4) is set: up to 60 rounds of the party turning (0x2489a,
 * every second round on the round counter [bp-0x10]), the world drawn as the F-keys draw it (0x19680) at the view's place and the
 * window copied to the screen (0x2b9ec); a make code on the keyboard port ends them (0x1732:0x198). Then the view slides a step a
 * round in x and in y towards (byte 5, byte 6), the world drawn without its people (the buffer's byte 0 = 0), until it is there (no
 * round when it already is); then the five party records and the 13 trail entries are put at that place (the trail facing 1). The
 * record loop does not run again ([bp-0xbe] is 0). */
static void warp(walk_t *w, unsigned si)
{
    walk_scene_t *s = w->scene;
    uint8_t *party = s->tab_b;
    const uint8_t *rec = s->tab_a + 1000 + si;
    if (!walk_flag_test(party, rec[3], rec[4]))
        return;
    unsigned x = party[0], y = party[1], v10 = 0;   /* [bp-0x8e], [bp-0xb6]; [bp-0x10] 0 here (GUESS, as door_picture's) */
    for (unsigned c4 = 0; c4 < 0x3C; c4++) {
        v10 = (v10 + 1) & 0xFF;
        if (v10 > 1) {
            v10 = 0;
            walk_spin(party);
        }
        frame_at(w, x, y, 1);
        walk_copy_window(w->scr, w->buf);
        if (w->zoom_show)
            w->zoom_show(w->zoom_ctx, w->scr);
        unsigned key = (w->live_port ? w->live_port() : w->port60) & 0xFF;
        if (key < 0x7F)
            break;
    }
    unsigned tx = rec[5], ty = rec[6];
    while (x != tx || y != ty) {
        frame_at(w, x, y, 0);
        walk_copy_window(w->scr, w->buf);
        if (w->zoom_show)
            w->zoom_show(w->zoom_ctx, w->scr);
        if (x > tx)
            x--;
        if (x < tx)
            x++;
        if (y > ty)
            y--;
        if (y < ty)
            y++;
    }
    for (unsigned o = 0; o < 0xC8; o += WS_PARTY_SIZE) {
        party[o] = (uint8_t)x;
        party[o + 1] = (uint8_t)y;
    }
    for (unsigned k = 0; k < 13; k++) {
        s->trail[3 * k] = (uint8_t)x;
        s->trail[3 * k + 1] = (uint8_t)y;
        s->trail[3 * k + 2] = 1;
    }
}

static void door(walk_t *w, unsigned si)
{
    walk_scene_t *s = w->scene;
    uint8_t *ta = s->tab_a;
    const uint8_t *rec = ta + 1000 + si;
    for (int i = 0; i < WS_VILLAGERS; i++)
        ta[WS_VILLAGER_SIZE * i + 8] = ta[WS_STATE + i];
    if (rec[0xE]) {
        door_picture(w);
        walk_sfx_play(w, SFX_EF, 0x5571);   /* 0x17a2b */
    } else {
        walk_sfx_play(w, SFX_EF, 0x4086);   /* [bp-0x96] = 0x4086 (0x17709) */
    }
    if (w->scr)
        zoom_out(w, w->scr);
    scene_leave(w, rec);
    w->vbe = 1;
    if (!w->scr || scene_enter(w, w->scr) != 0)
        note_unbuilt(w, "a door to a scene that does not load", si / WS_RECORD_SIZE);
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
    frame_at(w, w->scene->tab_b[0], w->scene->tab_b[1], 1);
}

/* The same with the view at (x, y); byte 0 = 0 (people 0) leaves out every villager and party member (a warp's slide). */
static void frame_at(walk_t *w, unsigned x, unsigned y, int people)
{
    walk_scene_t *s = w->scene;
    w->buf[0] = (uint8_t)people;
    w->buf[1] = (uint8_t)x;
    w->buf[2] = (uint8_t)y;
    w->buf[3] = s->header[5];
    walk_draw_ground(w->buf, s, x, y);
    if (people)
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
static void map_round(walk_t *w, screen_t *scr, unsigned port60);
static void order_round(walk_t *w, screen_t *scr, unsigned port60);
static void data_round(walk_t *w, screen_t *scr, unsigned port60);
static void shop_round(walk_t *w, screen_t *scr, unsigned port60);
static void shop_pick_round(walk_t *w, screen_t *scr, unsigned port60);
static void inn_begin(walk_t *w, screen_t *scr);
static void inn_round(walk_t *w, screen_t *scr, unsigned port60);
static void save_given(walk_t *w, screen_t *scr, unsigned slot);
static void leave_answer(walk_t *w, screen_t *scr, int yes);
static void lock_given(walk_t *w, screen_t *scr, unsigned value);
static void rec_box_round(walk_t *w, screen_t *scr, unsigned port60);
static void rec_end(walk_t *w);
static void cg_bet_round(walk_t *w, screen_t *scr, unsigned port60);
static void cg_wait_round(walk_t *w, screen_t *scr, unsigned port60);
static void cg_box_round(walk_t *w, screen_t *scr, unsigned port60);

/* The palette the yes / no buttons and the number step on: the dimmed copy in a talk, the palette itself in F7. */
static void ask_step(walk_t *w, screen_t *scr)
{
    if (w->using || w->sys || w->rec_kind)   /* a number lock's box steps the palette itself ([bp-0xac]) */
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
    if (w->sys) {
        leave_answer(w, scr, w->yn_sel);
        return;
    }
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
        if (w->sys)
            save_given(w, scr, 0xFFFF);
        else if (w->rec_kind)
            rec_end(w);
        else if (w->using)
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
    w->talk_count = w->sys ? WALK_SAVE_ASK_COUNT : w->rec_kind ? WALK_LOCK_ASK_COUNT : w->using ? WALK_USE_ASK_COUNT : 0x16;
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
    if (w->sys) {
        save_given(w, scr, w->ask_value);
        return;
    }
    if (w->rec_kind == 7) {
        lock_given(w, scr, w->ask_value);
        return;
    }
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
    if (w->talking == 9) {
        map_round(w, scr, port60);
        return;
    }
    if (w->talking == 10) {
        order_round(w, scr, port60);
        return;
    }
    if (w->talking == 11) {
        data_round(w, scr, port60);
        return;
    }
    if (w->talking == 12) {
        shop_round(w, scr, port60);
        return;
    }
    if (w->talking == 13) {
        shop_pick_round(w, scr, port60);
        return;
    }
    if (w->talking == 14) {
        inn_round(w, scr, port60);
        return;
    }
    if (w->talking == 15) {
        rec_box_round(w, scr, port60);
        return;
    }
    if (w->talking == 16) {
        cg_bet_round(w, scr, port60);
        return;
    }
    if (w->talking == 17) {
        cg_wait_round(w, scr, port60);
        return;
    }
    if (w->talking == 18) {
        cg_box_round(w, scr, port60);
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

/* ---- F8: the map (0x15ab0 .. 0x15bbd, 0x1c6e4) ---- */

/* The end of F8: the map's screen put back (0x27328(0x3d40, the world buffer + 0x32c8)) when it was shown, the button let go, then
 * 0x1729e. */
static void map_done(walk_t *w, screen_t *scr, int shown)
{
    if (shown)
        put(scr, 0x3D40, w->map_under, SCREEN_W, WALK_MAP_UNDER_H);
    walk_button_toggle(scr, 0xB184, 0x2F, 0x14);
    w->talking = 0;
    records_again(w, 0x384 / WS_RECORD_SIZE);
}

int walk_map_has(const uint8_t *party, unsigned *v96)
{
    for (unsigned si = 0x14; si < 0xC8; si += 0x28) {
        *v96 = si + 0x14;
        for (unsigned di = si; di < si + 0x14; di++)
            if (party[di] == 0x13)
                return 1;
    }
    return 0;
}

void walk_map_row(const uint8_t *map, unsigned w, uint8_t *out)
{
    out[0] = 0xB0;
    battle_stretch_row(map, 0x80, out + 1, w);
    out[w + 1] = 0xB0;
}

/* 0x1c6e4: the screen's rows 49 .. 150 kept; one pixel per map cell (the middle-left pixel, byte 0x20, of its tile), 128 x 100 in the
 * world buffer from byte 4, colours 0xbf, 1, 2, 3 turned 0x2f, 0x57, 0x59, 0x5a, then the hero's cell 0xbf. A line of 0xb7 over it
 * at (97, 49), 128 wide; then each map row in the map buffer [bp-0xba], stretched to the row's width (0x26de8) between two 0xb0 edges
 * and shaded 7 times for the first 12 rows, one time less every 13 rows after, from (96, 50), 130 wide, every second row one wider
 * each side: whether the first row already widens is a byte the frame never sets ([bp-0xc]; 0xc6 in every start probed: it does,
 * INFERRED). The hero's 0xbf found in his row is where the mark goes; under the last row a line of 0xb0 as wide. The mark: the 4 x 4
 * from a pixel up and left of him kept (in the map buffer, and 0x14 on), its middle rows 0x10 and the two middle pixels of the
 * first and last 0x12. */
static void map_show(walk_t *w, screen_t *scr)
{
    walk_scene_t *s = w->scene;
    uint8_t *party = s->tab_b, *b = w->buf, *c = w->zoom_grab;
    grab(scr, 0x3D40, SCREEN_W, WALK_MAP_UNDER_H, w->map_under);
    for (unsigned i = 0, j = 4; i < 0x6400; i += 2, j++) {
        unsigned t = (w16(s->gmap, i) << 6) & 0xFFFF;
        b[j] = (uint8_t)art_byte(s, 0x7F96 + t + 0x20);
    }
    static const uint8_t from[4] = {0xBF, 1, 2, 3}, to[4] = {0x2F, 0x57, 0x59, 0x5A};
    for (int k = 0; k < 4; k++)   /* 0x2741b */
        for (unsigned j = 4; j < 4 + 0x3200; j++)
            if (b[j] == from[k])
                b[j] = to[k];
    unsigned hy = (party[1] >> 1) & 0xFF;
    b[4 + (((party[0] & 0xFE) >> 1) + (hy << 7))] = 0xBF;
    unsigned left = (hy + 1) & 0xFF;
    fill(scr, 0x3DA1, 0x80, 1, 0xB7);
    unsigned width = 0x80, at = 0x3EE0, rows = 0, shades = 7, mark = 0;
    int toggle = WALK_MAP_TOGGLE != 0, found = 0;
    for (unsigned a = 0; a < 0x3200; a += 0x80) {
        b[a] = 0x80;   /* the row's header over the last 4 pixels of the one before */
        b[a + 1] = 0;
        b[a + 2] = 1;
        b[a + 3] = 0;
        if (++rows > 12) {
            shades--;
            rows = 0;
        }
        c[0] = (uint8_t)(width + 2);
        c[1] = (uint8_t)((width + 2) >> 8);
        c[2] = 1;
        c[3] = 0;
        walk_map_row(b + a + 4, width, c + 4);
        if (left != 0 && --left == 0) {
            mark = at + 0x13C;
            for (unsigned k = 5; k < 0x140; k++)
                if (c[k] == 0xBF) {
                    mark = at + k - 4;
                    found = 1;
                    break;
                }
        }
        for (unsigned t = 0; t < (shades & 0xFF); t++)   /* 0x25dca over the w + 2 bytes, the edges too */
            for (unsigned i = 0; i < width + 2; i++)
                c[4 + i] = w->table[c[4 + i]];
        put(scr, at, c + 4, width + 2, 1);
        at += SCREEN_W;
        if (toggle) {
            toggle = 0;
            at -= 1;
            width += 2;
        } else {
            toggle = 1;
        }
    }
    fill(scr, at, width + 2, 1, 0xB0);
    if (!found)
        note_unbuilt(w, "the map with the hero off its rows (a mark from the stack)", 0);
    mark -= 0x141;
    w->map_at = mark;
    c[0] = 4;
    c[1] = 0;
    c[2] = 4;
    c[3] = 0;
    grab(scr, mark, 4, 4, c + 4);
    memcpy(c + 0x14, c, 0x14);   /* 0x27503 */
    c[5] = c[6] = 0x12;
    memset(c + 8, 0x10, 8);
    c[0x11] = c[0x12] = 0x12;
    w->map_count = width;   /* [bp-0xe] is the last row's width when the loop starts */
    w->talking = 9;
}

/* F8: the button pressed (the pointer's corner put back first); the five party records' 20 item places looked at for the map
 * (item 0x13), [bp-0x96] the record's place + 0x28 as it goes (0xc8 when none has it). Found, the hero's x (even) in [bp-0xc4] and
 * the map. */
static void map_key(walk_t *w, screen_t *scr)
{
    uint8_t *party = w->scene->tab_b;
    if (w->mouse_on)
        walk_mouse_put(scr, w->mprev, w->mbg, 0);
    walk_button_toggle(scr, 0xB184, 0x2F, 0x14);   /* 0x1c3ba: F8's button pressed */
    if (!walk_map_has(party, &w->v96)) {
        map_done(w, scr, 0);
        return;
    }
    w->use_count = party[0] & 0xFE;
    map_show(w, scr);
}

/* A round of the map's loop (0x1ca82): the mark shown for 45 rounds and the kept 4 x 4 for 36; the palette step every round; Esc,
 * Space or Enter (no release needed) or the right button alone ends it. (The song's loop it also tests is the main loop's.) */
static void map_round(walk_t *w, screen_t *scr, unsigned port60)
{
    if (++w->map_count > 0x50)
        w->map_count = 0;
    put(scr, w->map_at, w->zoom_grab + (w->map_count < 0x2D ? 4 : 0x18), 4, 4);
    palette_cycle(w, scr);
    unsigned k = port60 & 0xFF;
    int mx, my;
    unsigned mb = 0;
    if (k == 1 || k == 0x39 || k == 0x1C || (walk_mouse(w, &mx, &my, &mb) && (mb & 0xFF) == 2))
        map_done(w, scr, 1);
}

/* ---- F9: the party's order (0x1546a .. 0x154e0, 0x1b748) ---- */

unsigned walk_order_members(const uint8_t *party)
{
    unsigned n = 0;
    while (n < 5 && party[n * 0x28 + 3] <= 4)
        n++;
    return n;
}

void walk_order_swap(uint8_t *party, unsigned at)
{
    for (unsigned i = 3; i < 0x28; i++) {   /* 0x25c8e, 0x25 bytes: each keeps its place and facing (bytes 0 .. 2) */
        uint8_t t = party[at + i];
        party[at + i] = party[at + 0x28 + i];
        party[at + 0x28 + i] = t;
    }
}

/* The lit row up or down (`d` -1 or 1); with a row picked up (`sel`), the member goes with it. */
static void order_move(walk_t *w, int d, int sel)
{
    uint8_t *party = w->scene->tab_b;
    if (sel != (w->pos_sel != 0))
        return;
    if (d < 0 && w->pos_at != 0) {
        w->pos_at -= 0x28;
        if (sel)
            walk_order_swap(party, w->pos_at);
    }
    if (d > 0 && w->pos_at < w->pos_last) {
        if (sel)
            walk_order_swap(party, w->pos_at);
        w->pos_at += 0x28;
    }
}

/* The end of F9: the function returns with the picture still shown (the world drawn after it covers it); the button let go, then
 * 0x1729e. */
static void order_done(walk_t *w, screen_t *scr)
{
    walk_button_toggle(scr, 0x498D, 0x2F, 0x14);
    w->talking = 0;
    records_again(w, 0x384 / WS_RECORD_SIZE);
}

/* F9: the button pressed (the pointer's corner put back first); 0x1b748: RPG.LCP 0x7b5a (0x271 bytes) decoded into the world buffer
 * (0x25eeb), a 20 x 100 picture of five 20 x 20 rows whose height becomes 20 for each member before the first record whose number
 * (byte 3) is above 4. The frame's count, blink count and key latch are never set: 0xc6, 0x45 and 0x32 in every start probed
 * (INFERRED, stack leftovers); the pointer's last y is where F9's call to its button put 0x498d. */
static void order_key(walk_t *w, screen_t *scr)
{
    uint8_t *party = w->scene->tab_b;
    if (w->mouse_on)
        walk_mouse_put(scr, w->mprev, w->mbg, 0);
    walk_button_toggle(scr, 0x498D, 0x2F, 0x14);   /* 0x1c3ba: F9's button pressed */
    size_t len = 0;
    uint8_t *pic = battle_read_unpack("RPG.LCP", 0x7B5A, 0x271, &len);
    if (!pic || len < 0x7D4) {
        free(pic);
        note_unbuilt(w, "F9's picture that RPG.LCP does not have", 0);
        order_done(w, scr);
        return;
    }
    memcpy(w->buf, pic, 0x7D4);
    free(pic);
    unsigned n = walk_order_members(party);
    w->buf[2] = (uint8_t)(n * 20);
    w->pos_last = (n * 0x28 - 0x28) & 0xFFFF;
    w->pos_at = 0;
    w->pos_sel = 0;
    w->pos_rel = 0;
    w->pos_frame = 1;
    w->pos_ctr = 0xC6;
    w->pos_blink = 0x45;
    w->pos_latch = 0x32;
    w->pos_my = 0x498D;
    w->talking = 10;
}

/* A round of 0x1b834: the frame's count to 0x3c, then the next of the walk's four frames (0, 1, 2, 1); the palette step on the
 * dimmed copy at counts 0, 15, 30 and 45; the clean picture copied to the world buffer + 0xbb8, the lit row filled (0x25, 0x10 when
 * picked up) for 40 of every 81 rounds, each member's walking-down sprite at (4, 1) in his row, shown at (150, 50). Then the key
 * (after a release: Esc ends, Space picks up or puts down, Up and Down move) and the pointer (its y moving up or down moves the row;
 * the left button after a release picks up or puts down, the right one alone ends). */
static void order_round(walk_t *w, screen_t *scr, unsigned port60)
{
    uint8_t *party = w->scene->tab_b, *b = w->buf, *c = w->buf + 0xBB8;
    if (++w->pos_ctr > 0x3C) {
        w->pos_ctr = 0;
        if (++w->pos_frame > 3)
            w->pos_frame = 0;
    }
    if (w->pos_ctr == 0 || w->pos_ctr == 0xF || w->pos_ctr == 0x1E || w->pos_ctr == 0x2D)
        palette_cycle_on(w, scr, w->dimpal);
    memcpy(c, b, 0x7D4);   /* 0x27503 */
    w->pos_blink = (w->pos_blink + 1) & 0xFF;
    if (w->pos_blink > 0x50)
        w->pos_blink = 0;
    if (w->pos_blink > 0x28)
        memset(c + 4 + w->pos_at * 10, w->pos_sel ? 0x10 : 0x25, 0x190);
    for (unsigned i = 0, at = 0x18; i < 5; i++, at += 0x190) {
        unsigned num = party[i * 0x28 + 3];
        if (num > 4)
            break;
        const uint8_t *src = w->scene->bank2 + walk_sprite_off_party(1, w->pos_frame, num);
        for (unsigned r = 0; r < 16; r++)   /* 0x26c04: colour 0 transparent, rows 20 apart */
            for (unsigned x = 0; x < 16; x++)
                if (src[r * 16 + x])
                    c[4 + at + r * 0x14 + x] = src[r * 16 + x];
    }
    put(scr, 0x3F16, c + 4, 0x14, c[2]);
    unsigned k = port60 & 0xFF;
    int mx, my;
    unsigned mb = 0;
    if (walk_mouse(w, &mx, &my, &mb)) {
        unsigned y = (unsigned)my;
        if (y != w->pos_my) {
            int d = y < w->pos_my ? -1 : 1;
            order_move(w, d, 0);
            order_move(w, d, 1);
            w->pos_my = y;
        }
        mb &= 0xFF;
        if (mb == 0)
            w->pos_rel = 1;
        if (mb == 2) {
            order_done(w, scr);
            return;
        }
        if (mb == 1 && w->pos_rel) {
            w->pos_rel = 0;
            w->pos_sel = !w->pos_sel;
        }
    }
    if (k > 0x7F)
        w->pos_latch = 1;
    if (k >= 0x80 || !w->pos_latch)
        return;
    w->pos_latch = 0;
    if (k == 1) {
        order_done(w, scr);
        return;
    }
    if (k == 0x39)
        w->pos_sel = !w->pos_sel;
    if (k == 0x48 || k == 0x50) {
        order_move(w, k == 0x48 ? -1 : 1, 0);
        order_move(w, k == 0x48 ? -1 : 1, 1);
    }
}

/* ---- F10: the party's data (0x153d8 .. 0x1545c, 0x1a122) ---- */

void walk_data_number(uint8_t *pix, unsigned at, unsigned v, const uint8_t *digits)
{
    unsigned div = 10000;
    int seen = 0;
    while (div != 0) {   /* 0x1a7c8: 4 x 7 digits 4 apart, colour 0 clear (0x25bda); leading zeros left out, the units always */
        unsigned q = (v / div) & 0xFF, src = q * 0x1C;
        v = (v - q * div) & 0xFFFF;
        div /= 10;
        int draw = div == 0 || q != 0 || seen, zero = div != 0 && q == 0;
        if (draw) {
            const uint8_t *d = digits + (zero ? 0 : src);
            for (unsigned r = 0; r < 7; r++)
                for (unsigned c = 0; c < 4; c++)
                    if (d[r * 4 + c] && at + r * SCREEN_W + c < SCREEN_W * SCREEN_H)
                        pix[at + r * SCREEN_W + c] = d[r * 4 + c];
            if (div == 0)
                break;
            at += 4;
        }
        if (q != 0)
            seen = 1;
    }
}

/* 0x27328 of a picture with its 4-byte header (width, height) */
static void put_pic(screen_t *scr, unsigned at, const uint8_t *p)
{
    put(scr, at, p + 4, w16(p, 0), w16(p, 2));
}

/* The end of F10 (0x1a789): the three parts kept put back, the button let go, then 0x1729e. */
static void data_done(walk_t *w, screen_t *scr)
{
    put_pic(scr, 0x2080, w->zoom_grab);
    put_pic(scr, 0x2180, w->zoom_grab + 0x280A);
    put_pic(scr, 0xDC0, w->dbuf);
    walk_button_toggle(scr, 0x628D, 0x2F, 0x14);
    w->talking = 0;
    records_again(w, 0x384 / WS_RECORD_SIZE);
}

/* 0x2723c: the screen's w x h at `at` kept with its header */
static void grab_pic(const screen_t *scr, unsigned at, unsigned bw, unsigned bh, uint8_t *out)
{
    out[0] = (uint8_t)bw, out[1] = (uint8_t)(bw >> 8), out[2] = (uint8_t)bh, out[3] = (uint8_t)(bh >> 8);
    grab(scr, at, bw, bh, out + 4);
}

/* F10: the button pressed (the pointer's corner put back first); 0x1a122: MANDATA1.LCC (0x23e6 bytes into the map buffer [bp-0xba])
 * run-length decoded into the world buffer: a 64-wide panel, three 3-frame pictures at 0x274d, 0x2b11 and 0x2ed5 and the 4 x 7
 * digits at 0x3299. The screen's 64 x 160 at (0, 26) and 64 x 149 at (256, 26) kept in the map buffer, its 320 x 15 at (0, 11) in
 * [bp-0x20], whose 0x1388 on gets MES_TXT.LCC 0x6a06 (50 bytes: the members' names, 6 bytes each). For each member before the first
 * record whose number (byte 3) is above 4 (at most five), from x 0 every 64: the panel at y 26; then his name (colour 0x98, over a
 * 52 x 8 box of 0x56, a shadow of 0x9c a row down each side) at (8, 12); his face, MAN_PIC.LCC 0xce8 x number (shaded 8 times when
 * his HP is 0), at (2, 28); his numbers from (17, 90). The blink counts start from what the frame holds: 0x28 x the members
 * ([bp-0x14], INFERRED from five starts) and the last word or byte read ([bp-0x16]: the fifth member's attack, or the number of the
 * first record not listed). */
static void data_key(walk_t *w, screen_t *scr)
{
    uint8_t *party = w->scene->tab_b, *b = w->buf;
    if (w->mouse_on)
        walk_mouse_put(scr, w->mprev, w->mbg, 0);
    walk_button_toggle(scr, 0x628D, 0x2F, 0x14);   /* 0x1c3ba: F10's button pressed */
    size_t len = 0;
    uint8_t *pic = battle_read_unpack("mandata1.lcc", 0, 0x23E6, &len);
    uint8_t names[50];
    if (!pic || len < 0x33C1 || assets_read_at("mes_txt.lcc", 0x6A06, names, sizeof names) != 0) {
        free(pic);
        note_unbuilt(w, "F10's pictures or names that the data files do not have", 0);
        walk_button_toggle(scr, 0x628D, 0x2F, 0x14);
        w->talking = 0;
        records_again(w, 0x384 / WS_RECORD_SIZE);
        return;
    }
    if (assets_read_at("mandata1.lcc", 0, w->zoom_grab, 0x23E6) != 0)
        memset(w->zoom_grab, 0, 0x23E6);
    memcpy(b, pic, len);
    free(pic);
    grab_pic(scr, 0x2080, 0x40, 0xA0, w->zoom_grab);
    grab_pic(scr, 0x2180, 0x40, 0x95, w->zoom_grab + 0x280A);
    grab_pic(scr, 0xDC0, 0x140, 0xF, w->dbuf);
    memcpy(w->dbuf + 0x1388, names, sizeof names);
    unsigned n = walk_order_members(party);
    for (unsigned i = 0; i < n; i++)
        put_pic(scr, 0x2080 + 0x40 * i, b);
    unsigned last = n < 5 ? party[n * 0x28 + 3] : party[4 * 0x28 + 0x13];
    for (unsigned i = 0; i < n; i++) {
        const uint8_t *r = party + i * 0x28;
        unsigned num = r[3], face = 0x2302 + 0x40 * i, at = (face + 0xEC06) & 0xFFFF, gw = 0;
        uint8_t px[48 * 14];
        if (battle_text_pic(w->font8, w->dbuf + 0x1388 + 6 * num, 6, 0x98, px, &gw))
            note_unbuilt(w, "a character the font does not have", 0);
        b[0] = 48, b[1] = 0, b[2] = 14, b[3] = 0;   /* 0x243ec writes the picture into the world buffer */
        memcpy(b + 4, px, sizeof px);
        fill(scr, at + 0x63E, 0x34, 8, 0x56);
        for (unsigned s = 0; s < 2; s++) {   /* 0x25b54 a row down, a pixel left and right; then 0x27371 */
            unsigned o = at + (s ? 0x141 : 0x13F);
            for (unsigned k = 0; k < sizeof px; k++)
                if (px[k] && o + (k / 48) * SCREEN_W + k % 48 < SCREEN_W * SCREEN_H)
                    scr->pix[o + (k / 48) * SCREEN_W + k % 48] = 0x9C;
        }
        for (unsigned k = 0; k < sizeof px; k++)
            if (px[k] && at + (k / 48) * SCREEN_W + k % 48 < SCREEN_W * SCREEN_H)
                scr->pix[at + (k / 48) * SCREEN_W + k % 48] = px[k];
        if (assets_read_at("man_pic.lcc", (size_t)0xCE8 * num, b, 0xCE8) != 0)
            note_unbuilt(w, "a face that MAN_PIC.LCC does not have", num);
        if (w16(r, 8) == 0)
            for (int t = 0; t < 8; t++)   /* 0x25dca */
                for (unsigned k = 4; k < 4 + 0xCE4; k++)
                    b[k] = w->table[b[k]];
        put_pic(scr, face, b);
        const uint8_t *dg = b + 0x3299;
        unsigned p = 0x7091 + 0x40 * i;
        walk_data_number(scr->pix, p, w16(r, 4), dg);
        walk_data_number(scr->pix, p + 0x795, w16(r, 6), dg);
        p += 0x1040;
        walk_data_number(scr->pix, p, w16(r, 8), dg);
        walk_data_number(scr->pix, p + 0x795, w16(r, 0xA), dg);
        p += 0x1040;
        walk_data_number(scr->pix, p + 0x795, r[0xC], dg);
        walk_data_number(scr->pix, p, w16(r, 0xD), dg);
        p += 0x17CC;
        walk_data_number(scr->pix, p, w16(r, 0x10), dg);
        p += 0x1040;
        walk_data_number(scr->pix, p, r[0x12], dg);
        p += 0x1040;
        walk_data_number(scr->pix, p, r[0x13], dg);
    }
    w->data_ctr = (0x28 * n) & 0xFF;
    w->data_anim = last;
    w->data_frame = 0x274D;
    w->talking = 11;
}

/* A round of 0x1a641: the palette step on the dimmed copy every 41st round; every 201st the next of the three pictures at each
 * member's (5, 90); Esc (no release needed) or the right button alone ends it. The pointer's place is read into the world buffer's
 * first word. */
static void data_round(walk_t *w, screen_t *scr, unsigned port60)
{
    uint8_t *party = w->scene->tab_b;
    w->data_ctr = (w->data_ctr + 1) & 0xFF;
    if (w->data_ctr > 0x28) {
        w->data_ctr = 0;
        palette_cycle_on(w, scr, w->dimpal);
    }
    w->data_anim = (w->data_anim + 1) & 0xFFFF;
    if (w->data_anim > 0xC8) {
        w->data_anim = 0;
        w->data_frame += 0x3C4;
        if (w->data_frame > 0x2ED5)
            w->data_frame = 0x274D;
        for (unsigned i = 0; i < 5 && party[i * 0x28 + 3] <= 4; i++)
            put_pic(scr, 0x7085 + 0x40 * i, w->buf + w->data_frame);
    }
    if ((port60 & 0xFF) == 1) {
        data_done(w, scr);
        return;
    }
    int mx, my;
    unsigned mb = 0;
    if (walk_mouse(w, &mx, &my, &mb)) {
        w->buf[0] = (uint8_t)my;
        w->buf[1] = (uint8_t)(my >> 8);
        if ((mb & 0xFF) == 2)
            data_done(w, scr);
    }
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
    static const int kind[19] = {0, 4, 8, 9, 4, 10, 4, 0, 12, 13, 14, 15, 17, 6, 18, 4, 19, 20, 4};
    if (w->boss_box)
        return 4;
    if (w->left)
        return WALK_KIND_LEFT;
    if (w->ended)
        return WALK_KIND_ENDED;
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
    if (w->use_selling) {   /* 0x18b18 .. 0x18b7d: the same, then [bp-0xbe] = 1 */
        w->use_selling = 0;
        w->vbe = 1;
    }
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

/* A message box of F7's (0x1afb0, colour 0x88 or 0x20 at a sell counter, the palette itself): waiting (talking 6, then use_done) or not.
 * 0 when drawn. */
static int use_box_in(walk_t *w, screen_t *scr, unsigned msg, uint8_t colour, int wait)
{
    int r = msgbox_open(&w->box, scr, w->font8, w->table, w->buf + 0x7305, msg, colour, wait);
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

static int use_box(walk_t *w, screen_t *scr, unsigned msg, int wait)
{
    return use_box_in(w, scr, msg, 0x88, wait);
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

unsigned walk_sell_text(uint8_t m[22], unsigned price)
{
    static const uint8_t head[5] = {0xAD, 0xEC, 0xBB, 0xF9, ':'}, mid[5] = {0xA5, 0x62, 0xBB, 0xF9, ':'};
    char d[8];
    memcpy(m, head, 5);
    snprintf(d, sizeof d, "%05u", price % 100000u);   /* 0x26450 */
    memcpy(m + 5, d, 6);
    m[0xA] = ' ';
    memcpy(m + 0xB, mid, 5);
    snprintf(d, sizeof d, "%05u", (price >> 1) % 100000u);
    memcpy(m + 0x10, d, 6);
    unsigned i = 5;
    for (; i < 9 && m[i] <= '0'; i++)
        m[i] = ' ';
    for (i = 0x10; i < 0x14 && m[i] <= '0'; i++)
        m[i] = ' ';
    return i;
}

/* 0x1864b .. 0x18b7d: F7's item at a sell counter (a scene record of kind 0x14). The item's name (12 bytes of the map buffer, colour
 * 0x10) as a picture; the 192 x 60 at (64, 50) shaded four times (0x2723c, 0x25dca, 0x27328), the name with a shadow of colour 0x16 a row
 * down and a pixel to the right at (116, 52), a frame of colour 0x2a round the part. An item that may be sold (byte 0x11 = 0): the map
 * buffer's first 21 bytes become 原價:NNNNN 半價:NNNNN (its price, the word at 0x12, and half of it, leading zeros as spaces up to the last
 * digit), drawn in colour 0x80 at (67, 70) over a shadow of 0x84 a row down and a pixel to the right; then 願意嗎? (colour 0x98, shadow
 * 0x9b) at (172, 87); then yes / no. [bp-0x96] ends at the second number's first digit shown. Otherwise message 0xd4 (colour 0x20,
 * waiting). */
static void use_sell(walk_t *w, screen_t *scr)
{
    uint8_t *m = w->zoom_grab, px[168 * 14];
    unsigned gw = 0;
    if (battle_text_pic(w->font8, m, 12, 0x10, px, &gw))
        note_unbuilt(w, "a character the font does not have", 0);
    static uint8_t part[192 * 60];
    grab(scr, 0x3EC0, 192, 60, part);
    for (int t = 0; t < 4; t++)
        for (unsigned i = 0; i < sizeof part; i++)
            part[i] = w->table[part[i]];
    put(scr, 0x3EC0, part, 192, 60);
    for (unsigned i = 0; i < 96 * 14; i++)
        if (px[i]) {
            unsigned o = (i / 96) * SCREEN_W + i % 96;
            scr->pix[0x42B4 + o] = 0x16;
            scr->pix[0x42B5 + o] = 0x16;
        }
    for (unsigned i = 0; i < 96 * 14; i++)
        if (px[i])
            scr->pix[0x4174 + (i / 96) * SCREEN_W + i % 96] = px[i];
    use_outline(scr, 0x3EC0, 192, 60, 0x2A);
    if (m[0x11] != 0) {
        w->use_selling = 1;
        if (use_box_in(w, scr, 0xD4, 0x20, 1) != 0)
            use_done(w, scr);
        return;
    }
    unsigned price = w16(m, 0x12);
    w->v96 = walk_sell_text(m, price);
    static const struct { unsigned n, at, sh; uint8_t c, s; } line[2] = {{21, 0x57C3, 0x5903, 0x80, 0x84}, {7, 0x6D6C, 0x6EAC, 0x98, 0x9B}};
    static const uint8_t ask[7] = {0xC4, 0x40, 0xB7, 0x4E, 0xB6, 0xDC, '?'};
    for (int l = 0; l < 2; l++) {
        if (l == 1)
            memcpy(m, ask, sizeof ask);
        unsigned pw = line[l].n * 8;
        if (battle_text_pic(w->font8, m, line[l].n, line[l].c, px, &gw))
            note_unbuilt(w, "a character the font does not have", 0);
        for (unsigned k = 0; k < pw * 14; k++)
            if (px[k]) {
                unsigned o = (k / pw) * SCREEN_W + k % pw;
                scr->pix[line[l].sh + 1 + o] = line[l].s;
                scr->pix[line[l].sh + o] = line[l].s;
            }
        for (unsigned k = 0; k < pw * 14; k++)
            if (px[k])
                scr->pix[line[l].at + (k / pw) * SCREEN_W + k % pw] = px[k];
    }
    w->use_half = price >> 1;
    w->use_selling = 1;
    if (w->mouse_on)
        walk_mouse_put(scr, w->mprev, w->mbg, 0);
    yesno_open(w, scr);
    w->talk_count = 0xDC;   /* [bp-8] at 0x18d23: 0x3fdc, left by the text drawing (0x243ec), SEEN */
}

/* 0x189f2 .. 0x18ae5: yes at a sell counter: the half price added to the coins one at a time (so past 65,535 it wraps), the coins drawn,
 * the item gone from the member's first place holding it, SND_EF.1's 0x1bf5, waited for when the sounds are on. */
static void use_sold(walk_t *w, screen_t *scr)
{
    uint8_t *party = w->scene->tab_b;
    unsigned gold = (w16(party, WALK_GOLD_AT) + w->use_half) & 0xFFFF;
    w->use_half = 0;
    party[WALK_GOLD_AT] = (uint8_t)gold;
    party[WALK_GOLD_AT + 1] = (uint8_t)(gold >> 8);
    walk_show_gold(w, scr);
    use_drop(party, w->use_member, w->use_item);
    walk_sfx_play(w, SFX_EF, 0x1BF5);
    if (w->sb == 0x81) {
        walk_sfx_wait(w);
        walk_sfx_stop(w);
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
        use_sell(w, scr);
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
    if (w->use_selling) {
        if (yes)
            use_sold(w, scr);
        use_done(w, scr);
        return;
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

/* ---- a shop (kind 5, 0x22c42 .. 0x24301) ---- */

enum { SHOP_ROW0 = 0x1E96, SHOP_ROW = 0x1A40, SHOP_PIC_AT = 0x1BC6, SHOP_STRIP_AT = 0xBE00, SHOP_PK_AT = 0x5D06 };

/* 0x25dca: n bytes through the shading table */
static void shade(walk_t *w, uint8_t *px, size_t n)
{
    for (size_t i = 0; i < n; i++)
        px[i] = w->table[px[i]];
}

/* A picture's non-zero pixels: in colour c (0x25b54) or as they are (0x27371, 0x26ad2), w wide and h high, at `at` in a picture
 * `stride` wide holding `limit` bytes (the screen: SCREEN_W, its size). */
static void blit_nz(uint8_t *dst, size_t limit, unsigned stride, unsigned at, const uint8_t *px, unsigned bw, unsigned bh, int c)
{
    for (unsigned r = 0; r < bh; r++)
        for (unsigned x = 0; x < bw; x++) {
            size_t o = at + (size_t)r * stride + x;
            if (px[r * bw + x] && o < limit)
                dst[o] = c >= 0 ? (uint8_t)c : px[r * bw + x];
        }
}

/* 0x24302 with its flag clear: a number in five 7 x 5 digits 8 apart (colour 0 clear), leading zeros left out but their places kept */
static void shop_number(walk_t *w, screen_t *scr, unsigned at, unsigned v)
{
    int started = 0;
    for (unsigned div = 10000; div; at += 8) {
        unsigned d = (v / div) & 0xFF;
        v -= d * div;
        div /= 10;
        if (div == 0 || d || started) {
            blit_nz(scr->pix, sizeof scr->pix, SCREEN_W, at, w->art + ART_DIGITS + d * 35, 7, 5, -1);
            started |= d != 0;
        }
    }
}

/* 0x243ec into the map buffer (header, then the picture), as the original leaves it; the picture in `px` */
static void shop_text(walk_t *w, const uint8_t *s, unsigned n, uint8_t colour, uint8_t *px)
{
    if (battle_text_pic(w->font8, s, n, colour, px, &w->shop_gw))
        note_unbuilt(w, "a character the font does not have", w->shop_si / WS_RECORD_SIZE);
    uint8_t *mb = w->zoom_grab;
    mb[0] = (uint8_t)(8 * n), mb[1] = (uint8_t)((8 * n) >> 8), mb[2] = 14, mb[3] = 0;
    memcpy(mb + 4, px, (size_t)8 * n * 14);
}

static unsigned shop_coins(walk_t *w)
{
    return w16(w->scene->tab_b, WALK_GOLD_AT);
}

static void shop_set_coins(walk_t *w, screen_t *scr, unsigned v)
{
    w->scene->tab_b[WALK_GOLD_AT] = (uint8_t)v;
    w->scene->tab_b[WALK_GOLD_AT + 1] = (uint8_t)(v >> 8);
    walk_show_gold(w, scr);   /* 0x1bd6e */
    w->shop_pkc = 0;   /* what 0x1bd6e's callees leave where the picker's count lies (SEEN once: 0) */
}

/* 0x22c42 .. 0x23383: the opening (the record's bytes 3 .. 14 as the shop's arguments; byte 3 the type): the window shaded six times;
 * the strip at (0, 152) kept, and two copies of it shaded three times, one with the greeting (MES_TXT.LCC type x 0x80 + 6, up to '#')
 * centred in colour 0x10, the other with the 10 bytes at 0x306 at x 120; the shopkeeper's middle picture (SHOP_PIC.LCC type x 0x3d86) at
 * (70, 22); the list's box at (150, 24); for types 0 to 2 the three items' names (OBJECT.LCC, colour 0x10, a shadow of 0x1a a row down)
 * and prices (word 0x12) in its rows, for types 3 and 4 one row (MES_TXT.LCC 0x3cb or 0x3bf, colour 0xa8, shadow 0xac) and the price
 * (type 4: times the members alive). The pointer at 0, the first row lit next round. */
static void shop_open(walk_t *w, screen_t *scr, unsigned si)
{
    walk_scene_t *s = w->scene;
    const uint8_t *rec = s->tab_a + 1000 + si;
    uint8_t *party = s->tab_b, *mb = w->zoom_grab;
    w->shop_si = si;
    memcpy(w->shop_rec, rec + 3, 12);
    unsigned t = w->shop_rec[0];
    if (w->mouse_on)   /* 0x17d7f: the world's pointer off */
        walk_mouse_put(scr, w->mprev, w->mbg, 0);
    static uint8_t win[WALK_ZOOM_W * WALK_ZOOM_H];
    grab(scr, 0x1440, WALK_ZOOM_W, WALK_ZOOM_H, win);
    for (int k = 0; k < 6; k++)
        shade(w, win, sizeof win);
    put(scr, 0x1440, win, WALK_ZOOM_W, WALK_ZOOM_H);
    grab(scr, SHOP_STRIP_AT, 320, 14, w->shop_strip);
    if (assets_read_at("SHOP_PIC.LCC", (size_t)t * 0x3D86, w->shop_pics, sizeof w->shop_pics) != 0) {
        note_unbuilt(w, "SHOP_PIC.LCC without this shop's pictures", si / WS_RECORD_SIZE);
        memset(w->shop_pics, 0, sizeof w->shop_pics);
    }
    put(scr, SHOP_PIC_AT, w->shop_pics + 0x1482, 70, 75);
    memcpy(w->shop_a, w->shop_strip, sizeof w->shop_a);
    for (int k = 0; k < 3; k++)
        shade(w, w->shop_a, sizeof w->shop_a);
    memcpy(w->shop_b, w->shop_a, sizeof w->shop_b);
    uint8_t msg[0x78 + 10], px[8 * 0x78 * 14];
    memset(msg, 0, sizeof msg);
    if (assets_read_at("MES_TXT.LCC", (size_t)t * 0x80 + 6, msg, 0x78) != 0 || assets_read_at("MES_TXT.LCC", 0x306, msg + 0x78, 10) != 0)
        note_unbuilt(w, "MES_TXT.LCC without the shop's greeting", si / WS_RECORD_SIZE);
    unsigned n = 0;
    while (n < 0x78 && msg[n] != '#')
        n++;
    shop_text(w, msg, n, 0x10, px);
    blit_nz(w->shop_a, sizeof w->shop_a, 320, ((320 - 8 * n) & 0xFFFF) >> 1, px, 8 * n, 14, -1);
    shop_text(w, msg + 0x78, 10, 0x10, px);
    blit_nz(w->shop_b, sizeof w->shop_b, 320, 0x78, px, 80, 14, -1);
    fill(scr, 0x1D55, 0x61, 0x40, 0x63);   /* 0x270e0 */
    fill(scr, SHOP_ROW0, 0x61, 0x40, 0x66);
    fill(scr, SHOP_ROW0, 0x60, 0x3F, 0xAE);
    w->shop_pal = party[1];   /* [bp-0x16], never set for types 3 and 4: 0x48, the hero's y, at scene 2's healer (INFERRED: y) */
    if (t < 3) {
        unsigned row = SHOP_ROW0;
        for (unsigned k = 0; k < 3; k++, row += SHOP_ROW) {
            unsigned id = w->shop_rec[1 + k];
            uint8_t o[0x1C];
            w->shop_pal = id > 1 ? 1 : id;   /* [bp-0x16] counted down to 1 for the file offset */
            if (assets_read_at("OBJECT.LCC", id ? (size_t)(id - 1) * 30 : 0, o, sizeof o) != 0)
                memset(o, 0, sizeof o);
            memcpy(mb, o, sizeof o);
            shop_text(w, o, 12, 0x10, px);
            unsigned price = w16(o, 0x12);
            w->shop_items[k] = id;
            w->shop_prices[k] = price;
            mb[0xBB8 + 3 * k] = (uint8_t)id;
            mb[0xBB9 + 3 * k] = (uint8_t)price;
            mb[0xBBA + 3 * k] = (uint8_t)(price >> 8);
            blit_nz(scr->pix, sizeof scr->pix, SCREEN_W, row + SCREEN_W, px, 96, 14, 0x1A);
            blit_nz(scr->pix, sizeof scr->pix, SCREEN_W, row, px, 96, 14, -1);
            shop_number(w, scr, row + 0x12F0, price);
        }
    } else {
        uint8_t name[12];
        if (assets_read_at("MES_TXT.LCC", t == 4 ? 0x3BF : 0x3CB, name, sizeof name) != 0)
            memset(name, 0, sizeof name);
        memcpy(mb, name, sizeof name);
        shop_text(w, name, 12, 0xA8, px);
        blit_nz(scr->pix, sizeof scr->pix, SCREEN_W, SHOP_ROW0 + SCREEN_W, px, 96, 14, 0xAC);
        blit_nz(scr->pix, sizeof scr->pix, SCREEN_W, SHOP_ROW0, px, 96, 14, -1);
        unsigned price = w16(w->shop_rec, 1);
        if (t == 4) {   /* the inn: the price for each member alive */
            unsigned sum = 0;
            for (unsigned o = 0; o < 0xC8; o += WS_PARTY_SIZE)
                if (party[o + 3] < 5 && w16(party, o + 8))
                    sum = (sum + price) & 0xFFFF;
            w->shop_rec[1] = (uint8_t)sum;
            w->shop_rec[2] = (uint8_t)(sum >> 8);
            price = sum;
        }
        shop_number(w, scr, SHOP_ROW0 + 0x12F0, price);
    }
    talk_mouse_start(w, scr);   /* 0x23332: [bp-0x22] 0, the corner kept */
    w->shop_prev = 0x38D6;
    w->shop_cur = SHOP_ROW0;
    w->shop_latch = w->shop_mlatch = 0;
    w->shop_pic = 0x1482;
    w->shop_count = 0xBB7;
    w->shop_pkc = 0xC6;   /* the picker's [bp-0x16], never set: 0xc6 (a segment byte) at the first picker in every start probed */
    w->shop_c = 0xBC;     /* [bp-0xc] before anything sets it: 0xbc at scene 2's inn without a mouse (INFERRED constant) */
    w->talking = 12;
}

/* 0x242ad .. 0x24301 and back in 0x17dff: the pointer off, the strip put back when the shop ends on a key (not after the inn), the
 * world's pointer at 0 with the corner kept; then the record loop again as for any record (0x18b82). */
static void shop_end(walk_t *w, screen_t *scr, int strip)
{
    if (w->mouse_on)
        walk_mouse_put(scr, w->talk_mprev, w->talk_mbg, 0);
    if (strip)
        put(scr, SHOP_STRIP_AT, w->shop_strip, 320, 14);
    if (w->mouse_on) {
        w->mprev = 0;
        walk_mouse_grab(scr, 0, w->mbg);
    }
    w->talking = 0;
    w->vbe = 5;
    records_again(w, w->shop_si / WS_RECORD_SIZE);
}

/* The pointer off and on around a drawing (it stays where it is) */
static void shop_ptr_off(walk_t *w, screen_t *scr)
{
    if (w->mouse_on)
        walk_mouse_put(scr, w->talk_mprev, w->talk_mbg, 0);
}

static void shop_ptr_on(walk_t *w, screen_t *scr)
{
    if (w->mouse_on) {
        walk_mouse_grab(scr, w->talk_mprev, w->talk_mbg);
        walk_mouse_put(scr, w->talk_mprev, w->mpic, 1);
    }
}

/* the 96 x 21 row at `at` with colour `from` turned `to` (0x2723c, 0x2741b, 0x27328) */
static void shop_recolour(screen_t *scr, unsigned at, uint8_t from, uint8_t to)
{
    for (unsigned r = 0; r < 21; r++)
        for (unsigned x = 0; x < 96; x++) {
            unsigned o = at + r * SCREEN_W + x;
            if (o < SCREEN_W * SCREEN_H && scr->pix[o] == from)
                scr->pix[o] = to;
        }
}

/* The picker for a purchase (dead 0) or a healing (dead 1) runs as w->cast (talking 13); then shop_picked. */
static void shop_pick(walk_t *w, screen_t *scr, int dead)
{
    w->cast = battle_world_pick(w, scr, dead, w->shop_pkc);
    if (!w->cast) {
        note_unbuilt(w, "the shop's picker", w->shop_si / WS_RECORD_SIZE);
        put(scr, SHOP_PK_AT, w->shop_pk, 90, 18);
        talk_mouse_start(w, scr);
        return;
    }
    battle_set_show(w->cast, w->show, w->show_ctx);
    w->talking = 13;
}

/* 0x238f4 .. 0x23a2d (a purchase) and 0x23af2 .. 0x23bf8 (a healing): with the member picked, the item into his first empty item place
 * or his HP to the most and word 0x10 to 0; the price taken, the coins
 * drawn again, the pass counter 0 and SND_EF 0x1bf5; then the picker's place put back and the pointer from the corner. */
static void shop_picked(walk_t *w, screen_t *scr)
{
    unsigned m = battle_picked(w->cast);
    w->shop_pkc = battle_pick_count(w->cast);   /* a picker after it starts from its count (INFERRED, as in F5) */
    w->shop_c = m & 0xFF;   /* 0x238f4, 0x23af2 */
    battle_free(w->cast);
    w->cast = NULL;
    w->talking = 12;
    uint8_t *party = w->scene->tab_b;
    unsigned t = w->shop_rec[0];
    if (t < 3) {
        unsigned k = (w->shop_cur - SHOP_ROW0) / SHOP_ROW, slot = m + 0x14;
        int found = 0;
        for (unsigned i = 0; slot < 0xC8 && i < 0x14; i++, slot++)
            if (party[slot] == 0) {
                found = 1;
                break;
            }
        if (found) {
            party[slot] = (uint8_t)w->shop_items[k];
            shop_set_coins(w, scr, (shop_coins(w) - w->shop_prices[k]) & 0xFFFF);
            w->shop_count = 0;
            walk_sfx_play(w, SFX_EF, 0x1BF5);
        }
    } else if (m + 8 < 0xC8) {
        shop_set_coins(w, scr, (shop_coins(w) - w16(w->shop_rec, 1)) & 0xFFFF);
        w->shop_count = 0;
        party[m + 8] = party[m + 0xA];
        party[m + 9] = party[m + 0xB];
        party[m + 0x10] = party[m + 0x11] = 0;
        walk_sfx_play(w, SFX_EF, 0x1BF5);
    }
    put(scr, SHOP_PK_AT, w->shop_pk, 90, 18);
    talk_mouse_start(w, scr);
}

/* 0x23386 .. 0x242aa: a round of the shop. The pass counter (at most 60,000) shows the greeting's strip at 3000 and the other at 2 (after
 * a purchase); the palette steps every 26th; now and then (rand() below 7 or above 0x7ff8) the shopkeeper's picture moves to the one
 * before or after; a new lit row has its colour 0xae turned 9, the old one's back. Keys after a release, buttons after all are up; the
 * pointer over the list (x 151 .. 244) lights the row under it and the left button takes it, the right one leaves; Esc leaves; Up and Down
 * move; Enter or Space buy the lit item (types 0 to 2) or, on the first row, heal (type 3), when the coins cover the price. */
static void shop_round(walk_t *w, screen_t *scr, unsigned port60)
{
    uint8_t *party = w->scene->tab_b;
    unsigned t = w->shop_rec[0];
    if (++w->shop_count > 0xEA60)
        w->shop_count = 0xEA60;
    if (w->shop_count == 2)
        put(scr, SHOP_STRIP_AT, w->shop_b, 320, 14);
    if (w->shop_count == 0xBB8)
        put(scr, SHOP_STRIP_AT, w->shop_a, 320, 14);
    w->shop_pal = (w->shop_pal + 1) & 0xFF;
    if (w->shop_pal > 0x19) {
        w->shop_pal = 0;
        palette_cycle(w, scr);
    }
    unsigned r = walk_rand(&w->rng);
    if (r < 7 || r > 0x7FF8) {
        if (r < 7 && w->shop_pic)
            w->shop_pic -= 0x1482;
        if (r > 0x7FF8 && w->shop_pic < 0x2904)
            w->shop_pic += 0x1482;
        shop_ptr_off(w, scr);
        put(scr, SHOP_PIC_AT, w->shop_pics + w->shop_pic, 70, 75);
        shop_ptr_on(w, scr);
    }
    if (w->shop_cur != w->shop_prev) {
        shop_ptr_off(w, scr);
        shop_recolour(scr, w->shop_cur, 0xAE, 9);
        shop_recolour(scr, w->shop_prev, 9, 0xAE);
        w->shop_prev = w->shop_cur;
        shop_ptr_on(w, scr);
    }
    unsigned k = port60 & 0xFF;
    if (k > 0x7F)
        w->shop_latch = 1;
    if (!w->shop_latch)
        k = 0x82;
    int mx, my;
    unsigned mb = 0;
    if (w->mouse_on) {   /* 0x23664 */
        talk_mouse_move(w, scr, &mx, &my, &mb);
        mb &= 0xFF;
        if (mb == 0)
            w->shop_mlatch = 1;
        if (!w->shop_mlatch)
            mb = 0;
        if (mb)
            w->shop_mlatch = 0;
        w->shop_c = mb;
        unsigned x = (unsigned)mx, y = (unsigned)my;
        if (x > 0x96 && x < 0xF5) {
            if (y > 0x18 && y < 0x2D)
                w->shop_cur = SHOP_ROW0;
            if (y > 0x2C && y < 0x42)
                w->shop_cur = SHOP_ROW0 + SHOP_ROW;
            if (y > 0x42 && y < 0x57)
                w->shop_cur = SHOP_ROW0 + 2 * SHOP_ROW;
            if (mb == 1)
                k = 0x1C;
        }
        if (mb == 2) {
            shop_end(w, scr, 1);
            return;
        }
    }
    if (k >= 0x7F)
        return;
    w->shop_latch = 0;
    if (k == 1) {
        shop_end(w, scr, 1);
        return;
    }
    if (k == 0x48 && w->shop_cur > SHOP_ROW0)
        w->shop_cur -= SHOP_ROW;
    if (k == 0x50 && w->shop_cur < SHOP_ROW0 + 2 * SHOP_ROW)
        w->shop_cur += SHOP_ROW;
    if (k != 0x1C && k != 0x39)
        return;
    if (t < 3) {
        unsigned i = (w->shop_cur - SHOP_ROW0) / SHOP_ROW;
        if (shop_coins(w) < w->shop_prices[i])
            return;
        shop_ptr_off(w, scr);
        grab(scr, SHOP_PK_AT, 90, 18, w->shop_pk);
        shop_pick(w, scr, 0);
        return;
    }
    if (w->shop_cur != SHOP_ROW0 || shop_coins(w) < w16(w->shop_rec, 1))
        return;
    if (t == 3) {
        grab(scr, SHOP_PK_AT, 90, 18, w->shop_pk);
        shop_ptr_off(w, scr);
        shop_pick(w, scr, 1);
        return;
    }
    (void)party;
    inn_begin(w, scr);
}

/* 0x26c04: `h` rows of a `bw`-wide picture, colour 0 clear, into a picture `stride` wide at `at` */
static void inn_blit(uint8_t *dst, size_t limit, unsigned at, unsigned stride, const uint8_t *px, unsigned bw, unsigned h)
{
    blit_nz(dst, limit, stride, at, px, bw, h, -1);
}

/* 0x23c4e .. 0x240e9, the inn (type 4) with the coins: the strip put back and the price taken; the window shaded 16 times and then
 * filled with colour 0 (only that is seen); RPG.LCP 0x7777 (0x366 bytes) unpacked into the scratch buffer (0x798 bytes: the bed at 0,
 * the sleepers' 9 x 14 pictures, the quilts from 0xa0, 0x100 apart), five of its 256-byte blocks from 0xa0 swapped at random; a 16 x 25
 * picture in the world buffer, cleared once. For each member alive (HP not 0, number below 5): HP to the most, MP (word 0xd) to at least
 * 20 x his level (byte 0xc, at most 4000); the bed's top 10 rows, his sprite (bank 2, number x 0xc00 + 0x400, one of three at random) at
 * row 4 and a quilt at row 9 into the picture (each over what the last one left), shown at (115, 88) + 18 a member. Then five Zzz's (a
 * word: where in the scratch buffer, 0x5a0 .. 0x71a; a byte: a frame count) at random; the music stopped. */
static void inn_begin(walk_t *w, screen_t *scr)
{
    walk_scene_t *s = w->scene;
    uint8_t *party = s->tab_b, *sc = w->inn_scr, *pic = w->buf;
    put(scr, SHOP_STRIP_AT, w->shop_strip, 320, 14);
    shop_set_coins(w, scr, (shop_coins(w) - w16(w->shop_rec, 1)) & 0xFFFF);
    uint8_t packed[0x366];
    memset(sc, 0, sizeof w->inn_scr);
    if (assets_read_at("RPG.LCP", 0x7777, packed, sizeof packed) != 0)
        note_unbuilt(w, "RPG.LCP without the inn's pictures (0x7777)", w->shop_si / WS_RECORD_SIZE);
    else
        walk_rle_unpack(packed, sizeof packed, sc, sizeof w->inn_scr);
    pic[0] = 0x10, pic[1] = 0, pic[2] = 0x19, pic[3] = 0;
    memset(pic + 4, 0, 0x190);
    for (int k = 0; k < 5; k++) {   /* 0x23dab: 0x25c8e swaps the two blocks */
        unsigned a = ((walk_rand(&w->rng) / 0x199A) << 8) + 0xA0, b = ((walk_rand(&w->rng) / 0x199A) << 8) + 0xA0;
        uint8_t t[0x100];
        memcpy(t, sc + a, 0x100);
        memmove(sc + a, sc + b, 0x100);
        memcpy(sc + b, t, 0x100);
    }
    fill(scr, 0x1440, WALK_ZOOM_W, WALK_ZOOM_H, 0);
    unsigned at = 0x6E73, quilt = 0xA0;
    w->inn_n = 0;
    w->inn_c = w->shop_c;
    for (unsigned o = 0; o < 0xC8; o += WS_PARTY_SIZE, quilt += 0x100) {
        if (w16(party, o + 8) == 0 || party[o + 3] >= 5)
            continue;
        party[o + 8] = party[o + 0xA];
        party[o + 9] = party[o + 0xB];
        unsigned mp = (party[o + 0xC] * 20u) & 0xFFFF;
        if (mp > 0xFA0)
            mp = 0xFA0;
        if (mp > w16(party, o + 0xD)) {
            party[o + 0xD] = (uint8_t)mp;
            party[o + 0xE] = (uint8_t)(mp >> 8);
        }
        inn_blit(pic, 4 + 0x190, 4, 0x10, sc, 0x10, 0xA);
        unsigned off = party[o + 3] * 0xC00u + 0x400 + (walk_rand(&w->rng) / 0x2AAB) * 0x300;
        inn_blit(pic, 4 + 0x190, 0x44, 0x10, s->bank2 + off, 0x10, 0x10);
        inn_blit(pic, 4 + 0x190, 0x94, 0x10, sc + quilt, 0x10, 0x10);
        blit_nz(scr->pix, sizeof scr->pix, SCREEN_W, at, pic + 4, 0x10, 0x19, -1);   /* 0x27371 */
        at += 0x12;
        w->inn_n++;
    }
    for (unsigned k = 0; k < 15; k += 3) {   /* 0x2400b */
        unsigned v = (walk_rand(&w->rng) / 0x2000) * 0x7E + 0x5A0;
        sc[k] = (uint8_t)v;
        sc[k + 1] = (uint8_t)(v >> 8);
        sc[k + 2] = (uint8_t)(walk_rand(&w->rng) / 0x82);
    }
    if (w->fm == 0x81)   /* 0x24081: the music stopped (0x165d:0x126) */
        w->inn_nights++;
    w->talking = 14;
}

/* 0x240ec .. 0x2428b: a round of the night. Each sleeper's Zzz (9 x 14, at (113, 73) + 18 a member) from its place in the scratch buffer;
 * every fifth round each frame count goes up, and past 0xfe back to 0 with the place one picture on (0x7e, from 0x71a back to 0x5a0);
 * with the sounds on and none playing, RPG.LCP 0x7dcb played again. A make code on the keyboard port (no release needed: the key that
 * chose the inn, still held, ends it at once) or any mouse button ends it: the sound cut, and the shop left without its strip. */
static void inn_round(walk_t *w, screen_t *scr, unsigned port60)
{
    uint8_t *sc = w->inn_scr;
    unsigned at = 0x5BB1;
    for (unsigned i = 0; i < w->inn_n; i++, at += 0x12) {
        unsigned v = sc[3 * i] | sc[3 * i + 1] << 8;
        if (v + 9 * 14 <= sizeof w->inn_scr)
            put(scr, at, sc + v, 9, 14);
    }
    w->inn_c = (w->inn_c + 1) & 0xFF;
    if (w->inn_c > 4) {
        w->inn_c = 0;
        for (unsigned k = 0; k < 15; k += 3) {
            uint8_t f = (uint8_t)(sc[k + 2] + 1);
            if (f > 0xFE) {
                f = 0;
                unsigned v = (sc[k] | sc[k + 1] << 8) + 0x7E;
                if (v > 0x71A)
                    v = 0x5A0;
                sc[k] = (uint8_t)v;
                sc[k + 1] = (uint8_t)(v >> 8);
            }
            sc[k + 2] = f;
        }
    }
    if (w->sb == 0x81 && !walk_sfx_busy(w))
        walk_sfx_play(w, SFX_RPG, 0x7DCB);
    int end = (port60 & 0xFF) < 0x80;
    int mx, my;
    unsigned mb = 0;
    if (!end && walk_mouse(w, &mx, &my, &mb) && (mb & 0xFF))
        end = 1;
    if (!end)
        return;
    if (w->sb == 0x81)
        walk_sfx_stop(w);
    shop_end(w, scr, 0);
}

static void shop_pick_round(walk_t *w, screen_t *scr, unsigned port60)
{
    if (battle_pass(w->cast, scr, port60) & WALK_PASS_UNBUILT)
        note_unbuilt(w, battle_unbuilt(w->cast) ? battle_unbuilt(w->cast) : "?", 0);
    if (battle_over(w->cast))
        shop_picked(w, scr);
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

/* ---- the boat ride, kind 8 (0x17ff4 .. 0x1813f, 0x1be44 .. 0x1c283) ---- */

/* 0x2758d with mode 2: each row of a picture with its header turned 2 pixels to the left (the first two go to the row's end) */
static void boat_turn(uint8_t *pic)
{
    unsigned bw = w16(pic, 0), bh = w16(pic, 2);
    for (unsigned r = 0; r < bh; r++) {
        uint8_t *p = pic + 4 + (size_t)r * bw, a = p[0], b = p[1];
        memmove(p, p + 2, bw - 2);
        p[bw - 2] = a;
        p[bw - 1] = b;
    }
}

/* 0x26ad2: a picture with its header, colour 0 clear, at `at` in the world buffer's picture whose pixels start at `base`, `stride` wide.
 * There is no clipping: the boat's first steps and the shore's lowest places go past the picture's 168 rows (up to 0x8925 bytes on);
 * the port drops what would fall past the world buffer. */
static void boat_blit(uint8_t *buf, unsigned base, unsigned at, unsigned stride, const uint8_t *pic)
{
    unsigned bw = w16(pic, 0), bh = w16(pic, 2);
    for (unsigned r = 0; r < bh; r++)
        for (unsigned c = 0; c < bw; c++) {
            size_t o = base + (((size_t)at + (size_t)r * stride + c) & 0xFFFF);
            uint8_t v = pic[4 + (size_t)r * bw + c];
            if (v && o < WALK_BUF_LEN)
                buf[o] = v;
        }
}

/* 0x26de8: an 80 x 80 picture with its header stretched to n x n, the header written */
static void boat_stretch(const uint8_t *src, uint8_t *dst, unsigned n)
{
    unsigned sw = w16(src, 0), sh = w16(src, 2), ys = (unsigned)((0xFFFFul * sh / n) & 0xFFFF), row = 0, yacc = 0;
    dst[0] = (uint8_t)n, dst[1] = (uint8_t)(n >> 8), dst[2] = (uint8_t)n, dst[3] = (uint8_t)(n >> 8);
    for (unsigned r = 0; r < n; r++) {
        battle_stretch_row(src + 4 + row, sw, dst + 4 + (size_t)r * n, n);
        yacc += ys;
        if (yacc > 0xFFFF) {
            yacc &= 0xFFFF;
            row += sw;
        }
    }
}

/* 0x1be44: the boat ride. BOAT.LCZ (0x43b7 bytes) read into the map buffer [bp-0xba] and unpacked (0x25eeb) three times: the water, a
 * 192 x 168 picture, into [bp-0x20]; the shore, 192 x 29, from 0x1b04 into the map buffer itself; from 0x28ec into [bp-0x20] + 0x7e04 the
 * boat's two frames (80 x 55 each) and their fronts (80 x 38, colour 0 clear). The frames go into two 80 x 80 pictures with headers at
 * the world buffer [bp-0x8c] and + 0x1904 (0x3208 bytes cleared first), each member numbered below 5 standing in both (bank 2, number x
 * 0xc00 + 0x100, 16 x 16 at row 39, 16 apart, dead ones too), then the fronts over them from row 42. Then 84 steps: the palette step
 * (0x1c284); the water turned 2 pixels and copied to the world buffer + 0x32c8; the boat's frame (each in turn) sized to n x n (168
 * down by 2: stretched while above 80, else shrunk and shaded 5 - n / 16 times) and put at [bp-0xa] (12 on, 0x241 or 0xc1 a step);
 * the shore turned 6 pixels and put at [bp-0x10] (0x6840, a row down or up a step, turning back outside 0x6900 .. 0x72c0); the
 * picture shown at (64, 16). Each step first ends a Sound Blaster sound that has played out (0x190e), which shows nothing. */
static void boat_ride(walk_t *w, screen_t *scr)
{
    static uint8_t big[0x10000];   /* [bp-0x20], a 64 KB buffer in the original (the port's [bp-0x20] holds less) */
    walk_scene_t *s = w->scene;
    uint8_t *mb = w->zoom_grab, *buf = w->buf, *party = s->tab_b;
    if (assets_read_at("BOAT.LCZ", 0, mb, 0x43B7) != 0) {
        note_unbuilt(w, "BOAT.LCZ missing or short", 0);
        return;
    }
    static uint8_t shore[0x43B7 - 0x1B04];
    memcpy(shore, mb + 0x1B04, sizeof shore);
    walk_rle_unpack(mb, 0x43B7, big, sizeof big);
    walk_rle_unpack(shore, sizeof shore, mb, sizeof w->zoom_grab);
    walk_rle_unpack(mb + 0x28EC, 0x43B7 - 0x28EC, big + 0x7E04, sizeof big - 0x7E04);
    memset(buf, 0, 0x3208);
    buf[0] = buf[2] = buf[0x1904] = buf[0x1906] = 0x50;
    memcpy(buf + 4, big + 0x7E04, 0x1130);
    memcpy(buf + 0x1908, big + 0x8F34, 0x1130);
    unsigned at = 0xC30;
    for (unsigned o = 3; o < 0xC8; o += WS_PARTY_SIZE) {
        if (party[o] >= 5)
            continue;
        const uint8_t *spr = s->bank2 + party[o] * 0xC00u + 0x100;
        inn_blit(buf + 4, 0x1900, at, 0x50, spr, 0x10, 0x10);
        inn_blit(buf + 0x1908, 0x1900, at, 0x50, spr, 0x10, 0x10);
        at += 0x10;
    }
    for (unsigned i = 0; i < 0xBE0; i++) {   /* 0x26d52 */
        if (big[0xA064 + i])
            buf[0xD24 + i] = big[0xA064 + i];
        if (big[0xAC44 + i])
            buf[0x2628 + i] = big[0xAC44 + i];
    }
    unsigned v10 = 0x6840, v6 = 0x180, n = 0xA8, v8 = 0, va = 0xC;
    while (n) {
        palette_cycle(w, scr);
        boat_turn(big);
        memcpy(buf + 0x32C8, big, 0x7E04);
        if (n > 0x50) {
            boat_stretch(buf + v8, big + 0x7E04, n);
        } else {
            uint8_t *d = big + 0x7E04;
            d[0] = (uint8_t)n, d[1] = 0, d[2] = (uint8_t)n, d[3] = 0;
            battle_shrink(buf + v8 + 4, 0x50, 0x50, d + 4, n, n);
            for (unsigned t = 5 - n / 16; t; t--)
                shade(w, big + 0x7E08, 0x1900);
        }
        boat_blit(buf, 0x32CC, va, 0xC0, big + 0x7E04);
        for (int k = 0; k < 3; k++)
            boat_turn(mb);
        boat_blit(buf, 0x32CC, v10, 0xC0, mb);
        put_pic(scr, 0x1440, buf + 0x32C8);   /* 0x27328 */
        if (w->zoom_show)
            w->zoom_show(w->zoom_ctx, scr);
        v8 = v8 + 0x1904 > 0x1904 ? 0 : v8 + 0x1904;
        n -= 2;
        va = (va + 0x181 + v6 - 0xC0) & 0xFFFF;
        v10 = (v10 + v6 - 0xC0) & 0xFFFF;
        if (v10 < 0x6900 || v10 > 0x72C0)
            v6 = v6 == 0x180 ? 0 : 0x180;
    }
}

/* Kind 8 (0x17ff4 .. 0x1816f): the pointer off, the boat ride, then as a door without its sound or zoom out: each villager's byte 8
 * put back, the scene's 1750 bytes into __1.NOW, party bytes 510 .. 513 = the record's bytes 3 .. 6, [bp-0x74] set (the loader and the
 * zoom in); the pointer's place 0 with the corner kept (0x1723:0xc). */
static void boat(walk_t *w, unsigned si)
{
    uint8_t *ta = w->scene->tab_a;
    const uint8_t *rec = ta + 1000 + si;
    walk_mouse_hide(w, w->scr);
    boat_ride(w, w->scr);
    for (int i = 0; i < WS_VILLAGERS; i++)   /* 0x18054 */
        ta[WS_VILLAGER_SIZE * i + 8] = ta[WS_STATE + i];
    scene_leave(w, rec);
    walk_mouse_reset(w, w->scr);
    w->vbe = 8;
    if (scene_enter(w, w->scr) != 0)
        note_unbuilt(w, "a boat to a scene that does not load", si / WS_RECORD_SIZE);
}

/* ---- number locks (kind 7, 0x17e3d .. 0x17fe2) and scripted talks (kind 0x11, 0x18305 .. 0x18536) ---- */

/* A message box of a record's (0x1afb0, the palette itself), waiting (talking 15) or not; 0 when drawn. */
static int rec_box(walk_t *w, screen_t *scr, unsigned msg, uint8_t colour, int wait)
{
    int r = msgbox_open(&w->box, scr, w->font8, w->table, w->buf + 0x7305, msg, colour, wait);
    if (r < 0) {
        note_unbuilt(w, "a message that MES_TXT.LCC does not have", msg);
        return -1;
    }
    if (r > 0)
        note_unbuilt(w, "a character the message font does not have", msg);
    if (wait)
        w->talking = 15;
    return 0;
}

/* The end of a lock or a talk: as for any record, [bp-0xbe] the kind and the record loop again (0x18b82). */
static void rec_end(walk_t *w)
{
    w->talking = 0;
    w->vbe = w->rec_kind;
    w->rec_kind = 0;
    records_again(w, w->rec_si / WS_RECORD_SIZE);
}

/* Kind 7: when the flag (byte 5, bit byte 6) is not set, message 9 (colour 0x68, not waiting), the pointer off, the number box
 * (0x18fdc). 0 when the flag is set (nothing happens). */
static int lock_open(walk_t *w, screen_t *scr, unsigned si)
{
    const uint8_t *rec = w->scene->tab_a + 1000 + si;
    if (walk_flag_test(w->scene->tab_b, rec[5], rec[6]))
        return 0;
    w->rec_kind = 7;
    w->rec_si = si;
    rec_box(w, scr, 9, 0x68, 0);
    walk_mouse_hide(w, scr);
    ask_open(w, scr);
    return 1;
}

/* The number given: the pointer's place 0 with the corner kept (0x1723:0xc); the word at byte 3 sets the flag and plays SND_EF.1's
 * 0x1bf5, another number gives message 10 (colour 0x68, waiting). */
static void lock_given(walk_t *w, screen_t *scr, unsigned value)
{
    const uint8_t *rec = w->scene->tab_a + 1000 + w->rec_si;
    walk_mouse_reset(w, scr);
    if (value == w16(rec, 3)) {
        walk_flag_set(w->scene->tab_b, rec[5], rec[6]);
        walk_sfx_play(w, SFX_EF, 0x1BF5);
        rec_end(w);
        return;
    }
    if (rec_box(w, scr, 10, 0x68, 1) != 0)
        rec_end(w);
}

/* The talk's next entry from [bp-0x96]: the picture (0x1c5e2) when its byte is not 0, then the message (colour 0xa8, waiting) when
 * its word is not 0; after the sixth, the pointer's place 0 with the corner kept and the end. */
static void rec_talk_next(walk_t *w, screen_t *scr)
{
    const uint8_t *mb = w->zoom_grab;
    while (w->rec_k < 0x12) {
        unsigned k = w->rec_k;
        w->rec_k += 3;
        if (mb[k + 2])
            look_picture(w, scr, mb[k + 2]);
        unsigned msg = w16(mb, k);
        if (msg && rec_box(w, scr, msg, 0xA8, 1) == 0)
            return;
    }
    walk_mouse_reset(w, scr);
    rec_end(w);
}

/* Kind 0x11: the pointer off; when the flag (byte 3, bit byte 4) is set and byte 5 is not 0: the flag (byte 0xd, bit byte 0xe) set,
 * byte 5 = 0 (it plays once), the map buffer's first 0x14 bytes cleared (0x273f8), the record's bytes 6 .. 11 copied there and, when
 * the next record is of kind 0x12, its bytes 3 .. 14 after them: six (message word, picture byte) entries. */
static void rec_talk_open(walk_t *w, screen_t *scr, unsigned si)
{
    uint8_t *rec = w->scene->tab_a + 1000 + si, *mb = w->zoom_grab;
    w->rec_kind = 0x11;
    w->rec_si = si;
    w->rec_k = 0x12;
    walk_mouse_hide(w, scr);
    if (walk_flag_test(w->scene->tab_b, rec[3], rec[4]) && rec[5]) {
        walk_flag_set(w->scene->tab_b, rec[0xD], rec[0xE]);
        rec[5] = 0;
        memset(mb, 0, 0x14);
        memcpy(mb, rec + 6, 6);
        if (rec[0x11] == 0x12)
            memcpy(mb + 6, rec + 0x12, 12);
        w->rec_k = 0;
    }
    rec_talk_next(w, scr);
}

/* A round of a lock's or a talk's waiting box (0x1b506, the palette itself) */
static void rec_box_round(walk_t *w, screen_t *scr, unsigned port60)
{
    int mx, my;
    unsigned mb = 0;
    int on = walk_mouse(w, &mx, &my, &mb);
    if (!msgbox_round(&w->box, scr, port60, on, mb, take_box_step, w))
        return;
    w->talking = 0;
    if (w->rec_kind == 0x11)
        rec_talk_next(w, scr);
    else
        rec_end(w);
}

/* ---- the card game, kind 0xe (0x18181 .. 0x182f3, 0x24962 .. 0x25b52) ---- */

/* The original's three far buffers as the game uses them, by their offsets: the world buffer [bp-0x8c] (B), [bp-0x20] (D) and the
 * map buffer [bp-0xba] (M). The game reaches past what the port's own buffers hold (B to 0xea60), so it has its own; the world
 * redraws its buffers on the next pass. */
static uint8_t cg_B[0x10000], cg_D[0x10000], cg_M[0x10000];

enum {
    CG_STRIP = 0x60D2, CG_STRIP_N = 0x8C4,      /* B: four 160 x 14 strips with their texts, 0x8c4 apart */
    CG_DEALER = 0x83D6, CG_DEALER_N = 0x1482,   /* B: five 70 x 75 pictures of the dealer */
    CG_WORK = 0x1B58,                           /* B: the cards' row being turned; D: the ten backs (190 x 36 with its header) */
    CG_SQUASH = 0x36B0,                         /* D: a row of cards squashed */
    CG_ROW_AT = 0x84C1, CG_STRIP_AT = 0xD390, CG_BET_AT = 0xC24D, CG_DEALER_AT = 0x197D,
    CG_CUR0 = 0xB301, CG_CUR9 = 0xB3AC, CG_SEL0 = 0x7E81, CG_SEL9 = 0x7F2C
};

/* 0x2723c: the screen's w x h at `at` into p with its header (width, height) */
static void cg_grab(screen_t *scr, unsigned at, unsigned bw, unsigned bh, uint8_t *p)
{
    p[0] = (uint8_t)bw, p[1] = (uint8_t)(bw >> 8), p[2] = (uint8_t)bh, p[3] = (uint8_t)(bh >> 8);
    grab(scr, at, bw, bh, p + 4);
}

/* 0x27371: a picture with its header on the screen, colour 0 clear */
static void cg_put_nz(screen_t *scr, unsigned at, const uint8_t *p)
{
    blit_nz(scr->pix, sizeof scr->pix, SCREEN_W, at, p + 4, w16(p, 0), w16(p, 2), -1);
}

/* 0x26f72 into a picture with its header */
static void cg_shrink(const uint8_t *src, uint8_t *dst, unsigned dw, unsigned dh)
{
    dst[0] = (uint8_t)dw, dst[1] = (uint8_t)(dw >> 8), dst[2] = (uint8_t)dh, dst[3] = (uint8_t)(dh >> 8);
    battle_shrink(src + 4, w16(src, 0), w16(src, 2), dst + 4, dw, dh);
}

/* 0x24302 (five digits, colour 0 clear) after a 40 x 5 box of colour 0 (0x270e0): the bet */
static void cg_show_bet(walk_t *w, screen_t *scr)
{
    fill(scr, CG_BET_AT, 0x28, 5, 0);
    shop_number(w, scr, CG_BET_AT, w->cg_bet);
}

/* The mark under card place `at` (row 143) or over it (rows 101 .. 106): a 19 x 4 frame and the 17 x 2 inside */
static void cg_mark(screen_t *scr, unsigned at, uint8_t frame, uint8_t inside)
{
    use_outline(scr, at, 0x13, 4, frame);
    fill(scr, at + 0x141, 0x11, 2, inside);
}

/* the coins while betting written to party word 0x122 (0x15fe:0x11; nothing drawn) */
static void cg_set_coins(walk_t *w)
{
    w->scene->tab_b[WALK_GOLD_AT] = (uint8_t)w->cg_coins;
    w->scene->tab_b[WALK_GOLD_AT + 1] = (uint8_t)(w->cg_coins >> 8);
}

static void cg_show(walk_t *w, screen_t *scr)
{
    if (w->zoom_show)
        w->zoom_show(w->zoom_ctx, scr);
}

/* The row of cards turned over (0x2563a .. 0x25786, 0x259e1 .. 0x25b21): `from` squashed from 36 rows to 2 in 18 steps, then `to`
 * from 2 to 34 in 17, each step into a copy of B's 190 x 36 picture at its start (B + 0x1b58), centred, shown at (65, 106). */
static void cg_turn(walk_t *w, screen_t *scr, const uint8_t *from, const uint8_t *to)
{
    unsigned at = 0x1B5C, n = 0x1AB8, h = 0x24;
    for (; h > 0; h -= 2) {
        memmove(cg_B + CG_WORK, cg_B, 0xD5E * 2);
        cg_shrink(from, cg_D + CG_SQUASH, 0xBE, h);
        memmove(cg_B + at, cg_D + CG_SQUASH + 4, n);
        n -= 0x17C;
        at += 0xBE;
        put_pic(scr, CG_ROW_AT, cg_B + CG_WORK);
        cg_show(w, scr);
    }
    n = 0x17C;
    at -= 0xBE;
    for (h = 2; h < 0x24; h += 2) {
        memmove(cg_B + CG_WORK, cg_B, 0xD5E * 2);
        cg_shrink(to, cg_D + CG_SQUASH, 0xBE, h);
        memmove(cg_B + at, cg_D + CG_SQUASH + 4, n);
        n += 0x17C;
        at -= 0xBE;
        put_pic(scr, CG_ROW_AT, cg_B + CG_WORK);
        cg_show(w, scr);
    }
}

/* A round's start (0x24dcd .. 0x25085): the ten backs shown; 20 swaps of two places of the deck at random (rand / 0xccd, which can
 * be 10: one byte past the deck, 0 here, GUESS); the way to win at random (rand / 0x4000; 1: higher, the lit arrow at (197, 59)); a
 * light running 20 .. 30 places along the marks over the cards, the dealer's picture and the palette stepping each place, where it
 * stops is the dealer's card; the ten marks under the cards; the bet shown; then the betting loop. */
static void cg_round(walk_t *w, screen_t *scr)
{
    put_pic(scr, CG_ROW_AT, cg_D + CG_WORK);
    for (int k = 0; k < 20; k++) {
        unsigned a = walk_rand(&w->rng) / 0xCCD, b = walk_rand(&w->rng) / 0xCCD;
        uint8_t t = w->cg_deck[a];
        w->cg_deck[a] = w->cg_deck[b];
        w->cg_deck[b] = t;
    }
    w->cg_dir = (walk_rand(&w->rng) / 0x4000) & 0xFF;
    use_outline(scr, w->cg_dir ? 0x4C85 : 0x6085, 0x1F, 0x10, 0xF);
    use_outline(scr, w->cg_dir ? 0x6085 : 0x4C85, 0x1F, 0x10, 0x18);
    unsigned a = CG_SEL0, b = CG_SEL9;
    for (unsigned n = walk_rand(&w->rng) / 0xCCC + 0x14; n; n--) {
        a += 0x13, b += 0x13;
        palette_cycle(w, scr);
        put(scr, CG_DEALER_AT, cg_B + CG_DEALER, 0x46, 0x4B);   /* 0x25b94 */
        if (a > CG_SEL9)
            a = CG_SEL0;
        if (b > CG_SEL9)
            b = CG_SEL0;
        cg_mark(scr, a, 0x18, 0x10);
        cg_mark(scr, b, 0x1F, 0x18);
    }
    w->cg_shown = w->cg_deck[(a - CG_SEL0) / 0x13];
    for (unsigned at = CG_CUR0; at < 0xB3BF; at += 0x13)
        cg_mark(scr, at, 0xBF, 0xB8);
    w->cg_cur = CG_CUR0;
    w->cg_last = CG_CUR0 + 0x13;
    w->cg_mlatch = 0;
    w->cg_latch = 0;
    w->cg_coins = shop_coins(w);
    cg_show_bet(w, scr);
    w->cg_strip = 0;
    w->talking = 16;
}

/* 0x24962: the window grabbed and shaded six times, each shown; the number box (RPG.LCP 0x1643b) at (49, 150) with a 46 x 10 box of
 * colour 0 at (137, 153); the 160 x 14 at (80, 169) kept four times, with MES_TXT.LCC 0x17586's four 128-byte texts (20, 20, 18 and
 * 18 characters, colour 0x78) centred in them; RPG.LCP 0x58f4: the ten 19 x 36 card faces, a card's back and the table
 * (packed, shown at (71, 62), colour 0 clear); SHOP_PIC.LCC 0x1339e: the dealer's five pictures; the 190 x 36 at (65, 66) kept, the
 * ten backs 19 apart in a copy of it; the deck 0 .. 9; the bet 0; then a round. */
static void cg_open(walk_t *w, screen_t *scr)
{
    if (w->mouse_on) {   /* 0x249a4: the game's pointer at 0 with what is there kept */
        w->talk_mprev = 0;
        walk_mouse_grab(scr, 0, w->talk_mbg);
    }
    cg_grab(scr, 0x1440, 0xC0, 0xA8, cg_B);
    for (int k = 0; k < 6; k++) {
        shade(w, cg_B + 4, 0x7E00);
        put_pic(scr, 0x1440, cg_B);
        cg_show(w, scr);
    }
    if (assets_read_at("RPG.LCP", 0x1643B, cg_B, 0x4BE) != 0 || assets_read_at("MES_TXT.LCC", 0x17586, cg_M + 0x1000, 0x200) != 0 ||
        assets_read_at("RPG.LCP", 0x58F4, cg_M + 0x2000, 0x1E83) != 0)
        note_unbuilt(w, "the card game's pictures or texts that the data files do not have", w->cg_si / WS_RECORD_SIZE);
    walk_rle_unpack(cg_B, 0x4BE, cg_M, 0x10000);
    put_pic(scr, 0xBBF1, cg_M + 0x280);
    fill(scr, 0xBFC9, 0x2E, 0xA, 0);
    cg_grab(scr, CG_STRIP_AT, 0xA0, 0xE, cg_B + CG_STRIP);
    for (int k = 1; k < 4; k++)
        memmove(cg_B + CG_STRIP + k * CG_STRIP_N, cg_B + CG_STRIP, CG_STRIP_N);
    memmove(cg_M, cg_M + 0x1000, 0x200);
    for (unsigned k = 0; k < 4; k++) {   /* 0x243ec into D, then 0x26ad2 into the strip */
        unsigned n = k < 2 ? 0x14 : 0x12;
        uint8_t *d = cg_D;
        memset(d + 3, 0, 8 * n * 15 + 1);   /* 0x243ec clears 15 rows and a byte from its header's byte 3 */
        d[0] = (uint8_t)(8 * n), d[1] = 0, d[2] = 0xE;
        if (battle_text_pic(w->font8, cg_M + 0x80 * k, n, 0x78, d + 4, &w->shop_gw))
            note_unbuilt(w, "a character the font does not have", w->cg_si / WS_RECORD_SIZE);
        blit_nz(cg_B + CG_STRIP + 4 + k * CG_STRIP_N, 0xA0 * 0xE, 0xA0, (0xA0 - 8 * n) >> 1, d + 4, 8 * n, 0xE, -1);
    }
    memmove(cg_M, cg_M + 0x2000, 0x1E83);
    walk_rle_unpack(cg_M + 0x1D68, 0x1E83 - 0x1D68, cg_B, 0x10000);
    cg_put_nz(scr, 0x4DC7, cg_B);
    /* read after the strips were copied: the fourth strip's last 12 bytes (to 0x83e2) are the first dealer picture's first */
    if (assets_read_at("SHOP_PIC.LCC", 0x1339E, cg_B + CG_DEALER, 0x668A) != 0)
        note_unbuilt(w, "the card game's dealer pictures that SHOP_PIC.LCC does not have", w->cg_si / WS_RECORD_SIZE);
    cg_grab(scr, 0x52C1, 0xBE, 0x24, cg_B);   /* over the table's picture, already shown */
    uint8_t *backs = cg_D + CG_WORK;
    backs[0] = 0xBE, backs[1] = 0, backs[2] = 0x24, backs[3] = 0;
    const uint8_t *back = cg_M + 0x1AB8;   /* 0x26a88: a card's back with its header, opaque */
    for (unsigned k = 0, at = 4; k < 10; k++, at += 0x13)
        for (unsigned r = 0; r < w16(back, 2); r++)
            memcpy(backs + at + r * 0xBE, back + 4 + r * w16(back, 0), w16(back, 0));
    for (unsigned k = 0; k < 10; k++)
        w->cg_deck[k] = (uint8_t)k;
    w->cg_deck[10] = 0;
    w->cg_bet = 0;
    w->cg_pal = WALK_CARDS_PAL;
    cg_round(w, scr);
}

/* The end (0x25b27): message 0xe1 (colour 0xb0, waiting, the palette itself), then the record's end */
static void cg_close(walk_t *w, screen_t *scr)
{
    int r = msgbox_open(&w->box, scr, w->font8, w->table, cg_B + 0x7305, 0xE1, 0xB0, 1);
    if (r < 0) {
        note_unbuilt(w, "a message that MES_TXT.LCC does not have", 0xE1);
        w->talking = 0;
        return;
    }
    if (r > 0)
        note_unbuilt(w, "a character the message font does not have", 0xE1);
    w->talking = 18;
}

/* 0x18213 .. 0x182f3: after the game the party and the trail's places at (byte 3, 2 x byte 4), the roof flag cleared; the pointer's
 * place 0 with the corner kept; the record loop again. */
static void cg_done(walk_t *w, screen_t *scr)
{
    walk_scene_t *s = w->scene;
    const uint8_t *rec = s->tab_a + 1000 + w->cg_si;
    uint8_t x = rec[3], y = (uint8_t)(rec[4] * 2);
    for (unsigned o = 0; o < 0xC8; o += WS_PARTY_SIZE) {
        s->tab_b[o] = x;
        s->tab_b[o + 1] = y;
    }
    for (unsigned k = 0; k < 13; k++) {
        s->trail[3 * k] = x;
        s->trail[3 * k + 1] = y;
    }
    w->alt[0] = 0;
    walk_mouse_reset(w, scr);
    w->talking = 0;
    w->vbe = 0xE;
    records_again(w, w->cg_si / WS_RECORD_SIZE);
}

/* Kind 0xe: the pointer off; with coins (party word 0x122) the game, else only the pointer's place 0 and the record loop again. */
static void cards_open(walk_t *w, screen_t *scr, unsigned si)
{
    walk_mouse_hide(w, scr);
    w->cg_si = si;
    if (shop_coins(w) == 0) {
        walk_mouse_reset(w, scr);
        w->vbe = 0xE;
        records_again(w, si / WS_RECORD_SIZE);
        return;
    }
    cg_open(w, scr);
}

/* The card picked (0x25586 .. 0x258aa): the coins written; no bet ends the game. Else the faces drawn into the kept 190 x 36 (D), the
 * row turned from the backs to the faces and the faces shown; a win (the card higher than the dealer's when the arrow says higher,
 * lower else; equal loses) doubles the bet, a loss takes it; the dealer's pictures 1 and 2 (a win) or 3 and 4; the strip's third or
 * fourth text; no bet and no coins ends the game; the bet shown, SND_EF.1 0x1bf5 (a win) or 0 played; then the wait. */
static void cg_picked(walk_t *w, screen_t *scr)
{
    cg_set_coins(w);
    if (w->cg_bet == 0) {
        cg_close(w, scr);
        return;
    }
    cg_D[0] = 0xBE, cg_D[1] = 0, cg_D[2] = 0x24, cg_D[3] = 0;
    for (unsigned k = 0, at = 4; k < 10; k++, at += 0x13)   /* 0x26c04 */
        inn_blit(cg_D, 0x10000, at, 0xBE, cg_M + w->cg_deck[k] * 0x2AC, 0x13, 0x24);
    cg_turn(w, scr, cg_D + CG_WORK, cg_D);
    put_pic(scr, CG_ROW_AT, cg_D);
    int win = w->cg_dir ? w->cg_pick > w->cg_shown : w->cg_pick < w->cg_shown;
    w->cg_bet = win ? (w->cg_bet << 1) & 0xFFFF : 0;
    w->cg_last = win ? CG_DEALER + CG_DEALER_N : CG_DEALER + 3 * CG_DEALER_N;
    put_pic(scr, CG_STRIP_AT, cg_B + CG_STRIP + (w->cg_bet ? 3 : 2) * CG_STRIP_N);
    w->cg_cur = w->cg_last + CG_DEALER_N;
    if (w->cg_bet == 0 && w->cg_coins == 0) {
        cg_close(w, scr);
        return;
    }
    cg_show_bet(w, scr);
    walk_sfx_play(w, SFX_EF, w->cg_bet ? 0x1BF5 : 0);
    w->cg_coins = shop_coins(w);
    w->talking = 17;
}

/* A round of the betting loop (0x25088) */
static void cg_bet_round(walk_t *w, screen_t *scr, unsigned port60)
{
    unsigned strip = w->cg_bet ? CG_STRIP + CG_STRIP_N : CG_STRIP;
    if (w->cg_strip != strip) {
        w->cg_strip = strip;
        put_pic(scr, CG_STRIP_AT, cg_B + strip);
    }
    w->cg_pal = (w->cg_pal + 1) & 0xFF;
    if (w->cg_pal > 0x1E) {
        w->cg_pal = 0;
        palette_cycle(w, scr);
    }
    if (w->cg_cur != w->cg_last) {
        cg_mark(scr, w->cg_last, 0xBF, 0xB8);
        cg_mark(scr, w->cg_cur, 0xB8, 0xB0);
        w->cg_last = w->cg_cur;
    }
    unsigned key = port60 & 0xFF;
    int mx, my;
    unsigned mb = 0;
    if (walk_mouse(w, &mx, &my, &mb)) {   /* 0x251b2 */
        mb &= 0xFF;
        if (mb == 0)
            w->cg_mlatch = 1;
        if (!w->cg_mlatch)
            mb = 0;
        if (mb)
            w->cg_mlatch = 0;
        unsigned pos = (unsigned)(my * SCREEN_W + mx) & 0xFFFF;
        if (pos != w->talk_mprev) {
            walk_mouse_put(scr, w->talk_mprev, w->talk_mbg, 0);
            walk_mouse_grab(scr, pos, w->talk_mbg);
            walk_mouse_put(scr, pos, w->mpic, 1);
            w->talk_mprev = pos;
        }
        unsigned x = (unsigned)mx, y = (unsigned)my;
        if (x > 0x70 && x < 0x83 && y > 0x95 && y < 0xA5 && mb == 1)
            key = 0xD;
        if (x > 0xBC && x < 0xCF && y > 0x95 && y < 0xA5 && mb == 1)
            key = 0xC;
        if (x > 0x40 && x < 0xFF && y > 0x6E && y < 0x84) {
            w->cg_cur = (x - 0x41) / 0x13 * 0x13 + CG_CUR0;
            if (mb == 1)
                key = 0x1C;
        }
    }
    if (key > 0x7F) {
        w->cg_latch = 1;
        w->cg_rep = 0xFA;
        w->cg_repc = 0xFA;
    }
    if (!w->cg_latch)
        key = 0x82;
    if (key >= 0x7F)
        return;
    w->cg_latch = 0;
    if (key == 0x4B) {
        w->cg_cur -= 0x13;
        if (w->cg_cur < CG_CUR0)
            w->cg_cur = CG_CUR9;
    }
    if (key == 0x4D) {
        w->cg_cur += 0x13;
        if (w->cg_cur > CG_CUR9)
            w->cg_cur = CG_CUR0;
    }
    for (int plus = 0; plus < 2; plus++) {   /* 0x4a or 0xc: one less; 0x4e or 0xd: one more; held, faster and faster */
        if (plus ? key != 0x4E && key != 0xD : key != 0x4A && key != 0xC)
            continue;
        w->cg_mlatch = 1;
        w->cg_latch = 1;
        w->cg_repc = (w->cg_repc + 1) & 0xFF;
        if (w->cg_repc > w->cg_rep) {
            w->cg_repc = 0;
            if (w->cg_rep)
                w->cg_rep--;
        }
        if (w->cg_repc != 0)
            continue;
        if (!plus && w->cg_bet) {
            w->cg_bet--;
            w->cg_coins++;
            w->cg_pal = 0x37;
        }
        if (plus && w->cg_coins && w->cg_bet < 0x1F4) {
            w->cg_bet++;
            w->cg_coins--;
            w->cg_pal = 0x37;
        }
    }
    if (w->cg_pal == 0x37) {
        cg_show_bet(w, scr);
        walk_draw_gold(scr, w->cg_coins, w->art + ART_DIGITS);   /* 0x1bd6e */
    }
    if (key == 0x39 || key == 0x1C) {
        w->cg_pick = w->cg_deck[(w->cg_cur - CG_CUR0) / 0x13];
        if (w->cg_pick != w->cg_shown)
            cg_picked(w, scr);
    }
}

/* A round of the wait after a card (0x258ad): the dealer's picture, the other of the two every 91st round; the palette every 6th; a
 * make code or any button ends it: the coins written, the row turned from the faces back to the backs, a new round. */
static void cg_wait_round(walk_t *w, screen_t *scr, unsigned port60)
{
    put(scr, CG_DEALER_AT, cg_B + w->cg_cur, 0x46, 0x4B);
    w->cg_rep = (w->cg_rep + 1) & 0xFF;
    if (w->cg_rep > 0x5A) {
        w->cg_rep = 0;
        unsigned t = w->cg_cur;
        w->cg_cur = w->cg_last;
        w->cg_last = t;
    }
    w->cg_pal = (w->cg_pal + 1) & 0xFF;
    if (w->cg_pal > 5) {
        w->cg_pal = 0;
        palette_cycle(w, scr);
    }
    int mx, my;
    unsigned mb = 0;
    if ((port60 & 0xFF) >= 0x7F && !(walk_mouse(w, &mx, &my, &mb) && (mb & 0xFF)))
        return;
    cg_set_coins(w);
    cg_turn(w, scr, cg_D, cg_D + CG_WORK);
    cg_round(w, scr);
}

static void cg_box_round(walk_t *w, screen_t *scr, unsigned port60)
{
    int mx, my;
    unsigned mb = 0;
    int on = walk_mouse(w, &mx, &my, &mb);
    if (msgbox_round(&w->box, scr, port60, on, mb, take_box_step, w))
        cg_done(w, scr);
}

/* ---- keys 1 to 4 and A (0x143f7 .. 0x14d46) ---- */

static void walk_counters(walk_t *w);

/* A button of the right column pressed or let go, the pointer's corner put back first and kept again after. */
static void sys_button(walk_t *w, screen_t *scr, unsigned at)
{
    if (w->mouse_on)
        walk_mouse_put(scr, w->mprev, w->mbg, 0);
    walk_button_toggle(scr, at, 0x2F, 0x14);
    if (w->mouse_on)
        walk_mouse_grab(scr, w->mprev, w->mbg);
}

enum { SYS_MUSIC = 0x7B8D, SYS_SOUND = 0x948D, SYS_SAVE = 0xAD8D, SYS_LEAVE = 0xC68D, SLIDE_AT = 0xE387 };

/* Key A (0x143f7 .. 0x144d4): sliding round corners on or off, shown on the 41 x 12 at (7, 182): its colour 0x1f turned 9 (on) or 9 turned
 * 0x1f (off) (0x2723c, 0x2741b over its 0x1ec bytes, 0x27328; the grab goes through the world buffer's start, which the next pass
 * draws again). */
static void slide_key(walk_t *w, screen_t *scr)
{
    if (w->mouse_on)
        walk_mouse_put(scr, w->mprev, w->mbg, 0);
    uint8_t from = w->slide ? 9 : 0x1F, to = w->slide ? 0x1F : 9;
    w->slide = !w->slide;
    for (unsigned r = 0; r < 0xC; r++)
        for (unsigned c = 0; c < 0x29; c++) {
            uint8_t *p = scr->pix + SLIDE_AT + r * SCREEN_W + c;
            if (*p == from)
                *p = to;
        }
    if (w->mouse_on)
        walk_mouse_grab(scr, w->mprev, w->mbg);
}

/* Key 3 (0x14647 .. 0x14763): the button pressed; message 0x253 (請輸入欲存編號(0-10)?, colour 0x20, not waiting), the pointer off, then
 * the number box (ask_round, then save_given). */
static void save_key(walk_t *w, screen_t *scr)
{
    sys_button(w, scr, SYS_SAVE);
    int r = msgbox_open(&w->box, scr, w->font8, w->table, w->buf + 0x7305, 0x253, 0x20, 0);
    if (r < 0) {
        note_unbuilt(w, "a message that MES_TXT.LCC does not have", 0x253);
        sys_button(w, scr, SYS_SAVE);
        return;
    }
    if (r > 0)
        note_unbuilt(w, "a character the message font does not have", 0x253);
    if (w->mouse_on)
        walk_mouse_put(scr, w->mprev, w->mbg, 0);
    w->sys = 1;
    ask_open(w, scr);
}

/* One save file: `n` bytes of `src` packed (0x25e14) into "saveNN.00K". Returns the packed length (what [bp-0x96] gets), 0 on failure. */
static unsigned save_file(walk_t *w, unsigned slot, unsigned k, const uint8_t *src, size_t n)
{
    uint8_t *packed = malloc(n + n / 2 + 8);
    if (!packed)
        return 0;
    size_t len = walk_rle_pack(src, n, packed);
    char name[16];
    snprintf(name, sizeof name, "save%02u.%03u", slot, k);
    if (assets_write(name, packed, len) != 0)
        note_unbuilt(w, "a save file that cannot be written", slot);
    free(packed);
    return (unsigned)(len & 0xFFFF);
}

/* 0x14734 .. 0x14b4f: the number given (the pointer's place 0 and the corner kept). 0 to 10 saves into slot NN:
 * - the party table's bytes 510 .. 513 (the scene, its start) copied to 500 .. 503; its first 504 bytes packed into saveNN.000;
 * - each villager's byte 8 put back from its copy (the start lowered it on a slow machine), the scene's 1750 bytes of state written
 *   into __1.NOW at 1750 x the scene;
 * - __1.NOW read back: its first 0xfcee bytes packed into saveNN.001, the rest (at most 0x9470, into the map buffer [bp-0xba]) into
 *   saveNN.002;
 * - on a slow machine (speed class below 2) the villagers' byte 8 lowered again and byte 9 zeroed, as at the start.
 * Then the button let go and the pass's walk counter (0x172a4). */
static void save_given(walk_t *w, screen_t *scr, unsigned slot)
{
    walk_scene_t *s = w->scene;
    uint8_t *tb = s->tab_b, *ta = s->tab_a;
    if (w->mouse_on) {
        w->mprev = 0;
        walk_mouse_grab(scr, 0, w->mbg);
    }
    if (slot < 0xB) {
        memcpy(tb + 500, tb + 510, 4);
        tb[502] = tb[512];
        tb[503] = tb[513];
        w->v96 = save_file(w, slot, 0, tb, 0x1F8);
        for (int i = 0; i < WS_VILLAGERS; i++)
            ta[WS_VILLAGER_SIZE * i + 8] = ta[WS_STATE + i];
        if (assets_write_at("__1.now", (size_t)WS_STATE * tb[510], ta, WS_STATE) != 0)
            note_unbuilt(w, "__1.NOW that cannot be written", tb[510]);
        size_t n = 0;
        uint8_t *now = assets_read("__1.now", &n);
        if (now) {
            size_t a = n < 0xFCEE ? n : 0xFCEE, b = n - a < 0x9470 ? n - a : 0x9470;
            w->v96 = save_file(w, slot, 1, now, a);
            w->v96 = save_file(w, slot, 2, now + a, b);
            memcpy(w->zoom_grab, now + a, b < sizeof w->zoom_grab ? b : sizeof w->zoom_grab);
            free(now);
        } else {
            note_unbuilt(w, "__1.NOW that cannot be read back", 0);
        }
        if (s->speed_class < 2) {
            unsigned k = 2u - (unsigned)s->speed_class;
            for (int i = 0; i < WS_VILLAGERS; i++) {
                uint8_t *v = ta + WS_VILLAGER_SIZE * i;
                v[8] = v[8] > k ? (uint8_t)(v[8] - k) : 1;
                v[9] = 0;
            }
        }
    }
    sys_button(w, scr, SYS_SAVE);
    w->sys = 0;
    w->talking = 0;
    walk_counters(w);
}

/* Key 4 (0x14bb4 .. 0x14c8e): the button pressed; message 0x252 (colour 0x20, not waiting), the pointer off, then the yes / no buttons
 * (yesno_round, then leave_answer). */
static void leave_key(walk_t *w, screen_t *scr)
{
    sys_button(w, scr, SYS_LEAVE);
    int r = msgbox_open(&w->box, scr, w->font8, w->table, w->buf + 0x7305, 0x252, 0x20, 0);
    if (r < 0) {
        note_unbuilt(w, "a message that MES_TXT.LCC does not have", 0x252);
        sys_button(w, scr, SYS_LEAVE);
        return;
    }
    if (r > 0)
        note_unbuilt(w, "a character the message font does not have", 0x252);
    if (w->mouse_on)
        walk_mouse_put(scr, w->mprev, w->mbg, 0);
    w->sys = 2;
    yesno_open(w, scr);
}

/* 0x14c96 .. 0x14d46: the answer (the pointer's place 0 and the corner kept). Yes: [bp-0xbe] = 0 and 0x172f0 with no record, which ends
 * WALK.SL at 0x18b91 (the sounds and the music stopped, the buffers freed) and starts MARK.EXE (walk_left). No: the button let go and
 * the pass's walk counter. */
static void leave_answer(walk_t *w, screen_t *scr, int yes)
{
    if (w->mouse_on) {
        w->mprev = 0;
        walk_mouse_grab(scr, 0, w->mbg);
    }
    w->sys = 0;
    w->talking = 0;
    if (yes) {
        w->vbe = 0;
        w->left = 1;
        return;
    }
    sys_button(w, scr, SYS_LEAVE);
    walk_counters(w);
}

int walk_left(const walk_t *w)
{
    return w->left;
}

int walk_ended(const walk_t *w)
{
    return w->ended;
}

/* Keys 1 to 4 and A. Returns 1 when the pass ends there (key 3's number box or key 4's buttons now run), else 0. */
static int sys_keys(walk_t *w, screen_t *scr, unsigned k)
{
    if (k == 0x1E) {
        slide_key(w, scr);
        return 0;
    }
    if (k == 2) {   /* 0x144d4: the music stopped (0x165d:0x126) or the scene's song from its start (0x165d:0x3c); the caller plays it */
        if (w->fm) {
            w->fm = w->fm == 0x81 ? 0x80 : 0x81;
            sys_button(w, scr, SYS_MUSIC);
        }
        return 0;
    }
    if (k == 3) {   /* 0x14597: the Sound Blaster's sounds off or on; one playing is not cut */
        if (w->sb) {
            w->sb = w->sb == 0x81 ? 0x80 : 0x81;
            sys_button(w, scr, SYS_SOUND);
        }
        return 0;
    }
    if (k == 4) {
        save_key(w, scr);
        return w->talking != 0;
    }
    if (k == 5) {
        leave_key(w, scr);
        return w->talking != 0;
    }
    return 0;
}

/* One of the walking handlers (0x13aed .. 0x1423d): the hero faces `dir` and takes a half-tile step that way; blocked, the step is taken
 * back (and [bp-0x2e] is 5). With `slide` (key A on, the first four handlers only) a blocked step looks at three places to the side,
 * the last that is open giving the key of a sideways step for the handlers after this one; none open, or no slide: the bump.
 * Returns the key the later handlers see: 1 after a step, 0 after a bump, else the sideways arrow. */
static unsigned hero_step(walk_t *w, unsigned k, int slide)
{
    walk_scene_t *s = w->scene;
    uint8_t *party = s->tab_b;
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
        return 1;
    }
    w->v2e = 5;
    unsigned side = 0;
    if (slide) {
        if (k == 0x48 || k == 0x50) {   /* up looks along its own row, down 4 rows lower; left, a little left, right */
            unsigned sy = k == 0x48 ? y : (y + 4) & 0xFF;
            if (walk_slide_ok(s->gmap, s->list, (x - 2) & 0xFF, sy))
                side = 0x4B;
            if (walk_slide_ok(s->gmap, s->list, (x - 1) & 0xFF, sy))
                side = 0x4B;
            if (walk_slide_ok(s->gmap, s->list, (x + 2) & 0xFF, sy))
                side = 0x4D;
        } else {                        /* left looks 2 to the left, right 2 to the right: at the row, 2 and 4 below */
            unsigned sx = k == 0x4B ? (x - 2) & 0xFF : (x + 2) & 0xFF;
            if (walk_slide_ok(s->gmap, s->list, sx, y))
                side = 0x48;
            if (walk_slide_ok(s->gmap, s->list, sx, (y + 2) & 0xFF))
                side = 0x48;
            if (walk_slide_ok(s->gmap, s->list, sx, (y + 4) & 0xFF))
                side = 0x50;
        }
    }
    if (side == 0)
        w->v48 = 2;   /* the bump */
    return side;
}

/* 0x135f0 .. 0x13ad9 and the four walking handlers. */
/* Returns 1 when an F-key's action ran: the pass then ends there (0x1729e jumps to 0x172f0 past the walk counters). */
static int walk_keys(walk_t *w, unsigned port60)
{
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
    if (k == 0x42 && w->scr) {   /* F8: the map */
        fkey_begin(w, w->scr);
        map_key(w, w->scr);
        return 1;
    }
    if (k == 0x43 && w->scr) {   /* F9: the party's order */
        fkey_begin(w, w->scr);
        order_key(w, w->scr);
        return 1;
    }
    if (k == 0x44 && w->scr) {   /* F10: the party's data */
        fkey_begin(w, w->scr);
        data_key(w, w->scr);
        return 1;
    }
    if ((k == 0x1E || (k >= 2 && k <= 5)) && w->scr)
        return sys_keys(w, w->scr, k);
    if (!arrow && k != 0) {
        char what[32];
        snprintf(what, sizeof what, "key 0x%x", k);
        note_unbuilt(w, what, 0);
        return 0;
    }
    if (w->v98 != 0 || k == 0)
        return 0;
    /* 0x13aed: up, down, left, right, then up and down again without sliding: each runs when the key is still its own */
    if (k == 0x48)
        k = hero_step(w, k, w->slide);
    if (k == 0x50)
        k = hero_step(w, k, w->slide);
    if (k == 0x4B)
        k = hero_step(w, k, w->slide);
    if (k == 0x4D)
        k = hero_step(w, k, w->slide);
    if (k == 0x48)
        k = hero_step(w, k, 0);
    if (k == 0x50)
        hero_step(w, k, 0);
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
    w->port60 = port60;
    walk_scene_t *s = w->scene;
    unsigned x = s->tab_b[0], y = s->tab_b[1];
    w->unbuilt_new = 0;
    w->scr = scr;
    if (w->left || w->ended)   /* WALK.SL has ended (key 4, or a kind 0x13 record) */
        return 0;
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
    if (stride && s->weather == 1)
        walk_draw_rain(w->buf, w->drops, stride);
    if (stride && s->weather == 2)   /* snow (scene 3) */
        walk_draw_snow(w->buf, w->drops, stride);
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
    walk_counters(w);
    return w->unbuilt_new ? WALK_PASS_UNBUILT : 0;
}

/* 0x172a4 .. 0x172ed: the walk frame's counter, at the end of a pass that did not go through 0x172f0. */
static void walk_counters(walk_t *w)
{
    w->ctr = (w->ctr + 1) & 0xFF;
    if (w->ctr > (unsigned)w->scene->speed_class + w->v2e) {
        w->ctr = 0;
        if (++w->hero_frame > 3)   /* 0x172d7 (a respawn can leave any value here) */
            w->hero_frame = 0;
    }
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

/* The label's row colours (0x89 rows 0-4, 0x8a rows 5-9, 0x88 below), for a translated label */
static uint8_t label_cell(void *ctx, unsigned gw, unsigned index)
{
    (void)ctx;
    unsigned y = index / gw;
    return y < 5 ? 0x89 : y < 10 ? 0x8A : 0x88;
}

int walk_label_sprite(const uint8_t *big5, int chars, uint8_t *out)
{
    int w = 16 * chars;
    memset(out, 0, (size_t)w * 14);
    const char *t = l10n_big5(big5, (size_t)(2 * chars));
    if (t) {   /* a translation, centred in the same w x 14 (the narrow font when the normal one does not fit) */
        size_t len = strlen(t);
        unsigned tw = l10n_width(t, len, L10N_FONT_NORMAL);
        int font = tw <= (unsigned)w ? L10N_FONT_NORMAL : L10N_FONT_NARROW;
        if (font == L10N_FONT_NARROW)
            tw = l10n_width(t, len, font);
        if (l10n_draw(t, len, font, out, (unsigned)w, tw < (unsigned)w ? ((unsigned)w - tw) / 2 : 0, label_cell, NULL, NULL) != 0)
            return -1;
    }
    for (int k = 0; !t && k < chars; k++) {
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
    w->zoom_show = show;
    w->zoom_ctx = ctx;
    walk_palette_load(w, scr);
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

void walk_palette_load(const walk_t *w, screen_t *scr)
{
    for (int i = 0; i < 16; i++)
        for (int ch = 0; ch < 3; ch++)
            scr->pal[i][ch] = w->palbuf[3 * i + ch] & 0x3F;
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

void walk_set_live_port(walk_t *w, unsigned (*port)(void))
{
    w->live_port = port;
}

unsigned walk_inn_nights(const walk_t *w)
{
    return w->inn_nights;
}

unsigned walk_scene_changes(const walk_t *w)
{
    return w->scene_changes;
}

void walk_set_zoom_show(walk_t *w, void (*show)(void *ctx, screen_t *scr), void *ctx)
{
    w->zoom_show = show;
    w->zoom_ctx = ctx;
}

battle_t *walk_cast(const walk_t *w)
{
    return w->talking == 7 || w->talking == 13 ? w->cast : NULL;
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
