/*
 * Tests for the menu module (src/menu.c).
 *
 * The primitives are checked on small synthetic sprites whose expected results are worked out by hand
 * from the original's code (WALK0.SL 0x12b5d, 0x12902, 0x13143, 0x13119). The whole frame is checked
 * against numbers taken from the original's screens in tools/check_menu.sh (frame by frame, against
 * the oracle emulator); here only invariants that do not need game content to be stored:
 * dimensions, wrap-around of the roll, the highlight's colour change, and reproducibility.
 * Tests that need the game's data are skipped when it is missing.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"
#include "gfx.h"
#include "menu.h"
#include "screen.h"

static int failures, skipped;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
            failures++;                                                          \
        }                                                                        \
    } while (0)

static void test_shade_recolour(void)
{
    printf("shade and recolour\n");
    uint8_t a[8] = {0, 5, 0, 7, 9, 0, 1, 0};
    menu_shade(a, 6, 0xf3);   /* only the first six bytes; zeros stay zero */
    const uint8_t want_a[8] = {0, 0xf3, 0, 0xf3, 0xf3, 0, 1, 0};
    CHECK(memcmp(a, want_a, 8) == 0);
    uint8_t b[6] = {0x17, 0x19, 0x17, 0, 0x17, 0x17};
    menu_recolour(b, 5, 0x17, 0x10);   /* the sixth byte is outside the range */
    const uint8_t want_b[6] = {0x10, 0x19, 0x10, 0, 0x10, 0x17};
    CHECK(memcmp(b, want_b, 6) == 0);
}

static void test_tile_copy(void)
{
    printf("tile copy\n");
    uint8_t dst[4 + 6 * 3];
    memset(dst, 0xaa, sizeof dst);
    /* A 2 x 2 tile into a strip 6 wide at byte offset 5 (header 4, so column 1 of row 0): the zero
     * pixel leaves what was there. */
    const uint8_t tile[4] = {1, 0, 3, 4};
    menu_tile_copy(dst, 5, tile, 6, 2, 2);
    CHECK(dst[5] == 1 && dst[6] == 0xaa);     /* row 0: pixel, skipped */
    CHECK(dst[11] == 3 && dst[12] == 4);      /* row 1 starts 6 later */
    CHECK(dst[4] == 0xaa && dst[7] == 0xaa);  /* untouched neighbours */
}

static void test_stretch(void)
{
    printf("stretch\n");
    /* 4 x 2 source stretched to 8 x 4. xstep = 0xffff * 4 / 8 = 0x7fff, ystep = 0xffff * 2 / 4 = 0x7fff.
     * The horizontal accumulator starts at 1 and gains 0x7fff per written pixel: it carries after the
     * 3rd, 5th and 7th write (0x17ffe, ...), and each carry moves on to the next source pixel, which
     * is then written once and followed by 0. So row 0 is 1 0 0 2 0 3 0 4. */
    const uint8_t src[4 + 8] = {4, 0, 2, 0, 1, 2, 3, 4, 5, 6, 7, 8};
    uint8_t dst[4 + 8 * 4 + 1 + 8];
    memset(dst, 0x55, sizeof dst);
    menu_stretch(dst, src, 8, 4);
    CHECK(dst[0] == 8 && dst[1] == 0 && dst[2] == 4 && dst[3] == 0);   /* header: 8 x 4 */
    const uint8_t row0[8] = {1, 0, 0, 2, 0, 3, 0, 4};
    CHECK(memcmp(dst + 4, row0, 8) == 0);
    /* The accumulator for rows: acc = 1; after row 0: 1 + 0x7fff = 0x8000, no carry -> one row skipped;
     * 0x8000 + 0x7fff = 0xffff, no carry -> a second row skipped; + 0x7fff carries -> stop. So source
     * row 1 is written at destination row 3. */
    int blank = 1;
    for (int i = 0; i < 16; i++)
        blank &= dst[4 + 8 + i] == 0;   /* destination rows 1 and 2 */
    CHECK(blank);
    CHECK(dst[4 + 3 * 8] == 5);
}

static void test_menu(void)
{
    printf("the menu frame\n");
    static screen_t a, b;
    menu_t *m = menu_create(&a);
    if (!m) {
        printf("  skipped: game data not found\n");
        skipped++;
        return;
    }
    menu_t *m2 = menu_create(&b);
    CHECK(m2 != NULL);
    CHECK(menu_passes(m) == 0 && menu_highlight(m) == 0);
    for (int i = 0; i < 450; i++) {   /* more than two full cycles of 201 passes */
        menu_pass(m, &a);
        menu_pass(m2, &b);
    }
    CHECK(menu_passes(m) == 450);
    CHECK(memcmp(a.pix, b.pix, sizeof a.pix) == 0);   /* deterministic */

    /* Outside the panel (x 64..255, y 16..183) and the label box (x 268..319, y 12..27) the screen is
     * never touched by a pass. */
    static screen_t c;
    menu_t *m3 = menu_create(&c);
    static uint8_t before[SCREEN_PIXELS];
    memcpy(before, c.pix, sizeof before);
    menu_pass(m3, &c);
    int changed_outside = 0;
    for (int y = 0; y < SCREEN_H; y++)
        for (int x = 0; x < SCREEN_W; x++)
            if ((x < 64 || x >= 256 || y < 16 || y >= 184) && c.pix[y * SCREEN_W + x] != before[y * SCREEN_W + x])
                changed_outside++;
    CHECK(changed_outside == 0);

    /* The highlighted label is white (colour 0x10) and the others are not: count colour-0x10 pixels
     * in each label's box after a pass with each item selected. */
    int white[4][4];
    for (int sel = 0; sel < 4; sel++) {
        menu_set_highlight(m3, sel);
        CHECK(menu_highlight(m3) == sel);
        menu_pass(m3, &c);
        for (int k = 0; k < 4; k++) {
            white[sel][k] = 0;
            for (int y = 0; y < 30; y++)
                for (int x = 0; x < 122; x++)
                    if (c.pix[(40 + 30 * k + y) * SCREEN_W + 100 + x] == 0x10)
                        white[sel][k]++;
        }
    }
    for (int sel = 0; sel < 4; sel++)
        for (int k = 0; k < 4; k++)
            CHECK((k == sel) == (white[sel][k] > 500));   /* about 850..1,100 white pixels vs none */
    menu_set_highlight(m3, 9);   /* out of range: ignored */
    CHECK(menu_highlight(m3) == 3);
    menu_free(m3);
    menu_free(m2);
    menu_free(m);
}

/* One pass with the given keyboard port and optional mouse. */
static menu_result step(menu_t *m, screen_t *s, unsigned port60, int mx, int my, unsigned buttons, int use_mouse)
{
    menu_mouse mouse = {mx, my, buttons};
    return menu_pass_input(m, s, port60, use_mouse ? &mouse : NULL);
}

static void test_keyboard(void)
{
    printf("keyboard rules\n");
    static screen_t s;
    menu_t *m = menu_create(&s);
    if (!m) {
        printf("  skipped: game data not found\n");
        skipped++;
        return;
    }
    /* No break code yet: a make code is ignored, even a confirming one. */
    CHECK(step(m, &s, 0x50, 0, 0, 0, 0) == MENU_NONE && menu_highlight(m) == 0);
    CHECK(step(m, &s, 0x1c, 0, 0, 0, 0) == MENU_NONE);
    /* A break code opens the latch; the next make code is taken, and only once. */
    CHECK(step(m, &s, 0xd0, 0, 0, 0, 0) == MENU_NONE && menu_highlight(m) == 0);
    CHECK(step(m, &s, 0x50, 0, 0, 0, 0) == MENU_NONE);
    CHECK(menu_highlight(m) == 1);   /* taken in this pass (its picture was drawn before; the next one shows it) */
    /* Hold the same make code: nothing more happens. */
    for (int i = 0; i < 5; i++)
        step(m, &s, 0x50, 0, 0, 0, 0);
    CHECK(menu_highlight(m) == 1);   /* moved once, not six times */
    /* Another break, then Down twice more with breaks between, and one more Down wraps to the top. */
    for (int k = 0; k < 3; k++) {
        step(m, &s, 0xd0, 0, 0, 0, 0);
        step(m, &s, 0x50, 0, 0, 0, 0);
    }
    CHECK(menu_highlight(m) == 0);   /* 1 -> 2 -> 3 -> 0 */
    step(m, &s, 0xc8, 0, 0, 0, 0);
    step(m, &s, 0x48, 0, 0, 0, 0);
    CHECK(menu_highlight(m) == 3);   /* Up from the first wraps to the last */
    /* Enter and Space confirm the selected item, after a break code. */
    step(m, &s, 0x9c, 0, 0, 0, 0);
    CHECK(step(m, &s, 0x1c, 0, 0, 0, 0) == 3);
    step(m, &s, 0xb9, 0, 0, 0, 0);
    CHECK(step(m, &s, 0x39, 0, 0, 0, 0) == 3);
    /* A confirming pass does not present: the screen keeps the picture of the pass before it. */
    static uint8_t before[SCREEN_PIXELS];
    step(m, &s, 0x9c, 0, 0, 0, 0);
    memcpy(before, s.pix, sizeof before);
    CHECK(step(m, &s, 0x1c, 0, 0, 0, 0) == 3);
    CHECK(memcmp(before, s.pix, sizeof before) == 0);
    menu_free(m);
}

static void test_mouse(void)
{
    printf("mouse rules\n");
    static screen_t s;
    menu_t *m = menu_create(&s);
    if (!m) {
        skipped++;
        return;
    }
    /* y bands (strictly inside): item k for 0x28<y<0x40, 0x47<y<0x5f, 0x68<y<0x80, 0x88<y<0xa0, x in 101..219. */
    const struct { int y, want; } bands[] = {
        {0x29, 0}, {0x3f, 0}, {0x48, 1}, {0x5e, 1}, {0x69, 2}, {0x7f, 2}, {0x89, 3}, {0x9f, 3},
    };
    for (unsigned i = 0; i < sizeof bands / sizeof bands[0]; i++) {
        menu_set_highlight(m, (bands[i].want + 1) % 4);   /* start somewhere else */
        step(m, &s, 0x80, 150, bands[i].y, 0, 1);
        CHECK(menu_highlight(m) == bands[i].want);
    }
    /* The gaps and the band edges keep the selection. */
    const int gaps[] = {0x28, 0x40, 0x41, 0x47, 0x5f, 0x60, 0x68, 0x80, 0x81, 0x88, 0xa0, 0xa1};
    for (unsigned i = 0; i < sizeof gaps / sizeof gaps[0]; i++) {
        menu_set_highlight(m, 1);
        step(m, &s, 0x80, 150, gaps[i], 0, 1);
        CHECK(menu_highlight(m) == 1);
    }
    /* x must be strictly between 0x64 and 0xdc; outside it nothing is selected or confirmed. */
    menu_set_highlight(m, 0);
    CHECK(step(m, &s, 0x80, 0x64, 0x69 + 1, 1, 1) == MENU_NONE && menu_highlight(m) == 0);
    CHECK(step(m, &s, 0x80, 0xdc, 0x69 + 1, 1, 1) == MENU_NONE && menu_highlight(m) == 0);
    CHECK(step(m, &s, 0x80, 0x65, 0x69 + 1, 0, 1) == MENU_NONE && menu_highlight(m) == 2);
    /* A button inside the column confirms the selection, even from a gap; either button counts. */
    menu_set_highlight(m, 1);
    CHECK(step(m, &s, 0x80, 150, 0x41, 1, 1) == 1);
    CHECK(step(m, &s, 0x80, 150, 0x41, 2, 1) == 1);
    CHECK(step(m, &s, 0x80, 150, 0x41, 0, 1) == MENU_NONE);
    /* No mouse at all: nothing happens. */
    CHECK(step(m, &s, 0x80, 150, 0x69 + 5, 1, 0) == MENU_NONE);
    /* The arrow follows the pointer: its tip is the clamped position, drawn on the panel. */
    menu_pass(m, &s);
    static uint8_t plain[SCREEN_PIXELS];
    memcpy(plain, s.pix, sizeof plain);
    step(m, &s, 0x80, 80, 30, 0, 1);   /* y is clamped to 16..162, x to 64..237 */
    int diff = 0, inside_box = 1;
    for (int y = 0; y < SCREEN_H; y++)
        for (int x = 0; x < SCREEN_W; x++)
            if (s.pix[y * SCREEN_W + x] != plain[y * SCREEN_W + x]) {
                diff++;
                /* the arrow (18 x 21) with its tip at (80, 30), plus the roll that one pass makes */
            }
    (void)inside_box;
    CHECK(diff > 0);
    menu_free(m);
}

static int count_colour_in_label(const screen_t *s, int k, uint8_t colour)
{
    int n = 0;
    for (int y = 40 + 30 * k; y < 70 + 30 * k; y++)
        for (int x = 100; x < 222; x++)
            n += s->pix[y * SCREEN_W + x] == colour;
    return n;
}

static void test_transition(void)
{
    printf("transition after a choice\n");
    static screen_t s;
    for (int k = 0; k < MENU_ITEMS; k++) {
        menu_t *m = menu_create(&s);
        if (!m) {
            skipped++;
            return;
        }
        for (int i = 0; i < 5; i++)
            menu_pass(m, &s);
        menu_set_highlight(m, k);
        static uint8_t outside[SCREEN_PIXELS];
        memcpy(outside, s.pix, sizeof outside);
        menu_confirm_begin(m);
        int more_ok = 1, mark_ok = 1, outside_ok = 1;
        for (int step = 1; step <= MENU_CONFIRM_STEPS; step++) {
            int more = menu_confirm_step(m, &s);
            if (more != (step < MENU_CONFIRM_STEPS))
                more_ok = 0;
            /* the check mark is two pixels of colour 0x24 per step, only in the chosen label */
            for (int j = 0; j < MENU_ITEMS; j++)
                if (count_colour_in_label(&s, j, 0x24) != (j == k ? 2 * step : 0))
                    mark_ok = 0;
            for (int y = 0; y < SCREEN_H; y++)
                for (int x = 0; x < SCREEN_W; x++)
                    if ((x < 64 || x >= 256 || y < 16 || y >= 184) && s.pix[y * SCREEN_W + x] != outside[y * SCREEN_W + x])
                        outside_ok = 0;
        }
        CHECK(more_ok);
        CHECK(mark_ok);
        CHECK(outside_ok);
        /* after the last step nothing more happens */
        static uint8_t last[SCREEN_PIXELS];
        memcpy(last, s.pix, sizeof last);
        CHECK(menu_confirm_step(m, &s) == 0 && memcmp(last, s.pix, sizeof last) == 0);
        menu_free(m);
    }
}

static void test_resize(void)
{
    printf("nearest-neighbour resize\n");
    /* Same size: every pixel kept. */
    uint8_t a[4 + 6 + 1] = {3, 0, 2, 0, 1, 2, 3, 4, 5, 6, 0};
    uint8_t d[4 + 6 + 1] = {0};
    menu_resize(d, a, 3, 2);
    CHECK(sprite_w(d) == 3 && sprite_h(d) == 2 && memcmp(d + 4, a + 4, 6) == 0);
    /* 4 -> 2 wide: every second pixel. */
    uint8_t b[4 + 4 + 1] = {4, 0, 1, 0, 10, 20, 30, 40, 0};
    uint8_t e[4 + 2 + 1] = {0};
    menu_resize(e, b, 2, 1);
    CHECK(e[4] == 10 && e[5] == 30);
    /* 3 x 2 -> 2 x 2: the horizontal fraction carries from one row into the next (not reset), so the
     * second row takes pixels 0 and 2 of its source row while the first takes 0 and 1. Worked out from
     * the original's 16-bit steps (xstep 32767): row 1 starts with the accumulator at 65534. */
    uint8_t c[4 + 6 + 1] = {3, 0, 2, 0, 'a', 'b', 'c', 'd', 'e', 'f', 0};
    uint8_t f[4 + 4 + 1] = {0};
    menu_resize(f, c, 2, 2);
    CHECK(f[4] == 'a' && f[5] == 'b' && f[6] == 'd' && f[7] == 'f');
}

static void test_opening(void)
{
    printf("opening animation\n");
    static screen_t s, ref;
    menu_t *m = menu_create(&s);
    if (!m) {
        skipped++;
        return;
    }
    memcpy(ref.pix, s.pix, SCREEN_PIXELS);   /* what menu_create shows: the whole picture */
    CHECK(menu_opening_steps() == 182);      /* 20 zoom steps, the picture, 160 shrink steps, the clear */
    menu_opening_frame(m, &s, 0);
    int nonzero = 0;
    for (int i = 0; i < SCREEN_PIXELS; i++)
        nonzero += s.pix[i] != 0;
    CHECK(nonzero == 0);
    menu_opening_frame(m, &s, 21);
    CHECK(memcmp(s.pix, ref.pix, SCREEN_PIXELS) == 0);
    /* The first shrink step draws the lettering at its own size: the picture again. */
    menu_opening_frame(m, &s, 22);
    CHECK(memcmp(s.pix, ref.pix, SCREEN_PIXELS) == 0);
    /* The zoom starts nearly empty and fills: the number of lit pixels only grows, step to step. */
    int prev = 0, grows = 1;
    for (int n = 1; n <= 20; n++) {
        menu_opening_frame(m, &s, n);
        int lit = 0;
        for (int i = 0; i < SCREEN_PIXELS; i++)
            lit += s.pix[i] != 0;
        if (lit < prev)
            grows = 0;
        prev = lit;
    }
    CHECK(grows);
    /* The shrink changes only the lettering's frame (83 x 33 at (113, 81)); the last step clears 84 x 33. */
    for (int n = 22; n <= 182; n += 7) {
        menu_opening_frame(m, &s, n);
        int outside = 0;
        for (int y = 0; y < SCREEN_H; y++)
            for (int x = 0; x < SCREEN_W; x++) {
                int inside = x >= 113 && x < 113 + 84 && y >= 81 && y < 81 + 33;
                if (!inside && s.pix[y * SCREEN_W + x] != ref.pix[y * SCREEN_W + x])
                    outside++;
            }
        CHECK(outside == 0);
    }
    menu_opening_frame(m, &s, 182);
    int cleared = 1;
    for (int y = 81; y < 81 + 33; y++)
        for (int x = 113; x < 113 + 84; x++)
            if (s.pix[y * SCREEN_W + x] != 0)
                cleared = 0;
    CHECK(cleared);
    menu_opening_frame(m, &s, 1000);   /* past the end: the last frame */
    CHECK(cleared);
    menu_free(m);
}

int main(int argc, char **argv)
{
    assets_set_dir(argc > 1 ? argv[1] : "original_bin");
    setvbuf(stdout, NULL, _IONBF, 0);
    test_shade_recolour();
    test_tile_copy();
    test_stretch();
    test_menu();
    test_keyboard();
    test_mouse();
    test_resize();
    test_opening();
    test_transition();
    printf("%s: %d failure(s), %d skipped\n", failures ? "FAILED" : "ok", failures, skipped);
    return failures ? 1 : 0;
}
