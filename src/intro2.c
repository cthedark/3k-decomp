#include "intro2.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gfx.h"
#include "timing.h"

/* Layout facts, each read from STORY2.SL's code (addresses are in that program):
 *  - the picture : STORY2.LCZ chunk 0, 64,000 bytes, decoded straight onto the screen (0x10eb2);
 *  - hair        : STORY2.LCZ chunk 1, three headerless 155x125 frames (0x10ecd), and chunk 2, one
 *                  more frame (0x10ee8); all drawn at screen offset 0x5e65 (row 75, column 165);
 *  - figure      : STORY2_1.LCZ chunk 0, four headerless 75x78 frames (0x10f39), drawn at
 *                  0x334f (row 41, column 15);
 *  - lines       : STORY2_T.LCZ chunk 0, 3 lines of 600 bytes. */
enum {
    PIC_BYTES = 64000,
    HAIR_W = 155,
    HAIR_H = 125,
    HAIR_BYTES = HAIR_W * HAIR_H,   /* 19,375 */
    HAIR_AT = 0x5e65,
    FIG_W = 75,
    FIG_H = 78,
    FIG_BYTES = FIG_W * FIG_H,      /* 5,850 */
    FIG_FRAMES = 4,
    FIG_AT = 0x334f,
    FIG_HOLD = 20,                  /* a frame stays for 21 passes: the counter must pass 20 */
    HAIR_CYCLE = 90,                /* the hair counter wraps when it passes 90 */
    ANIM_PASSES = 500,
    LINES_BYTES = 1800
};

struct intro2 {
    uint8_t hair[3 * HAIR_BYTES];   /* chunk 1: frames 0, 1, 2 */
    uint8_t hair_rest[HAIR_BYTES];  /* chunk 2: the frame shown while the counter is below 15 */
    uint8_t fig[FIG_FRAMES * FIG_BYTES];
    uint8_t pal1[768], pal2[768], cur[768], black[768];
    subs_t *subs;

    intro2_phase phase;
    unsigned fade_step;

    unsigned anim_done;
    uint8_t ctr_hair, ctr_fig;
    unsigned fig_at;
};

intro2_t *intro2_create(screen_t *scr, int faithful_keys)
{
    (void)faithful_keys;
    intro2_t *t = calloc(1, sizeof *t);
    if (!t)
        return NULL;

    uint8_t *pic = malloc(PIC_BYTES);
    uint8_t *lines = malloc(LINES_BYTES);
    int ok = pic && lines;
    if (ok) {
        uint8_t *d1[3] = {pic, t->hair, t->hair_rest};
        size_t w1[3] = {PIC_BYTES, 3 * HAIR_BYTES, HAIR_BYTES};
        uint8_t *d2[1] = {t->fig};
        size_t w2[1] = {FIG_FRAMES * FIG_BYTES};
        uint8_t *d3[1] = {lines};
        size_t w3[1] = {LINES_BYTES};
        ok = slide_load_chunks("story2.lcz", 3, d1, w1) == 0 && slide_load_chunks("story2_1.lcz", 1, d2, w2) == 0 &&
             slide_load_chunks("story2_t.lcz", 1, d3, w3) == 0 && slide_load_palettes(t->pal1, t->pal2) == 0;
    }
    if (ok) {
        t->subs = subs_create(lines, LINES_BYTES);
        if (t->subs && subs_load_voice(t->subs, 2) != 0)
            fprintf(stderr, "intro2: no voice-over, using the timed hold\n");
        ok = t->subs != NULL;
    }
    if (!ok) {
        free(pic);
        free(lines);
        subs_free(t->subs);
        free(t);
        return NULL;
    }

    memcpy(scr->pix, pic, PIC_BYTES);
    free(pic);
    free(lines);
    gfx_blit_rect(scr, HAIR_AT, t->hair, HAIR_W, HAIR_H);   /* 0x10f4f */
    gfx_blit_rect(scr, FIG_AT, t->fig, FIG_W, FIG_H);       /* 0x10f64 */
    slide_start_palette(scr, t->cur, t->pal1);

    t->phase = INTRO2_FADE_DIM;
    /* Stack slots left over from slide 1. The figure counter ends slide 1's 320 passes at
     * 320 mod 11 = 1; the hair counter holds the last scan code read there, 0 with no key down.
     * (A skipped slide 1 would leave other values: only the length of the first frame differs.) */
    t->ctr_fig = 1;
    t->ctr_hair = 0;
    return t;
}

void intro2_free(intro2_t *t)
{
    if (t) {
        subs_free(t->subs);
        free(t);
    }
}

intro2_phase intro2_phase_now(const intro2_t *t) { return t->phase; }

/* Hair frame to draw for a counter value (0x10ff4..0x1112b), or NULL for "leave it". The counter
 * values 15, 30, 45, 60, 75 and 90 draw nothing, so the previous frame stays. */
static const uint8_t *hair_frame(const intro2_t *t, unsigned c)
{
    if (c < 15)
        return t->hair_rest;
    if (c > 15 && c < 30)
        return t->hair + HAIR_BYTES;
    if (c > 30 && c < 45)
        return t->hair + 2 * HAIR_BYTES;
    if (c > 45 && c < 60)
        return t->hair + HAIR_BYTES;
    if (c > 60 && c < 75)
        return t->hair;
    if (c > 75 && c < 90)
        return t->hair + HAIR_BYTES;
    return NULL;
}

/* One pass of the animation loop (0x10f9d..0x11155). Returns non-zero when the loop is over. */
static int anim_pass(intro2_t *t, screen_t *scr, int skip)
{
    if (++t->ctr_hair > HAIR_CYCLE)
        t->ctr_hair = 0;
    if (++t->ctr_fig > FIG_HOLD) {
        t->ctr_fig = 0;
        t->fig_at += FIG_BYTES;
        if (t->fig_at > (FIG_FRAMES - 1) * FIG_BYTES)
            t->fig_at = 0;
    }
    gfx_blit_rect(scr, FIG_AT, t->fig + t->fig_at, FIG_W, FIG_H);
    const uint8_t *h = hair_frame(t, t->ctr_hair);
    if (h)
        gfx_blit_rect(scr, HAIR_AT, h, HAIR_W, HAIR_H);
    t->anim_done++;
    return skip || t->anim_done >= ANIM_PASSES;
}

static void fade_to(intro2_t *t, screen_t *scr, const uint8_t *target, intro2_phase next)
{
    slide_fade_step(scr, t->cur, target);
    if (++t->fade_step >= SLIDE_FADE_STEPS) {
        t->fade_step = 0;
        t->phase = next;
        if (next == INTRO2_SUBS)
            subs_begin(t->subs, scr);
    }
}

intro2_phase intro2_step(intro2_t *t, screen_t *scr, unsigned tick, int skip)
{
    switch (t->phase) {
    case INTRO2_FADE_DIM:
        fade_to(t, scr, t->pal2, INTRO2_FADE_BRIGHT);
        break;
    case INTRO2_FADE_BRIGHT:
        fade_to(t, scr, t->pal1, INTRO2_ANIM);
        break;
    case INTRO2_ANIM:
        if (anim_pass(t, scr, skip))
            t->phase = INTRO2_FADE_DIM2;
        break;
    case INTRO2_FADE_DIM2:
        fade_to(t, scr, t->pal2, INTRO2_SUBS);
        break;
    case INTRO2_SUBS:
        if (subs_step(t->subs, scr, tick, skip) == SUBS_DONE)
            t->phase = INTRO2_FADE_BLACK;
        break;
    case INTRO2_FADE_BLACK:
        fade_to(t, scr, t->black, INTRO2_DONE);
        break;
    case INTRO2_DONE:
        break;
    }
    return t->phase;
}

/* The uniform interface (slide.h). */
static void *ops_create(screen_t *scr, int faithful_keys) { return intro2_create(scr, faithful_keys); }
static void ops_destroy(void *t) { intro2_free(t); }
static void ops_step(void *t, screen_t *scr, unsigned tick, int skip) { intro2_step(t, scr, tick, skip); }

static void ops_probe(const void *p, slide_probe *out)
{
    const intro2_t *t = p;
    out->done = t->phase == INTRO2_DONE;
    out->anim = t->phase == INTRO2_ANIM;
    out->anim_done = t->anim_done;
    out->anim_hz = TK_INTRO2_PASS_HZ;
    out->subs = t->phase == INTRO2_SUBS ? t->subs : NULL;
    out->looks = out->anim || (out->subs && subs_state_now(t->subs) == SUBS_WAIT);
}

const slide_ops intro2_ops = {"intro2", ops_create, ops_destroy, ops_step, ops_probe};
