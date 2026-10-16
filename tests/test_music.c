/*
 * Tests for the CMF music player (src/music.c) and the OPL2 wrapper (src/opl.c).
 *
 * Independent references used here:
 *  - the standard AdLib value for A4 (440 Hz): f-number 0x244, block 4, i.e. register 0xA0 = 0x44
 *    and 0xB0 = 0x32 with the key on;
 *  - a pitch measured on the rendered audio itself (zero crossings of a near-sine patch), which
 *    ties the f-number formula to the chip's real output rate;
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

typedef struct {
    uint64_t when[64];
    uint8_t reg[64], val[64];
    unsigned n;
} trace_t;

static void trace_cb(void *user, uint64_t sample, uint8_t reg, uint8_t value)
{
    trace_t *t = user;
    if (t->n < 64) {
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
    for (unsigned i = 0; i < t->n && i < 64; i++)
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
    /* The patch lands on channel 0's operators (modulator 0x00, carrier 0x03). */
    CHECK(writes_of(&tr, 0x20, v, 8) == 1 && v[0] == 0x21);
    CHECK(writes_of(&tr, 0x23, v, 8) == 1 && v[0] == 0x21);
    CHECK(writes_of(&tr, 0x40, v, 8) == 1 && v[0] == 0x3f);
    CHECK(writes_of(&tr, 0x60, v, 8) == 1 && v[0] == 0xf0);
    CHECK(writes_of(&tr, 0x83, v, 8) == 1 && v[0] == 0x0f);
    CHECK(writes_of(&tr, 0xe0, v, 8) == 1 && v[0] == 0x00);
    CHECK(writes_of(&tr, 0xc0, v, 8) == 1 && v[0] == 0x00);
    /* The carrier level is written twice: by the patch load (0x00), then by the note-on, where
     * velocity 100 gives 63 - 50 = 13, the key-scale bits of the patch (0) kept. */
    unsigned n43 = writes_of(&tr, 0x43, v, 8);
    CHECK(n43 == 2);
    CHECK(n43 == 2 && v[0] == 0x00 && v[1] == 0x0d);
    /* A4: the standard AdLib value is f-number 0x244, block 4. */
    CHECK(writes_of(&tr, 0xa0, v, 8) == 1 && v[0] == 0x44);
    unsigned nb = writes_of(&tr, 0xb0, v, 8);
    CHECK(nb == 2);
    CHECK(nb == 2 && v[0] == 0x32);   /* key on  | block 4 << 2 | fnum high 2 */
    CHECK(nb == 2 && v[1] == 0x12);   /* key off, same pitch bits */
    /* The note-off is at tick 96 = 96 * 49,716 / 96 samples. */
    for (unsigned i = 0; i < tr.n && i < 64; i++)
        if (tr.reg[i] == 0xb0 && tr.val[i] == 0x12)
            CHECK(tr.when[i] == 49716);
    music_free(m);
}

static void test_velocity_and_keyscale(void)
{
    printf("velocity -> carrier level, key-scale kept\n");
    uint8_t patch[16];
    memcpy(patch, k_sine_patch, 16);
    patch[3] = 0x80 | 0x2a;   /* key-scale 2 and a patch level that must be replaced */
    const uint8_t track[] = {0x00, 0xc0, 0x00, 0x00, 0x90, 60, 76, 0x00, 0x90, 62, 1, 0x00, 0xff, 0x2f, 0x00};
    trace_t tr;
    music_t *m = player(patch, 1, track, sizeof track, &tr);
    CHECK(m != NULL);
    if (!m)
        return;
    static int16_t buf[1000];
    music_render(m, buf, 1000);
    uint8_t v[8];
    unsigned n = writes_of(&tr, 0x43, v, 8);
    CHECK(n == 3);
    CHECK(n == 3 && v[0] == (0x80 | 0x2a));        /* the patch load: its own level */
    CHECK(n == 3 && v[1] == (0x80 | (63 - 38)));   /* velocity 76 -> 63 - 38 = 25 */
    CHECK(n == 3 && v[2] == (0x80 | 63));          /* velocity 1  -> 63 - 0 = 63, almost silent */
    music_free(m);
}

static void test_off_matching(void)
{
    printf("a note-off only releases the note the channel is playing\n");
    /* on 60; on 62 (releases 60 first); off 60 (stale: must be ignored); off 62. */
    const uint8_t track[] = {0x00, 0xc0, 0x00, 0x00, 0x90, 60, 100, 0x01, 0x90, 62, 100,
                             0x01, 0x80, 60, 0, 0x01, 0x80, 62, 0, 0x30, 0xff, 0x2f, 0x00};
    trace_t tr;
    music_t *m = player(k_sine_patch, 1, track, sizeof track, &tr);
    CHECK(m != NULL);
    if (!m)
        return;
    static int16_t buf[3000];
    music_render(m, buf, 3000);
    uint8_t v[16];
    unsigned n = writes_of(&tr, 0xb0, v, 16);
    /* key-on(60), key-off(60 by the new note), key-on(62), key-off(62): the stale off writes none. */
    CHECK(n == 4);
    CHECK(n == 4 && (v[0] & 0x20) && !(v[1] & 0x20) && (v[2] & 0x20) && !(v[3] & 0x20));
    /* The count alone cannot tell a stale off that is ignored from one that releases the new note
     * early (the real off would then be the skipped one). The last key-off must come at the real
     * off, tick 3, not at the stale one, tick 2. */
    unsigned last = 0;
    for (unsigned i = 0; i < tr.n && i < 64; i++)
        if (tr.reg[i] == 0xb0)
            last = i;
    CHECK(tr.when[last] == 3ull * 49716 / 96);
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
    printf("events the player does not act on are counted\n");
    /* program 0; a note on channel 12 (no OPL channel in melody mode); a pitch bend on channel 0;
     * controller 0x10 on channel 0; then a note on channel 0 that must still play. */
    const uint8_t track[] = {0x00, 0xc0, 0x00, 0x00, 0x9c, 60, 100, 0x00, 0xe0, 0x00, 0x40,
                             0x00, 0xb0, 0x10, 5, 0x00, 0x90, 60, 100, 0x00, 0xff, 0x2f, 0x00};
    trace_t tr;
    music_t *m = player(k_sine_patch, 1, track, sizeof track, &tr);
    CHECK(m != NULL);
    if (!m)
        return;
    static int16_t buf[1000];
    music_render(m, buf, 1000);
    CHECK(music_ignored_events(m) == 3);
    uint8_t v[8];
    CHECK(writes_of(&tr, 0xb0, v, 8) == 1);   /* only the channel-0 note keyed on */
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
    static const struct { uint8_t note; double hz; } cases[] = {
        {57, 220.0}, {69, 440.0}, {72, 523.2511}, {81, 880.0}, {45, 110.0}, {93, 1760.0},
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
        double err = fabs(hz - cases[i].hz) / cases[i].hz;
        if (err > 0.005)
            printf("  note %u: measured %.3f Hz, want %.3f\n", cases[i].note, hz, cases[i].hz);
        CHECK(err < 0.005);
        free(buf);
        music_free(m);
    }
}

static void test_transpose(void)
{
    printf("transpose controller (1/128 semitone)\n");
    /* Controller 0x68 = 127 raises by 127/128 semitone: ~ +5.8%. */
    const uint8_t track[] = {0x00, 0xc0, 0x00, 0x00, 0xb0, 0x68, 127, 0x00, 0x90, 69, 127, 0x00, 0xff, 0x2f, 0x00};
    music_t *m = player(k_sine_patch, 1, track, sizeof track, NULL);
    CHECK(m != NULL);
    if (!m)
        return;
    size_t n = music_rate();
    int16_t *buf = malloc(n * sizeof *buf);
    if (!buf) {
        music_free(m);
        return;
    }
    music_render(m, buf, n);
    double want = 440.0 * pow(2.0, 127.0 / 128.0 / 12.0);
    double hz = measure_hz(buf, n, music_rate());
    CHECK(fabs(hz - want) / want < 0.005);
    free(buf);
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
    test_transpose();
    test_loop();
    test_zero_length();
    test_intro_song();
    printf("%s: %d failure(s), %d skipped\n", failures ? "FAILED" : "ok", failures, skipped);
    return failures ? 1 : 0;
}
