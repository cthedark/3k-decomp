/*
 * Tests for the sound: the VOC and ADPCM decoder, the TAB.TAB lookup, and how the subtitle player
 * and slide 0 use a sound sink. The sink here is a fake whose "playing" flag the test controls,
 * so the scene logic is checked without a sound device or a clock.
 *
 * Expected numbers about the data files (entry counts, clip lengths) are facts found with
 * tools/voc_extract.py and recorded in docs/formats.md; no game content is stored here. Tests that
 * need the game files are skipped when they are missing.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"
#include "audio.h"
#include "intro0.h"
#include "screen.h"
#include "subs.h"
#include "voc.h"
#include "voice.h"

static int failures, skipped;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
            failures++;                                                          \
        }                                                                        \
    } while (0)

/* ---- the fake sink ---------------------------------------------------------------------- */
static struct {
    int plays, stops, playing;
    size_t frames;
    unsigned rate;
} fake;

static void fk_play(const int16_t *pcm, size_t frames, unsigned rate)
{
    (void)pcm;
    fake.plays++;
    fake.frames = frames;
    fake.rate = rate;
    fake.playing = 1;
}
static int fk_playing(void) { return fake.playing; }
static void fk_stop(void)
{
    fake.stops++;
    fake.playing = 0;
}
static const audio_sink fake_sink = {fk_play, fk_playing, fk_stop, NULL, NULL};

static void fake_reset(void) { memset(&fake, 0, sizeof fake); }

/* ---- decoder ---------------------------------------------------------------------------- */
static void test_adpcm(void)
{
    printf("ADPCM 4-bit\n");
    uint8_t out[8];
    /* Start 128, step 0. 0x7: +7 -> 135, step 1. 0xF: -((7<<1)+1) = -15 -> 120, step 2.
     * 0x0: +((0<<2)+2) = +2 -> 122, step 1. 0x8: -((0<<1)+1) = -1 -> 121, step 0. */
    const uint8_t a[] = {128, 0x7f, 0x08};
    CHECK(voc_adpcm4_decode(a, sizeof a, out) == 4);
    CHECK(out[0] == 135 && out[1] == 120 && out[2] == 122 && out[3] == 121);
    /* Clamping at both ends. */
    const uint8_t hi[] = {250, 0x77};
    CHECK(voc_adpcm4_decode(hi, sizeof hi, out) == 2);
    CHECK(out[0] == 255 && out[1] == 255);
    const uint8_t lo[] = {2, 0xff};
    CHECK(voc_adpcm4_decode(lo, sizeof lo, out) == 2);
    CHECK(out[0] == 0 && out[1] == 0);
    /* The step stops at 3 (delta (7<<3)+4 = 60): 128, +7, +15, +30, +60, then clamped. */
    const uint8_t big[] = {128, 0x77, 0x77, 0x77};
    CHECK(voc_adpcm4_decode(big, sizeof big, out) == 6);
    CHECK(out[0] == 135 && out[1] == 150 && out[2] == 180 && out[3] == 240 && out[4] == 255 && out[5] == 255);
    /* Zero nibbles walk the step back down: from step 3 the deltas are 4, 2, 1, 0. */
    const uint8_t down[] = {100, 0x77, 0x77, 0x00, 0x00, 0x00};
    uint8_t d[10];
    CHECK(voc_adpcm4_decode(down, sizeof down, d) == 10);
    CHECK(d[3] == 100 + 7 + 15 + 30 + 60 && d[4] == d[3] + 4 && d[5] == d[4] + 2 && d[6] == d[5] + 1 && d[7] == d[6]);
    CHECK(voc_adpcm4_decode(a, 0, out) == 0);
    CHECK(voc_adpcm4_decode(a, 1, out) == 0);
}

/* A VOC file with the given blocks appended after the 26-byte header. */
static size_t make_voc(uint8_t *f, const uint8_t *blocks, size_t n)
{
    memcpy(f, "Creative Voice File\x1a", 20);
    f[20] = 26;
    f[21] = 0;
    f[22] = 0x0a; f[23] = 0x01; f[24] = 0x29; f[25] = 0x11;
    memcpy(f + 26, blocks, n);
    return 26 + n;
}

static void test_voc(void)
{
    printf("VOC files\n");
    uint8_t f[128];
    voc_clip c;

    /* Type 1, size 5 (3 bytes: 5,0,0): divisor 156 = 10,000 Hz, codec 0, three samples; then the end. */
    const uint8_t pcm[] = {1, 5, 0, 0, 156, 0, 0x80, 0xff, 0x00, 0};
    size_t n = make_voc(f, pcm, sizeof pcm);
    CHECK(voc_decode(f, n, &c) == 0);
    CHECK(c.rate == 10000 && c.frames == 3);
    CHECK(c.pcm[0] == 0 && c.pcm[1] == 127 * 256 && c.pcm[2] == -128 * 256);
    voc_free(&c);

    /* A continuation block (type 2) extends the clip. */
    const uint8_t cont[] = {1, 4, 0, 0, 156, 0, 0x80, 0x90, 2, 2, 0, 0, 0x70, 0x80, 0};
    n = make_voc(f, cont, sizeof cont);
    CHECK(voc_decode(f, n, &c) == 0 && c.frames == 4 && c.pcm[2] == -16 * 256);
    voc_free(&c);

    /* ADPCM: the first data byte is the start level and gives no sample. 3 data bytes -> 4 samples. */
    const uint8_t ad[] = {1, 5, 0, 0, 156, 1, 128, 0x7f, 0x08, 0};
    n = make_voc(f, ad, sizeof ad);
    CHECK(voc_decode(f, n, &c) == 0 && c.frames == 4);
    CHECK(c.pcm[0] == (135 - 128) * 256 && c.pcm[3] == (121 - 128) * 256);
    voc_free(&c);

    /* Things that must be refused, not guessed at. */
    n = make_voc(f, pcm, sizeof pcm);
    f[0] = 'X';
    CHECK(voc_decode(f, n, &c) != 0);
    f[0] = 'C';
    CHECK(voc_decode(f, 10, &c) != 0);             /* too short */
    CHECK(voc_decode(f, n - 4, &c) != 0);          /* block runs past the end */
    const uint8_t codec2[] = {1, 5, 0, 0, 156, 2, 0x80, 0xff, 0x00, 0};
    n = make_voc(f, codec2, sizeof codec2);
    CHECK(voc_decode(f, n, &c) != 0);              /* codec 2 (2.6-bit ADPCM) is not used */
    const uint8_t silence[] = {3, 3, 0, 0, 0x10, 0x00, 156, 0};
    n = make_voc(f, silence, sizeof silence);
    CHECK(voc_decode(f, n, &c) != 0);              /* silence blocks are not used either */
    const uint8_t none[] = {0};
    n = make_voc(f, none, sizeof none);
    CHECK(voc_decode(f, n, &c) != 0);              /* no sound data */
}

/* ---- TAB.TAB ---------------------------------------------------------------------------- */
static void test_table_synthetic(void)
{
    printf("TAB.TAB lookup (synthetic)\n");
    uint8_t tab[0x500];
    memset(tab, 0, sizeof tab);
    /* slide 2: three entries at 0x3cf, lengths 100, 200, 300 */
    uint8_t *e = tab + 0x3cf;
    const size_t len[3] = {100, 200, 300};
    size_t off = 0;
    for (int i = 0; i < 3; i++) {
        e[5 * i] = off & 255; e[5 * i + 1] = off >> 8 & 255; e[5 * i + 2] = off >> 16;
        e[5 * i + 3] = len[i] & 255; e[5 * i + 4] = len[i] >> 8;
        off += len[i];
    }
    size_t o[3], l[3];
    CHECK(voice_table_entries(tab, sizeof tab, 2, 3, 600, o, l) == 0);
    CHECK(o[0] == 0 && o[1] == 100 && o[2] == 300 && l[2] == 300);
    CHECK(voice_table_entries(tab, sizeof tab, 2, 3, 601, o, l) != 0);   /* does not tile the file */
    CHECK(voice_table_entries(tab, sizeof tab, 2, 2, 600, o, l) != 0);
    CHECK(voice_table_entries(tab, sizeof tab, 0, 3, 600, o, l) != 0);   /* no slide 0 voice */
    CHECK(voice_table_entries(tab, sizeof tab, 7, 3, 600, o, l) != 0);
    CHECK(voice_table_entries(tab, 0x3d0, 2, 3, 600, o, l) != 0);        /* table cut short */
}

static int have_voice_files(void)
{
    size_t n;
    uint8_t *d = assets_read("tab.tab", &n);
    free(d);
    return d != NULL;
}

static void test_real_voice(void)
{
    printf("voice-over files\n");
    if (!have_voice_files()) {
        printf("  skipped (no game files)\n");
        skipped++;
        return;
    }
    static const unsigned lines[7] = {0, 8, 3, 10, 5, 8, 11};   /* clips per slide (docs/formats.md) */
    static const int total_cs[7] = {0, 3178, 1139, 3510, 1659, 2854, 3407};   /* sum of clip lengths, 1/100 s */
    for (unsigned s = 1; s <= 6; s++) {
        voice_bank_t *b = voice_bank_load(s, lines[s]);
        CHECK(b != NULL);
        if (!b)
            continue;
        CHECK(voice_bank_count(b) == lines[s]);
        double secs = 0;
        int rates_ok = 1;
        for (unsigned i = 0; i < lines[s]; i++) {
            const voc_clip *c = voice_bank_clip(b, i);
            if (!c || c->rate != 13158)
                rates_ok = 0;
            else
                secs += (double)c->frames / c->rate;
        }
        CHECK(rates_ok);
        CHECK((int)(secs * 100 + 0.5) == total_cs[s]);
        CHECK(voice_bank_clip(b, lines[s]) == NULL);
        voice_bank_free(b);
        CHECK(voice_bank_load(s, lines[s] + 1) == NULL);          /* a count that does not tile the file */
    }
}

/* ---- the subtitle player with a sink ---------------------------------------------------- */
static screen_t scr;

/* Run the cross-fade of the current line to its end; returns the number of steps taken. */
static int fade_in(subs_t *s, unsigned *tick)
{
    int n = 0;
    while (subs_state_now(s) == SUBS_PASS && n < 100) {
        subs_step(s, &scr, (*tick)++, 0);
        n++;
    }
    return n;
}

static void test_subs_with_voice(void)
{
    printf("subtitle player with a voice\n");
    if (!have_voice_files()) {
        printf("  skipped (no game files)\n");
        skipped++;
        return;
    }
    static uint8_t lines[600 * 3];
    memset(lines, 0, sizeof lines);
    size_t tl = 0, vl = 0;
    uint8_t *tab = assets_read("tab.tab", &tl);
    uint8_t *v = assets_read("story2_v", &vl);
    size_t offs[3], lens[3];
    CHECK(tab && v && voice_table_entries(tab, tl, 2, 3, vl, offs, lens) == 0);
    free(tab);
    free(v);

    audio_set_sink(&fake_sink);
    fake_reset();
    subs_t *s = subs_create(lines, sizeof lines);
    CHECK(s && subs_load_voice(s, 2) == 0);
    subs_begin(s, &scr);
    unsigned tick = 1000;

    /* Line 1: the clip starts when the cross-fade ends, not before; the hold lasts while it plays,
     * however many ticks go by. A clip's samples are its entry minus 26 (file header) + 4 (block
     * header) + 2 (divisor, codec) + 1 (end byte) = 33 bytes. */
    int steps = fade_in(s, &tick);
    CHECK(steps == 14);
    CHECK(subs_state_now(s) == SUBS_WAIT && fake.plays == 1);
    CHECK(fake.rate == 13158 && fake.frames == lens[0] - 33);
    for (int i = 0; i < 50; i++) {
        tick += 100;   /* far past the 74 ticks of the timed hold */
        CHECK(subs_step(s, &scr, tick, 0) == SUBS_WAIT);
    }
    fake.playing = 0;   /* the clip ends */
    CHECK(subs_step(s, &scr, tick, 0) == SUBS_PASS);
    CHECK(fake.stops == 0);

    /* Line 2 plays its own clip; Space cuts it short and silences it. */
    fade_in(s, &tick);
    CHECK(fake.plays == 2 && fake.frames == lens[1] - 33);
    CHECK(subs_step(s, &scr, tick, 0) == SUBS_WAIT);
    CHECK(subs_step(s, &scr, tick, 1) == SUBS_PASS);
    CHECK(fake.stops == 1 && !fake.playing);

    /* Line 3 is the last: when its clip ends the player goes on to the fade-out. */
    fade_in(s, &tick);
    CHECK(fake.plays == 3 && fake.frames == lens[2] - 33);
    fake.playing = 0;
    CHECK(subs_step(s, &scr, tick, 0) == SUBS_OUT);
    subs_free(s);

    /* Freed in the middle of a clip: it is silenced. */
    fake_reset();
    s = subs_create(lines, sizeof lines);
    CHECK(s && subs_load_voice(s, 2) == 0);
    subs_begin(s, &scr);
    fade_in(s, &tick);
    CHECK(fake.playing);
    subs_free(s);
    CHECK(!fake.playing);

    /* No sink: nothing is loaded and the hold is the original's 74 ticks. */
    audio_set_sink(NULL);
    fake_reset();
    s = subs_create(lines, sizeof lines);
    CHECK(s && subs_load_voice(s, 2) == 0);
    subs_begin(s, &scr);
    tick = 5000;
    fade_in(s, &tick);
    CHECK(subs_state_now(s) == SUBS_WAIT && fake.plays == 0);
    unsigned t0 = tick - 1;   /* the tick at which the last pass was made */
    CHECK(subs_step(s, &scr, t0 + 73, 0) == SUBS_WAIT);
    CHECK(subs_step(s, &scr, t0 + 74, 0) == SUBS_PASS);
    subs_free(s);
}

/* ---- slide 0's sound ---------------------------------------------------------------------- */
static void test_intro0_sound(void)
{
    printf("slide 0 sound\n");
    size_t n;
    uint8_t *probe = assets_read("story0_1.lcz", &n);
    free(probe);
    if (!probe) {
        printf("  skipped (no game files)\n");
        skipped++;
        return;
    }

    /* Replay off (the default): the clip starts on the 20th pass, once. */
    audio_set_sink(&fake_sink);
    fake_reset();
    intro0_t *t = intro0_create(&scr);
    CHECK(t != NULL);
    if (!t)
        return;
    intro0_set_music_replay(t, 0);   /* the single-play case; replay is the default */
    while (intro0_phase_now(t) == INTRO0_FADE_IN)
        intro0_step(t, &scr, 0);
    int started_at = -1;
    for (int pass = 0; pass < 60; pass++) {
        int before = fake.plays;
        intro0_step(t, &scr, 0);
        if (fake.plays > before && started_at < 0)
            started_at = pass;
    }
    CHECK(started_at == 19);   /* the 20th pass (0x105e6: the counter passes 0x14) */
    CHECK(fake.plays == 1 && fake.rate == 6061 && fake.frames == 126458);

    /* While it plays nothing more happens. After it ends, with replay off, nothing restarts. */
    for (int i = 0; i < 100; i++)
        intro0_step(t, &scr, 0);
    CHECK(fake.plays == 1);
    fake.playing = 0;
    for (int i = 0; i < 100; i++)
        intro0_step(t, &scr, 0);
    CHECK(fake.plays == 1);
    intro0_free(t);

    /* Replay (the default, as in the original): it starts again 20 passes after the pass that
     * sees it stopped. */
    fake_reset();
    t = intro0_create(&scr);
    while (intro0_phase_now(t) == INTRO0_FADE_IN)
        intro0_step(t, &scr, 0);
    for (int i = 0; i < 20; i++)
        intro0_step(t, &scr, 0);
    CHECK(fake.plays == 1);
    fake.playing = 0;
    for (int i = 0; i < 19; i++)
        intro0_step(t, &scr, 0);
    CHECK(fake.plays == 1);
    intro0_step(t, &scr, 0);
    CHECK(fake.plays == 2);
    intro0_free(t);

    /* At the end of the loop with a clip still playing the scene waits for it, with the screen
     * as it is; with no sink it goes straight to the fade-out. */
    fake_reset();
    t = intro0_create(&scr);
    intro0_set_music_replay(t, 0);
    while (intro0_phase_now(t) == INTRO0_FADE_IN)
        intro0_step(t, &scr, 0);
    unsigned tick = 0;
    for (int guard = 0; intro0_phase_now(t) == INTRO0_RUN && guard < 100000; guard++)
        intro0_step(t, &scr, tick += 1);   /* one tick per pass: every hold ends */
    CHECK(intro0_phase_now(t) == INTRO0_MUSIC_WAIT && fake.playing);
    for (int i = 0; i < 20; i++)
        CHECK(intro0_step(t, &scr, tick) == INTRO0_MUSIC_WAIT);
    fake.playing = 0;
    CHECK(intro0_step(t, &scr, tick) == INTRO0_FADE_OUT);
    intro0_free(t);

    audio_set_sink(NULL);
    t = intro0_create(&scr);
    while (intro0_phase_now(t) == INTRO0_FADE_IN)
        intro0_step(t, &scr, 0);
    for (int guard = 0; intro0_phase_now(t) == INTRO0_RUN && guard < 100000; guard++)
        intro0_step(t, &scr, tick += 1);
    CHECK(intro0_phase_now(t) == INTRO0_FADE_OUT);
    intro0_free(t);
}

/* ---- the music entry points of the sink ---------------------------------------------------- */
static struct { int starts, stops; audio_render_fn fn; void *user; unsigned rate; } mus;
static void mu_start(audio_render_fn fn, void *user, unsigned rate)
{
    mus.starts++;
    mus.fn = fn;
    mus.user = user;
    mus.rate = rate;
}
static void mu_stop(void) { mus.stops++; }
static void mu_render(void *user, int16_t *out, size_t frames) { (void)user; (void)out; (void)frames; }

static void test_music_sink(void)
{
    printf("music entry points\n");
    memset(&mus, 0, sizeof mus);
    /* A sink without music support (like fake_sink, whose music fields are zero) ignores them. */
    audio_set_sink(&fake_sink);
    CHECK(!audio_music_available());
    audio_music_start(mu_render, NULL, 49716);
    audio_music_stop();
    CHECK(mus.starts == 0 && mus.stops == 0);
    /* No sink at all: also a no-op. */
    audio_set_sink(NULL);
    CHECK(!audio_music_available());
    audio_music_start(mu_render, NULL, 49716);
    audio_music_stop();
    CHECK(mus.starts == 0 && mus.stops == 0);
    /* A sink with music: the calls are forwarded unchanged, and audio_stop() leaves music alone. */
    static const audio_sink with_music = {fk_play, fk_playing, fk_stop, mu_start, mu_stop};
    audio_set_sink(&with_music);
    CHECK(audio_music_available());
    int token;
    audio_music_start(mu_render, &token, 49716);
    CHECK(mus.starts == 1 && mus.fn == mu_render && mus.user == &token && mus.rate == 49716);
    audio_stop();
    CHECK(mus.stops == 0);
    audio_music_stop();
    CHECK(mus.stops == 1);
    audio_set_sink(NULL);
}

int main(int argc, char **argv)
{
    if (argc > 1)
        assets_set_dir(argv[1]);
    test_adpcm();
    test_voc();
    test_table_synthetic();
    test_real_voice();
    test_subs_with_voice();
    test_intro0_sound();
    test_music_sink();
    printf("%s: %d failures, %d skipped\n", failures ? "FAILED" : "ok", failures, skipped);
    return failures ? 1 : 0;
}
