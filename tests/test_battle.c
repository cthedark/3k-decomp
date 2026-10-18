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

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    test_stretch();
    test_shrink();
    test_sway();
    test_number();
    printf("%s: %d failure(s)\n", failures ? "FAILED" : "ok", failures);
    return failures ? 1 : 0;
}
