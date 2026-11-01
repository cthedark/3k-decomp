/*
 * Tests for the WALK.SL first-screen port (src/walk.c, src/walkscene.c).
 *
 * The small routines are checked on inputs whose results are worked out by hand from the original's code (the
 * addresses are in WALK.SL, see src/walk.h). Values for scene 0 are the ones the oracle showed
 * (docs/findings.md, "WALK.SL step 1" to "step 4"). The whole first screen and the loader's buffers are compared
 * with the original in tools/check_walk.sh; here only what needs no stored reference. Tests that need the game's
 * data are skipped when it is missing.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"
#include "cmf.h"
#include "music.h"
#include "screen.h"
#include "walk.h"

static int failures, skipped;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
            failures++;                                                          \
        }                                                                        \
    } while (0)

static void test_flags(void)
{
    printf("flag test\n");
    uint8_t tb[WS_TAB_B];
    memset(tb, 0, sizeof tb);
    tb[WS_FLAGS_AT + 3] = 0x05;   /* bits 0 and 2 */
    CHECK(walk_flag_test(tb, 3, 0) == 1);
    CHECK(walk_flag_test(tb, 3, 1) == 0);
    CHECK(walk_flag_test(tb, 3, 2) == 1);
    CHECK(walk_flag_test(tb, 3, 7) == 0);
    CHECK(walk_flag_test(tb, 3, 8) == 0);    /* shift count 9 and up gives 0 */
    CHECK(walk_flag_test(tb, 3, 30) == 0);
    CHECK(walk_flag_test(tb, 3, 31) == 1);   /* count 32 masks to 0: the carry is left as the compare set it */
    CHECK(walk_flag_test(tb, 3, 32) == 1);   /* count 33 masks to 1: bit 0 again */
    CHECK(walk_flag_test(tb, 4, 0) == 0);
    tb[WS_FLAGS_AT + 255] = 0x80;
    CHECK(walk_flag_test(tb, 255, 7) == 1);
}

static void test_rain(void)
{
    printf("rain\n");
    uint16_t d[WALK_RAIN_DROPS];
    walk_rain_initial(d);
    CHECK(d[0] == 0x26 && d[1] == 0x1e27 && d[2] == 0x52f6);   /* the first values after srand(0), seen in the oracle */
    for (int i = 0; i < WALK_RAIN_DROPS; i++)
        CHECK(d[i] < 0x8000);
    static uint8_t buf[WALK_W * WALK_H];
    memset(buf, 0, sizeof buf);
    uint16_t first[WALK_RAIN_DROPS];
    memcpy(first, d, sizeof d);
    walk_draw_rain(buf, d, 0x384);
    int n = 0;
    for (size_t i = 0; i < sizeof buf; i++)
        n += buf[i] != 0;
    CHECK(n == 330);   /* 40 x 3 + 35 x 6, if no two drops overlap */
    CHECK(buf[first[0]] == 0xae && buf[first[0] + 0xe1] == 0xad && buf[first[0] + 2 * 0xe1] == 0xac);
    CHECK(buf[first[40]] == 0xad && buf[first[40] + 5 * 0xe1] == 0xa8);
    CHECK(d[0] == first[0] + 450);     /* a short drop moves by stride / 2, a long one by stride */
    CHECK(d[40] == first[40] + 900);
    uint16_t w[WALK_RAIN_DROPS];
    memset(w, 0, sizeof w);
    w[3] = 0xA0FF;
    w[50] = 0xA0FF;
    walk_draw_rain(buf, w, 0x384);
    CHECK(w[3] == 0xA0FF + 450 - 0xA100);   /* wraps modulo 0xa100 */
    CHECK(w[50] == 0xA0FF + 900 - 0xA100);
}

static void test_positions(void)
{
    printf("sprite positions and offsets\n");
    /* The hero: (0x24, 0x2c) with the view centred on it is at buffer (104, 96): 96 * 224 + 104. */
    CHECK(walk_screen_pos(0x24, 0x2C, 0x24, 0x2C, 1, 1) == 96 * 224 + 104);
    CHECK(walk_screen_pos(0, 0x2C, 0x24, 0x2C, 1, 1) == WALK_HIDDEN);   /* x or y 0, or a set outside 1..5: hidden */
    CHECK(walk_screen_pos(0x24, 0, 0x24, 0x2C, 1, 1) == WALK_HIDDEN);
    CHECK(walk_screen_pos(0x24, 0x2C, 0x24, 0x2C, 0, 0) == WALK_HIDDEN);
    CHECK(walk_screen_pos(0x24, 0x2C, 0x24, 0x2C, 0, 6) == WALK_HIDDEN);
    /* A villager: visible only in a box. Left edge: dl = px + 4 must not be below x - 0x16. */
    CHECK(walk_screen_pos(0x24, 0x2C, 0x24, 0x2C, 0, 1) == 96 * 224 + 104);
    CHECK(walk_screen_pos(0x0A, 0x2C, 0x24, 0x2C, 0, 1) == 96 * 224);   /* dl = 0x0e = bl: the left edge, shown at x = 0 */
    CHECK(walk_screen_pos(0x09, 0x2C, 0x24, 0x2C, 0, 1) == WALK_HIDDEN);   /* dl = 0x0d is below bl */
    CHECK(walk_screen_pos(0x09, 0x2C, 0x24, 0x2C, 1, 1) != WALK_HIDDEN);   /* the party has no box */
    /* The other edges (view centre 0x24, 0x2c: the box is dl in [0x0e, 0x42], dh in [0x18, 0x46]). */
    CHECK(walk_screen_pos(0x3E, 0x2C, 0x24, 0x2C, 0, 1) != WALK_HIDDEN);   /* dl = 0x42: the right edge */
    CHECK(walk_screen_pos(0x3F, 0x2C, 0x24, 0x2C, 0, 1) == WALK_HIDDEN);
    CHECK(walk_screen_pos(0x24, 0x14, 0x24, 0x2C, 0, 1) != WALK_HIDDEN);   /* dh = 0x18: the top edge */
    CHECK(walk_screen_pos(0x24, 0x13, 0x24, 0x2C, 0, 1) == WALK_HIDDEN);
    CHECK(walk_screen_pos(0x24, 0x42, 0x24, 0x2C, 0, 1) != WALK_HIDDEN);   /* dh = 0x46: the bottom edge */
    CHECK(walk_screen_pos(0x24, 0x43, 0x24, 0x2C, 0, 1) == WALK_HIDDEN);
    /* With the view far to the right (bl above 0xcb) the right-edge test is skipped. */
    CHECK(walk_screen_pos(0xFA, 0x2C, 0xE0, 0x2C, 0, 1) != WALK_HIDDEN);   /* bl = 0xca: dl = 0xfe, limit 0xfe */
    CHECK(walk_screen_pos(0xFB, 0x2C, 0xE0, 0x2C, 0, 1) == WALK_HIDDEN);
    CHECK(walk_screen_pos(0xFA, 0x2C, 0xE2, 0x2C, 0, 1) == 24 * 0x380 + 0xC8);   /* bl = 0xcc: no right-edge test */
}

static void test_sprite_offsets(void)
{
    printf("sprite offsets\n");
    CHECK(walk_sprite_off_party(2, 3, 1) == 2 * 0x300 + 0x100 + 0xC00);   /* a counter of 3 is taken as 1 */
    CHECK(walk_sprite_off_party(0, 0, 0) == 0);
    CHECK(walk_sprite_off_party(1, 2, 0) == 0x300 + 0x200);
    CHECK(walk_sprite_off_party(3, 0, 5) == (3 * 0x300 + 5 * 0xC00) % 0x10000);
    CHECK(walk_sprite_off_villager(5, 1, 2) == 0x800 + 1 * 0x200 + 0x100);   /* directions 4..7 map to 0..3 */
    CHECK(walk_sprite_off_villager(0, 0, 1) == 0);
    CHECK(walk_sprite_off_villager(3, 0, 5) == 4 * 0x800 + 3 * 0x200);
    CHECK(walk_sprite_off_villager(0x61, 5, 1) == (2 * 0x100 + 0xE80A) % 0x10000);
}

static void test_order(void)
{
    printf("draw order\n");
    static walk_scene_t s;
    uint8_t order[WALK_ENTITIES];
    memset(&s, 0, sizeof s);
    walk_entity_order(&s, order);   /* all y equal: nothing moves */
    for (int i = 0; i < WALK_ENTITIES; i++)
        CHECK(order[i] == i);
    for (int i = 0; i < WS_VILLAGERS; i++)
        s.tab_a[i * WS_VILLAGER_SIZE + 1] = (uint8_t)(100 - i);   /* descending: reversed */
    for (int i = 0; i < 5; i++)
        s.tab_b[i * WS_PARTY_SIZE + 1] = (uint8_t)(50 - i);
    walk_entity_order(&s, order);
    for (int i = 0; i < 5; i++)
        CHECK(order[i] == 24 - i);                   /* the party (50..46) first, smallest y first */
    for (int i = 0; i < WS_VILLAGERS; i++)
        CHECK(order[5 + i] == 19 - i);
    for (int i = 0; i < WS_VILLAGERS; i++)
        s.tab_a[i * WS_VILLAGER_SIZE + 1] = 7;
    for (int i = 0; i < 5; i++)
        s.tab_b[i * WS_PARTY_SIZE + 1] = 7;
    s.tab_a[3 * WS_VILLAGER_SIZE + 1] = 1;
    s.tab_b[2 * WS_PARTY_SIZE + 1] = 1;
    walk_entity_order(&s, order);
    CHECK(order[0] == 3 && order[1] == 22);          /* equal keys keep their order (a swap needs strictly greater) */
    CHECK(order[2] == 0 && order[3] == 1 && order[4] == 2 && order[5] == 4);
}

static void test_ground_objects(void)
{
    printf("ground and objects\n");
    static walk_scene_t s;
    static uint8_t buf[WALK_W * WALK_H];
    memset(&s, 0, sizeof s);
    s.tiles = calloc(WS_TILES_PAD, 1);
    s.sprites = calloc(WS_SPRITES_PAD, 1);
    CHECK(s.tiles && s.sprites);
    for (int i = 0; i < 64; i++)
        s.tiles[64 + i] = (uint8_t)(0x40 + i);   /* tile 1 */
    memset(s.omap, 0xFF, sizeof s.omap);
    /* The view centre (36, 44): the map cell of the first tile is (x / 2 - 11, y / 2 - 10) = (7, 12). */
    s.gmap[(12 * 128 + 7) * 2] = 1;
    walk_draw_ground(buf, &s, 36, 44);
    CHECK(buf[16 * WALK_W + 16] == 0x40);                       /* tile 1's first pixel at buffer (16, 16) */
    CHECK(buf[(16 + 7) * WALK_W + 16 + 7] == 0x40 + 63);       /* and its last at (23, 23) */
    CHECK(buf[16 * WALK_W + 24] == 0 && buf[24 * WALK_W + 16] == 0);   /* the neighbours are tile 0 */
    /* An odd x moves the picture 4 pixels left, an odd y 4 rows up (0x380 = 4 rows of 224). */
    memset(buf, 0, sizeof buf);
    walk_draw_ground(buf, &s, 37, 44);   /* x & 0xfe = 36: the same map cell */
    CHECK(buf[16 * WALK_W + 12] == 0x40);
    memset(buf, 0, sizeof buf);
    walk_draw_ground(buf, &s, 36, 45);
    CHECK(buf[12 * WALK_W + 16] == 0x40);
    /* An object: pixels where the mask is 0, the screen where it is 0xff. */
    memset(buf, 0x33, sizeof buf);
    for (int i = 0; i < 64; i++) {
        s.sprites[5 * 128 + i] = (uint8_t)(0x90 + i % 4);
        s.sprites[5 * 128 + 64 + i] = (i % 2) ? 0xFF : 0x00;
    }
    s.omap[12 * 128 + 7] = 5;   /* object cell (x / 2 - 11) = 7 for x = 36, y / 2 - 10 = 12 */
    walk_draw_objects(buf, &s, 36, 44);
    CHECK(buf[16 * WALK_W + 16] == 0x90);   /* mask 0: the sprite's pixel */
    CHECK(buf[16 * WALK_W + 17] == (0x33 | 0x91));   /* mask 0xff keeps the screen, and the pixel is OR-ed in */
    /* An odd x: the object column is still (x - 0x16) / 2 = 7 and the picture moves 4 pixels left. */
    memset(buf, 0x33, sizeof buf);
    walk_draw_objects(buf, &s, 37, 44);
    CHECK(buf[16 * WALK_W + 12] == 0x90);
    /* A view centre left of 0x16: the column is worked out in a byte (0x2c472), (0x11 - 0x16) & 0xff = 0xfb, halved: column 125
     * of the same row (correction, 2026-10-24: this test had the column negative, -3, read from the row before) */
    s.omap[12 * 128 + 7] = 0xFF;
    s.omap[12 * 128 + 125] = 5;
    memset(buf, 0x33, sizeof buf);
    walk_draw_objects(buf, &s, 0x11, 44);
    CHECK(buf[16 * WALK_W + 12] == 0x90);
    s.omap[12 * 128 + 125] = 0xFF;
    /* rows above the map: the row in a byte too (y / 2 - 10 = 0xfe for y = 16), 0xfe << 7 = 0x7f00 from the object map, past its end:
     * the segment's bytes there (walk_seg_byte; 0x9300 + 0x7f00 + 7 wraps to 0x1207, inside the ground map) */
    memset(s.gmap, 0, sizeof s.gmap);
    s.gmap[0x1207 - 0x16] = 5;
    memset(buf, 0x33, sizeof buf);
    walk_draw_objects(buf, &s, 36, 16);
    CHECK(buf[16 * WALK_W + 16] == 0x90);
    s.gmap[0x1207 - 0x16] = 0;
    /* the ground below the map: row 100 is the villagers' sprite bank, at the segment's 0x6416 */
    static uint8_t bank[WS_BANK1_PAD];
    s.bank1 = bank;
    bank[(100 - 100) * 256 + 0] = 1;   /* the word of map cell (0, 100) */
    memset(buf, 0, sizeof buf);
    walk_draw_ground(buf, &s, 0x16, 220);   /* first row y / 2 - 10 = 100, first column 0 */
    CHECK(buf[16 * WALK_W + 16] == 0x40);
    s.bank1 = NULL;
    /* A tile number is a 16-bit word shifted left by 6 in 16 bits: 0x0401 selects tile 1 again. */
    memset(s.gmap, 0, sizeof s.gmap);
    s.gmap[(12 * 128 + 7) * 2] = 0x01;
    s.gmap[(12 * 128 + 7) * 2 + 1] = 0x04;
    memset(buf, 0, sizeof buf);
    walk_draw_ground(buf, &s, 36, 44);
    CHECK(buf[16 * WALK_W + 16] == 0x40);
    /* The grid is 25 tiles wide: the last is at buffer x = 16 + 24 * 8 = 208 (outside the window copy). */
    s.gmap[(12 * 128 + 7 + 24) * 2] = 1;
    s.gmap[(12 * 128 + 7 + 24) * 2 + 1] = 0;
    memset(buf, 0, sizeof buf);
    walk_draw_ground(buf, &s, 36, 44);
    CHECK(buf[16 * WALK_W + 208] == 0x40);
    free(s.tiles);
    free(s.sprites);
}

/* The entity pass on a made-up scene: sprites are filled with a marker value so that where each lands (and whether it
 * is drawn at all) can be checked without the game's data. Positions are worked out by hand from walk_screen_pos. */
static void test_entities(void)
{
    printf("entities\n");
    static walk_scene_t s;
    static uint8_t buf[WALK_W * WALK_H];
    memset(&s, 0, sizeof s);
    s.bank1 = calloc(WS_BANK1_PAD, 1);
    s.bank2 = calloc(WS_BANK2_PAD, 1);
    CHECK(s.bank1 && s.bank2);
    for (int i = 0; i < 5; i++)
        s.tab_b[i * WS_PARTY_SIZE + 3] = 0x64;   /* nobody is in the party yet */
    /* Villager 0 at the view centre (0x24, 0x2c), set 5, direction 0, frame 0: offset 0x2000, still bank 1. */
    uint8_t *v = s.tab_a;
    v[0] = 0x24, v[1] = 0x2C, v[0xB] = 5;
    memset(s.bank1 + 0x2000, 0x41, 256);
    /* Villager 1: the special direction 0x61, set 1, frame 0: offset 0xe90a, bank 2 at 0xe90a - 0x9470 = 0x549a. */
    v = s.tab_a + WS_VILLAGER_SIZE;
    v[0] = 0x30, v[1] = 0x34, v[2] = 0x61, v[0xB] = 1;
    memset(s.bank2 + 0x549A, 0x52, 256);
    /* Party 1: number 0x63, direction 0, counter 0: bank 2 at 0x63 * 0xc00 = 0x4a400, 0xa400 in 16 bits. */
    uint8_t *p = s.tab_b + WS_PARTY_SIZE;
    p[0] = 0x40, p[1] = 0x30, p[3] = 0x63;
    memset(s.bank2 + 0xA400, 0x63, 256);
    /* Party 2: number 0x64 is not in the party and is not drawn. */
    p = s.tab_b + 2 * WS_PARTY_SIZE;
    p[0] = 0x40, p[1] = 0x3C, p[3] = 0x64;
    memset(s.bank2 + 0xB000, 0x64, 256);
    /* Party 3: far left of the view, outside the villagers' box, but the party has no box. */
    p = s.tab_b + 3 * WS_PARTY_SIZE;
    p[0] = 0x05, p[1] = 0x2C, p[3] = 0x62;
    memset(s.bank2 + 0x9800, 0x62, 256);
    walk_draw_entities(buf, &s, 0x24, 0x2C, 0);
    CHECK(buf[96 * WALK_W + 104] == 0x41 && buf[111 * WALK_W + 119] == 0x41);   /* villager 0 at (104, 96) */
    CHECK(buf[32 * 0x380 + 152] == 0x52);                                        /* villager 1: row 32, al = 0x98 */
    CHECK(buf[32 * 0x380 + 152 + 15 * WALK_W + 15] == 0x52);                     /* and its last pixel */
    CHECK(buf[28 * 0x380 + 216] == 0x63);                                        /* party 1: row 28, al = 0xd8 */
    CHECK(buf[24 * 0x380 + 0xEC] == 0x62);                                       /* party 3: row 24, al = 0xec */
    int n64 = 0;
    for (size_t i = 0; i < sizeof buf; i++)
        n64 += buf[i] == 0x64;
    CHECK(n64 == 0);
    /* A villager outside the box is skipped, and so is one whose set is 0. */
    memset(buf, 0, sizeof buf);
    s.tab_a[0] = 0x05;
    s.tab_a[WS_VILLAGER_SIZE + 0xB] = 0;
    walk_draw_entities(buf, &s, 0x24, 0x2C, 0);
    int n41 = 0, n52 = 0;
    for (size_t i = 0; i < sizeof buf; i++)
        n41 += buf[i] == 0x41, n52 += buf[i] == 0x52;
    CHECK(n41 == 0 && n52 == 0);
    free(s.bank1);
    free(s.bank2);
}

static void test_label(void)
{
    printf("label sprites\n");
    if (!assets_exists("NUMBER.LCZ")) {
        printf("  skipped (no data)\n");
        skipped++;
        return;
    }
    const uint8_t jiao_tan[4] = {0xA5, 0xE6, 0xBD, 0xCD};   /* 交談 */
    CHECK(walk_glyph_offset(0xA5, 0xE6) == 0x5CFE);          /* the first read the oracle logged */
    CHECK(walk_glyph_offset(0xBD, 0xCD) == 0x213A0);         /* and the second */
    uint8_t px[32 * 14];
    CHECK(walk_label_sprite(jiao_tan, 2, px) == 0);
    uint8_t g[28];
    CHECK(assets_read_at("NUMBER.LCZ", 0x5CFE, g, 28) == 0);
    int bad = 0, set = 0;
    for (int y = 0; y < 14; y++) {
        unsigned v = (unsigned)g[2 * y] << 8 | g[2 * y + 1];
        for (int x = 0; x < 16; x++) {
            uint8_t p = px[y * 32 + x];
            int bit = v >> (15 - x) & 1;
            set += bit;
            if ((p != 0) != bit)
                bad++;                                  /* most significant bit first */
            if (y < 5 && bit && p != 0x89)
                bad++;                                  /* row bands: 0..4 are 0x89, 5..9 are 0x8a */
            if (y >= 5 && y < 10 && bit && p != 0x8A)
                bad++;
        }
    }
    CHECK(set > 20 && bad == 0);
    for (int i = 120; i < 510 && i < 32 * 14; i++)
        CHECK(px[i] != 0x88);   /* the recolouring covers bytes 120..509 of a two-character label */
    const uint8_t shi[2] = {0xAC, 0x4F};   /* 是 */
    uint8_t one[16 * 14];
    CHECK(walk_label_sprite(shi, 1, one) == 0);
    int has88 = 0;
    for (int i = 0; i < 16 * 14; i++)
        has88 |= one[i] == 0x88;
    CHECK(has88);   /* a lone character is not recoloured: rows 10..13 keep 0x88 */
}

static void test_scene0(void)
{
    printf("scene 0\n");
    if (!assets_exists("__2.ALL") || !assets_exists("TAB.TAB")) {
        printf("  skipped (no data)\n");
        skipped++;
        return;
    }
    walk_scene_t *s = walk_scene_load();
    CHECK(s != NULL);
    if (!s)
        return;
    CHECK(s->scene == 0);
    CHECK(memcmp(s->header, "\x02\x14\x11\x00\x01\x00", 6) == 0);
    CHECK(s->weather == 1);
    CHECK(s->start_x == 0x24 && s->start_y == 0x2C);   /* MAN_INFO.NOW bytes 502, 503 = 0x24, 0x16; y is doubled */
    CHECK(s->tab_b[0] == 0x24 && s->tab_b[1] == 0x2C && s->tab_b[2] == 1);
    CHECK(s->tiles_len == 0x3E80 && s->sprites_len == 0x3E00);
    CHECK(s->bank1_len == 0x2800 && s->bank2_len == 15360);
    CHECK(s->song_pos == 0x1E085 && s->song_len == 0x2593);
    /* the song is a CMF song of 2,410 events ending at tick 6180 (the register writes of the original's driver for all of it are
     * compared with the player's in tools/check_walk.sh) and the player can be made from it */
    {
        uint8_t *buf = malloc(s->song_len);
        cmf_t *c = buf && assets_read_at("MUC.ALL", s->song_pos, buf, s->song_len) == 0 ? cmf_parse(buf, s->song_len) : NULL;
        CHECK(c != NULL);
        if (c) {
            cmf_cursor cur;
            cmf_event ev;
            unsigned n = 0;
            cmf_rewind(c, &cur);
            while (cmf_next(&cur, &ev))
                n++;
            CHECK(n == 2410 && cur.tick == 6180 && cmf_ticks_per_second(c) == 96);
            cmf_free(c);
        }
        free(buf);
        music_t *m = music_load_at(s->song_pos, s->song_len);
        CHECK(m != NULL);
        music_free(m);
    }
    int cells = 0;
    for (int i = 0; i < WS_OMAP_BYTES; i++)
        cells += s->omap[i] != 0xFF;
    CHECK(cells == 885);   /* seen in the oracle */
    CHECK(s->events == 0);
    /* the copy of each villager's byte 8 */
    for (int i = 0; i < WS_VILLAGERS; i++)
        CHECK(s->tab_a[WS_STATE + i] == s->tab_a[8 + 50 * i] || s->speed_class < 2);
    walk_scene_free(s);
}

static void test_first_screen(void)
{
    printf("first screen\n");
    if (!assets_exists("__2.ALL") || !assets_exists("NUMBER.LCZ")) {
        printf("  skipped (no data)\n");
        skipped++;
        return;
    }
    walk_t *w = walk_create();
    CHECK(w != NULL);
    if (!w)
        return;
    static screen_t a, b;
    memset(a.pix, 0xFE, sizeof a.pix);
    memset(b.pix, 0x11, sizeof b.pix);
    CHECK(walk_first_screen(w, &a) == 0);
    CHECK(walk_first_screen(w, &b) == 0);   /* a second call starts the rain over: the same picture */
    /* Only the interface and the window are drawn: the corners keep what the menu left. */
    CHECK(a.pix[0] == 0xFE && a.pix[SCREEN_W - 1] == 0xFE && a.pix[(SCREEN_H - 1) * SCREEN_W] == 0xFE);
    CHECK(b.pix[0] == 0x11 && b.pix[SCREEN_PIXELS - 1] == 0x11);
    int win_same = 1, win_all = 1;
    for (int y = 16; y < 184; y++)
        for (int x = 64; x < 256; x++) {
            win_same &= a.pix[y * SCREEN_W + x] == b.pix[y * SCREEN_W + x];
            win_all &= a.pix[y * SCREEN_W + x] != 0xFE || b.pix[y * SCREEN_W + x] != 0x11;
        }
    CHECK(win_same);   /* the window is written whole, whatever was under it */
    CHECK(win_all);
    /* outside the window, the label at (17, 4) and the bottom-right box change something */
    int changed = 0;
    for (int y = 4; y < 20; y++)
        for (int x = 17; x < 49; x++)
            changed += a.pix[y * SCREEN_W + x] != 0xFE;
    CHECK(changed > 30);
    CHECK(a.pix[187 * SCREEN_W + 273] == 0 && a.pix[187 * SCREEN_W + 312] == 0);   /* the box of colour 0 */
    CHECK(a.pix[185 * SCREEN_W + 272] == 0xFE && a.pix[185 * SCREEN_W + 313] == 0xFE);   /* and no wider */
    walk_free(w);
}

/* ---- the world in motion (walk.h): each piece on inputs worked out by hand from the original's code ---- */

static void test_rand_and_stride(void)
{
    printf("rand and rain stride\n");
    uint32_t st = 0;
    CHECK(walk_rand(&st) == 0x26 && st == 0x269EC3);   /* 0 * 0x343FD + 0x269EC3, >> 16 */
    CHECK(walk_rand(&st) == 0x1E27);
    CHECK(walk_rand(&st) == 0x52F6);
    CHECK(walk_rain_stride(1, 0) == 0x384);
    CHECK(walk_rain_stride(1, 1023) == 0x384);
    CHECK(walk_rain_stride(1, 1024) == 0xE1 + 0x384);
    CHECK(walk_rain_stride(1, 5 * 1024) == 5 * 0xE1 + 0x384);
    CHECK(walk_rain_stride(1, 10 * 1024) == 10 * 0xE1 + 0x384);   /* 10 is the peak */
    CHECK(walk_rain_stride(1, 11 * 1024) == 9 * 0xE1 + 0x384);    /* above 10 it is 20 - n */
    CHECK(walk_rain_stride(1, 0x4FFF) == 1 * 0xE1 + 0x384);       /* n = 19 */
    CHECK(walk_rain_stride(1, 0x5000) == 0);
    CHECK(walk_rain_stride(1, 0x9FFF) == 0);
    CHECK(walk_rain_stride(1, 0xA001) == 0);
    CHECK(walk_rain_stride(1, 0x10000) == 0x384);   /* only the low 16 bits count */
    CHECK(walk_rain_stride(2, 0) == 0x384);
    CHECK(walk_rain_stride(0, 0) == 0 && walk_rain_stride(3, 0) == 0);
}

static void test_animation_tables(void)
{
    printf("animated tiles and objects\n");
    static uint8_t gmap[WS_GMAP_BYTES];
    uint8_t table[WS_BLOCK_BYTES];
    memset(gmap, 0, sizeof gmap);
    memset(table, 0, sizeof table);
    /* entry 0: the cell at byte 4, period 1, tiles 0x10 .. 0x12; entry 1 ends the list; entry 2 must never run */
    const uint8_t e0[8] = {4, 0, 1, 0, 0x10, 0, 0x12, 0};
    const uint8_t e2[8] = {8, 0, 0, 0, 0x30, 0, 0x31, 0};
    memcpy(table, e0, 8);
    memcpy(table + 16, e2, 8);
    gmap[4] = 0x10;
    gmap[8] = 0x30;
    /* the counter must pass the period (1) before the tile steps: every second pass; after the last tile, the first */
    const unsigned want[8] = {0x10, 0x11, 0x11, 0x12, 0x12, 0x10, 0x10, 0x11};
    for (int p = 0; p < 8; p++) {
        walk_animate_ground(table, gmap);
        CHECK(gmap[4] == want[p]);
    }
    CHECK(gmap[8] == 0x30);
    /* the high byte follows: first 0x100, last 0x1ff, period 0 (every pass) */
    memset(table, 0, sizeof table);
    const uint8_t e1[8] = {6, 0, 0, 0, 0x00, 0x01, 0xFF, 0x01};
    memcpy(table, e1, 8);
    gmap[6] = 0xFF;
    gmap[7] = 0x01;
    walk_animate_ground(table, gmap);
    CHECK(gmap[6] == 0x00 && gmap[7] == 0x01);
    walk_animate_ground(table, gmap);
    CHECK(gmap[6] == 0x01 && gmap[7] == 0x01);
    memset(table, 0, sizeof table);      /* a tile already past the last one just goes on: only an equal one is reset */
    const uint8_t e3[8] = {10, 0, 0, 0, 0x10, 0, 0x12, 0};
    memcpy(table, e3, 8);
    gmap[10] = 0x14;
    gmap[11] = 0;
    walk_animate_ground(table, gmap);
    CHECK(gmap[10] == 0x15);
    memset(table, 0, sizeof table);      /* an entry after the terminator is never run */
    const uint8_t e4[8] = {12, 0, 0, 0, 0x10, 0, 0x12, 0};
    memcpy(table + 8, e4, 8);
    gmap[12] = 0x10;
    walk_animate_ground(table, gmap);
    CHECK(gmap[12] == 0x10);

    static uint8_t omap[WS_OMAP_BYTES];
    memset(omap, 0xFF, sizeof omap);
    memset(table, 0, sizeof table);
    const uint8_t o0[6] = {7, 0, 2, 0, 5, 6};   /* cell 7, period 2, ids 5 .. 6 */
    const uint8_t o1[6] = {9, 0, 1, 0, 20, 21}; /* the second entry starts 6 bytes on */
    memcpy(table, o0, 6);
    memcpy(table + 6, o1, 6);
    omap[9] = 20;
    omap[7] = 5;
    walk_animate_objects(table, omap);   /* counter 1: not yet */
    CHECK(omap[7] == 5 && table[3] == 1);
    CHECK(omap[9] == 21);                /* the second entry (period 1) stepped already */
    walk_animate_objects(table, omap);   /* counter 2 == period: step, counter back to 0 */
    CHECK(omap[7] == 6 && table[3] == 0);
    CHECK(omap[9] == 20);                /* past its last id 21: back to the first */
    walk_animate_objects(table, omap);
    walk_animate_objects(table, omap);   /* 7 > last 6: back to the first */
    CHECK(omap[7] == 5);
    CHECK(omap[9] == 20);
    table[3] = 3;                        /* a counter already past the period must run round to 256 before it hits */
    walk_animate_objects(table, omap);
    CHECK(omap[7] == 5 && table[3] == 4);
}

static void test_palette_pieces(void)
{
    printf("palette cycle pieces\n");
    uint8_t p[24];
    for (int i = 0; i < 24; i++)
        p[i] = (uint8_t)i;
    walk_pal_rotate(p, 1, 3, 1);   /* entries 1, 2, 3: entry k gets the colour of k + 1, the last gets the first's */
    const uint8_t fwd[24] = {0, 1, 2, 6, 7, 8, 9, 10, 11, 3, 4, 5, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23};
    CHECK(memcmp(p, fwd, 24) == 0);
    for (int i = 0; i < 24; i++)
        p[i] = (uint8_t)i;
    walk_pal_rotate(p, 2, 4, 0);   /* entries 2 .. 5: entry k gets k - 1's colour, the first gets the last's */
    const uint8_t bwd[24] = {0, 1, 2, 3, 4, 5, 15, 16, 17, 6, 7, 8, 9, 10, 11, 12, 13, 14, 18, 19, 20, 21, 22, 23};
    CHECK(memcmp(p, bwd, 24) == 0);
    uint8_t q[48];   /* the ramp on entry 15 (bytes 45 .. 47) */
    memset(q, 0, sizeof q);
    q[45] = 1;
    q[46] = 31;
    walk_pal_ramp(q);
    CHECK(q[45] == 1 && q[46] == 38);
    q[46] = 56;
    walk_pal_ramp(q);   /* lands on 63 exactly: the red byte becomes 0, and the next steps go down */
    CHECK(q[45] == 0 && q[46] == 63);
    walk_pal_ramp(q);
    CHECK(q[45] == 0 && q[46] == 56);
    q[46] = 15;
    walk_pal_ramp(q);   /* 8 is above 1: still falling */
    CHECK(q[45] == 0 && q[46] == 8);
    q[46] = 8;
    walk_pal_ramp(q);   /* 1 or less turns it up again */
    CHECK(q[45] == 1 && q[46] == 1);
    q[46] = 60;
    walk_pal_ramp(q);   /* passing 63 without landing on it goes on (the DAC keeps the low 6 bits) */
    CHECK(q[45] == 1 && q[46] == 67);
}

static void test_specks(void)
{
    printf("dark specks\n");
    static uint8_t buf[0x10000];
    uint16_t pos[WALK_SPECKS];
    for (int i = 0; i < WALK_SPECKS; i++)
        pos[i] = (uint16_t)(0x2000 + 2 * i);
    memset(buf, 10, sizeof buf);
    pos[0] = 0x1000;
    pos[1] = 0xA011;    /* + 0xdf = 0xa0f0: wraps by -0x92e0 to 0xe10 */
    pos[2] = 0xA010;    /* 0xa0ef: stays */
    pos[150] = 0xF00;   /* - 0xdf = 0xe21: stays */
    pos[151] = 0xE20;   /* - 0xdf = 0xd41, not above 0xe10: wraps by +0xa0f0 to 0xae31 */
    walk_draw_specks(buf, sizeof buf, pos);
    CHECK(buf[0x1000] == 8 && buf[0xA011] == 8 && buf[0xA010] == 8 && buf[0xF00] == 8 && buf[0xE20] == 8);
    CHECK(pos[0] == 0x1000 + 0xDF);
    CHECK(pos[1] == 0xE10);
    CHECK(pos[2] == 0xA0EF);
    CHECK(pos[150] == 0xE21);
    CHECK(pos[151] == 0xAE31);
    for (int i = 0; i < WALK_SPECKS; i++)
        pos[i] = (uint16_t)(0x3000 + 2 * i);
    buf[0x3000] = 1;
    walk_draw_specks(buf, sizeof buf, pos);
    CHECK(buf[0x3000] == 0xFF);   /* a byte wraps */
    for (int i = 0; i < WALK_SPECKS; i++)
        pos[i] = (uint16_t)(0x3000 + 2 * i);
    pos[5] = 0xAF00;
    walk_draw_specks(buf, 0xAF00, pos);   /* a position at the buffer's length is not written */
    CHECK(buf[0xAF00] == 10);
}

static void make_villager(uint8_t r[WS_VILLAGER_SIZE])
{
    memset(r, 0, WS_VILLAGER_SIZE);
    r[0] = 10;       /* x */
    r[1] = 10;       /* y */
    r[2] = 3;        /* direction 3: x + 1 */
    r[3] = 5;        /* x limits 5 .. 20, y limits 6 .. 21 (different, so that a mix-up shows) */
    r[4] = 6;
    r[5] = 20;
    r[6] = 21;
    r[8] = 1;        /* steps every call */
    r[0xB] = 1;      /* sprite set */
}

static void test_villager(void)
{
    printf("villager step\n");
    static uint8_t gmap[WS_GMAP_BYTES];
    uint8_t list[WS_LIST_BYTES];
    uint8_t r[WS_VILLAGER_SIZE];
    for (int i = 0; i < WS_GMAP_BYTES; i += 2) {
        gmap[i] = 7;       /* every tile is number 7 */
        gmap[i + 1] = 0;
    }
    memset(list, 0, sizeof list);
    list[0] = 7;           /* and 7 is walkable */

    make_villager(r);
    CHECK(walk_villager_step(r, gmap, list) == 1);   /* byte 7: 0 -> 1 */
    CHECK(r[0] == 11 && r[1] == 10 && r[9] == 0 && r[0xA] == 1 && r[7] == 1);
    CHECK(walk_villager_step(r, gmap, list) == 2);
    CHECK(r[0] == 12 && r[0xA] == 0);                /* the frame flips 0 / 1 */

    make_villager(r);                                /* the speed gate: a step only every r[8] calls */
    r[8] = 3;
    CHECK(walk_villager_step(r, gmap, list) == -1 && r[9] == 1 && r[0] == 10);
    CHECK(walk_villager_step(r, gmap, list) == -1 && r[9] == 2);
    CHECK(walk_villager_step(r, gmap, list) == 1 && r[9] == 0 && r[0] == 11);

    make_villager(r);                                /* the four directions and their limits (x 5 .. 20, y 6 .. 21) */
    r[2] = 0;                                        /* y - 1 down to r[4] = 6 */
    walk_villager_step(r, gmap, list);
    CHECK(r[1] == 9 && r[0] == 10);
    r[1] = 6;
    walk_villager_step(r, gmap, list);
    CHECK(r[1] == 6);
    r[2] = 1;                                        /* y + 1 up to r[6] = 21 */
    r[1] = 20;
    walk_villager_step(r, gmap, list);
    CHECK(r[1] == 21);
    walk_villager_step(r, gmap, list);
    CHECK(r[1] == 21);
    r[2] = 2;                                        /* x - 1 down to r[3] = 5 */
    r[0] = 6;
    walk_villager_step(r, gmap, list);
    CHECK(r[0] == 5);
    walk_villager_step(r, gmap, list);
    CHECK(r[0] == 5);
    r[2] = 3;                                        /* x + 1 up to r[5] = 20 */
    r[0] = 19;
    walk_villager_step(r, gmap, list);
    CHECK(r[0] == 20);
    walk_villager_step(r, gmap, list);
    CHECK(r[0] == 20);
    r[2] = 4;                                        /* 4 .. 7: no movement (the counters still run) */
    walk_villager_step(r, gmap, list);
    CHECK(r[0] == 20 && r[1] == 21);

    make_villager(r);                                /* the counter in byte 9 passing byte 8 does not step (it must hit it) */
    r[8] = 3;
    r[9] = 5;
    CHECK(walk_villager_step(r, gmap, list) == -1 && r[9] == 6 && r[0] == 10);

    make_villager(r);                                /* a tile that is not on the list blocks the step */
    list[0] = 9;
    CHECK(walk_villager_step(r, gmap, list) == 1);
    CHECK(r[0] == 10 && r[1] == 10 && r[0xA] == 1);
    memset(list, 0, sizeof list);                    /* a hit in the last place of the list does not count */
    list[2 * 99] = 7;
    walk_villager_step(r, gmap, list);
    CHECK(r[0] == 10);
    list[2 * 98] = 7;                                /* one place earlier does */
    walk_villager_step(r, gmap, list);
    CHECK(r[0] == 11);

    memset(list, 0, sizeof list);                    /* an odd x also needs a third cell, an odd y the row below */
    list[0] = 7;
    make_villager(r);                                /* x 10 -> 11: cells idx, idx + 2, idx + 4 */
    unsigned idx = ((10u >> 1) + 1) << 8 | 10;
    gmap[idx + 4] = 9;
    walk_villager_step(r, gmap, list);
    CHECK(r[0] == 10);
    gmap[idx + 4] = 7;
    gmap[idx + 2] = 9;
    walk_villager_step(r, gmap, list);
    CHECK(r[0] == 10);
    gmap[idx + 2] = 7;
    make_villager(r);                                /* y 10 -> 11: the row below too (same row base: 11 >> 1 = 5) */
    r[2] = 1;
    gmap[idx + 0x100] = 9;
    walk_villager_step(r, gmap, list);
    CHECK(r[1] == 10);
    gmap[idx + 0x100] = 7;
    gmap[idx + 0x102] = 9;
    walk_villager_step(r, gmap, list);
    CHECK(r[1] == 10);
    gmap[idx + 0x102] = 7;
    walk_villager_step(r, gmap, list);
    CHECK(r[1] == 11);
    make_villager(r);                                /* odd x and odd y: all of idx + 4, + 0x100, + 0x102, + 0x104 */
    r[0] = 11;
    r[2] = 1;                                        /* x stays 11, y 10 -> 11 */
    gmap[idx + 0x104] = 9;
    walk_villager_step(r, gmap, list);
    CHECK(r[0] == 11 && r[1] == 10);
    gmap[idx + 0x104] = 7;
    gmap[idx + 4] = 9;
    walk_villager_step(r, gmap, list);
    CHECK(r[1] == 10);
    gmap[idx + 4] = 7;
    walk_villager_step(r, gmap, list);
    CHECK(r[1] == 11);

    make_villager(r);                                /* hidden villagers do nothing */
    r[0xB] = 0;
    CHECK(walk_villager_step(r, gmap, list) == -1 && r[0] == 10 && r[9] == 0);
    r[0xB] = 6;
    CHECK(walk_villager_step(r, gmap, list) == -1);
    make_villager(r);                                /* the counter reaching 10 asks for a new direction; above 10 it resets */
    r[7] = 9;
    CHECK(walk_villager_step(r, gmap, list) == 0xA && r[7] == 10);
    make_villager(r);
    r[7] = 10;
    CHECK(walk_villager_step(r, gmap, list) == 11 && r[7] == 0);
    make_villager(r);                                /* direction 0x61 (smoke) counts up, returns 0 meanwhile, and times out into 0xc */
    r[2] = 0x61;
    r[0xA] = 9;                                      /* at 10 the old port returned 0xa and asked for a direction: 0x2bd15 returns 0 */
    CHECK(walk_villager_step(r, gmap, list) == 0 && r[0xA] == 10 && r[0] == 10);
    r[0xA] = 0x13;
    CHECK(walk_villager_step(r, gmap, list) == 0xC && r[2] == 1 && r[0xA] == 1);

    /* shots: a shot flying down zigzags 2 across each pass; an unused slot (direction 4) moves like 'up' */
    uint8_t fx[WALK_FX_BYTES];
    memset(fx, 0xFA, sizeof fx);
    fx[0] = 100, fx[1] = 50, fx[2] = 1, fx[3] = 0;
    fx[4] = 10, fx[5] = 10, fx[6] = 4, fx[7] = 1;
    walk_shots_move(fx);
    CHECK(fx[0] == 98 && fx[1] == 52 && fx[3] == 1 && fx[4] == 12 && fx[5] == 8 && fx[7] == 0);
    walk_shots_move(fx);
    CHECK(fx[0] == 100 && fx[1] == 54 && fx[3] == 0);
    /* a hit on an enemy of sprite set 2 turns it the shot's way; the fourth destroys it (smoke, counter back to 0) */
    uint8_t ta[WS_VILLAGERS * WS_VILLAGER_SIZE];
    memset(ta, 0, sizeof ta);
    ta[3 * 50 + 0] = 41, ta[3 * 50 + 1] = 21, ta[3 * 50 + 2] = 0, ta[3 * 50 + 0xB] = 2, ta[3 * 50 + 0x31] = 1;
    const uint8_t shot[4] = {42, 21, 3, 0};          /* (x & ~1) + 2, (y & ~1) + 1: the fourth of the four cells tested */
    const uint8_t miss[4] = {44, 21, 3, 0};
    CHECK(walk_shot_hit(miss, ta) == 0);
    CHECK(walk_shot_hit(shot, ta) == 1 && ta[3 * 50 + 2] == 3 && ta[3 * 50 + 0x28] == 1);
    CHECK(walk_shot_hit(shot, ta) == 1 && walk_shot_hit(shot, ta) == 1);
    CHECK(walk_shot_hit(shot, ta) == 2 && ta[3 * 50 + 2] == 0x61 && ta[3 * 50 + 0x28] == 0 && ta[3 * 50 + 0xA] == 0);
    CHECK(walk_shot_hit(shot, ta) == 0);             /* in smoke it is not hit again */
    /* the coin count: right-aligned (one digit at (289, 185); 307 starts at 281), 8 pixels apart, in a 40 x 5 box cleared to 0 */
    static screen_t gs;
    uint8_t digits[350];
    for (int i = 0; i < 350; i++)
        digits[i] = (uint8_t)(1 + i / 35);           /* digit d is all colour d + 1 */
    memset(gs.pix, 0x55, sizeof gs.pix);
    walk_draw_gold(&gs, 307, digits);
    CHECK(gs.pix[185 * SCREEN_W + 273] == 0 && gs.pix[189 * SCREEN_W + 312] == 0 && gs.pix[185 * SCREEN_W + 313] == 0x55);
    CHECK(gs.pix[185 * SCREEN_W + 281] == 4 && gs.pix[189 * SCREEN_W + 287] == 4 && gs.pix[185 * SCREEN_W + 288] == 0);
    CHECK(gs.pix[185 * SCREEN_W + 289] == 1 && gs.pix[185 * SCREEN_W + 297] == 8 && gs.pix[184 * SCREEN_W + 297] == 0x55);
}

static void test_decide(void)
{
    printf("villager direction choice\n");
    uint8_t r[WS_VILLAGER_SIZE];
    uint32_t rng = 0;   /* the first draws are 0x26, 0x1e27, 0x52f6 */
    make_villager(r);
    r[0x31] = 1;
    r[0] = 40;
    /* 0x26 < 0x2aaa: towards the hero's x (36), left since 40 > 36; no rain, byte 0x31 set: kept after one draw */
    CHECK(walk_villager_decide(r, &rng, 0x1235, 36, 44, 0) == 1);
    CHECK(r[2] == 2 && r[7] == 5);   /* 0x1235 & 7 */
    r[0] = 30;
    rng = 0;
    walk_villager_decide(r, &rng, 0, 36, 44, 0);
    CHECK(r[2] == 3);                /* 30 <= 36: right */
    /* 0x52f6 lies between the two limits: no rule applies, a second draw gives 0 .. 7 */
    uint32_t chk = 0;
    walk_rand(&chk);
    walk_rand(&chk);
    walk_rand(&chk);                 /* chk is now after 0x52f6 */
    rng = 0;
    walk_rand(&rng);
    walk_rand(&rng);
    CHECK(walk_villager_decide(r, &rng, 0, 36, 44, 0) == 2);
    CHECK(r[2] == walk_rand(&chk) / 0x1000);   /* the draw after 0x52f6 is the fourth value */
    chk = 0;                                   /* rain (stride <> 0): the random direction, from the second draw */
    rng = 0;
    walk_rand(&chk);
    CHECK(walk_villager_decide(r, &rng, 0, 36, 44, 0x384) == 2);
    CHECK(r[2] == walk_rand(&chk) / 0x1000);
    r[0x31] = 0;                               /* byte 0x31 clear: also random */
    rng = 0;
    CHECK(walk_villager_decide(r, &rng, 0, 36, 44, 0) == 2);
    uint32_t t = 0, at = 0;                    /* a value above 0x5554 goes towards the hero's y */
    unsigned v = 0;
    for (int i = 0; i < 100 && v <= 0x5554; i++) {
        at = t;
        v = walk_rand(&t);
    }
    CHECK(v > 0x5554);
    r[0x31] = 1;
    r[1] = 50;
    rng = at;
    CHECK(walk_villager_decide(r, &rng, 0, 36, 44, 0) == 1 && r[2] == 0);   /* 50 > 44: up */
    r[1] = 40;
    rng = at;
    walk_villager_decide(r, &rng, 0, 36, 44, 0);
    CHECK(r[2] == 1);
    t = 0;                                     /* a value from 0x2000 up to the limit 0x2aaa still goes towards the hero's x */
    v = 0;
    for (int i = 0; i < 2000 && !(v >= 0x2000 && v < 0x2AAA); i++) {
        at = t;
        v = walk_rand(&t);
    }
    CHECK(v >= 0x2000 && v < 0x2AAA);
    r[0] = 40;
    rng = at;
    CHECK(walk_villager_decide(r, &rng, 0, 36, 44, 0) == 1 && r[2] == 2);
    r[0] = 30;                                 /* and the hero's x is the third argument, y the fourth */
    rng = at;
    walk_villager_decide(r, &rng, 0, 31, 0, 0);
    CHECK(r[2] == 3);                          /* 30 <= 31: right (with the arguments swapped it would be left) */
}

static void test_motion_whole(void)
{
    printf("the world in motion\n");
    if (!assets_exists("__2.ALL") || !assets_exists("NUMBER.LCZ")) {
        printf("  skipped (no data)\n");
        skipped++;
        return;
    }
    walk_t *w = walk_create();
    CHECK(w != NULL);
    if (!w)
        return;
    static screen_t a, b;
    for (int i = 0; i < 256; i++)
        for (int c = 0; c < 3; c++)
            a.pal[i][c] = b.pal[i][c] = 15;
    memset(a.pix, 0x33, sizeof a.pix);
    memset(b.pix, 0x33, sizeof b.pix);
    CHECK(walk_begin(w, &a, 0, 20) == 0);
    for (int n = 1; n < 3; n++)
        CHECK(walk_pass(w, &a, 20 + (uint32_t)n / 3, 0) == 0);
    CHECK(a.pal[15][0] == 1 && a.pal[15][1] == 31 && a.pal[16][0] == 15);   /* loaded from RPG.LCP, no cycle yet: the counters need 4 passes */
    CHECK(walk_pass(w, &a, 21, 0) == 0);
    CHECK(a.pal[15][0] == 1 && a.pal[15][1] == 38 && a.pal[15][2] == 0);   /* entry 15 gained 7 (31 -> 38), as in the oracle */
    CHECK(a.pal[16][0] == 15 && a.pal[200][2] == 15);      /* only entries 0 .. 15 are written */
    for (int n = 4; n < 60; n++)
        walk_pass(w, &a, 20 + (uint32_t)n / 3, 0);
    CHECK(walk_begin(w, &b, 0, 20) == 0);                  /* the same start again repeats exactly */
    for (int n = 1; n < 60; n++)
        walk_pass(w, &b, 20 + (uint32_t)n / 3, 0);
    CHECK(memcmp(a.pix, b.pix, sizeof a.pix) == 0 && memcmp(a.pal, b.pal, sizeof a.pal) == 0);
    CHECK(walk_begin(w, &b, 1, 20) == 0);                  /* another seed: another rain */
    for (int n = 1; n < 60; n++)
        walk_pass(w, &b, 20 + (uint32_t)n / 3, 0);
    CHECK(memcmp(a.pix, b.pix, sizeof a.pix) != 0);
    walk_free(w);
}

static void test_pass_wiring(void)
{
    printf("a pass steps the villagers in order and gives each decision the hero's position\n");
    if (!assets_exists("__2.ALL") || !assets_exists("NUMBER.LCZ")) {
        printf("  skipped (no data)\n");
        skipped++;
        return;
    }
    walk_t *w = walk_create();
    CHECK(w != NULL);
    if (!w)
        return;
    static screen_t scr;
    CHECK(walk_begin(w, &scr, 0, 0x6000) == 0);   /* a dry clock: the direction rules towards the hero apply */
    walk_scene_t *s = (walk_scene_t *)walk_scene(w);
    unsigned hx = s->tab_b[0], hy = s->tab_b[1];
    CHECK(hx == 36 && hy == 44);
    for (int k = 0; k < 12; k++) {   /* twelve villagers, all one step from asking for a direction; x between the two coordinates */
        uint8_t *r = s->tab_a + k * WS_VILLAGER_SIZE;
        r[0] = (uint8_t)(38 + k % 4);
        r[1] = (uint8_t)(40 + 3 * (k % 5));
        r[2] = 3;
        r[3] = 1; r[4] = 1; r[5] = 200; r[6] = 200;
        r[7] = 9; r[8] = 1; r[9] = 0;
        r[0xB] = 1;
        r[0x31] = 1;
    }
    uint8_t st[WALK_STATE_BYTES];
    walk_state_dump(w, st);
    uint32_t rng = (uint32_t)st[1000] | (uint32_t)st[1001] << 8 | (uint32_t)st[1002] << 16 | (uint32_t)st[1003] << 24;
    uint8_t exp[WS_VILLAGERS * WS_VILLAGER_SIZE];
    memcpy(exp, s->tab_a, sizeof exp);
    int asked = 0;
    for (int k = 0; k < WS_VILLAGERS; k++) {   /* what the pass should do, by the primitives, in the original's order */
        uint8_t *r = exp + k * WS_VILLAGER_SIZE;
        if (walk_villager_step(r, s->gmap, s->list) == 0xA) {
            walk_villager_decide(r, &rng, 0x6000, hx, hy, 0);
            asked++;
        }
    }
    CHECK(asked >= 12);
    CHECK(walk_pass(w, &scr, 0x6000, 0) == 0);
    CHECK(memcmp(exp, s->tab_a, sizeof exp) == 0);
    walk_state_dump(w, st);
    CHECK(((uint32_t)st[1000] | (uint32_t)st[1001] << 8 | (uint32_t)st[1002] << 16 | (uint32_t)st[1003] << 24) == rng);
    int toward = 0;   /* the hero is to the left and above (x 36 < 38, y 44 > 40 for some), so some go left */
    for (int k = 0; k < 12; k++)
        toward += s->tab_a[k * WS_VILLAGER_SIZE + 2] == 2;
    CHECK(toward >= 1);
    walk_free(w);
}

/* 0x2c8a0: the key of a left press in the world. The places and keys were SEEN in the oracle (one press at pass 3 of scene 1, the key
 * read back at 0x1372d after the call, 2026-10-19), the last key 0 (not an arrow). */
static void test_mouse_key(void)
{
    static const struct { unsigned x, y, k; } seen[] = {
        {20, 5, 0x3B}, {20, 25, 0x3C}, {20, 45, 0x3D}, {20, 65, 0x3E}, {20, 85, 0x3F}, {20, 105, 0x40}, {20, 125, 0x41},
        {20, 150, 0x42}, {20, 165, 0x4B}, {20, 190, 0x1E}, {290, 60, 0x43}, {290, 85, 0x44}, {290, 100, 2}, {290, 120, 3},
        {290, 140, 4}, {290, 170, 5}, {4, 50, 0x3D}, {50, 50, 0x3D}, {51, 50, 0x4B}, {269, 60, 0x43}, {300, 57, 0x4D},
        {300, 178, 0x4D}, {100, 30, 0x48}, {230, 120, 0x4D}, {160, 150, 0x50}, {160, 100, 0x4D},
    };
    for (size_t i = 0; i < sizeof seen / sizeof seen[0]; i++)
        CHECK(walk_mouse_key(seen[i].x, seen[i].y, 0) == seen[i].k);
    /* read from the code: after an arrow the press is an arrow everywhere; the edges of the two button columns */
    CHECK(walk_mouse_key(20, 5, 0x48) == 0x4B);
    CHECK(walk_mouse_key(20, 5, 0x50) == 0x4B);
    CHECK(walk_mouse_key(20, 5, 0x51) == 0x3B);
    CHECK(walk_mouse_key(20, 5, 0x47) == 0x3B);
    CHECK(walk_mouse_key(3, 50, 0) == 0x4B && walk_mouse_key(0x33, 50, 0) == 0x4B && walk_mouse_key(0x32, 50, 0) == 0x3D);
    CHECK(walk_mouse_key(20, 0x15, 0) == 0x3B && walk_mouse_key(20, 0x16, 0) == 0x3C && walk_mouse_key(20, 0x8D, 0) == 0x41);
    CHECK(walk_mouse_key(20, 0xA1, 0) == 0x42 && walk_mouse_key(20, 0xA2, 0) == 0x4B && walk_mouse_key(20, 0xAE, 0) == 0x4B);
    CHECK(walk_mouse_key(20, 0xAF, 0) == 0x1E);
    CHECK(walk_mouse_key(0x10C, 60, 0) == 0x4D && walk_mouse_key(0x10D, 60, 0) == 0x43 && walk_mouse_key(0x13C, 60, 0) == 0x4D);
    CHECK(walk_mouse_key(290, 0x3A, 0) == 0x43 && walk_mouse_key(290, 0x4D, 0) == 0x43 && walk_mouse_key(290, 0x4E, 0) == 0x44);
    CHECK(walk_mouse_key(290, 0x9D, 0) == 4 && walk_mouse_key(290, 0x9E, 0) == 5 && walk_mouse_key(290, 0xB1, 0) == 5);
    CHECK(walk_mouse_key(290, 0xB2, 0) == 0x4D);
    CHECK(walk_mouse_key(0xA0, 0x64, 0) == 0x4D && walk_mouse_key(0x9F, 0x64, 0) == 0x4B);   /* equal distances: across */
    CHECK(walk_mouse_key(0xA0, 0x63, 0) == 0x48 && walk_mouse_key(0xA0, 0x65, 0) == 0x50);
    CHECK(walk_mouse_key(0xA5, 0x5F, 0) == 0x4D && walk_mouse_key(0xA5, 0x5E, 0) == 0x48);
}

static void test_boss_after(void)
{
    printf("after a story enemy's battle: five killed takes it away and sets its flag; fewer puts the party and its trail back\n");
    if (!assets_exists("__2.ALL") || !assets_exists("NUMBER.LCZ")) {
        printf("  skipped (no data)\n");
        skipped++;
        return;
    }
    for (unsigned kills = 4; kills <= 5; kills++) {
        walk_t *w = walk_create();
        CHECK(w != NULL);
        if (!w)
            return;
        static screen_t scr;
        CHECK(walk_begin(w, &scr, 0, 20) == 0);
        walk_scene_t *s = (walk_scene_t *)walk_scene(w);
        uint8_t *r = s->tab_a + 13 * WS_VILLAGER_SIZE;   /* an enemy with byte 0x2f set and no opening message, on the hero */
        r[0] = s->tab_b[0];
        r[1] = s->tab_b[1];
        r[2] = 3;
        r[0xB] = 6;
        r[0xC] = 5;      /* its flag: byte 5, bit 3 */
        r[0xD] = 3;
        r[0x11] = r[0x12] = 0;
        r[0x28] = 50;    /* where a party that did not kill all five goes */
        r[0x29] = 60;
        r[0x2F] = 1;
        r[0x30] = 4;
        r[0x31] = 1;
        uint8_t x0 = r[0], y0 = r[1];
        CHECK(walk_pass(w, &scr, 20, 0) & WALK_PASS_BATTLE);
        CHECK(r[0] == x0 && r[1] == y0);   /* not moved by the touch */
        walk_boss_after(w, kills);
        if (kills == 5) {
            CHECK(r[0] == 0 && r[1] == 0);
            CHECK(walk_flag_test(s->tab_b, 5, 3));
            CHECK(s->tab_b[0] == x0);   /* the party stays */
        } else {
            CHECK(r[0] == x0 && r[1] == y0);
            CHECK(!walk_flag_test(s->tab_b, 5, 3));
            for (int i = 0; i < 5; i++)
                CHECK(s->tab_b[WS_PARTY_SIZE * i] == 50 && s->tab_b[WS_PARTY_SIZE * i + 1] == 60);
            for (int i = 0; i < 13; i++)
                CHECK(s->trail[3 * i] == 50 && s->trail[3 * i + 1] == 60 && s->trail[3 * i + 2] == 1);
        }
        walk_free(w);
    }
}

static void test_resume_stride(void)
{
    printf("the pass resumed after a battle decides with the rain stride of the pass that touched, not the clock's\n");
    if (!assets_exists("__2.ALL") || !assets_exists("NUMBER.LCZ")) {
        printf("  skipped (no data)\n");
        skipped++;
        return;
    }
    walk_t *w = walk_create();
    CHECK(w != NULL);
    if (!w)
        return;
    static screen_t scr;
    CHECK(walk_begin(w, &scr, 0, 20) == 0);   /* scene 0 rains at clock 20; the battle ends at 0x6000, which is dry */
    walk_scene_t *s = (walk_scene_t *)walk_scene(w);
    unsigned hx = s->tab_b[0], hy = s->tab_b[1];
    CHECK(s->weather == 1 && walk_rain_stride(1, 20) != 0 && walk_rain_stride(1, 0x6000) == 0);
    for (int k = 0; k < 13; k++) {   /* twelve enemies one step from asking for a direction, and a thirteenth on the hero */
        uint8_t *r = s->tab_a + k * WS_VILLAGER_SIZE;
        r[0] = k < 12 ? (uint8_t)(38 + k % 4) : (uint8_t)hx;
        r[1] = k < 12 ? (uint8_t)(40 + 3 * (k % 5)) : (uint8_t)hy;
        r[2] = 3;
        r[3] = 1; r[4] = 1; r[5] = 200; r[6] = 200;
        r[7] = 9; r[8] = 1; r[9] = 0;
        r[0xB] = 1;
        r[0x11] = r[0x12] = 0;
        r[0x2F] = 1;   /* not 0: the enemy stays where it is */
        r[0x31] = 1;
    }
    CHECK(walk_pass(w, &scr, 20, 0) & WALK_PASS_BATTLE);
    uint8_t st[WALK_STATE_BYTES];
    walk_state_dump(w, st);
    uint32_t rng = (uint32_t)st[1000] | (uint32_t)st[1001] << 8 | (uint32_t)st[1002] << 16 | (uint32_t)st[1003] << 24;
    uint8_t exp[WS_VILLAGERS * WS_VILLAGER_SIZE];
    memcpy(exp, s->tab_a, sizeof exp);
    int asked = 0;
    for (int k = 0; k < WS_VILLAGERS; k++) {
        uint8_t *r = exp + k * WS_VILLAGER_SIZE;
        if (walk_villager_step(r, s->gmap, s->list) == 0xA) {
            walk_villager_decide(r, &rng, 0x6000, hx, hy, walk_rain_stride(1, 20));
            asked++;
        }
    }
    CHECK(asked >= 12);
    walk_resume(w, 0x6000, 0, 0);
    CHECK(memcmp(exp, s->tab_a, sizeof exp) == 0);
    walk_state_dump(w, st);
    CHECK(((uint32_t)st[1000] | (uint32_t)st[1001] << 8 | (uint32_t)st[1002] << 16 | (uint32_t)st[1003] << 24) == rng);
    walk_free(w);
}

/* ---- the hero ---- */

static void test_foot(void)
{
    printf("the hero's footprint\n");
    static uint8_t gmap[WS_GMAP_BYTES];
    uint8_t list[WS_LIST_BYTES];
    for (int i = 0; i < WS_GMAP_BYTES; i += 2) {
        gmap[i] = 7;
        gmap[i + 1] = 0;
    }
    memset(list, 0, sizeof list);
    list[0] = 7;
    list[2] = 9;   /* 7 and 9 are walkable */
    CHECK(walk_foot_ok(gmap, list, 10, 10) == 1);
    /* (x, y) = (10, 10): the cells idx, idx + 2 of row (10 >> 1) + 1 = 6 at column 10 */
    unsigned idx = (6u << 8) | 10;
    gmap[idx] = 5;
    CHECK(walk_foot_ok(gmap, list, 10, 10) == 0);
    CHECK(walk_foot_ok(gmap, list, 11, 10) == 0);   /* odd x: the same two cells (x & 0xfe), plus a third */
    CHECK(walk_foot_ok(gmap, list, 10, 11) == 0);   /* odd y: row (11 >> 1) + 1 = 6 as well */
    gmap[idx] = 9;
    CHECK(walk_foot_ok(gmap, list, 10, 10) == 1);
    gmap[idx + 2] = 5;
    CHECK(walk_foot_ok(gmap, list, 10, 10) == 0);
    gmap[idx + 2] = 7;
    gmap[idx + 4] = 5;                              /* the third cell matters only for an odd x */
    CHECK(walk_foot_ok(gmap, list, 10, 10) == 1);
    CHECK(walk_foot_ok(gmap, list, 11, 10) == 0);
    gmap[idx + 4] = 7;
    gmap[idx + 0x100] = 5;                          /* the next row matters only for an odd y */
    CHECK(walk_foot_ok(gmap, list, 10, 10) == 1);
    CHECK(walk_foot_ok(gmap, list, 10, 11) == 0);
    gmap[idx + 0x100] = 7;
    gmap[idx + 0x102] = 5;
    CHECK(walk_foot_ok(gmap, list, 10, 10) == 1);
    CHECK(walk_foot_ok(gmap, list, 10, 11) == 0);
    gmap[idx + 0x102] = 7;
    gmap[idx + 0x104] = 5;                          /* both odd: also the corner cell */
    CHECK(walk_foot_ok(gmap, list, 10, 11) == 1);
    CHECK(walk_foot_ok(gmap, list, 11, 10) == 1);
    CHECK(walk_foot_ok(gmap, list, 11, 11) == 0);
    gmap[idx + 0x104] = 7;
    CHECK(walk_foot_ok(gmap, list, 11, 11) == 1);
    /* a tile in the list's last place (the 100th) does not count */
    gmap[idx] = 7;
    memset(list, 0, sizeof list);
    list[2 * 99] = 7;
    CHECK(walk_foot_ok(gmap, list, 10, 10) == 0);
    list[2 * 98] = 7;
    CHECK(walk_foot_ok(gmap, list, 10, 10) == 1);
    /* off the map: not walkable */
    CHECK(walk_foot_ok(gmap, list, 10, 255) == 0);
}

static void test_trail_and_records(void)
{
    printf("the party trail and the scene records under the hero\n");
    uint8_t trail[WS_TRAIL_BYTES], party[200];
    memset(party, 0, sizeof party);
    for (int k = 0; k < 13; k++) {
        trail[3 * k] = (uint8_t)(10 + k);
        trail[3 * k + 1] = (uint8_t)(100 + k);
        trail[3 * k + 2] = (uint8_t)(k % 4);
    }
    party[2] = 3;   /* the hero's direction */
    walk_trail_step(trail, party, 50, 60);
    CHECK(trail[0] == 50 && trail[1] == 60 && trail[2] == 3);
    for (int k = 0; k < 12; k++)   /* entry k + 1 is the old entry k */
        CHECK(trail[3 * (k + 1)] == 10 + k && trail[3 * (k + 1) + 1] == 100 + k && trail[3 * (k + 1) + 2] == k % 4);
    CHECK(party[0] == 50 && party[1] == 60 && party[2] == 3);
    CHECK(party[40] == 12 && party[41] == 102 && party[42] == 2);   /* record 1: entry 3 = old entry 2 */
    CHECK(party[80] == 15 && party[81] == 105 && party[82] == 1);   /* record 2: entry 6 = old entry 5 */
    CHECK(party[120] == 18 && party[121] == 108 && party[122] == 0);   /* record 3: entry 9 = old entry 8 */
    CHECK(party[160] == 21 && party[161] == 111 && party[162] == 3);   /* record 4: entry 12 = old entry 11 */

    uint8_t ta[WS_TAB_A];
    memset(ta, 0, sizeof ta);
    uint8_t *r = ta + 1000 + 15 * 3;   /* record 3: x 38, y 22, kind 3 */
    r[0] = 38; r[1] = 22; r[2] = 3;
    uint8_t *q = ta + 1000 + 15 * 7;   /* record 7: the same place again; the first wins */
    q[0] = 38; q[1] = 22; q[2] = 6;
    uint8_t *k5 = ta + 1000 + 15 * 9;  /* a kind 5 record at (40, 30) */
    k5[0] = 40; k5[1] = 30; k5[2] = 5;
    CHECK(walk_find_record(ta, 38, 44) == 45);
    CHECK(walk_find_record(ta, 39, 44) == 45);       /* x & 0xfe */
    CHECK(walk_find_record(ta, 38, 45) == 45);       /* y >> 1 */
    CHECK(walk_find_record(ta, 40, 44) == 0x2EE);
    CHECK(walk_find_record(ta, 38, 46) == 0x2EE);
    CHECK(walk_find_record(ta, 40, 60) == 135);      /* kind 5 with even coordinates: found */
    CHECK(walk_find_record(ta, 41, 60) == 0x320);    /* with an odd one: no hit */
    CHECK(walk_find_record(ta, 40, 61) == 0x320);
}

/* A run on the real data: scene 0, the hero at (36, 44) facing down. `keys` is a list of {pass, code}; the value set at a pass holds
 * from there on. Returns the state after `n` passes and leaves the hero's position in the party table. */
typedef struct { int pass; unsigned code; } keyev;

static unsigned port_for(const keyev *k, int nk, int pass)
{
    unsigned v = 0;
    int best = -1;
    for (int i = 0; i < nk; i++)
        if (k[i].pass <= pass && k[i].pass >= best) {
            best = k[i].pass;
            v = k[i].code;
        }
    return v;
}

static int run_walk(walk_t *w, screen_t *scr, const keyev *k, int nk, int n, int *posx, int *posy, unsigned flags[])
{
    /* walk_begin has run pass 0 (no key); this runs passes 1 .. n - 1 and records the state after each pass p at index p */
    walk_scene_t *s = (walk_scene_t *)walk_scene(w);
    int rc = 0;
    if (posx)
        posx[0] = s->tab_b[0];
    if (posy)
        posy[0] = s->tab_b[1];
    if (flags)
        flags[0] = 0;
    for (int p = 1; p < n; p++) {
        int got = walk_pass(w, scr, 20 + (uint32_t)p / 3, port_for(k, nk, p));
        if (flags)
            flags[p] = (unsigned)got;
        if (posx)
            posx[p] = s->tab_b[0];
        if (posy)
            posy[p] = s->tab_b[1];
        rc |= got;
    }
    return rc;
}

static void test_walking(void)
{
    printf("walking\n");
    if (!assets_exists("__2.ALL") || !assets_exists("NUMBER.LCZ")) {
        printf("  skipped (no data)\n");
        skipped++;
        return;
    }
    static screen_t scr;
    walk_t *w = walk_create();
    CHECK(w != NULL);
    if (!w)
        return;
    walk_scene_t *s = (walk_scene_t *)walk_scene(w);
    /* walk_begin reloads the scene: the pointer is taken again after each one */
#define BEGIN() (walk_begin(w, &scr, 0, 20) == 0 && ((s = (walk_scene_t *)walk_scene(w)) != NULL))
    CHECK(s->tab_b[0] == 36 && s->tab_b[1] == 44);
    int px[40], py[40];
    unsigned fl[40];

    /* Right held from pass 0. The counter in [bp-0x98] wraps to 0 at pass 2, 5 ...; a step is made then, and in the next pass the
     * hero stands on a record when x & 0xfe is 38 (record 2 of scene 0: kind 3, the house's roof): that pass does nothing else, so the
     * counter stops for a pass. Hand-worked from the code; the same sequence was compared with the original in the oracle. */
    const keyev right[] = {{0, 0x4D}};
    CHECK(BEGIN());
    run_walk(w, &scr, right, 1, 22, px, py, fl);
    const int want_x[22] = {36, 36, 37, 37, 37, 38, 38, 38, 38, 39, 39, 39, 39, 40, 40, 40, 41, 41, 41, 42, 42, 42};
    for (int i = 0; i < 22; i++)
        CHECK(px[i] == want_x[i] && py[i] == 44);
    CHECK(s->tab_b[2] == 3);   /* facing right */
    CHECK(walk_unbuilt(w) == NULL);   /* walking through the roof record sideways does nothing */

    /* the release: the break code of the key stops it; the break code of another key does not */
    const keyev rel[] = {{0, 0x4D}, {7, 0xCD}};
    CHECK(BEGIN());
    run_walk(w, &scr, rel, 2, 30, px, py, NULL);
    CHECK(px[29] == 38);   /* the steps at passes 2 and 5 only */
    const keyev other[] = {{0, 0x4D}, {7, 0xC8}};   /* the break code of Up */
    CHECK(BEGIN());
    run_walk(w, &scr, other, 2, 30, px, py, NULL);
    CHECK(px[29] > 38);

    /* Down: y + 1 at pass 2; Left: x - 1 */
    const keyev down[] = {{0, 0x50}};
    CHECK(BEGIN());
    run_walk(w, &scr, down, 1, 10, px, py, NULL);
    CHECK(py[1] == 44 && py[2] == 45 && px[2] == 36 && s->tab_b[2] == 1);
    const keyev left[] = {{0, 0x4B}};
    CHECK(BEGIN());
    run_walk(w, &scr, left, 1, 10, px, py, NULL);
    CHECK(px[1] == 36 && px[2] == 35 && s->tab_b[2] == 2);

    /* Up from the start is blocked: the hero turns, does not move, and the bump shakes the window for two passes. The shake uses
     * rand(): the state after the bump is not the state without it. */
    const keyev up[] = {{0, 0x48}};
    CHECK(BEGIN());
    uint8_t st[WALK_STATE_BYTES], st2[WALK_STATE_BYTES];
    run_walk(w, &scr, up, 1, 3, px, py, NULL);   /* the step is attempted in pass 2 */
    walk_state_dump(w, st);
    CHECK(px[2] == 36 && py[2] == 44 && s->tab_b[2] == 0);
    CHECK(st[WALK_STATE_BYTES - WALK_STATE_TAIL - 4] == 2);        /* [bp-0x48] = 2 */
    CHECK(st[WALK_STATE_BYTES - WALK_STATE_TAIL - 8] == 5);        /* [bp-0x2e] = 5 after a blocked step */
    walk_pass(w, &scr, 21, 0x48);                    /* pass 3: the shake runs in it */
    walk_state_dump(w, st2);
    CHECK(st2[WALK_STATE_BYTES - WALK_STATE_TAIL - 4] == 1);
    uint32_t rand_before = (uint32_t)st[1000] | (uint32_t)st[1001] << 8 | (uint32_t)st[1002] << 16 | (uint32_t)st[1003] << 24;
    uint32_t rand_after = (uint32_t)st2[1000] | (uint32_t)st2[1001] << 8 | (uint32_t)st2[1002] << 16 | (uint32_t)st2[1003] << 24;
    CHECK(rand_after != rand_before);                /* the shake drew from rand() */

    /* a key the original does nothing with is reported as not built, once per pass it is seen (key 5, 0x06; F1 to F10, 1 to 4 and A
     * are built) */
    const keyev f1[] = {{0, 0x06}};
    CHECK(BEGIN());
    int rc = run_walk(w, &scr, f1, 1, 3, NULL, NULL, NULL);
    CHECK((rc & WALK_PASS_UNBUILT) && walk_unbuilt(w) && strstr(walk_unbuilt(w), "key 0x6"));

    /* the keys 1, 2 and A are released while they repeat: A held from pass 3 turns sliding on once (its mark, the 41 x 12 at (7, 182),
     * has its colour 0x1f turned 9 then, and not back) */
    const keyev akey[] = {{3, 0x1E}};
    CHECK(BEGIN());
    for (int r = 0; r < 12; r++)   /* the mark's colours come from the start's screen (the menu's), not drawn here: stand-ins */
        memset(scr.pix + (182 + r) * SCREEN_W + 7, r & 1 ? 0x1F : 0x20, 41);
    int ones[3] = {0}, k9 = 0;
    for (int n = 1; n <= 8; n++) {
        walk_pass(w, &scr, 20 + (uint32_t)n / 3, n >= akey[0].pass ? akey[0].code : 0);
        if (n == 2 || n == 3 || n == 8) {
            int c = 0;
            for (int r = 0; r < 12; r++)
                for (int x = 0; x < 41; x++)
                    c += scr.pix[(182 + r) * SCREEN_W + 7 + x] == 9;
            ones[n == 2 ? 0 : n == 3 ? 1 : 2] = c;
        }
    }
    k9 = ones[1] - ones[0];
    CHECK(k9 > 0 && ones[2] == ones[1]);

    /* the break code of Up does not stop a held Down (Down, 0x50, is the top of the range of arrows): it still gets its two steps */
    const keyev dn_up[] = {{1, 0x50}, {2, 0xC8}};
    int y_up[12];
    CHECK(BEGIN());
    run_walk(w, &scr, dn_up, 2, 12, NULL, y_up, NULL);
    CHECK(y_up[11] == 46);   /* steps at passes 2 and 5; the third is blocked */
    /* the break code of Up does not stop a held Right either, the extended prefix 0xe0 means "the held key", and the held key's own break
     * code stops it */
    const keyev r_up[] = {{0, 0x4D}, {7, 0xC8}};
    const keyev r_e0[] = {{0, 0x4D}, {7, 0xE0}};
    const keyev r_rel[] = {{0, 0x4D}, {7, 0xCD}};
    int x_r[30], x_up[30], x_e0[30], x_rel[30];
    CHECK(BEGIN());
    run_walk(w, &scr, right, 1, 30, x_r, NULL, NULL);
    CHECK(BEGIN());
    run_walk(w, &scr, r_up, 2, 30, x_up, NULL, NULL);
    CHECK(BEGIN());
    run_walk(w, &scr, r_e0, 2, 30, x_e0, NULL, NULL);
    CHECK(BEGIN());
    run_walk(w, &scr, r_rel, 2, 30, x_rel, NULL, NULL);
    CHECK(x_r[29] > 40 && x_up[29] == x_r[29] && x_e0[29] == x_r[29]);
    CHECK(x_rel[29] == 38);

    /* a scene record that is stepped on makes the record loop run again on the live state: a patched map cell that a kind 2 record
     * writes (record 13 of scene 0: tile 0x19 at map word (30, 15)) is restored */
    const keyev rt[] = {{0, 0x4D}};
    CHECK(BEGIN());
    size_t cell = (15u << 8) + 30;
    CHECK(s->gmap[cell] == 0x19);
    s->gmap[cell] = 0xEE;
    run_walk(w, &scr, rt, 1, 6, px, NULL, NULL);   /* the hero is at x = 38 after pass 5, the record hit is pass 6 */
    CHECK(px[5] == 38 && s->gmap[cell] == 0xEE);
    walk_pass(w, &scr, 22, 0x4D);
    CHECK(s->gmap[cell] == 0x19);
    /* kind 6 stepped on does nothing; a sell counter (kind 0x14) stepped on opens the sale's box (a message box or yes / no), and
     * every kind is built now, so nothing is reported */
    CHECK(BEGIN());
    s->tab_a[1000 + 15 * 2 + 2] = 6;
    rc = run_walk(w, &scr, rt, 1, 8, NULL, NULL, NULL);
    CHECK(!(rc & WALK_PASS_UNBUILT) && walk_kind(w) == 0);
    CHECK(BEGIN());
    s->tab_a[1000 + 15 * 2 + 2] = 0x14;
    rc = run_walk(w, &scr, rt, 1, 8, NULL, NULL, NULL);
    /* the name is the map buffer's bytes, here the start's view: a character the font lacks may be reported, never the kind */
    CHECK((walk_kind(w) == 4 || walk_kind(w) == 8) && !(walk_unbuilt(w) && strstr(walk_unbuilt(w), "scene record kind")));

    /* through the door of the house: Right to x = 38, then Up (the roof comes off, the rain stops), then Down again (the roof goes back,
     * the window shakes, the rain returns). The record is record 1 of scene 0: the roof is x 12 .. 21, y 11 .. 21 of the object map. */
    const keyev door[] = {{10, 0x4D}, {16, 0xCD}, {30, 0x48}, {130, 0xC8}, {150, 0x50}, {260, 0xD0}};
    CHECK(BEGIN());
    uint8_t before[10 * 11], inside[10 * 11], after[10 * 11];
    for (int r = 0; r < 11; r++) {
        memcpy(before + r * 10, s->omap + 12 + ((11 + r) << 7), 10);
        memset(inside + r * 10, 0xFF, 10);
    }
    int nonempty = 0;
    for (int i = 0; i < 110; i++)
        nonempty += before[i] != 0xFF;
    CHECK(nonempty > 20 && s->weather == 1);
    unsigned fl2[300];
    int py2[300];
    rc = run_walk(w, &scr, door, 6, 300, NULL, py2, fl2);
    CHECK(rc == 0 && walk_unbuilt(w) == NULL);
    CHECK(py2[29] == 44 && py2[30] == 43 && py2[129] == 36);
    (void)before;
    /* replay to just after the roof comes off (pass 31) and just after it goes back (pass 176 in the oracle's run) */
    CHECK(BEGIN());
    run_walk(w, &scr, door, 6, 33, NULL, NULL, NULL);
    for (int r = 0; r < 11; r++)
        memcpy(after + r * 10, s->omap + 12 + ((11 + r) << 7), 10);
    CHECK(memcmp(after, inside, sizeof inside) == 0);   /* the roof is gone */
    CHECK(s->weather == 4);                              /* and with it the rain */
    CHECK(BEGIN());
    run_walk(w, &scr, door, 6, 300, NULL, NULL, NULL);
    for (int r = 0; r < 11; r++)
        memcpy(after + r * 10, s->omap + 12 + ((11 + r) << 7), 10);
    CHECK(memcmp(after, before, sizeof before) == 0);   /* put back exactly */
    CHECK(s->weather == 1);
#undef BEGIN
    walk_free(w);
}

static void test_roof(void)
{
    printf("a house's roof\n");
    static uint8_t omap[WS_OMAP_BYTES];
    static uint8_t alt[1 + 256 * 256];
    uint8_t rec[15];
    memset(rec, 0, sizeof rec);
    rec[4] = 3; rec[5] = 2; rec[6] = 5; rec[7] = 4;   /* x 3 .. 5, y 2 .. 4: 3 wide, 3 high */
    for (int i = 0; i < WS_OMAP_BYTES; i++)
        omap[i] = (uint8_t)(i % 200);
    uint8_t orig[WS_OMAP_BYTES];
    memcpy(orig, omap, sizeof orig);
    alt[0] = 0x5A;
    CHECK(walk_roof(omap, alt, rec, 1) == 0);
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 10; x++) {
            int inside = x >= 3 && x <= 5 && y >= 2 && y <= 4;
            CHECK(omap[(y << 7) + x] == (inside ? 0xFF : orig[(y << 7) + x]));
        }
    CHECK(alt[0] == 0x5A);                                     /* the flag byte is the caller's */
    CHECK(alt[1] == orig[(2 << 7) + 3] && alt[3] == orig[(2 << 7) + 5]);   /* row by row, from byte 1 */
    CHECK(alt[4] == orig[(3 << 7) + 3] && alt[9] == orig[(4 << 7) + 5]);
    CHECK(walk_roof(omap, alt, rec, 0) == 0);
    CHECK(memcmp(omap, orig, sizeof orig) == 0);
    rec[7] = 1;                                                /* y1 < y0: nothing to do */
    CHECK(walk_roof(omap, alt, rec, 1) == 0 && memcmp(omap, orig, sizeof orig) == 0);
    rec[7] = 4;
    rec[6] = 2;                                                /* x1 < x0 */
    CHECK(walk_roof(omap, alt, rec, 1) == -1);
    rec[6] = 5;
    rec[7] = 120;                                              /* below the map */
    CHECK(walk_roof(omap, alt, rec, 1) == -1);
    rec[7] = 99;                                               /* the last row is inside */
    CHECK(walk_roof(omap, alt, rec, 1) == 0 && walk_roof(omap, alt, rec, 0) == 0);
    rec[7] = 100;                                              /* one past it is not */
    CHECK(walk_roof(omap, alt, rec, 1) == -1);
    rec[7] = 4;
    rec[4] = 100;
    rec[6] = 130;                                              /* right of the map's 128 columns */
    CHECK(walk_roof(omap, alt, rec, 1) == -1);
    rec[6] = 127;                                              /* the last column is inside */
    CHECK(walk_roof(omap, alt, rec, 1) == 0 && walk_roof(omap, alt, rec, 0) == 0);
    rec[6] = 128;                                              /* one past it is not */
    CHECK(walk_roof(omap, alt, rec, 1) == -1);
    rec[4] = 7; rec[5] = 9; rec[6] = 8; rec[7] = 9;            /* a rectangle of one row is a rectangle */
    CHECK(walk_roof(omap, alt, rec, 1) == 0);
    CHECK(omap[(9 << 7) + 7] == 0xFF && omap[(9 << 7) + 8] == 0xFF && omap[(9 << 7) + 6] == orig[(9 << 7) + 6]);
    CHECK(alt[1] == orig[(9 << 7) + 7] && alt[2] == orig[(9 << 7) + 8]);
    CHECK(walk_roof(omap, alt, rec, 0) == 0);
    CHECK(memcmp(omap, orig, sizeof orig) == 0);
}

static void test_shake(void)
{
    printf("the bump shake\n");
    /* strength 2: t = 8, d = 0x7fff / 8 + 1 = 4096; a = r / 4096 (0 .. 7); above 2 it is (a - 2) rows of 224 */
    CHECK(walk_shake_offset(2, 0) == 0);
    CHECK(walk_shake_offset(2, 4095) == 0);
    CHECK(walk_shake_offset(2, 4096) == 1);
    CHECK(walk_shake_offset(2, 8191) == 1);
    CHECK(walk_shake_offset(2, 8192) == 2);
    CHECK(walk_shake_offset(2, 12288) == 224);
    CHECK(walk_shake_offset(2, 16384) == 448);
    CHECK(walk_shake_offset(2, 0x7FFF) == 1120);
    /* strength 1: t = 4, d = 8192; above 1 it is (a - 1) rows */
    CHECK(walk_shake_offset(1, 8191) == 0);
    CHECK(walk_shake_offset(1, 8192) == 1);
    CHECK(walk_shake_offset(1, 16384) == 224);
    CHECK(walk_shake_offset(1, 0x7FFF) == 448);
}

/* 0x2c658 and 0x2c581 on a made-up source: pixel (64 + c, 57 + r) = 1 + r (so each source row is told apart) and a table t[p] = p + 100.
 * The plain doubling writes p, t[p] / t[p], t[t[p]] at (16 + 2c, 16 + 2r); the dark one does that only for source rows 34 .. 60 and
 * p, 0 / 0, 0 above and below; then rows 83 .. 138 of the result are ANDed with the mask from its first byte. */
static void test_doubling(void)
{
    printf("the doublings of a dark scene\n");
    enum { LEN = WALK_W * WALK_H };
    uint8_t *src = calloc(1, LEN), *dst = calloc(1, LEN), *mask = malloc(0x187A * 2), table[256];
    if (!src || !dst || !mask) {
        CHECK(0);
        free(src);
        free(dst);
        free(mask);
        return;
    }
    for (int i = 0; i < 256; i++)
        table[i] = (uint8_t)(i + 100);
    for (int r = 0; r < 84; r++)
        for (int c = 0; c < 96; c++)
            src[(57 + r) * WALK_W + 64 + c] = (uint8_t)(1 + r);
    memset(mask, 0xFF, 0x187A * 2);
    walk_double(dst, src, table);
    CHECK(dst[16 * WALK_W + 16] == 1 && dst[16 * WALK_W + 17] == 101 && dst[17 * WALK_W + 16] == 101 && dst[17 * WALK_W + 17] == 201);
    CHECK(dst[183 * WALK_W + 207] == (uint8_t)(84 + 200));   /* the last pixel: row 83 of the source, t[t[84]] */
    CHECK(dst[16 * WALK_W + 15] == 0 && dst[16 * WALK_W + 208] == 0 && dst[15 * WALK_W + 16] == 0);   /* nothing outside 192 x 168 */

    memset(dst, 0xEE, LEN);
    walk_double_dark(dst, src, table, mask);
    /* source row 33 (the last unshaded one above) is window row 82/83; source row 34 the first shaded one, rows 84/85 */
    CHECK(dst[82 * WALK_W + 16] == 34 && dst[82 * WALK_W + 17] == 0 && dst[83 * WALK_W + 16] == 0 && dst[83 * WALK_W + 17] == 0);
    CHECK(dst[84 * WALK_W + 16] == 35 && dst[84 * WALK_W + 17] == 135 && dst[85 * WALK_W + 16] == 135 && dst[85 * WALK_W + 17] == 235);
    /* source row 60 the last shaded (rows 136/137), 61 the first unshaded below (rows 138/139) */
    CHECK(dst[137 * WALK_W + 207] == (uint8_t)(61 + 200));
    CHECK(dst[138 * WALK_W + 16] == 62 && dst[138 * WALK_W + 17] == 0 && dst[139 * WALK_W + 16] == 0);
    CHECK(dst[183 * WALK_W + 207] == 0 && dst[182 * WALK_W + 206] == 84);
    CHECK(dst[16 * WALK_W + 15] == 0xEE && dst[15 * WALK_W + 16] == 0xEE);   /* outside the window untouched */
    /* the mask: from row 83 (offset 0x48a0) for 0x187a words, so the last byte ANDed is row 138, column 211 */
    memset(mask, 0, 0x187A * 2);
    mask[(84 - 83) * WALK_W + 17] = 0xFF;
    memset(dst, 0xEE, LEN);
    walk_double_dark(dst, src, table, mask);
    CHECK(dst[84 * WALK_W + 17] == 135 && dst[84 * WALK_W + 16] == 0 && dst[85 * WALK_W + 17] == 0);
    CHECK(dst[82 * WALK_W + 16] == 34);                       /* row 82 is above the mask */
    CHECK(dst[138 * WALK_W + 211] == 0 && dst[138 * WALK_W + 212] == 0xEE);   /* 12,532 bytes: row 138 ends at column 211 */
    CHECK(dst[139 * WALK_W + 16] == 0 && dst[139 * WALK_W + 18] == 0);        /* below: p, 0 / 0, 0, not masked */
    CHECK(dst[140 * WALK_W + 16] == 63);
    free(src);
    free(dst);
    free(mask);
}

/* 0x26e5f worked by hand. Full size (192 x 168): the step 0xffff carries at the first add, not at the second, then at every add, so a
 * row comes out as s0, s1, 0, s2, s3 .. s190 and the rows likewise (source rows 0, 1 at rows 0, 1, row 2 empty, then 2, 3 ..). 24 x 24:
 * across the step is 0xffff * 24 / 192 = 8191 added from 1, carrying after the 9th add and then after every 8th (source pixels at 0, 9,
 * 17, 25); down 0xffff * 24 / 168 = 9362, carrying after the 8th add and then the 7th (source rows at rows 0, 8, 15). */
static void test_zoom_scale(void)
{
    printf("the start's zoom scaling\n");
    enum { W = WALK_ZOOM_W, H = WALK_ZOOM_H };
    static uint8_t src[W * H + 1], dst[W * H];
    for (int r = 0; r < H; r++)
        for (int c = 0; c < W; c++)
            src[r * W + c] = (uint8_t)(1 + (c % 200));
    src[W] = 0xAA;   /* row 1's first pixel, told apart */
    walk_zoom_scale(dst, src, W, H);
    CHECK(dst[0] == 1 && dst[1] == 2 && dst[2] == 0 && dst[3] == 3 && dst[4] == 4 && dst[191] == 191);
    CHECK(dst[W] == 0xAA && dst[2 * W] == 0 && dst[2 * W + 3] == 0 && dst[3 * W + 3] == 3);
    CHECK(dst[167 * W + 191] == 191);
    memset(src, 0, sizeof src);
    for (int r = 0; r < 24; r++)
        for (int c = 0; c < 24; c++)
            src[r * 24 + c] = (uint8_t)(10 * r + c + 1);
    walk_zoom_scale(dst, src, 24, 24);
    CHECK(dst[0] == 1 && dst[9] == 2 && dst[17] == 3 && dst[25] == 4);
    CHECK(dst[1] == 0 && dst[8] == 0 && dst[10] == 0 && dst[16] == 0 && dst[18] == 0);
    CHECK(dst[8 * W] == 11 && dst[8 * W + 9] == 12 && dst[15 * W] == 21);
    for (int r = 1; r < 8; r++)
        CHECK(dst[r * W] == 0 && dst[r * W + 9] == 0);
}

/* F2's record search (0x2c28f) and which message a kind 9 record gives (0x15959 .. 0x15a09). */
/* Keys 3 and A: the saves' packing (0x25e14) and the sliding test (0x2c318), with values worked by hand from the code. */
static void test_save_pack(void)
{
    printf("key 3: the saves' packing; key A: the sliding test\n");
    static uint8_t out[1024];
    /* the marker is the byte used least, the lowest of a tie (0, never used); four 5s are a run, two 2s and the lone bytes literals */
    const uint8_t a[] = {5, 5, 5, 5, 1, 2, 2, 3};
    const uint8_t pa[] = {0, 10, 0, 0, 4, 5, 1, 2, 2, 3};
    CHECK(walk_rle_pack(a, sizeof a, out) == sizeof pa && memcmp(out, pa, sizeof pa) == 0);
    /* two bytes alike are literals; three at the end are a run that the end closes */
    const uint8_t b[] = {4, 4}, pb[] = {0, 5, 0, 4, 4};
    CHECK(walk_rle_pack(b, sizeof b, out) == sizeof pb && memcmp(out, pb, sizeof pb) == 0);
    const uint8_t c[] = {9, 9, 9}, pc[] = {0, 6, 0, 0, 3, 9};
    CHECK(walk_rle_pack(c, sizeof c, out) == sizeof pc && memcmp(out, pc, sizeof pc) == 0);
    /* 300 alike: a run of 255, then one of the 45 left */
    static uint8_t d[300];
    memset(d, 7, sizeof d);
    const uint8_t pd[] = {0, 9, 0, 0, 0xFF, 7, 0, 45, 7};
    CHECK(walk_rle_pack(d, sizeof d, out) == sizeof pd && memcmp(out, pd, sizeof pd) == 0);
    /* every value used, 0 twice: the marker is 1 (the lowest used once); the lone 1 goes as a run counted one short, (1, 0, 1) */
    static uint8_t e[257];
    e[0] = e[1] = 0;
    for (int i = 1; i < 256; i++)
        e[i + 1] = (uint8_t)i;
    size_t ne = walk_rle_pack(e, sizeof e, out);
    CHECK(ne == 262 && out[0] == 1 && out[1] == 6 && out[2] == 1);
    CHECK(out[3] == 0 && out[4] == 0 && out[5] == 1 && out[6] == 0 && out[7] == 1 && out[8] == 2 && out[261] == 255);

    /* the unpacker (WALK0.SL 0x11c97): literals, (marker, count, byte) for count copies; a packed save gives back its bytes except
     * one marker byte per run of them (e above: the lone 1 is lost) */
    uint8_t u[512];
    CHECK(walk_rle_unpack(pa, sizeof pa, u, sizeof u) == sizeof a && memcmp(u, a, sizeof a) == 0);
    CHECK(walk_rle_unpack(pd, sizeof pd, u, sizeof u) == 300 && u[0] == 7 && u[299] == 7);
    CHECK(walk_rle_unpack(out, ne, u, sizeof u) == 256 && u[0] == 0 && u[1] == 0 && u[2] == 2 && u[255] == 255);
    /* a count of 0 gives nothing; the header's length ends it (the bytes after it are not read) */
    const uint8_t f[] = {9, 9, 0, 1, 9, 0, 4, 2, 9, 2, 5, 3, 0xAA};
    CHECK(walk_rle_unpack(f, sizeof f, u, sizeof u) == 4 && u[0] == 1 && u[1] == 2 && u[2] == 5 && u[3] == 5);
    /* past the end of what was read the port stops (the original reads on into its buffer); the output stops at its room */
    CHECK(walk_rle_unpack(f, 9, u, sizeof u) == 2 && walk_rle_unpack(pd, sizeof pd, u, 10) == 10);
    CHECK(walk_rle_unpack(f, 2, u, sizeof u) == 0);

    /* the sliding test: both words at row y / 2, column x with its low bit cleared, on the list */
    static uint8_t gmap[WS_GMAP_BYTES], list[WS_LIST_BYTES];
    memset(gmap, 0x55, sizeof gmap);
    memset(list, 0xFF, sizeof list);
    list[20] = 7;
    list[21] = 0;
    unsigned at = (10u << 8) | 10u;
    gmap[at] = 7;
    gmap[at + 1] = 0;
    CHECK(!walk_slide_ok(gmap, list, 10, 20));   /* the second word is not listed */
    gmap[at + 2] = 7;
    gmap[at + 3] = 0;
    CHECK(walk_slide_ok(gmap, list, 10, 20) && walk_slide_ok(gmap, list, 11, 21));
    CHECK(!walk_slide_ok(gmap, list, 10, 22) && !walk_slide_ok(gmap, list, 12, 20) && !walk_slide_ok(gmap, list, 9, 20));
    /* the foot test looks a row lower (y / 2 + 1): the same cells are not under the foot of (10, 20) */
    CHECK(!walk_foot_ok(gmap, list, 10, 20));
}

static void test_use(void)
{
    printf("F7: the record an item or coins are used on, and the name's blink\n");
    static uint8_t tab_a[1750], party[WS_TAB_B];
    uint8_t *rec = tab_a + 1000, cell[3];
    int scene;
    party[0] = 83, party[1] = 69, party[2] = 1;   /* facing down from (82, 34): the second cell is (84, 34) */
    rec[15 * 5 + 0] = 84, rec[15 * 5 + 1] = 36, rec[15 * 5 + 2] = 6;   /* byte 3 = 0: an item's place, two steps on */
    CHECK(walk_use_find(tab_a, party, 0, &scene, cell) == 75 && !scene && cell[0] == 82 && cell[1] == 36 && cell[2] == 84);
    /* coins: an item's place is passed over; after four steps the last looked at is the second cell's kind 6 (84, 37): none */
    CHECK(walk_use_find(tab_a, party, 1, &scene, cell) == 0x2EE && cell[0] == 82 && cell[1] == 38 && cell[2] == 84);
    rec[15 * 7 + 0] = 84, rec[15 * 7 + 1] = 37, rec[15 * 7 + 2] = 6;
    /* ... unless an item's place stands there: it is returned though not taken (its byte 4 then decides) */
    CHECK(walk_use_find(tab_a, party, 1, &scene, cell) == 105);
    rec[15 * 9 + 0] = 82, rec[15 * 9 + 1] = 35, rec[15 * 9 + 2] = 0x14;   /* a sell counter one step on: before the place */
    CHECK(walk_use_find(tab_a, party, 0, &scene, cell) == 135 && scene && cell[1] == 35);
    CHECK(walk_use_find(tab_a, party, 1, &scene, cell) == 105 && !scene);   /* coins look for no sell counter */
    party[2] = 2;   /* facing left: the second cell is below the first, both move two to the left */
    rec[15 * 11 + 0] = 80, rec[15 * 11 + 1] = 35, rec[15 * 11 + 2] = 6, rec[15 * 11 + 3] = 1;   /* a price */
    CHECK(walk_use_find(tab_a, party, 1, &scene, cell) == 165 && cell[0] == 80 && cell[1] == 34 && cell[2] == 80);
    party[0] = 1, party[1] = 0, party[2] = 0;   /* facing up from (0, 0): the bytes wrap */
    CHECK(walk_use_find(tab_a, party, 0, &scene, cell) == 0x2EE && cell[0] == 0 && cell[1] == 252 && cell[2] == 2);

    CHECK(walk_use_blink(0) == 398);        /* 1200 = 2 x 401 + 398: the name shown */
    CHECK(walk_use_blink(0xF) == 12);       /* 1215 - 3 x 401: what was there */
    CHECK(walk_use_blink(0xEA60) == 397);   /* above 0x190: 0 at once, then 1199 */
    CHECK(walk_use_blink(399) == 396);      /* 400, 0, then 1198 */

    /* the sell counter's text (0x18723 .. 0x18877): 20 -> "   20" and 10 -> "   10"; 6300 -> " 6300", 3150 -> " 3150"; 1 -> 0 */
    uint8_t m[22];
    CHECK(walk_sell_text(m, 20) == 0x13 && !memcmp(m, "\xAD\xEC\xBB\xF9:   20 \xA5\x62\xBB\xF9:   10", 21) && m[0x15] == 0);
    CHECK(walk_sell_text(m, 6300) == 0x11 && !memcmp(m + 5, " 6300 ", 6) && !memcmp(m + 0x10, " 3150", 5));
    CHECK(walk_sell_text(m, 1) == 0x14 && !memcmp(m + 5, "    1", 5) && !memcmp(m + 0x10, "    0", 5));
    CHECK(walk_sell_text(m, 0) == 0x14 && !memcmp(m + 5, "    0", 5));   /* the last digit stays, even a 0 */
    CHECK(walk_sell_text(m, 65535) == 0x10 && !memcmp(m + 5, "65535", 5) && !memcmp(m + 0x10, "32767", 5));
}

static void test_map(void)
{
    printf("F8: the map item looked for, and a map row stretched\n");
    static uint8_t party[WS_TAB_B];
    unsigned v96 = 0;
    CHECK(!walk_map_has(party, &v96) && v96 == 0xC8);   /* none: the last record's place 0xb4 + 0x14 */
    party[0x13] = 0x13;                                  /* byte 0x13 is before the item places */
    CHECK(!walk_map_has(party, &v96));
    party[0x3C] = 0x13;                                  /* the second member's first item place */
    CHECK(walk_map_has(party, &v96) && v96 == 0x50);
    party[0x27] = 0x13;                                  /* the hero's last: found first */
    CHECK(walk_map_has(party, &v96) && v96 == 0x28);
    memset(party, 0, sizeof party);
    party[0xC7] = 0x13;                                  /* the fifth record's last */
    CHECK(walk_map_has(party, &v96) && v96 == 0xC8);

    uint8_t src[128], out[232];
    for (int i = 0; i < 128; i++)
        src[i] = (uint8_t)i;
    /* 128 wide: step 0xffff from 1, so the first pixel moves on at once and the second is drawn twice; the last cell is not drawn */
    walk_map_row(src, 128, out);
    CHECK(out[0] == 0xB0 && out[1] == 0 && out[2] == 1 && out[3] == 1 && out[4] == 2 && out[128] == 126 && out[129] == 0xB0);
    /* 228 wide (the last row): step 0xffff * 128 / 228 = 36791; pixel j is cell (1 + 36791 j) / 65536 */
    walk_map_row(src, 228, out);
    CHECK(out[0] == 0xB0 && out[2] == 0 && out[3] == 1 && out[101] == 56 && out[228] == 127 && out[229] == 0xB0);
}

static void test_party_order(void)
{
    printf("F9: the members listed, and two records swapped\n");
    static uint8_t party[WS_TAB_B];
    for (int i = 0; i < 5; i++)
        party[i * 0x28 + 3] = (uint8_t)(0xC8 + i);
    party[3] = 0;
    CHECK(walk_order_members(party) == 1);
    party[0x28 + 3] = 4;                     /* 4 is the highest number that counts */
    party[0x50 + 3] = 5;
    party[0x78 + 3] = 2;                     /* after one that does not count: not listed */
    CHECK(walk_order_members(party) == 2);
    for (int i = 0; i < 5; i++)
        party[i * 0x28 + 3] = (uint8_t)i;
    CHECK(walk_order_members(party) == 5);   /* at most five */
    for (int i = 0; i < 0x50; i++)
        party[i] = (uint8_t)i;
    walk_order_swap(party, 0);
    CHECK(party[0] == 0 && party[2] == 2 && party[0x28] == 0x28 && party[0x2A] == 0x2A);   /* place and facing stay */
    CHECK(party[3] == 0x2B && party[0x27] == 0x4F && party[0x2B] == 3 && party[0x4F] == 0x27);
}

static void test_data_number(void)
{
    printf("F10: numbers in 4 x 7 digits\n");
    static uint8_t digits[10 * 28], pix[320 * 8];
    for (int d = 0; d < 10; d++)
        for (int k = 0; k < 28; k++)
            digits[d * 28 + k] = k == 0 ? (uint8_t)(0x40 + d) : 0;   /* each digit: its top-left pixel only */
    memset(pix, 0xEE, sizeof pix);
    walk_data_number(pix, 0, 10005, digits);   /* five digits, zeros inside drawn */
    CHECK(pix[0] == 0x41 && pix[4] == 0x40 && pix[8] == 0x40 && pix[12] == 0x40 && pix[16] == 0x45 && pix[20] == 0xEE);
    CHECK(pix[1] == 0xEE);                     /* colour 0 is clear */
    memset(pix, 0xEE, sizeof pix);
    walk_data_number(pix, 0, 907, digits);     /* leading zeros left out: starts at once */
    CHECK(pix[0] == 0x49 && pix[4] == 0x40 && pix[8] == 0x47 && pix[12] == 0xEE);
    memset(pix, 0xEE, sizeof pix);
    walk_data_number(pix, 0, 0, digits);       /* the units always */
    CHECK(pix[0] == 0x40 && pix[4] == 0xEE);
    memset(pix, 0xEE, sizeof pix);
    walk_data_number(pix, 0, 60000, digits);
    CHECK(pix[0] == 0x46 && pix[16] == 0x40 && pix[20] == 0xEE);
}

static void test_look(void)
{
    printf("F2: the record looked at and its message\n");
    static uint8_t tab_a[1750];
    uint8_t *rec = tab_a + 1000;
    rec[15 * 2 + 0] = 38, rec[15 * 2 + 1] = 55, rec[15 * 2 + 2] = 3;   /* the same cell, another kind: passed over */
    rec[15 * 4 + 0] = 38, rec[15 * 4 + 1] = 55, rec[15 * 4 + 2] = 9;
    rec[15 * 6 + 0] = 38, rec[15 * 6 + 1] = 55, rec[15 * 6 + 2] = 9;   /* a second one there: the first is taken */
    rec[15 * 49 + 0] = 200, rec[15 * 49 + 1] = 7, rec[15 * 49 + 2] = 9;
    rec[15 * 8 + 0] = 39, rec[15 * 8 + 1] = 55, rec[15 * 8 + 2] = 9;   /* x is compared as it is, odd or not */
    CHECK(walk_look_record(tab_a, 38, 55) == 60);
    CHECK(walk_look_record(tab_a, 200, 7) == 0x2DF);
    CHECK(walk_look_record(tab_a, 39, 55) == 120);
    CHECK(walk_look_record(tab_a, 38, 56) == 0x2EE);
    CHECK(walk_look_record(tab_a, 200, 8) == 0x2EE);

    static uint8_t party[WS_TAB_B];
    uint8_t r[15] = {38, 55, 9, 0, 0, 10, 0, 6, 0, 20, 0, 49, 7, 30, 0};
    party[200] = 1;   /* flag 0 bit 0: the first entry's */
    CHECK(walk_look_entry(party, r) == 7);    /* the second entry's flag (6, bit 0) not set: the first message */
    party[206] = 1;
    CHECK(walk_look_entry(party, r) == 11);   /* the third's (49, bit 7) not set: the second */
    party[249] = 0x80;
    CHECK(walk_look_entry(party, r) == 15);   /* all three set: the third */
    party[249] = 0x7F;
    CHECK(walk_look_entry(party, r) == 11);
    r[13] = r[14] = 0;
    party[249] = 0;
    CHECK(walk_look_entry(party, r) == 11);   /* the third has no message: the second, its flag not looked at */
    r[9] = 0;
    CHECK(walk_look_entry(party, r) == 7);    /* the second has none: the first */
    party[200] = 0;
    CHECK(walk_look_entry(party, r) == 3);    /* the first's flag not set: the word before it, bytes 1 and 2 */
    r[4] = 31;
    CHECK(walk_look_entry(party, r) == 7);    /* bit 31: (31 + 1) & 31 = 0, always set */

    /* F3's search for kind 2 and where a taken item goes */
    rec[15 * 3 + 0] = 38, rec[15 * 3 + 1] = 55, rec[15 * 3 + 2] = 2;
    CHECK(walk_record_of_kind(tab_a, 38, 55, 2) == 45 && walk_record_of_kind(tab_a, 38, 55, 9) == 60);
    static uint8_t pt[WS_TAB_B];
    for (int m = 0; m < 5; m++) {
        pt[m * 40 + 3] = (uint8_t)m;
        pt[m * 40 + 8] = 10;
        memset(pt + m * 40 + 0x14, 7, 20);
    }
    CHECK(walk_take_slot(pt) == 0);   /* every place full */
    pt[2 * 40 + 0x14 + 19] = 0;
    CHECK(walk_take_slot(pt) == 2 * 40 + 0x27);   /* member 2's last place */
    pt[1 * 40 + 0x14 + 5] = 0;
    CHECK(walk_take_slot(pt) == 1 * 40 + 0x19);   /* member 1 before him */
    pt[1 * 40 + 8] = 0;
    CHECK(walk_take_slot(pt) == 2 * 40 + 0x27);   /* member 1 has no HP: passed over */
    pt[2 * 40 + 3] = 5;
    CHECK(walk_take_slot(pt) == 0);   /* number 5: passed over too, and the search goes on to the end */
    pt[4 * 40 + 0x14] = 0;
    CHECK(walk_take_slot(pt) == 4 * 40 + 0x14);
}

static void test_missing_data(void)
{
    printf("missing data\n");
    const char *saved = assets_dir();
    assets_set_dir("no_such_directory_for_walk");
    CHECK(walk_scene_load() == NULL);
    CHECK(walk_create() == NULL);
    assets_set_dir(saved);
}

/* A door's picture turns the party (0x2489a); a kind 4 record (0x17c41 .. 0x17d5a). Values worked by hand from the code. */
static void test_door_records(void)
{
    printf("doors: the party turning; kind 4 records\n");
    static uint8_t p[WS_TAB_B];
    p[2] = 0, p[0x2A] = 1, p[0x52] = 2, p[0x7A] = 3, p[0xA2] = 7;
    walk_spin(p);
    CHECK(p[2] == 3 && p[0x2A] == 2 && p[0x52] == 0 && p[0x7A] == 1 && p[0xA2] == 7);
    walk_spin(p);
    CHECK(p[2] == 1 && p[0x2A] == 0 && p[0x52] == 3 && p[0x7A] == 2);
    /* member 2 in the second record, with HP: byte 0xc raised to 9, flag 0x8c bit 5 set */
    memset(p, 0, sizeof p);
    p[3] = 0, p[8] = 1;
    p[0x2B] = 2, p[0x30] = 0x58, p[0x31] = 2, p[0x34] = 4;
    const uint8_t r[15] = {0, 0, 4, 2, 9, 0x8C, 5};
    walk_record_kind4(p, r);
    CHECK(p[0x34] == 9 && p[WS_FLAGS_AT + 0x8C] == 0x20 && p[0x0C] == 0);
    /* byte 0xc only raised, never lowered; the flag still set */
    const uint8_t r2[15] = {0, 0, 4, 2, 3, 0x10, 0};
    walk_record_kind4(p, r2);
    CHECK(p[0x34] == 9 && p[WS_FLAGS_AT + 0x10] == 1);
    /* no HP (word 8 is 0): nothing; a number no record has: nothing */
    p[0x30] = p[0x31] = 0;
    const uint8_t r3[15] = {0, 0, 4, 2, 20, 0x11, 1};
    walk_record_kind4(p, r3);
    CHECK(p[0x34] == 9 && p[WS_FLAGS_AT + 0x11] == 0);
    const uint8_t r4[15] = {0, 0, 4, 6, 20, 0x12, 1};
    walk_record_kind4(p, r4);
    CHECK(p[WS_FLAGS_AT + 0x12] == 0);
}

int main(int argc, char **argv)
{
    assets_set_dir(argc > 1 ? argv[1] : "original_bin");
    setvbuf(stdout, NULL, _IONBF, 0);
    test_flags();
    test_rain();
    test_positions();
    test_sprite_offsets();
    test_order();
    test_ground_objects();
    test_entities();
    test_label();
    test_scene0();
    test_first_screen();
    test_rand_and_stride();
    test_animation_tables();
    test_palette_pieces();
    test_specks();
    test_villager();
    test_decide();
    test_motion_whole();
    test_pass_wiring();
    test_resume_stride();
    test_boss_after();
    test_doubling();
    test_zoom_scale();
    test_look();
    test_use();
    test_save_pack();
    test_door_records();
    test_map();
    test_party_order();
    test_data_number();
    test_mouse_key();
    test_foot();
    test_trail_and_records();
    test_walking();
    test_roof();
    test_shake();
    test_missing_data();
    printf("%s: %d failure(s), %d skipped\n", failures ? "FAILED" : "ok", failures, skipped);
    return failures ? 1 : 0;
}
