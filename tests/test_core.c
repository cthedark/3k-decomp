/*
 * Tests for the pieces that can be checked without a window.
 *
 * The expected values are facts about the original data files (chunk counts and
 * decoded sizes) found with tools/lcz_decode.py and recorded in docs/formats.md.
 * They are numbers only; no game content is stored here. Tests that need the game
 * files are skipped, not failed, when the files are missing.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"
#include "lcz.h"
#include "screen.h"

static int failures, skipped;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
            failures++;                                                          \
        }                                                                        \
    } while (0)

/* A hand-made chunk: esc=0x01. Literals 'A','B'; run of 5 x 'Z'; literal 'C'; run of 0 x 'Q'.
 * Size field = 3 header bytes + 9 body bytes = 12. */
static void test_synthetic(void)
{
    printf("synthetic chunk\n");
    const uint8_t file[] = {0x01, 0x0c, 0x00, 'A', 'B', 0x01, 5, 'Z', 'C', 0x01, 0, 'Q'};
    size_t pos = 0;
    lcz_chunk c;
    if (lcz_next_chunk(file, sizeof file, &pos, &c) != LCZ_OK) {
        printf("  FAIL %s:%d: synthetic chunk did not decode\n", __FILE__, __LINE__);
        failures++;
        return;
    }
    CHECK(c.len == 8);
    CHECK(memcmp(c.pixels, "ABZZZZZC", 8) == 0);
    CHECK(pos == sizeof file);
    lcz_chunk_free(&c);
    CHECK(lcz_next_chunk(file, sizeof file, &pos, &c) == LCZ_END);
}

static void test_malformed(void)
{
    printf("malformed chunks\n");
    lcz_chunk c;
    size_t pos = 0;
    const uint8_t short_hdr[] = {0x01, 0x05};
    CHECK(lcz_next_chunk(short_hdr, sizeof short_hdr, &pos, &c) == LCZ_ERR);
    pos = 0;
    const uint8_t too_big[] = {0x01, 0x20, 0x00, 'A'};
    CHECK(lcz_next_chunk(too_big, sizeof too_big, &pos, &c) == LCZ_ERR);
    pos = 0;
    const uint8_t bad_size[] = {0x01, 0x02, 0x00};
    CHECK(lcz_next_chunk(bad_size, sizeof bad_size, &pos, &c) == LCZ_ERR);
    pos = 0;
    const uint8_t cut_esc[] = {0x01, 0x05, 0x00, 0x01, 0x03};
    CHECK(lcz_next_chunk(cut_esc, sizeof cut_esc, &pos, &c) == LCZ_ERR);
}

static void test_palette(void)
{
    printf("palette scaling\n");
    static screen_t s;
    uint8_t pal[6] = {0, 0, 0, 63, 63, 63};
    screen_set_palette(&s, pal, 0, 2);
    s.pix[0] = 0;
    s.pix[1] = 1;
    static uint8_t rgb[SCREEN_PIXELS * 3];
    screen_to_rgb888(&s, rgb);
    CHECK(rgb[0] == 0 && rgb[1] == 0 && rgb[2] == 0);
    CHECK(rgb[3] == 255 && rgb[4] == 255 && rgb[5] == 255);
    uint8_t over[3] = {64 + 5, 0, 0}; /* the DAC keeps 6 bits */
    screen_set_palette(&s, over, 7, 1);
    CHECK(s.pal[7][0] == 5);
}

struct expect {
    const char *file;
    int chunks;
    size_t lens[8];
};

static void test_real_files(void)
{
    printf("real data files\n");
    static const struct expect table[] = {
        {"title1.lcz", 7, {13404, 6244, 4489, 4489, 4489, 2688, 2422}},
        {"story1_2.lcz", 3, {13764, 12484, 47232}},
        {"gmain.lcz", 1, {64000}},
    };
    for (size_t t = 0; t < sizeof table / sizeof table[0]; t++) {
        size_t flen;
        uint8_t *data = assets_read(table[t].file, &flen);
        if (!data) {
            printf("  skip %s (not found in %s)\n", table[t].file, assets_dir());
            skipped++;
            continue;
        }
        size_t pos = 0;
        int n = 0;
        lcz_chunk c;
        while (lcz_next_chunk(data, flen, &pos, &c) == LCZ_OK) {
            if (n < table[t].chunks)
                CHECK(c.len == table[t].lens[n]);
            n++;
            lcz_chunk_free(&c);
        }
        CHECK(n == table[t].chunks);
        CHECK(pos == flen);
        free(data);
        printf("  %s: %d chunks\n", table[t].file, n);
    }
}

int main(int argc, char **argv)
{
    const char *dir = getenv("TK_DATA");
    if (argc > 1)
        dir = argv[1];
    if (dir)
        assets_set_dir(dir);
    test_synthetic();
    test_malformed();
    test_palette();
    test_real_files();
    printf("%s (%d failures, %d skipped)\n", failures ? "FAILED" : "ok", failures, skipped);
    return failures ? 1 : 0;
}
