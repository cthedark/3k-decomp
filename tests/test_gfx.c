/*
 * Tests for src/gfx.c.
 *
 * The rotate routine is compared with a literal transcription of the original 8086
 * loop (MARK.EXE 0x12124, registers and the LOOP instruction's semantics written out),
 * so the C implementation is checked against the machine code's behaviour and not
 * only against my own reading of it.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gfx.h"
#include "tkrand.h"

static int failures;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
            failures++;                                                          \
        }                                                                        \
    } while (0)

/* Literal transcription of the "direction == 1" path of 0x12124. `mem` holds the sprite
 * at offset 0. All register arithmetic is 16-bit, as on the 8086. */
static void ref_rotate_asm(uint8_t *mem)
{
    uint16_t bx = (uint16_t)(mem[0] | mem[1] << 8);
    uint16_t cx = (uint16_t)(mem[2] | mem[3] << 8);
    uint16_t ax = (uint16_t)(cx * bx + 4);
    uint16_t si = ax, di = ax, dx;
    uint8_t al, ah;
    bx--;
    do {                         /* 0x12157 */
        dx = cx;
        al = mem[si];
        si--;
        cx = bx;
        do {                     /* 0x1215e */
            ah = mem[si];
            mem[di] = ah;
            si--;
            di--;
            cx--;
        } while (cx != 0);       /* LOOP */
        mem[di] = al;
        di--;
        cx = dx;
        cx--;                    /* LOOP at 0x1216d */
    } while (cx != 0);
}

static void test_rotate(void)
{
    printf("rotate rows vs. transcribed assembly\n");
    srand(12345);
    int cases = 0;
    for (unsigned w = 2; w <= 9; w++)
        for (unsigned h = 1; h <= 6; h++) {
            size_t n = SPRITE_HDR + w * h + 1;
            uint8_t a[128], b[128];
            for (size_t i = 0; i < sizeof a; i++)
                a[i] = (uint8_t)rand();
            sprite_set_size(a, w, h);
            memcpy(b, a, sizeof a);
            ref_rotate_asm(a);                  /* the assembly model reads the spare byte as-is */
            gfx_rotate_rows(b);
            if (memcmp(a, b, n) != 0) {
                printf("  FAIL w=%u h=%u\n", w, h);
                failures++;
            }
            /* The spare byte is state: a second call must also agree. */
            ref_rotate_asm(a);
            gfx_rotate_rows(b);
            if (memcmp(a, b, n) != 0) {
                printf("  FAIL (second call) w=%u h=%u\n", w, h);
                failures++;
            }
            cases++;
        }
    printf("  %d sizes compared\n", cases);

    /* A fixed small case, so a behaviour change is noticed even if both models change
     * together: w=3, h=2, pixels 10 11 12 / 20 21 22, past-end byte 99. */
    uint8_t s[16] = {3, 0, 2, 0, 10, 11, 12, 20, 21, 22, 99};
    gfx_rotate_rows(s);
    uint8_t t[16] = {3, 0, 2, 0, 10, 11, 12, 20, 21, 22, 99};
    ref_rotate_asm(t);
    CHECK(memcmp(s, t, 11) == 0);
}

static void test_scroll(void)
{
    printf("scroll up\n");
    uint8_t s[4 + 9] = {3, 0, 3, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    gfx_scroll_up(s, 1);
    const uint8_t want[] = {3, 0, 3, 0, 4, 5, 6, 7, 8, 9, 7, 8, 9};
    CHECK(memcmp(s, want, sizeof want) == 0);
    uint8_t z[4 + 4] = {2, 0, 2, 0, 1, 2, 3, 4};
    gfx_scroll_up(z, 0);
    const uint8_t keep[] = {2, 0, 2, 0, 1, 2, 3, 4};
    CHECK(memcmp(z, keep, sizeof keep) == 0);
}

static void test_overlay_and_rect(void)
{
    printf("overlay, copy rectangle, fill\n");
    uint8_t dst[6] = {9, 9, 9, 9, 9, 9};
    const uint8_t src[6] = {0, 1, 0, 2, 0, 3};
    gfx_overlay(dst, src, 6);
    const uint8_t want[6] = {9, 1, 9, 2, 9, 3};
    CHECK(memcmp(dst, want, 6) == 0);

    /* 4-wide image, take the 2x2 rectangle starting at column 1, row 1 */
    const uint8_t img[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    uint8_t rect[4];
    gfx_copy_rect(rect, img + 1 * 4 + 1, 4, 2, 2);
    const uint8_t rw[4] = {5, 6, 9, 10};
    CHECK(memcmp(rect, rw, 4) == 0);

    static screen_t sc;
    gfx_fill_rect(&sc, 321, 3, 2, 7);
    CHECK(sc.pix[320] == 0 && sc.pix[321] == 7 && sc.pix[323] == 7 && sc.pix[324] == 0);
    CHECK(sc.pix[641] == 7 && sc.pix[643] == 7);
    gfx_fill_rect(&sc, SCREEN_PIXELS - 2, 5, 3, 1); /* runs off the end: must not write past it */
    CHECK(sc.pix[SCREEN_PIXELS - 1] == 1);
}

static void test_draw_sprite(void)
{
    printf("draw sprite\n");
    static screen_t sc;
    const uint8_t sp[4 + 4] = {2, 0, 2, 0, 1, 2, 3, 4};
    gfx_draw_sprite(&sc, 321, sp);
    CHECK(sc.pix[321] == 1 && sc.pix[322] == 2 && sc.pix[641] == 3 && sc.pix[642] == 4);
    CHECK(sc.pix[320] == 0 && sc.pix[323] == 0);
}

static void test_palette_step(void)
{
    printf("palette step\n");
    uint8_t cur[3] = {5, 3, 4}, tgt[3] = {3, 5, 4};
    gfx_palette_step(cur, tgt, 3);
    CHECK(cur[0] == 4 && cur[1] == 4 && cur[2] == 4);
    uint8_t c[1] = {0}, t[1] = {63};
    for (int i = 0; i < 63; i++)
        gfx_palette_step(c, t, 1);
    CHECK(c[0] == 63);
    gfx_palette_step(c, t, 1);
    CHECK(c[0] == 63); /* stays on target */
}

/* Literal transcription of the "direction == 1" path of STORY2.SL 0x13b19 (0x13b7a..0x13ba5),
 * including the odd/even split on (width - 1) that decides whether a `movsb` is needed.
 * `mem` holds the sprite at offset 0. */
static void ref_rotate_left_asm(uint8_t *mem)
{
    uint16_t bx = (uint16_t)(mem[0] | mem[1] << 8);
    uint16_t cx = (uint16_t)(mem[2] | mem[3] << 8);
    uint16_t si = 4, di = 4, dx;
    uint8_t al;
    bx--;
    int odd = bx & 1;      /* CF after `shr bx, 1` */
    bx >>= 1;
    do {
        dx = cx;
        al = mem[si];
        si++;
        for (cx = bx; cx != 0; cx--) {   /* rep movsw: read the word, then write it */
            uint8_t lo = mem[si], hi = mem[si + 1];
            mem[di] = lo;
            mem[di + 1] = hi;
            si += 2;
            di += 2;
        }
        if (odd) {                       /* movsb */
            mem[di] = mem[si];
            si++;
            di++;
        }
        mem[di] = al;
        di++;
        cx = dx;
        cx--;
    } while (cx != 0);
}

static void test_rotate_left(void)
{
    printf("rotate left\n");
    static const unsigned sizes[][2] = {{2, 1}, {3, 2}, {4, 3}, {5, 4}, {8, 2}, {9, 3}, {320, 200}};
    srand(7);
    for (unsigned k = 0; k < sizeof sizes / sizeof sizes[0]; k++) {
        unsigned w = sizes[k][0], h = sizes[k][1];
        size_t n = SPRITE_HDR + (size_t)w * h;
        uint8_t *a = malloc(n + 8), *b = malloc(n + 8);
        sprite_set_size(a, w, h);
        for (size_t i = SPRITE_HDR; i < n; i++)
            a[i] = (uint8_t)rand();
        memcpy(b, a, n);
        gfx_rotate_rows_left(a);
        ref_rotate_left_asm(b);
        CHECK(memcmp(a, b, n) == 0);
        free(a);
        free(b);
    }
    const uint8_t in[4 + 6] = {3, 0, 2, 0, 1, 2, 3, 4, 5, 6};
    uint8_t s[4 + 6];
    memcpy(s, in, sizeof s);
    gfx_rotate_rows_left(s);
    const uint8_t want[4 + 6] = {3, 0, 2, 0, 2, 3, 1, 5, 6, 4};
    CHECK(memcmp(s, want, sizeof s) == 0);
}

static void test_expand_bits(void)
{
    printf("expand bits\n");
    const uint8_t src[2] = {0xA5, 0x01};
    uint8_t out[16];
    memset(out, 0xEE, sizeof out);
    gfx_expand_bits(out, src, 2, 0x10);
    const uint8_t want[16] = {0x10, 0, 0x10, 0, 0, 0x10, 0, 0x10, 0, 0, 0, 0, 0, 0, 0, 0x10};
    CHECK(memcmp(out, want, 16) == 0);
}

static void test_stamp(void)
{
    printf("stamp\n");
    uint8_t dst[3 * 4];
    memset(dst, 0, sizeof dst);
    const uint8_t sp[4 + 4] = {2, 0, 2, 0, 1, 0, 0, 2};
    gfx_stamp(dst, 4, 1, sp, 9);   /* non-zero pixels become 9, zero pixels leave dst alone */
    CHECK(dst[1] == 9 && dst[2] == 0 && dst[4 + 1] == 0 && dst[4 + 2] == 9);
    dst[2] = 5;
    gfx_stamp(dst, 4, 0, sp, 7);
    CHECK(dst[0] == 7 && dst[1] == 9 && dst[2] == 5 && dst[5] == 7);
    /* a pixel past the end of a row lands at the start of the next, as in the original */
    memset(dst, 0, sizeof dst);
    gfx_stamp(dst, 2, 1, sp, 3);   /* stride 2, x = 1: the 2-wide sprite's second column overflows */
    CHECK(dst[1] == 3 && dst[2 + 1] == 0 && dst[2 * 1 + 2] == 3);
}

static void test_rand(void)
{
    printf("rand\n");
    /* The Microsoft C generator from seed 1: the well-known sequence. */
    tk_srand(1);
    CHECK(tk_rand() == 41);
    CHECK(tk_rand() == 18467);
    CHECK(tk_rand() == 6334);
    CHECK(tk_rand() == 26500);
    CHECK(tk_rand() == 19169);
    /* srand stores a 16-bit value with the high word cleared (STORY2.SL 0x13f22) */
    tk_srand(1);
    CHECK(tk_rand() == 41);
    tk_srand(0xFFFF);
    int v = tk_rand();
    CHECK(v >= 0 && v <= 0x7fff);
}

static void test_blits(void)
{
    puts("masked sprite blit, rect blit");
    /* 3x2 sprite, 0 transparent, into rows of 5; the untouched cells keep their value */
    uint8_t spr[SPRITE_HDR + 6] = {3, 0, 2, 0, 1, 0, 2, 0, 3, 0};
    uint8_t dst[10];
    memset(dst, 9, sizeof dst);
    gfx_blit_masked(dst + 1, 5, spr);
    const uint8_t want[10] = {9, 1, 9, 2, 9, 9, 9, 3, 9, 9};
    CHECK(memcmp(dst, want, sizeof want) == 0);

    static screen_t sc;
    memset(sc.pix, 0, sizeof sc.pix);
    const uint8_t src[6] = {1, 2, 3, 4, 5, 6};
    gfx_blit_rect(&sc, 321, src, 3, 2);
    CHECK(sc.pix[321] == 1 && sc.pix[323] == 3 && sc.pix[641] == 4 && sc.pix[643] == 6);
    CHECK(sc.pix[320] == 0 && sc.pix[324] == 0 && sc.pix[640] == 0);
    gfx_blit_rect(&sc, SCREEN_PIXELS - 1, src, 3, 2);   /* off the end: dropped, no overrun */
    CHECK(sc.pix[SCREEN_PIXELS - 1] == 1);
}

int main(void)
{
    test_rotate();
    test_blits();
    test_rotate_left();
    test_expand_bits();
    test_stamp();
    test_rand();
    test_scroll();
    test_overlay_and_rect();
    test_draw_sprite();
    test_palette_step();
    printf("%s (%d failures)\n", failures ? "FAILED" : "ok", failures);
    return failures ? 1 : 0;
}
