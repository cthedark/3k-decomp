/*
 * Tests for the CMF music player (src/music.c) and the OPL2 wrapper (src/opl.c).
 *
 * Independent references used here:
 *  - register writes the ORIGINAL's FM driver made, recorded in the oracle emulator (tools/dosemu.py --opl-log, see
 *    docs/findings.md): the values in the rhythm, pitch and allocation tests are copied from those logs. The whole of
 *    every song is compared with the original in tools/check_music.sh (25 of 25 identical); here only small cases;
 *  - the standard AdLib frequency formula, f = fnum * 49716 / 2^(20 - block), against a pitch measured on the
 *    rendered audio itself (zero crossings of a near-sine patch);
 *  - the note counts of tools/cmf_survey.py / cmf_behaviour.py for the intro song (2,402 notes,
 *    none overlapping, none unmatched).
 * No game content is stored here; tests that need MUC.ALL are skipped when it is missing.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"
#include "cmf.h"
#include "music.h"
#include "opl.h"

static int failures, skipped;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
            failures++;                                                          \
        }                                                                        \
    } while (0)

/* ---------- a synthetic song ---------- */

/* A version 1.1 song with `n_patch` patches (16 bytes each, from `patches`) and the given track. */
static cmf_t *make_song(const uint8_t *patches, unsigned n_patch, const uint8_t *track, size_t n)
{
    uint8_t f[512];
    size_t inst_at = 40, music_at = inst_at + 16u * n_patch;
    memset(f, 0, sizeof f);
    memcpy(f, "CTMF", 4);
    f[4] = 1; f[5] = 1;
    f[6] = (uint8_t)inst_at;
    f[8] = (uint8_t)music_at;
    f[10] = 48;   /* ticks per quarter */
    f[12] = 96;   /* ticks per second */
    f[36] = (uint8_t)n_patch;
    memcpy(f + inst_at, patches, 16u * n_patch);
    memcpy(f + music_at, track, n);
    return cmf_parse(f, music_at + n);
}

/* A patch that is a near-sine: modulator silent (level 63), carrier sustained at full strength. */
static const uint8_t k_sine_patch[16] = {
    0x21, 0x21,   /* characteristics: sustained, multiplier 1 */
    0x3f, 0x00,   /* levels: modulator off, carrier full (the player overrides the carrier's) */
    0xf0, 0xf0,   /* attack 15, decay 0 */
    0x0f, 0x0f,   /* sustain level 0, release 15 */
    0x00, 0x00,   /* sine */
    0x00,         /* FM, no feedback */
};

enum { TRACE_MAX = 512 };
typedef struct {
    uint64_t when[TRACE_MAX];
    uint8_t reg[TRACE_MAX], val[TRACE_MAX];
    unsigned n;
} trace_t;

static void trace_cb(void *user, uint64_t sample, uint8_t reg, uint8_t value)
{
    trace_t *t = user;
    if (t->n < TRACE_MAX) {
        t->when[t->n] = sample;
        t->reg[t->n] = reg;
        t->val[t->n] = value;
    }
    t->n++;
}

/* The writes of one register in a trace, in order. */
static unsigned writes_of(const trace_t *t, uint8_t reg, uint8_t *vals, unsigned max)
{
    unsigned k = 0;
    for (unsigned i = 0; i < t->n && i < TRACE_MAX; i++)
        if (t->reg[i] == reg && k < max)
            vals[k++] = t->val[i];
    return k;
}

/* Render a song for `frames` samples with a trace that is set up AFTER creation, so the chip
 * initialisation writes are not part of it. */
static music_t *player(const uint8_t *patches, unsigned n_patch, const uint8_t *track, size_t n, trace_t *tr)
{
    cmf_t *song = make_song(patches, n_patch, track, n);
    music_t *m = song ? music_create(song) : NULL;
    if (!m)
        cmf_free(song);
    else if (tr) {
        memset(tr, 0, sizeof *tr);
        music_set_trace(m, trace_cb, tr);
    }
    return m;
}

static void test_registers(void)
{
    printf("register writes for one note\n");
    /* t=0 program 0; t=0 note on A4 (69) velocity 100; t=96 note off; end at t=192. */
    const uint8_t track[] = {0x00, 0xc0, 0x00, 0x00, 0x90, 69, 100, 0x60, 0x80, 69, 0, 0x60, 0xff, 0x2f, 0x00};
    trace_t tr;
    music_t *m = player(k_sine_patch, 1, track, sizeof track, &tr);
    CHECK(m != NULL);
    if (!m)
        return;
    static int16_t buf[60000];
    music_render(m, buf, 60000);   /* past tick 96 = sample 49,716 */

    uint8_t v[8];
    /* The patch lands on the operators of voice 0 (modulator 0x00, carrier 0x03), when the note needs it. */
    CHECK(writes_of(&tr, 0x20, v, 8) == 1 && v[0] == 0x21);
    CHECK(writes_of(&tr, 0x23, v, 8) == 1 && v[0] == 0x21);
    CHECK(writes_of(&tr, 0x40, v, 8) == 1 && v[0] == 0x3f);
    CHECK(writes_of(&tr, 0x60, v, 8) == 1 && v[0] == 0xf0);
    CHECK(writes_of(&tr, 0x83, v, 8) == 1 && v[0] == 0x0f);
    CHECK(writes_of(&tr, 0xe0, v, 8) == 1 && v[0] == 0x00);
    CHECK(writes_of(&tr, 0xc0, v, 8) == 1 && v[0] == 0x00);
    /* The carrier's level is written once, by the note-on, not by the patch load (the driver's register log
     * shows no 0x43 at a load). Velocity 100, patch level 0: 63 - (63 * (0x80 | 100) >> 8) = 63 - 56 = 7. */
    unsigned n43 = writes_of(&tr, 0x43, v, 8);
    CHECK(n43 == 1);
    CHECK(n43 == 1 && v[0] == 0x07);
    /* A4 is f-number 0x241 (577), block 4: the driver's table, not the 0x244 of equal temperament. A note-off
     * writes both pitch registers again, without the key-on bit. */
    unsigned na = writes_of(&tr, 0xa0, v, 8);
    CHECK(na == 2 && v[0] == 0x41 && v[1] == 0x41);
    unsigned nb = writes_of(&tr, 0xb0, v, 8);
    CHECK(nb == 2);
    CHECK(nb == 2 && v[0] == 0x32);   /* key on  | block 4 << 2 | fnum high 2 */
    CHECK(nb == 2 && v[1] == 0x12);   /* key off, same pitch bits */
    /* The note-off is at tick 96 = 96 * 49,716 / 96 samples. */
    for (unsigned i = 0; i < tr.n && i < TRACE_MAX; i++)
        if (tr.reg[i] == 0xb0 && tr.val[i] == 0x12)
            CHECK(tr.when[i] == 49716);
    music_free(m);
}

static void test_velocity_and_keyscale(void)
{
    printf("velocity -> carrier level, key-scale kept\n");
    uint8_t patch[16];
    memcpy(patch, k_sine_patch, 16);
    patch[3] = 0x80 | 0x2a;   /* key-scale 2 and patch level 42 */
    /* note 60 velocity 76, released, then note 62 velocity 1 on the same channel: the voice is reused, no reload */
    const uint8_t track[] = {0x00, 0xc0, 0x00, 0x00, 0x90, 60, 76, 0x01, 0x80, 60, 0, 0x00, 0x90, 62, 1, 0x60, 0xff, 0x2f, 0x00};
    trace_t tr;
    music_t *m = player(patch, 1, track, sizeof track, &tr);
    CHECK(m != NULL);
    if (!m)
        return;
    static int16_t buf[1000];
    music_render(m, buf, 1000);
    uint8_t v[8];
    unsigned n = writes_of(&tr, 0x43, v, 8);
    CHECK(n == 2);
    /* level = 63 - ((63 - 42) * (0x80 | velocity) >> 8), key-scale bits kept:
     * velocity 76: 21 * 204 >> 8 = 16 -> 47;  velocity 1: 21 * 129 >> 8 = 10 -> 53 */
    CHECK(n == 2 && v[0] == (0x80 | 47));
    CHECK(n == 2 && v[1] == (0x80 | 53));
    CHECK(writes_of(&tr, 0x20, v, 8) == 1);   /* the patch was loaded once: the second note reused the voice */
    music_free(m);
}

static void test_off_matching(void)
{
    printf("a note-off only releases the note a voice of that channel holds\n");
    /* on 60; off 61 (nothing holds it: must do nothing); off 60 at tick 2. */
    const uint8_t track[] = {0x00, 0xc0, 0x00, 0x00, 0x90, 60, 100, 0x01, 0x80, 61, 0,
                             0x01, 0x80, 60, 0, 0x30, 0xff, 0x2f, 0x00};
    trace_t tr;
    music_t *m = player(k_sine_patch, 1, track, sizeof track, &tr);
    CHECK(m != NULL);
    if (!m)
        return;
    static int16_t buf[3000];
    music_render(m, buf, 3000);
    uint8_t v[16];
    unsigned n = writes_of(&tr, 0xb0, v, 16);
    CHECK(n == 2);   /* key-on and one key-off */
    CHECK(n == 2 && (v[0] & 0x20) && !(v[1] & 0x20));
    /* The count alone cannot tell the stale off from the real one: the key-off must come at tick 2. */
    unsigned last = 0;
    for (unsigned i = 0; i < tr.n && i < TRACE_MAX; i++)
        if (tr.reg[i] == 0xb0)
            last = i;
    CHECK(tr.when[last] == 2ull * 49716 / 96);
    music_free(m);

    /* A second channel playing the same note number is a different note: the off of channel 1 leaves channel 0's. */
    const uint8_t two[] = {0x00, 0xc0, 0x00, 0x00, 0xc1, 0x00, 0x00, 0x90, 60, 100, 0x00, 0x91, 60, 100,
                           0x01, 0x81, 60, 0, 0x30, 0xff, 0x2f, 0x00};
    m = player(k_sine_patch, 1, two, sizeof two, &tr);
    CHECK(m != NULL);
    if (!m)
        return;
    music_render(m, buf, 3000);
    unsigned off0 = 0, off1 = 0;   /* key-offs of voice 0 (channel 0) and voice 1 (channel 1) */
    for (unsigned i = 0; i < tr.n && i < TRACE_MAX; i++) {
        if (tr.reg[i] == 0xb0 && !(tr.val[i] & 0x20))
            off0++;
        if (tr.reg[i] == 0xb1 && !(tr.val[i] & 0x20))
            off1++;
    }
    CHECK(off0 == 0 && off1 == 1);
    music_free(m);
}

static void test_velocity_zero_is_off(void)
{
    printf("note-on with velocity 0 is a note-off\n");
    const uint8_t track[] = {0x00, 0xc0, 0x00, 0x00, 0x90, 60, 100, 0x01, 0x90, 60, 0, 0x30, 0xff, 0x2f, 0x00};
    trace_t tr;
    music_t *m = player(k_sine_patch, 1, track, sizeof track, &tr);
    CHECK(m != NULL);
    if (!m)
        return;
    static int16_t buf[1000];
    music_render(m, buf, 1000);
    uint8_t v[8];
    unsigned n = writes_of(&tr, 0xb0, v, 8);
    CHECK(n == 2 && n == 2 && (v[0] & 0x20) && !(v[1] & 0x20));
    music_free(m);
}

static void test_ignored(void)
{
    printf("events the driver does not act on are counted\n");
    /* program 0; a pitch bend; controllers 0x10, 0x63 and 0x68 (the driver handles only 0x66 and 0x67); then a
     * note on channel 0 that must still play. */
    const uint8_t track[] = {0x00, 0xc0, 0x00, 0x00, 0xe0, 0x00, 0x40, 0x00, 0xb0, 0x10, 5, 0x00, 0xb0, 0x63, 3,
                             0x00, 0xb0, 0x68, 127, 0x00, 0xb0, 0x66, 9,   /* 0x66 is the song marker: not counted */
                             0x00, 0x90, 60, 100, 0x60, 0xff, 0x2f, 0x00};
    trace_t tr;
    music_t *m = player(k_sine_patch, 1, track, sizeof track, &tr);
    CHECK(m != NULL);
    if (!m)
        return;
    static int16_t buf[1000];
    music_render(m, buf, 1000);
    CHECK(music_ignored_events(m) == 4);
    uint8_t v[8];
    CHECK(writes_of(&tr, 0xb0, v, 8) == 1);   /* the note keyed on */
    CHECK(writes_of(&tr, 0xa0, v, 8) == 1 && v[0] == 0x57);   /* and the transposition controller did nothing: note 60 */
    music_free(m);
}

/* ---------- measured pitch ---------- */

/* Frequency of the audio by counting rising zero crossings between the 10% and 90% points. */
static double measure_hz(const int16_t *a, size_t n, unsigned rate)
{
    size_t from = n / 10, to = n * 9 / 10;
    unsigned cross = 0;
    long first = -1, last = -1;
    for (size_t i = from + 1; i < to; i++)
        if (a[i - 1] < 0 && a[i] >= 0) {
            if (first < 0)
                first = (long)i;
            last = (long)i;
            cross++;
        }
    if (cross < 3)
        return 0;
    return (double)(cross - 1) * rate / (double)(last - first);
}

static void test_pitch(void)
{
    printf("rendered pitch\n");
    /* The driver's table: twelve f-numbers and the block one octave below the MIDI number. The frequency is the
     * standard AdLib formula f = fnum * 49716 / 2^(20 - block); it is within 0.6% below equal temperament. */
    static const struct { uint8_t note; unsigned fnum, block; double et; } cases[] = {
        {57, 577, 3, 220.0}, {69, 577, 4, 440.0}, {72, 343, 5, 523.2511}, {81, 577, 5, 880.0},
        {45, 577, 2, 110.0}, {93, 577, 6, 1760.0},
    };
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        const uint8_t track[] = {0x00, 0xc0, 0x00, 0x00, 0x90, cases[i].note, 127, 0x00, 0xff, 0x2f, 0x00};
        music_t *m = player(k_sine_patch, 1, track, sizeof track, NULL);
        CHECK(m != NULL);
        if (!m)
            return;
        size_t n = music_rate();   /* one second */
        int16_t *buf = malloc(n * sizeof *buf);
        if (!buf) {
            music_free(m);
            return;
        }
        music_render(m, buf, n);
        double hz = measure_hz(buf, n, music_rate());
        double want = cases[i].fnum * (double)music_rate() / (double)(1u << (20 - cases[i].block));
        if (fabs(hz - want) / want > 0.005)
            printf("  note %u: measured %.3f Hz, want %.3f\n", cases[i].note, hz, want);
        CHECK(fabs(hz - want) / want < 0.005);
        CHECK(fabs(want - cases[i].et) / cases[i].et < 0.006);   /* sanity: about the right note */
        free(buf);
        music_free(m);
    }
}

/* The pitch registers the driver wrote, from its log: (note, 0xA0, 0xB0 without key-on). */
static void test_pitch_registers(void)
{
    printf("pitch registers (the driver's table)\n");
    static const struct { uint8_t note, a0, b0; } cases[] = {
        {48, 0x57, 0x0d}, {51, 0x98, 0x0d}, {15, 0x98, 0x01}, {50, 0x81, 0x0d},   /* seen on the rhythm voices */
        {63, 0x98, 0x11}, {64, 0xb0, 0x11}, {69, 0x41, 0x12}, {88, 0xb0, 0x19},   /* seen on melodic voices */
        {0, 0x57, 0x01}, {11, 0x87, 0x02}, {12, 0x57, 0x01}, {23, 0x87, 0x02}, {24, 0x57, 0x05},   /* the low end: block 0 */
        {107, 0x87, 0x1e}, {108, 0x57, 0x1d}, {119, 0x87, 0x1e}, {127, 0x87, 0x1e},               /* the top: block 7 */
        /* (these two rows are the driver's note table, CAST.SL data 0xc7, not a log) */
    };
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        const uint8_t track[] = {0x00, 0xc0, 0x00, 0x00, 0x90, cases[i].note, 100, 0x00, 0xff, 0x2f, 0x00};
        trace_t tr;
        music_t *m = player(k_sine_patch, 1, track, sizeof track, &tr);
        CHECK(m != NULL);
        if (!m)
            return;
        static int16_t buf[100];
        music_render(m, buf, 100);
        uint8_t a[4], b[4];
        unsigned na = writes_of(&tr, 0xa0, a, 4), nb = writes_of(&tr, 0xb0, b, 4);
        CHECK(na == 1 && nb == 1);
        if (na == 1 && nb == 1 && (a[0] != cases[i].a0 || b[0] != (0x20 | cases[i].b0)))
            printf("  note %u: A0=%02x B0=%02x, want %02x %02x\n", cases[i].note, a[0], b[0], cases[i].a0, 0x20 | cases[i].b0);
        CHECK(na == 1 && nb == 1 && a[0] == cases[i].a0 && b[0] == (0x20 | cases[i].b0));
        music_free(m);
    }
}

static void test_transpose(void)
{
    printf("the driver ignores the transpose controller\n");
    /* Controller 0x68 (the port once treated it as a transposition) changes nothing: A4 stays at f-number 0x241. */
    const uint8_t track[] = {0x00, 0xc0, 0x00, 0x00, 0xb0, 0x68, 127, 0x00, 0x90, 69, 127, 0x00, 0xff, 0x2f, 0x00};
    trace_t tr;
    music_t *m = player(k_sine_patch, 1, track, sizeof track, &tr);
    CHECK(m != NULL);
    if (!m)
        return;
    static int16_t buf[200];
    music_render(m, buf, 200);
    uint8_t v[4];
    CHECK(writes_of(&tr, 0xa0, v, 4) == 1 && v[0] == 0x41);
    CHECK(music_ignored_events(m) == 1);
    music_free(m);
}

static void test_loop(void)
{
    printf("looping and the empty song\n");
    /* A song 96 ticks (one second) long: note on at 0, off at 48, end marker at 96. */
    const uint8_t track[] = {0x00, 0xc0, 0x00, 0x00, 0x90, 60, 100, 0x30, 0x80, 60, 0, 0x30, 0xff, 0x2f, 0x00};
    trace_t tr;
    music_t *m = player(k_sine_patch, 1, track, sizeof track, &tr);
    CHECK(m != NULL);
    if (!m)
        return;
    unsigned rate = music_rate();
    int16_t *buf = malloc((size_t)rate * 3 * sizeof *buf);
    if (!buf) {
        music_free(m);
        return;
    }
    music_render(m, buf, (size_t)rate * 26 / 10);   /* 2.6 s: two passes and 0.6 s of the third */
    CHECK(music_loops(m) == 2);
    uint8_t v[16];
    unsigned n = writes_of(&tr, 0xb0, v, 16);
    CHECK(n == 6);   /* three note-ons and three note-offs (the recorded writes: 64 at most) */
    free(buf);
    music_free(m);

    /* A song with no events at all must give silence and must not spin. */
    const uint8_t empty[] = {0x00, 0xff, 0x2f, 0x00};
    m = player(k_sine_patch, 1, empty, sizeof empty, NULL);
    CHECK(m != NULL);
    if (m) {
        int16_t b[512];
        memset(b, 0x55, sizeof b);
        music_render(m, b, 512);
        int silent = 1;
        for (int i = 0; i < 512; i++)
            silent &= b[i] == 0;
        CHECK(silent);
        music_free(m);
    }
}

/* A song whose events are all at tick 0 has zero length. It once restarted forever without
 * producing a sample (a hang found on 2026-10-15); it must return, apply its events, and never
 * count a loop. The Makefile runs the test binaries under a time limit, so a regression fails
 * instead of hanging. */
static void test_zero_length(void)
{
    printf("a zero-length song does not spin\n");
    const uint8_t track[] = {0x00, 0xc0, 0x00, 0x00, 0x90, 69, 127, 0x00, 0xff, 0x2f, 0x00};
    trace_t tr;
    music_t *m = player(k_sine_patch, 1, track, sizeof track, &tr);
    CHECK(m != NULL);
    if (!m)
        return;
    static int16_t buf[5000];
    music_render(m, buf, 5000);
    music_render(m, buf, 5000);
    CHECK(music_loops(m) == 0);
    uint8_t v[8];
    CHECK(writes_of(&tr, 0xb0, v, 8) == 1);   /* the note keyed on once, and was not restarted */
    int16_t peak = 0;
    for (int i = 0; i < 5000; i++)
        if (buf[i] > peak)
            peak = buf[i];
    CHECK(peak > 1000);   /* the note is still sounding in the second call */
    music_free(m);
}


/* ---------- levels, from the driver's log ---------- */

static void test_levels(void)
{
    printf("output levels (points seen in the driver's log)\n");
    /* (velocity, patch carrier level, level the driver wrote). 127/0 -> 1, 101/0 -> 7, 95/0 -> 9, 116/3 -> 6,
     * 127/5 -> 6 are from the logs of songs 0 and 22; the others follow the same rule. */
    static const struct { uint8_t vel, patch, want; } cases[] = {
        {127, 0, 1}, {101, 0, 7}, {95, 0, 9}, {116, 3, 6}, {127, 5, 6},
        {1, 0, 63 - (63 * 129 >> 8)}, {127, 63, 63}, {64, 63, 63}, {1, 63, 63},   /* patch level 63: always silent */
    };
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        uint8_t patch[16];
        memcpy(patch, k_sine_patch, 16);
        patch[3] = cases[i].patch;
        const uint8_t track[] = {0x00, 0xc0, 0x00, 0x00, 0x90, 60, cases[i].vel, 0x00, 0xff, 0x2f, 0x00};
        trace_t tr;
        music_t *m = player(patch, 1, track, sizeof track, &tr);
        CHECK(m != NULL);
        if (!m)
            return;
        static int16_t buf[100];
        music_render(m, buf, 100);
        uint8_t v[4];
        unsigned n = writes_of(&tr, 0x43, v, 4);
        if (n != 1 || v[0] != cases[i].want)
            printf("  velocity %u, patch level %u: wrote %u, want %u\n", cases[i].vel, cases[i].patch, n ? v[0] : 0, cases[i].want);
        CHECK(n == 1 && v[0] == cases[i].want);
        music_free(m);
    }
}

/* ---------- rhythm mode, from the driver's log ---------- */

static void test_rhythm(void)
{
    printf("rhythm mode (the writes of the driver for song 0)\n");
    /* The three drum patches of song 0: bass drum, tom-tom, hi-hat. */
    static const uint8_t patches[3 * 16] = {
        0x32, 0x11, 0x44, 0x00, 0xf8, 0xf5, 0xff, 0x7f, 0x00, 0x00, 0x0e, 0, 0, 0, 0, 0,
        0x04, 0x00, 0x00, 0x00, 0xf7, 0xd6, 0xb5, 0x4f, 0x00, 0x00, 0x00, 0, 0, 0, 0, 0,
        0x31, 0x31, 0x9c, 0x80, 0xf1, 0xf7, 0xf9, 0xe6, 0x00, 0x00, 0x0e, 0, 0, 0, 0, 0,
    };
    const uint8_t track[] = {
        0x00, 0xb0, 0x67, 0x01,                      /* rhythm mode */
        0x00, 0xcb, 0x00, 0x00, 0xcd, 0x01, 0x00, 0xcf, 0x02,   /* channels 11, 13, 15 take patches 0, 1, 2 */
        0x00, 0x9b, 48, 95,                          /* bass drum, note 48 */
        0x10, 0x9b, 48, 0,                           /* off */
        0x10, 0x9d, 50, 95,                          /* tom-tom, note 50 */
        0x00, 0x9f, 15, 95,                          /* hi-hat, note 15, the same tick */
        0x05, 0x9d, 50, 0,
        0x00, 0x9f, 15, 0,
        0x60, 0xff, 0x2f, 0x00,   /* the song ends a second later: the render stops before it */
    };
    static const uint8_t want[][2] = {
        {0xbd, 0xe0},
        /* bass drum patch: both operators of voice 6 */
        {0x30, 0x32}, {0x33, 0x11}, {0x50, 0x44}, {0x70, 0xf8}, {0x73, 0xf5}, {0x90, 0xff}, {0x93, 0x7f},
        {0xf0, 0x00}, {0xf3, 0x00}, {0xc6, 0x0e},
        /* tom-tom: the modulator of voice 8; hi-hat: the modulator of voice 7 */
        {0x32, 0x04}, {0x72, 0xf7}, {0x92, 0xb5}, {0xf2, 0x00}, {0xc8, 0x00},
        {0x31, 0x31}, {0x71, 0xf1}, {0x91, 0xf9}, {0xf1, 0x00}, {0xc7, 0x0e},
        /* bass drum on, off */
        {0x53, 0x09}, {0xa6, 0x57}, {0xb6, 0x0d}, {0xbd, 0xf0},
        {0xbd, 0xe0},
        /* tom-tom on, hi-hat on (the key-on bits add up), tom-tom off, hi-hat off */
        {0x52, 0x09}, {0xa8, 0x81}, {0xb8, 0x0d}, {0xbd, 0xe4},
        {0x51, 0xa1}, {0xa7, 0x98}, {0xb7, 0x01}, {0xbd, 0xe5},
        {0xbd, 0xe1},
        {0xbd, 0xe0},
    };
    trace_t tr;
    music_t *m = player(patches, 3, track, sizeof track, &tr);
    CHECK(m != NULL);
    if (!m)
        return;
    static int16_t buf[40000];
    music_render(m, buf, 40000);   /* 37 ticks of events = 19,200 samples; the song ends at tick 133 */
    const unsigned nwant = sizeof want / sizeof want[0];
    CHECK(tr.n == nwant);
    unsigned bad = 0;
    for (unsigned i = 0; i < nwant && i < tr.n; i++)
        if (tr.reg[i] != want[i][0] || tr.val[i] != want[i][1]) {
            if (bad++ < 4)
                printf("  write %u: %02x=%02x, want %02x=%02x\n", i, tr.reg[i], tr.val[i], want[i][0], want[i][1]);
        }
    CHECK(bad == 0);
    CHECK(music_ignored_events(m) == 0);
    music_free(m);

    /* A snare and a cymbal use the modulator bytes of their patch on one operator each (0x14 and 0x15); their
     * bits are 0x08 and 0x02. Seen in the logs of song 3 (tools/check_music.sh compares it whole); here the shape. */
    const uint8_t sc[] = {
        0x00, 0xb0, 0x67, 0x01, 0x00, 0xcc, 0x01, 0x00, 0xce, 0x01,
        0x00, 0x9c, 40, 100, 0x01, 0x9c, 40, 0, 0x00, 0x9e, 40, 100, 0x01, 0x9e, 40, 0, 0x60, 0xff, 0x2f, 0x00,
    };
    m = player(patches, 3, sc, sizeof sc, &tr);
    CHECK(m != NULL);
    if (!m)
        return;
    music_render(m, buf, 40000);
    uint8_t v[16];
    unsigned nbd = writes_of(&tr, 0xbd, v, 16);
    CHECK(nbd == 5);   /* mode, snare on, snare off, cymbal on, cymbal off */
    CHECK(nbd == 5 && v[0] == 0xe0 && v[1] == 0xe8 && v[2] == 0xe0 && v[3] == 0xe2 && v[4] == 0xe0);
    CHECK(writes_of(&tr, 0x34, v, 4) == 1 && v[0] == 0x04);   /* snare: operator 0x14, patch 1's first byte */
    CHECK(writes_of(&tr, 0x35, v, 4) == 1 && v[0] == 0x04);   /* cymbal: operator 0x15 */
    CHECK(writes_of(&tr, 0x54, v, 4) == 1);                   /* the snare's level, written at each note */
    CHECK(writes_of(&tr, 0x55, v, 4) == 1);
    /* pitch: the snare shares voice 7 with the hi-hat, the cymbal voice 8 with the tom-tom; note 40 = f-number
     * 0x1b0 (432) block 2: A0 = 0xb0, B0 = 0x09 without the key bit */
    CHECK(writes_of(&tr, 0xa7, v, 4) == 1 && v[0] == 0xb0);
    CHECK(writes_of(&tr, 0xb7, v, 4) == 1 && v[0] == 0x09);
    CHECK(writes_of(&tr, 0xa8, v, 4) == 1 && v[0] == 0xb0);
    CHECK(writes_of(&tr, 0xb8, v, 4) == 1 && v[0] == 0x09);
    CHECK(writes_of(&tr, 0xa6, v, 4) == 0);                   /* the bass drum's voice stays quiet */
    music_free(m);
}

/* In rhythm mode only voices 0..5 are melodic: a seventh held note cuts the oldest instead of using a drum voice. */
static void test_rhythm_voices(void)
{
    printf("rhythm mode leaves six melodic voices\n");
    uint8_t track[4 + 3 + 7 * 4 + 4];
    unsigned k = 0;
    track[k++] = 0x00; track[k++] = 0xb0; track[k++] = 0x67; track[k++] = 0x01;
    track[k++] = 0x00; track[k++] = 0xc0; track[k++] = 0x00;
    for (unsigned ch = 0; ch < 7; ch++) {
        track[k++] = ch ? 0x01 : 0x00;
        track[k++] = (uint8_t)(0x90 | ch);
        track[k++] = (uint8_t)(60 + ch);
        track[k++] = 100;
    }
    track[k++] = 0x60; track[k++] = 0xff; track[k++] = 0x2f; track[k++] = 0x00;
    trace_t tr;
    music_t *m = player(k_sine_patch, 1, track, k, &tr);
    CHECK(m != NULL);
    if (!m)
        return;
    static int16_t buf[6000];
    music_render(m, buf, 4000);   /* seven notes, a tick (518 samples) apart */
    uint8_t v[8];
    CHECK(writes_of(&tr, 0xb6, v, 8) == 0);   /* voice 6 belongs to the bass drum: no melodic note used it */
    unsigned nb0 = writes_of(&tr, 0xb0, v, 8);
    CHECK(nb0 == 3 && (v[0] & 0x20) && !(v[1] & 0x20) && (v[2] & 0x20));   /* voice 0 was cut for the seventh note */
    music_free(m);
}

/* ---------- voices, from the driver's code ---------- */

static void test_voices(void)
{
    printf("voice allocation\n");
    /* ch1 first (gets voice 0, not voice 1), then ch0 (voice 1); both released; ch1 again reuses its voice
     * without a reload; a new channel takes the next free voice. */
    const uint8_t track[] = {
        0x00, 0xc1, 0x00, 0x00, 0xc0, 0x00, 0x00, 0xc2, 0x00,
        0x00, 0x91, 60, 100,
        0x01, 0x90, 62, 100,
        0x01, 0x81, 60, 0, 0x00, 0x80, 62, 0,
        0x01, 0x91, 60, 100,
        0x01, 0x92, 64, 100,
        0x60, 0xff, 0x2f, 0x00,
    };
    trace_t tr;
    music_t *m = player(k_sine_patch, 1, track, sizeof track, &tr);
    CHECK(m != NULL);
    if (!m)
        return;
    static int16_t buf[6000];
    music_render(m, buf, 3000);
    uint8_t v[8];
    CHECK(writes_of(&tr, 0x20, v, 8) == 1);   /* voice 0 loaded once: the second note on channel 1 reused it */
    CHECK(writes_of(&tr, 0x21, v, 8) == 1);   /* voice 1 (channel 0) */
    CHECK(writes_of(&tr, 0x22, v, 8) == 1);   /* voice 2 (channel 2) */
    unsigned on0 = 0, on1 = 0, on2 = 0;
    for (unsigned i = 0; i < tr.n && i < TRACE_MAX; i++) {
        if (tr.reg[i] == 0xb0 && (tr.val[i] & 0x20)) on0++;
        if (tr.reg[i] == 0xb1 && (tr.val[i] & 0x20)) on1++;
        if (tr.reg[i] == 0xb2 && (tr.val[i] & 0x20)) on2++;
    }
    CHECK(on0 == 2 && on1 == 1 && on2 == 1);
    music_free(m);

    printf("a program change reloads held voices, and frees released ones\n");
    uint8_t two[32];
    memcpy(two, k_sine_patch, 16);
    memcpy(two + 16, k_sine_patch, 16);
    two[16] = 0x31;   /* patch 1 differs in its first byte */
    const uint8_t held[] = {0x00, 0xc0, 0x00, 0x00, 0x90, 60, 100, 0x01, 0xc0, 0x01, 0x01, 0x80, 60, 0, 0x60, 0xff, 0x2f, 0x00};
    m = player(two, 2, held, sizeof held, &tr);
    CHECK(m != NULL);
    if (!m)
        return;
    music_render(m, buf, 3000);
    unsigned n = writes_of(&tr, 0x20, v, 8);
    CHECK(n == 2 && v[0] == 0x21 && v[1] == 0x31);   /* reloaded at once, while the note is held */
    music_free(m);

    const uint8_t freed[] = {0x00, 0xc0, 0x00, 0x00, 0x90, 60, 100, 0x01, 0x80, 60, 0, 0x01, 0xc0, 0x01,
                             0x01, 0x90, 62, 100, 0x60, 0xff, 0x2f, 0x00};
    m = player(two, 2, freed, sizeof freed, &tr);
    CHECK(m != NULL);
    if (!m)
        return;
    music_render(m, buf, 3000);
    n = writes_of(&tr, 0x20, v, 8);
    CHECK(n == 2 && v[0] == 0x21 && v[1] == 0x31);   /* the released voice was forgotten: the next note loads the new patch */
    music_free(m);

    printf("the oldest voice is cut when all nine are held\n");
    uint8_t steal[4 + 10 * 4 + 4];
    unsigned k = 0;
    steal[k++] = 0x00; steal[k++] = 0xc0; steal[k++] = 0x00;
    k = 0;
    uint8_t big[3 + 10 * 4 + 4];
    big[k++] = 0x00; big[k++] = 0xc0; big[k++] = 0x00;
    for (unsigned ch = 0; ch < 10; ch++) {   /* ten channels, one note each, a tick apart, none released */
        big[k++] = ch ? 0x01 : 0x00;
        big[k++] = (uint8_t)(0x90 | ch);
        big[k++] = (uint8_t)(60 + ch);
        big[k++] = 100;
    }
    big[k++] = 0x60; big[k++] = 0xff; big[k++] = 0x2f; big[k++] = 0x00;
    (void)steal;
    m = player(k_sine_patch, 1, big, k, &tr);
    CHECK(m != NULL);
    if (!m)
        return;
    music_render(m, buf, 6000);   /* ten notes, a tick (518 samples) apart */
    /* Channel 9 found no free voice: voice 0, the oldest, was keyed off (pitch registers without the key bit) and
     * took the new note. Its channel 9 differs from the old owner, so the patch was loaded again (two loads). */
    unsigned nb0 = writes_of(&tr, 0xb0, v, 8);
    CHECK(nb0 == 3);
    CHECK(nb0 == 3 && (v[0] & 0x20) && !(v[1] & 0x20) && (v[2] & 0x20));
    CHECK(writes_of(&tr, 0x20, v, 8) == 2);
    CHECK(music_ignored_events(m) == 0);
    music_free(m);
}

/* ---------- the real song ---------- */

typedef struct {
    unsigned on, off, first_pass_only;
    uint64_t last_sample;
    unsigned loops_seen;
} count_t;

static void count_cb(void *user, uint64_t sample, uint8_t reg, uint8_t value)
{
    count_t *c = user;
    if (sample < c->last_sample)
        c->loops_seen++;   /* the position restarts at 0 at each pass */
    c->last_sample = sample;
    if (c->loops_seen == 0 && reg >= 0xb0 && reg <= 0xb8) {
        if (value & 0x20)
            c->on++;
        else
            c->off++;
    }
}

static uint32_t hash_i16(uint32_t h, const int16_t *a, size_t n)
{
    for (size_t i = 0; i < n; i++)
        h = (h ^ (uint16_t)a[i]) * 16777619u;
    return h;
}

static uint32_t render_hash(size_t total, size_t chunk, int16_t *peak_out)
{
    music_t *m = music_load_intro();
    if (!m)
        return 0;
    int16_t *buf = malloc(chunk * sizeof *buf);
    if (!buf) {
        music_free(m);
        return 0;
    }
    uint32_t h = 2166136261u;
    int16_t peak = 0;
    for (size_t done = 0; done < total;) {
        size_t n = total - done < chunk ? total - done : chunk;
        music_render(m, buf, n);
        h = hash_i16(h, buf, n);
        for (size_t i = 0; i < n; i++) {
            int16_t a = buf[i] < 0 ? (int16_t)-buf[i] : buf[i];
            if (a > peak)
                peak = a;
        }
        done += n;
    }
    if (peak_out)
        *peak_out = peak;
    free(buf);
    music_free(m);
    return h;
}

static void test_intro_song(void)
{
    printf("the intro song (MUC.ALL)\n");
    music_t *m = music_load_intro();
    if (!m) {
        printf("  skipped: MUC.ALL not found\n");
        skipped++;
        return;
    }
    count_t c;
    memset(&c, 0, sizeof c);
    music_set_trace(m, count_cb, &c);

    /* The song ends at tick 12,599 = 131.2 s; render 135 s to cross the loop point once. */
    size_t total = (size_t)music_rate() * 135;
    size_t chunk = 4096;
    int16_t *buf = malloc(chunk * sizeof *buf);
    CHECK(buf != NULL);
    if (!buf) {
        music_free(m);
        return;
    }
    for (size_t done = 0; done < total; done += chunk)
        music_render(m, buf, chunk);
    free(buf);

    /* tools/cmf_behaviour.py: 2,402 notes (the survey's 4,804 counts note-ons plus note-offs), none
     * overlapping, every off matches a held note. */
    if (c.on != 2402 || c.off != 2402)
        printf("  key-ons %u, key-offs %u, loop seen %u times (want 2402 each)\n", c.on, c.off, c.loops_seen);
    CHECK(c.on == 2402);
    CHECK(c.off == 2402);
    CHECK(music_loops(m) == 1);
    CHECK(music_ignored_events(m) == 0);
    music_free(m);

    /* Rendering is deterministic and does not depend on the chunk size. */
    size_t part = (size_t)music_rate() * 20;
    int16_t pk1 = 0, pk2 = 0;
    uint32_t h1 = render_hash(part, 4096, &pk1);
    uint32_t h2 = render_hash(part, 997, &pk2);
    uint32_t h3 = render_hash(part, 1, &pk2);   /* sample at a time */
    CHECK(h1 != 0 && h1 == h2 && h1 == h3);
    printf("  20 s peak amplitude %d of 32767\n", pk1);
    CHECK(pk1 > 3000);    /* clearly audible */
    CHECK(pk1 < 32000);   /* and not clipping */
}

int main(int argc, char **argv)
{
    assets_set_dir(argc > 1 ? argv[1] : "original_bin");
    setvbuf(stdout, NULL, _IONBF, 0);   /* if a test hangs, its name must already be on screen */
    test_registers();
    test_velocity_and_keyscale();
    test_off_matching();
    test_velocity_zero_is_off();
    test_ignored();
    test_pitch();
    test_pitch_registers();
    test_levels();
    test_rhythm();
    test_rhythm_voices();
    test_voices();
    test_transpose();
    test_loop();
    test_zero_length();
    test_intro_song();
    printf("%s: %d failure(s), %d skipped\n", failures ? "FAILED" : "ok", failures, skipped);
    return failures ? 1 : 0;
}
