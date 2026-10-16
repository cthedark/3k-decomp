#include "intro5.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gfx.h"
#include "timing.h"

/* Layout facts, each read from STORY2.SL's code (addresses are in that program):
 *  - the picture : STORY5_1.LCZ chunk 0, 64,000 bytes, decoded straight onto the screen (0x11ce2);
 *  - the face    : STORY5_1.LCZ chunk 1 (file offset 0x9aed), three headerless 97x84 frames
 *                  (0x11cfd); a fourth slot is a copy of the second (0x11d22), so the cycle is
 *                  0 1 2 1. Drawn at screen offset 0x28c0 (row 32, column 192);
 *  - lines       : STORY5_T.LCZ chunk 0, 8 lines of 600 bytes. */
enum {
    PIC_BYTES = 64000,
    FACE_W = 97,
    FACE_H = 84,
    FACE_BYTES = FACE_W * FACE_H,   /* 8,148 = 0x1fd4 */
    FACE_FRAMES = 4,
    FACE_AT = 0x28c0,
    FACE_HOLD = 150,                /* a frame stays for 151 passes: the counter must pass 150 */
    ANIM_PASSES = 2000,
    LINES_BYTES = 4800
};

struct intro5 {
    uint8_t face[FACE_FRAMES * FACE_BYTES];
    uint8_t pal1[768], pal2[768], cur[768], black[768];
    subs_t *subs;

    intro5_phase phase;
    unsigned fade_step;

    unsigned anim_done;
    uint8_t ctr;
    unsigned face_at;
};

intro5_t *intro5_create(screen_t *scr, int faithful_keys)
{
    (void)faithful_keys;
    intro5_t *t = calloc(1, sizeof *t);
    if (!t)
        return NULL;

    uint8_t *pic = malloc(PIC_BYTES);
    uint8_t *lines = malloc(LINES_BYTES);
    int ok = pic && lines;
    if (ok) {
        uint8_t *d1[2] = {pic, t->face};
        size_t w1[2] = {PIC_BYTES, 3 * FACE_BYTES};
        uint8_t *d2[1] = {lines};
        size_t w2[1] = {LINES_BYTES};
        ok = slide_load_chunks("story5_1.lcz", 2, d1, w1) == 0 && slide_load_chunks("story5_t.lcz", 1, d2, w2) == 0 &&
             slide_load_palettes(t->pal1, t->pal2) == 0;
    }
    if (ok) {
        t->subs = subs_create(lines, LINES_BYTES);
        if (t->subs && subs_load_voice(t->subs, 5) != 0)
            fprintf(stderr, "intro5: no voice-over, using the timed hold\n");
        ok = t->subs != NULL;
    }
    free(lines);
    if (!ok) {
        free(pic);
        subs_free(t->subs);
        free(t);
        return NULL;
    }

    memcpy(t->face + 3 * FACE_BYTES, t->face + FACE_BYTES, FACE_BYTES);   /* 0x11d22 */
    memcpy(scr->pix, pic, PIC_BYTES);
    free(pic);
    slide_start_palette(scr, t->cur, t->pal1);

    t->phase = INTRO5_FADE_DIM;
    /* The frame counter's stack slot is left over from slide 4, where it holds a temporary
     * (0 or 1); 0 is used. */
    t->ctr = 0;
    return t;
}

void intro5_free(intro5_t *t)
{
    if (t) {
        subs_free(t->subs);
        free(t);
    }
}

intro5_phase intro5_phase_now(const intro5_t *t) { return t->phase; }

/* One pass of the animation loop (0x11d5b..0x11dc8). Returns non-zero when the loop is over. */
static int anim_pass(intro5_t *t, screen_t *scr, int skip)
{
    if (++t->ctr > FACE_HOLD) {
        t->ctr = 0;
        t->face_at += FACE_BYTES;
        if (t->face_at > (FACE_FRAMES - 1) * FACE_BYTES)
            t->face_at = 0;
    }
    gfx_blit_rect(scr, FACE_AT, t->face + t->face_at, FACE_W, FACE_H);
    t->anim_done++;
    return skip || t->anim_done >= ANIM_PASSES;
}

static void fade_to(intro5_t *t, screen_t *scr, const uint8_t *target, intro5_phase next)
{
    slide_fade_step(scr, t->cur, target);
    if (++t->fade_step >= SLIDE_FADE_STEPS) {
        t->fade_step = 0;
        t->phase = next;
        if (next == INTRO5_SUBS)
            subs_begin(t->subs, scr);
    }
}

intro5_phase intro5_step(intro5_t *t, screen_t *scr, unsigned tick, int skip)
{
    switch (t->phase) {
    case INTRO5_FADE_DIM:
        fade_to(t, scr, t->pal2, INTRO5_FADE_BRIGHT);
        break;
    case INTRO5_FADE_BRIGHT:
        fade_to(t, scr, t->pal1, INTRO5_ANIM);
        break;
    case INTRO5_ANIM:
        if (anim_pass(t, scr, skip))
            t->phase = INTRO5_FADE_DIM2;
        break;
    case INTRO5_FADE_DIM2:
        fade_to(t, scr, t->pal2, INTRO5_SUBS);
        break;
    case INTRO5_SUBS:
        if (subs_step(t->subs, scr, tick, skip) == SUBS_DONE)
            t->phase = INTRO5_FADE_BLACK;
        break;
    case INTRO5_FADE_BLACK:
        fade_to(t, scr, t->black, INTRO5_DONE);
        break;
    case INTRO5_DONE:
        break;
    }
    return t->phase;
}

/* The uniform interface (slide.h). */
static void *ops_create(screen_t *scr, int faithful_keys) { return intro5_create(scr, faithful_keys); }
static void ops_destroy(void *t) { intro5_free(t); }
static void ops_step(void *t, screen_t *scr, unsigned tick, int skip) { intro5_step(t, scr, tick, skip); }

static void ops_probe(const void *p, slide_probe *out)
{
    const intro5_t *t = p;
    out->done = t->phase == INTRO5_DONE;
    out->anim = t->phase == INTRO5_ANIM;
    out->anim_done = t->anim_done;
    out->anim_hz = TK_INTRO5_PASS_HZ;
    out->subs = t->phase == INTRO5_SUBS ? t->subs : NULL;
    out->looks = out->anim || (out->subs && subs_state_now(t->subs) == SUBS_WAIT);
}

const slide_ops intro5_ops = {"intro5", ops_create, ops_destroy, ops_step, ops_probe};
