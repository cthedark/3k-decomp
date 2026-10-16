/*
 * Tests for the credits module (src/credits.c).
 *
 * The primitives are checked on small inputs whose results are worked out by hand from the original's code
 * (CAST.SL 0x10748, 0x114e2, 0x10b6f). The whole wipe and loop are checked against the original's own screens in
 * tools/check_credits.sh; here only what needs no stored reference: the wipe's end state, the pass count at which the
 * loop ends, the keys, and reproducibility. Tests that need the game's data are skipped when it is missing.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"
#include "credits.h"
#include "screen.h"

static int failures, skipped;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
            failures++;                                                          \
        }                                                                        \
    } while (0)

static void test_expand_bits(void)
{
    printf("1-bit expansion\n");
    const uint8_t src[2] = {0xa5, 0x01};   /* 1010 0101, 0000 0001 */
    uint8_t dst[17];
    memset(dst, 0xee, sizeof dst);
    credits_expand_bits(dst, src, 2, 0x10);
    const uint8_t want[16] = {0x10, 0, 0x10, 0, 0, 0x10, 0, 0x10, 0, 0, 0, 0, 0, 0, 0, 0x10};
    CHECK(memcmp(dst, want, 16) == 0);   /* most significant bit first, a clear bit is 0 */
    CHECK(dst[16] == 0xee);              /* nothing written past 8 * nbytes pixels */
}

static void test_resize_line(void)
{
    printf("line resize\n");
    /* 4 pixels to 8: step = 0xffff * 4 / 8 = 0x7fff. The accumulator starts at 1 and the source index moves on
     * when it carries past 0xffff: 1, 0x8000, 0xffff, 0x17ffe (carry)... */
    const uint8_t src[5] = {10, 20, 30, 40, 99};
    uint8_t dst[8];
    credits_resize_line(dst, src, 4, 8);
    const uint8_t want[8] = {10, 10, 10, 20, 20, 30, 30, 40};   /* worked out by hand, source index 0 0 0 1 1 2 2 3 */
    CHECK(memcmp(dst, want, 8) == 0);
    CHECK(dst[0] == 10 && dst[7] == 40);
    /* the same size is not a copy: the accumulator starts at 1, so the step 0xffff carries at once and the
     * line comes out as it does in the original (a pixel repeated, the last one lost) */
    const uint8_t id[6] = {1, 2, 3, 4, 5, 6};
    uint8_t out[6];
    credits_resize_line(out, id, 6, 6);
    const uint8_t want_id[6] = {1, 2, 2, 3, 4, 5};   /* step 0xffff: carries on all but the third; the 6 is never read */
    CHECK(memcmp(out, want_id, 6) == 0);
    /* a line stretched to 320 reads only 120 source pixels plus at most one spare */
    uint8_t line[121], wide[320];
    for (int i = 0; i < 120; i++)
        line[i] = (uint8_t)(i + 1);
    line[120] = 0xee;
    credits_resize_line(wide, line, 120, 320);
    CHECK(wide[0] == 1 && wide[319] == 120);
    for (int i = 1; i < 320; i++)
        CHECK(wide[i] >= wide[i - 1]);   /* never goes back */
}

static void test_scroll_up(void)
{
    printf("scroll up\n");
    uint8_t spr[4 + 3 * 4] = {4, 0, 3, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3};
    credits_scroll_up(spr);
    const uint8_t want[4 + 3 * 4] = {4, 0, 3, 0, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3};
    CHECK(memcmp(spr, want, sizeof want) == 0);   /* rows move up, the last row stays */
    uint8_t one[4 + 4] = {4, 0, 1, 0, 7, 7, 7, 7};
    credits_scroll_up(one);
    CHECK(one[4] == 7);   /* a one-row sprite is unchanged */
}

static int have_data(void)
{
    if (assets_exists("cast1.lcz") && assets_exists("cast2.lcz"))
        return 1;
    printf("  (skipped: no game data)\n");
    skipped++;
    return 0;
}

static void fill_screen(screen_t *s, uint8_t v)
{
    memset(s, 0, sizeof *s);
    memset(s->pix, v, sizeof s->pix);
    for (int i = 0; i < 768; i++)
        ((uint8_t *)s->pal)[i] = (uint8_t)(i % 64);
}

static void test_wipe(void)
{
    printf("wipe\n");
    if (!have_data())
        return;
    static screen_t s, t;
    fill_screen(&s, 0x55);
    t = s;
    credits_t *c = credits_create(&s);
    CHECK(c != NULL);
    if (!c)
        return;
    int steps = 0, more = 1;
    while (more) {
        more = credits_wipe_step(c, &s);
        steps++;
    }
    CHECK(steps == CREDITS_WIPE_STEPS);
    CHECK(credits_wipe_step(c, &s) == 0);   /* past the end: nothing more */
    /* the upper half is colour 0 and none of the old picture is left */
    int upper_clear = 1, old_left = 0;
    for (int i = 0; i < SCREEN_PIXELS / 2; i++)
        if (s.pix[i] != 0)
            upper_clear = 0;
    for (int i = 0; i < SCREEN_PIXELS; i++)
        if (s.pix[i] == 0x55)
            old_left = 1;
    CHECK(upper_clear);
    CHECK(!old_left);
    CHECK(memcmp(s.pal, t.pal, sizeof s.pal) == 0);   /* the palette is never touched */
    credits_free(c);
}

static void test_loop(void)
{
    printf("loop\n");
    if (!have_data())
        return;
    static screen_t s;
    fill_screen(&s, 0);
    credits_t *c = credits_create(&s);
    if (!c) {
        CHECK(c != NULL);
        return;
    }
    while (credits_wipe_step(c, &s))
        ;
    /* with no key the loop runs to the end: 1,790 passes draw, the 1,791st finds the scroll finished */
    unsigned n = 0;
    credits_status st;
    while ((st = credits_pass(c, &s, 0x9c)) == CREDITS_RUNNING)
        n++;
    CHECK(st == CREDITS_END_TIME);
    CHECK(n == 1790);
    CHECK(credits_passes(c) == 1790);
    credits_free(c);

    /* Space or Enter as the last scancode ends the loop after the pass has drawn; Esc, a break code and
     * anything else do not */
    static const struct { unsigned code; int ends; } keys[] = {
        {0x39, 1}, {0x1c, 1}, {0x01, 0}, {0xb9, 0}, {0x9c, 0}, {0x00, 0}, {0x3a, 0}, {0xc7, 0}
    };
    for (unsigned k = 0; k < sizeof keys / sizeof keys[0]; k++) {
        fill_screen(&s, 0);
        c = credits_create(&s);
        if (!c) {
            CHECK(c != NULL);
            return;
        }
        while (credits_wipe_step(c, &s))
            ;
        CHECK(credits_pass(c, &s, 0x9c) == CREDITS_RUNNING);
        credits_status r = credits_pass(c, &s, keys[k].code);
        CHECK((r == CREDITS_END_KEY) == keys[k].ends);
        CHECK(credits_passes(c) == 2);   /* the pass that saw the key had drawn */
        credits_free(c);
    }
}

static void test_reproducible(void)
{
    printf("reproducibility\n");
    if (!have_data())
        return;
    static screen_t a, b;
    for (int run = 0; run < 2; run++) {
        screen_t *s = run ? &b : &a;
        fill_screen(s, 0x33);
        credits_t *c = credits_create(s);
        if (!c) {
            CHECK(c != NULL);
            return;
        }
        while (credits_wipe_step(c, s))
            ;
        for (int i = 0; i < 300; i++)
            credits_pass(c, s, 0);
        credits_free(c);
    }
    CHECK(memcmp(a.pix, b.pix, sizeof a.pix) == 0);
    /* after 300 passes there is text in the window and something on the floor */
    int win = 0;
    for (int y = 0; y < 100; y++)
        for (int x = 100; x < 220; x++)
            if (a.pix[y * SCREEN_W + x] >= 0xd0 && a.pix[y * SCREEN_W + x] <= 0xdf)
                win = 1;
    CHECK(win);
}

int main(int argc, char **argv)
{
    assets_set_dir(argc > 1 ? argv[1] : "original_bin");
    setvbuf(stdout, NULL, _IONBF, 0);
    test_expand_bits();
    test_resize_line();
    test_scroll_up();
    test_wipe();
    test_loop();
    test_reproducible();
    printf("%s: %d failure(s), %d skipped\n", failures ? "FAILED" : "ok", failures, skipped);
    return failures ? 1 : 0;
}
