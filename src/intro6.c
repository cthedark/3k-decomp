#include "intro6.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gfx.h"
#include "timing.h"

/* Layout facts, each read from STORY2.SL's code (addresses are in that program):
 *  - the picture : STORY6.LCZ chunk 0, 64,000 bytes, decoded straight onto the screen (0x11eba);
 *  - hair        : STORY6_1.LCZ chunks 0, 1 and 2, each one headerless 189x160 frame (0x11f0b,
 *                  0x11f26, 0x11f41), drawn at screen offset 0x325a (row 40, column 90);
 *  - lines       : STORY6_T.LCZ chunk 0, 11 lines of 600 bytes. */
enum {
    PIC_BYTES = 64000,
    HAIR_W = 189,
    HAIR_H = 160,
    HAIR_BYTES = HAIR_W * HAIR_H,   /* 30,240 */
    HAIR_AT = 0x325a,
    PERIOD = 50,                    /* the counter wraps when it passes 50: a period of 51 passes */
    ANIM_PASSES = 2000,
    LINES_BYTES = 6600
};

struct intro6 {
    uint8_t hair[3][HAIR_BYTES];
    uint8_t pal1[768], pal2[768], cur[768], black[768];
    subs_t *subs;

    intro6_phase phase;
    unsigned fade_step;

    unsigned anim_done;
    uint8_t ctr;
    unsigned k;   /* periods completed, 0..10 */
};

intro6_t *intro6_create(screen_t *scr, int faithful_keys)
{
    (void)faithful_keys;
    intro6_t *t = calloc(1, sizeof *t);
    if (!t)
        return NULL;

    uint8_t *pic = malloc(PIC_BYTES);
    uint8_t *lines = malloc(LINES_BYTES);
    int ok = pic && lines;
    if (ok) {
        uint8_t *d1[1] = {pic};
        size_t w1[1] = {PIC_BYTES};
        uint8_t *d2[3] = {t->hair[0], t->hair[1], t->hair[2]};
        size_t w2[3] = {HAIR_BYTES, HAIR_BYTES, HAIR_BYTES};
        uint8_t *d3[1] = {lines};
        size_t w3[1] = {LINES_BYTES};
        ok = slide_load_chunks("story6.lcz", 1, d1, w1) == 0 && slide_load_chunks("story6_1.lcz", 3, d2, w2) == 0 &&
             slide_load_chunks("story6_t.lcz", 1, d3, w3) == 0 && slide_load_palettes(t->pal1, t->pal2) == 0;
    }
    if (ok) {
        t->subs = subs_create(lines, LINES_BYTES);
        if (t->subs && subs_load_voice(t->subs, 6) != 0)
            fprintf(stderr, "intro6: no voice-over, using the timed hold\n");
        ok = t->subs != NULL;
    }
    free(lines);
    if (!ok) {
        free(pic);
        subs_free(t->subs);
        free(t);
        return NULL;
    }

    memcpy(scr->pix, pic, PIC_BYTES);
    free(pic);
    gfx_blit_rect(scr, HAIR_AT, t->hair[0], HAIR_W, HAIR_H);   /* 0x11f58 */
    slide_start_palette(scr, t->cur, t->pal1);

    t->phase = INTRO6_FADE_DIM;
    /* The counter's stack slot is left over from slide 5, whose counter ends its 2,000 passes at
     * 2000 mod 151 = 37 (a skipped slide 5 would leave another value). */
    t->ctr = 37;
    return t;
}

void intro6_free(intro6_t *t)
{
    if (t) {
        subs_free(t->subs);
        free(t);
    }
}

intro6_phase intro6_phase_now(const intro6_t *t) { return t->phase; }

/* One pass of the animation loop (0x11f93..0x12068). Returns non-zero when the loop is over.
 * The counter runs in periods of 51 passes. For ten periods the hair alternates between frame 0
 * (counter below 25) and frame 2 (above 25; at exactly 25 nothing is drawn); the eleventh period
 * shows frame 1 until the counter passes 24, which also starts the cycle again. */
static int anim_pass(intro6_t *t, screen_t *scr, int skip)
{
    if (++t->ctr > PERIOD) {
        t->k++;
        t->ctr = 0;
    }
    if (t->ctr < 25 && t->k < 10)
        gfx_blit_rect(scr, HAIR_AT, t->hair[0], HAIR_W, HAIR_H);
    if (t->ctr > 25 && t->k < 10)
        gfx_blit_rect(scr, HAIR_AT, t->hair[2], HAIR_W, HAIR_H);
    if (t->k > 9) {
        gfx_blit_rect(scr, HAIR_AT, t->hair[1], HAIR_W, HAIR_H);
        if (t->ctr > 24)
            t->k = 0;
    }
    t->anim_done++;
    return skip || t->anim_done >= ANIM_PASSES;
}

static void fade_to(intro6_t *t, screen_t *scr, const uint8_t *target, intro6_phase next)
{
    slide_fade_step(scr, t->cur, target);
    if (++t->fade_step >= SLIDE_FADE_STEPS) {
        t->fade_step = 0;
        t->phase = next;
        if (next == INTRO6_SUBS)
            subs_begin(t->subs, scr);
    }
}

intro6_phase intro6_step(intro6_t *t, screen_t *scr, unsigned tick, int skip)
{
    switch (t->phase) {
    case INTRO6_FADE_DIM:
        fade_to(t, scr, t->pal2, INTRO6_FADE_BRIGHT);
        break;
    case INTRO6_FADE_BRIGHT:
        fade_to(t, scr, t->pal1, INTRO6_ANIM);
        break;
    case INTRO6_ANIM:
        if (anim_pass(t, scr, skip))
            t->phase = INTRO6_FADE_DIM2;
        break;
    case INTRO6_FADE_DIM2:
        fade_to(t, scr, t->pal2, INTRO6_SUBS);
        break;
    case INTRO6_SUBS:
        if (subs_step(t->subs, scr, tick, skip) == SUBS_DONE)
            t->phase = INTRO6_FADE_BLACK;
        break;
    case INTRO6_FADE_BLACK:
        fade_to(t, scr, t->black, INTRO6_DONE);
        break;
    case INTRO6_DONE:
        break;
    }
    return t->phase;
}

/* The uniform interface (slide.h). */
static void *ops_create(screen_t *scr, int faithful_keys) { return intro6_create(scr, faithful_keys); }
static void ops_destroy(void *t) { intro6_free(t); }
static void ops_step(void *t, screen_t *scr, unsigned tick, int skip) { intro6_step(t, scr, tick, skip); }

static void ops_probe(const void *p, slide_probe *out)
{
    const intro6_t *t = p;
    out->done = t->phase == INTRO6_DONE;
    out->anim = t->phase == INTRO6_ANIM;
    out->anim_done = t->anim_done;
    out->anim_hz = TK_INTRO6_PASS_HZ;
    out->subs = t->phase == INTRO6_SUBS ? t->subs : NULL;
    out->looks = out->anim || (out->subs && subs_state_now(t->subs) == SUBS_WAIT);
}

const slide_ops intro6_ops = {"intro6", ops_create, ops_destroy, ops_step, ops_probe};
