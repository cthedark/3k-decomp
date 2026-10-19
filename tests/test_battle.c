/*
 * Tests for the battle's pieces (src/battle.c). Expected values are worked out by hand from the original's routines (the addresses
 * are in src/battle.h). The whole battle is compared with the original pass by pass in tools/check_battle.sh.
 */
#include <stdio.h>
#include <string.h>

#include "battle.h"

static int failures;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
            failures++;                                                          \
        }                                                                        \
    } while (0)

static void test_stretch(void)
{
    /* equal widths: step 0xffff, the first add (1 + 0xffff) carries, the second does not: a b b, not a b c */
    const uint8_t abc[3] = {'a', 'b', 'c'};
    uint8_t out[8];
    battle_stretch_row(abc, 3, out, 3);
    CHECK(memcmp(out, "abb", 3) == 0);
    /* 2 to 4: step 0x7fff; sums 1+0x7fff = 0x8000, 0xffff, 0x17ffe (carry, 0x7ffe), 0xfffd: a a a b */
    battle_stretch_row(abc, 2, out, 4);
    CHECK(memcmp(out, "aaab", 4) == 0);
}

static void test_shrink(void)
{
    /* 60 x 60 to 30 x 30: whole steps of 2 across and 2 rows down, no fraction: pixel (2c, 2r) */
    static uint8_t src[60 * 60], dst[30 * 30];
    for (int i = 0; i < 60 * 60; i++)
        src[i] = (uint8_t)(i * 7 + i / 60);
    battle_shrink(src, 60, 60, dst, 30, 30);
    int ok = 1;
    for (int r = 0; r < 30; r++)
        for (int c = 0; c < 30; c++)
            if (dst[r * 30 + c] != src[(2 * r) * 60 + 2 * c])
                ok = 0;
    CHECK(ok);
    /* 5 to 2 across, 1 row: whole step 2, fraction 1 * (0xffff / 2) = 0x7fff per pixel: 0, 2 (the fraction carries only later) */
    const uint8_t row[5] = {10, 11, 12, 13, 14};
    uint8_t two[2];
    battle_shrink(row, 5, 1, two, 2, 1);
    CHECK(two[0] == 10 && two[1] == 12);
}

static void test_sway(void)
{
    /* the offsets of rows 0.. for count 1, dir 1: 1 2 3 2 1 0 1 2 3 2 ... (up to 3, down to 0) */
    static uint8_t back[BATTLE_BACK_W * BATTLE_FIELD_H], field[BATTLE_FIELD_W * BATTLE_FIELD_H];
    for (int r = 0; r < BATTLE_FIELD_H; r++)
        for (int c = 0; c < BATTLE_BACK_W; c++)
            back[r * BATTLE_BACK_W + c] = (uint8_t)c;
    battle_sway(back, field, 1, 1);
    static const int want[10] = {1, 2, 3, 2, 1, 0, 1, 2, 3, 2};
    for (int r = 0; r < 10; r++)
        CHECK(field[r * BATTLE_FIELD_W] == want[r]);
    battle_sway(back, field, 3, 0);   /* going down from 3: 3 2 1 0 1 */
    CHECK(field[0] == 3 && field[BATTLE_FIELD_W] == 2 && field[3 * BATTLE_FIELD_W] == 0 && field[4 * BATTLE_FIELD_W] == 1);
}

static void test_number(void)
{
    uint8_t font[150], out[100];
    for (int d = 0; d < 10; d++)   /* digit d: its top-left pixel 0x10, the rest 0 except a marker in the middle (colour 0x20 + d) */
        for (int i = 0; i < 15; i++)
            font[d * 15 + i] = i == 0 ? 0x10 : i == 7 ? (uint8_t)(0x20 + d) : 0;
    battle_number(600, font, out, 0xF0);
    /* 00600: the two leading zeros left out; 6 at x = 8, 0 at 12 and 16; ink 0x10 becomes 0xf0 */
    CHECK(out[0] == 0 && out[4] == 0);
    CHECK(out[8] == 0xF0 && out[20 * 2 + 8 + 1] == 0x26);
    CHECK(out[12] == 0xF0 && out[20 * 2 + 12 + 1] == 0x20 && out[20 * 2 + 16 + 1] == 0x20);
    battle_number(0, font, out, 0xB0);   /* only the last digit */
    CHECK(out[12] == 0 && out[16] == 0xB0 && out[20 * 2 + 17] == 0x20);
    battle_number(10203, font, out, 0xF0);   /* every digit, an inner 0 included */
    CHECK(out[20 * 2 + 1] == 0x21 && out[20 * 2 + 5] == 0x20 && out[20 * 2 + 9] == 0x22 && out[20 * 2 + 17] == 0x23);
}

static void test_blows(void)
{
    /* party blow: 2400 / 1200 = 2, plus (10 / 5) * 4 = 8 -> 10; word 0x10 = 0 gives t = 17, so no reduction */
    CHECK(battle_blow(2400, 10, 0, 0) == 10);
    CHECK(battle_blow(2399, 10, 0, 0) == 9);    /* the divisor is exactly 1200 */
    CHECK(battle_blow(2400, 10, 0, 1) == 6);    /* the last two members get half the stat part */
    CHECK(battle_blow(0, 0, 0, 0) == 1);        /* at least 1 */
    /* word 0x10 = 81: t = (1919 >> 7) + 2 = 16, so 10 - 10 / 16 = 10; 2000: t = 2, 10 - 5 = 5 */
    CHECK(battle_blow(2400, 10, 81, 0) == 10);
    CHECK(battle_blow(2400, 10, 2000, 0) == 5);
    CHECK(battle_blow(36000, 0, 1500, 0) == 30 - 30 / 5);   /* t = (500 >> 7) + 2 = 5 */
    /* monster blow: halved from the back row, less defence, not below 0 */
    CHECK(battle_monster_blow(30, 0, 0) == 30);
    CHECK(battle_monster_blow(30, 0, 7) == 23);
    CHECK(battle_monster_blow(30, 1, 7) == 8);
    CHECK(battle_monster_blow(30, 0, 40) == 0);
    /* the slot under a column */
    CHECK(battle_hit_slot(0) == 2 && battle_hit_slot(0x1E) == 2 && battle_hit_slot(0x1F) == 3);
    CHECK(battle_hit_slot(0x60) == 3 && battle_hit_slot(0x61) == 4 && battle_hit_slot(0x70) == 4 && battle_hit_slot(0xBF) == 4);
}

static void put16(uint8_t *m, int o, unsigned v)
{
    m[o] = (uint8_t)v;
    m[o + 1] = (uint8_t)(v >> 8);
}

static unsigned get16(const uint8_t *m, int o)
{
    return m[o] | (unsigned)m[o + 1] << 8;
}

/* what follows a win: worked out from 0x130e2 .. 0x13246 and 0x1fb02 .. 0x1fb3b */
static void test_after_win(void)
{
    uint8_t m[0x28] = {0};
    put16(m, 4, 199), put16(m, 6, 200), put16(m, 8, 300), put16(m, 0xA, 600);
    CHECK(battle_level_up(m) == 0 && get16(m, 4) == 199 && get16(m, 8) == 300);   /* one short */
    put16(m, 4, 230);
    CHECK(battle_level_up(m) == 1);
    CHECK(get16(m, 4) == 200 && get16(m, 6) == 700 && get16(m, 0xA) == 650 && get16(m, 8) == 650);   /* the rest is lost; 600 / 12 */
    put16(m, 4, 59900), put16(m, 6, 59800), put16(m, 0xA, 59000);
    CHECK(battle_level_up(m) == 1 && get16(m, 6) == 60000 && get16(m, 0xA) == 60000);   /* both capped */
    put16(m, 4, 60000), put16(m, 6, 100);
    CHECK(battle_level_up(m) == 0 && get16(m, 4) == 60000);   /* 60,000 or more: never again */
    put16(m, 4, 65535), put16(m, 6, 65400), put16(m, 0xA, 11);
    CHECK(battle_level_up(m) == 0);
    put16(m, 4, 0), put16(m, 6, 0), put16(m, 0xA, 11);
    CHECK(battle_level_up(m) == 1 && get16(m, 6) == 500 && get16(m, 0xA) == 11 && get16(m, 8) == 11);   /* 11 / 12 = 0 */
    put16(m, 4, 0xFF00), put16(m, 6, 0xFF00);
    CHECK(battle_level_up(m) == 0);
    put16(m, 4, 59999), put16(m, 6, 59999), put16(m, 0xA, 0xFFF0);
    CHECK(battle_level_up(m) == 1 && get16(m, 0xA) == (0xFFF0 + 0xFFF0 / 12) - 0x10000);   /* 16-bit, wraps below the cap */
    CHECK(battle_add_coins(0, 5, 1) == 5 && battle_add_coins(100, 0, 9) == 100);
    CHECK(battle_add_coins(49999, 1, 1) == 50000 && battle_add_coins(49999, 2, 1) == 50000 && battle_add_coins(60000, 0, 0) == 50000);
    CHECK(battle_add_coins(65535, 1, 1) == 0);   /* the sum is 16-bit */
    /* the message font: Big5 lead bytes 0xa4 .. 0xc6, then 0xc9 on after 0x1519 cells (0x2464a) */
    CHECK(battle_glyph_offset(0xA4, 0x40) == 0x3B20 && battle_glyph_offset(0xA4, 0x7E) == 0x3B20 + 0x1E * 0x3E);
    CHECK(battle_glyph_offset(0xA4, 0xA1) == 0x3B20 + 0x1E * 0x3F && battle_glyph_offset(0xA5, 0x40) == 0x3B20 + 0x1E * 157);
    CHECK(battle_glyph_offset(0xC9, 0x40) == 0x3B20 + 0x1E * 0x1519u && battle_glyph_offset(0xC7, 0x40) == (size_t)-1);
    CHECK(battle_glyph_offset(0xAE, 0xA5) == 0x3B20 + 0x1E * (10 * 157 + 0x43));   /* 恭 */
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    test_stretch();
    test_shrink();
    test_sway();
    test_number();
    test_blows();
    test_after_win();
    printf("%s: %d failure(s)\n", failures ? "FAILED" : "ok", failures);
    return failures ? 1 : 0;
}
