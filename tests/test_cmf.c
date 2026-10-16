/*
 * Tests for the CMF song reader. The numbers about MUC.ALL (25 songs; song 22 has 4,819 channel
 * events and ends at tick 12,599) come from the independent survey in tools/cmf_survey.py, written
 * before this reader. No game content is stored here; tests that need the file are skipped when it
 * is missing.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"
#include "cmf.h"

static int failures, skipped;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
            failures++;                                                          \
        }                                                                        \
    } while (0)

/* A version 1.1 song: 40-byte header, two instruments at 40, track at 72. */
static size_t make_song(uint8_t *f, const uint8_t *track, size_t n)
{
    memset(f, 0, 72);
    memcpy(f, "CTMF", 4);
    f[4] = 1; f[5] = 1;
    f[6] = 40;                 /* instrument block */
    f[8] = 72;                 /* music block */
    f[10] = 48;                /* ticks per quarter */
    f[12] = 96;                /* ticks per second */
    f[20] = 1; f[21] = 1;      /* channels 0 and 1 in use */
    f[36] = 2;                 /* instruments */
    f[40] = 0x31;              /* first byte of instrument 0 */
    f[56] = 0x32;              /* first byte of instrument 1 */
    memcpy(f + 72, track, n);
    return 72 + n;
}

static void test_synthetic(void)
{
    printf("synthetic song\n");
    uint8_t f[128];
    /* t=0 program change ch0 -> 5; t=0 note on ch0 60 vel 100; t=96 (running status) note on vel 0;
     * a tempo meta event and a sysex skipped at t=96; t=200 controller on ch1; end of track. */
    const uint8_t track[] = {
        0x00, 0xc0, 0x05,
        0x00, 0x90, 60, 100,
        0x60, 60, 0,
        0x00, 0xff, 0x51, 0x03, 1, 2, 3,
        0x00, 0xf0, 0x02, 0x7d, 0xf7,
        0x81, 0x68 /* 232 = 0x81 0x68 -> 1*128+104 */, 0xb1, 0x67, 0x01,
        0x00, 0xff, 0x2f, 0x00};
    size_t n = make_song(f, track, sizeof track);
    cmf_t *c = cmf_parse(f, n);
    CHECK(c != NULL);
    if (!c)
        return;
    CHECK(cmf_ticks_per_second(c) == 96 && cmf_ticks_per_quarter(c) == 48);
    CHECK(cmf_instrument_count(c) == 2 && cmf_instrument(c, 0)[0] == 0x31 && cmf_instrument(c, 1)[0] == 0x32);
    CHECK(cmf_instrument(c, 2) == NULL);
    CHECK(cmf_channel_used(c, 0) && cmf_channel_used(c, 1) && !cmf_channel_used(c, 2) && !cmf_channel_used(c, 16));

    cmf_cursor cur;
    cmf_event e;
    cmf_rewind(c, &cur);
    CHECK(cmf_next(&cur, &e) && e.tick == 0 && e.status == 0xc0 && e.a == 5 && e.b == 0);
    CHECK(cmf_next(&cur, &e) && e.tick == 0 && e.status == 0x90 && e.a == 60 && e.b == 100);
    CHECK(cmf_next(&cur, &e) && e.tick == 96 && e.status == 0x90 && e.a == 60 && e.b == 0);   /* running status */
    CHECK(cmf_next(&cur, &e) && e.tick == 96 + 232 && e.status == 0xb1 && e.a == 0x67 && e.b == 1);
    CHECK(!cmf_next(&cur, &e) && cur.done && !cur.error && cur.tick == 96 + 232);
    CHECK(!cmf_next(&cur, &e));   /* stays at the end */

    cmf_rewind(c, &cur);          /* a rewound cursor reads the same song again */
    CHECK(cmf_next(&cur, &e) && e.status == 0xc0);
    cmf_free(c);
}

static void test_malformed(void)
{
    printf("malformed songs\n");
    uint8_t f[128];
    cmf_event e;
    cmf_cursor cur;

    CHECK(cmf_parse(f, 10) == NULL);
    const uint8_t t0[] = {0x00, 0xff, 0x2f, 0x00};
    size_t n = make_song(f, t0, sizeof t0);
    f[0] = 'X';
    CHECK(cmf_parse(f, n) == NULL);
    f[0] = 'C';
    f[5] = 2;                                       /* version 2.x */
    CHECK(cmf_parse(f, n) == NULL);
    f[5] = 1;
    f[8] = 0xff; f[9] = 0xff;                       /* music block past the end */
    CHECK(cmf_parse(f, n) == NULL);
    f[8] = 72; f[9] = 0;
    f[36] = 200;                                    /* instrument table past the end */
    CHECK(cmf_parse(f, n) == NULL);
    f[36] = 2;
    f[12] = 0; f[13] = 0;                           /* a zero clock would divide by zero */
    CHECK(cmf_parse(f, n) == NULL);
    f[12] = 96;

    /* A data byte with no status before it, a note cut short, a delta time that never ends. */
    const uint8_t bad1[] = {0x00, 0x3c, 0x40};
    n = make_song(f, bad1, sizeof bad1);
    cmf_t *c = cmf_parse(f, n);
    CHECK(c != NULL);
    cmf_rewind(c, &cur);
    CHECK(!cmf_next(&cur, &e) && cur.error);
    cmf_free(c);
    const uint8_t bad2[] = {0x00, 0x90, 60};
    n = make_song(f, bad2, sizeof bad2);
    c = cmf_parse(f, n);
    cmf_rewind(c, &cur);
    CHECK(!cmf_next(&cur, &e) && cur.error);
    cmf_free(c);
    const uint8_t bad3[] = {0x80, 0x80, 0x80, 0x80, 0x80};
    n = make_song(f, bad3, sizeof bad3);
    c = cmf_parse(f, n);
    cmf_rewind(c, &cur);
    CHECK(!cmf_next(&cur, &e) && cur.error);
    cmf_free(c);

    /* No end-of-track marker: the end of the data ends the song without an error. */
    const uint8_t open[] = {0x00, 0x90, 60, 100};
    n = make_song(f, open, sizeof open);
    c = cmf_parse(f, n);
    cmf_rewind(c, &cur);
    CHECK(cmf_next(&cur, &e) && !cmf_next(&cur, &e) && cur.done && !cur.error);
    cmf_free(c);
}

static void test_muc_all(void)
{
    printf("MUC.ALL\n");
    size_t len = 0;
    uint8_t *d = assets_read("muc.all", &len);
    if (!d) {
        printf("  skipped (no game files)\n");
        skipped++;
        return;
    }
    /* Songs are back to back; each starts with "CTMF" and runs to the next one. */
    size_t start[64];
    int n = 0;
    for (size_t i = 0; i + 4 <= len && n < 64; i++)
        if (memcmp(d + i, "CTMF", 4) == 0)
            start[n++] = i;
    CHECK(n == 25);
    int parsed = 0, clean = 0;
    for (int i = 0; i < n; i++) {
        size_t end = i + 1 < n ? start[i + 1] : len;
        cmf_t *c = cmf_parse(d + start[i], end - start[i]);
        if (!c)
            continue;
        parsed++;
        cmf_cursor cur;
        cmf_event e;
        unsigned events = 0;
        cmf_rewind(c, &cur);
        while (cmf_next(&cur, &e))
            events++;
        if (!cur.error && cmf_ticks_per_second(c) == 96)
            clean++;
        if (start[i] == 0x27137) {   /* the song the intro loads (STORY2.SL 0x10288: 0x49a2 bytes) */
            CHECK(end - start[i] == 0x49a2);
            CHECK(cmf_ticks_per_quarter(c) == 65 && cmf_instrument_count(c) == 10);
            CHECK(events == 4819 && cur.tick == 12599);
        }
        cmf_free(c);
    }
    CHECK(parsed == 25 && clean == 25);
    free(d);
}

int main(int argc, char **argv)
{
    if (argc > 1)
        assets_set_dir(argv[1]);
    test_synthetic();
    test_malformed();
    test_muc_all();
    printf("%s: %d failures, %d skipped\n", failures ? "FAILED" : "ok", failures, skipped);
    return failures ? 1 : 0;
}
