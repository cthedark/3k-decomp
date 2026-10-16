#include "intro1.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"
#include "gfx.h"
#include "slide.h"
#include "timing.h"

/* Layout facts, each read from STORY2.SL's code (addresses are in that program):
 *  - the picture   : STORY1.LCZ chunk 0, 64,000 bytes, decoded straight onto the screen (0x10b06);
 *  - clouds 0, 1   : STORY1_2.LCZ chunks 0 and 1, sprites 320x43 and 320x39 (0x10b6d, 0x10b8f);
 *  - face frames   : STORY1_2.LCZ chunk 2, four headerless 123x96 frames; a fifth frame is a copy
 *                    of the third, so the cycle is 0 1 2 3 2 (0x10bcf);
 *  - patch         : the screen rectangle at row 15, column 0, 230x28, saved right after the picture
 *                    was drawn (0x10b1c) and laid over the clouds on every pass;
 *  - lines         : STORY1_T.LCZ chunk 0, 8 lines of 600 bytes (560 used: 14 rows of 40 bytes). */
enum {
    PIC_BYTES = 64000,
    CLOUD0_BYTES = SPRITE_HDR + 320 * 43,
    CLOUD1_BYTES = SPRITE_HDR + 320 * 39,
    FACE_W = 123,
    FACE_H = 96,
    FACE_BYTES = FACE_W * FACE_H,   /* 11,808 */
    FACE_FRAMES = 5,
    FACE_AT = 0x3b0f,               /* row 47, column 79 */
    FACE_HOLD = 10,                 /* a frame stays for 11 passes: the counter must pass 10 */
    PATCH_AT = 0x12c0,              /* row 15, column 0 */
    PATCH_W = 230,
    PATCH_H = 28,
    CLOUD_PIX = 320 * 43,
    CLOUD1_PIX = 320 * 39,
    CLOUD_A_EVERY = 5,              /* counter must pass 5: every sixth pass */
    CLOUD_B_EVERY = 2,              /* counter must pass 2: every third pass */
    ANIM_PASSES = 320,
    LINES_BYTES = 4800
};

struct intro1 {
    uint8_t cloud0[CLOUD0_BYTES + 8];
    uint8_t cloud1[CLOUD1_BYTES + 8];
    uint8_t face[FACE_FRAMES * FACE_BYTES];
    uint8_t fbuf[CLOUD0_BYTES + 8];
    uint8_t patch[SPRITE_HDR + PATCH_W * PATCH_H];
    uint8_t pal1[768], pal2[768], cur[768], black[768];
    subs_t *subs;

    intro1_phase phase;
    int faithful_keys;
    unsigned fade_step;

    /* animation loop; the first two counters start at the values slide 0 leaves in the same
     * stack slots, which make both cloud strips move on the first pass (see intro1_create) */
    unsigned anim_done;
    uint8_t ctr_a, ctr_b, ctr_face;
    unsigned face_at;
};

/* The cloud composite that every pass draws (0x10be6..0x10c43): strip 0, strip 1 over it
 * (non-zero pixels only), the patch over both, the result onto the top rows of the screen. */
static void draw_sky(intro1_t *t, screen_t *scr)
{
    memcpy(t->fbuf, t->cloud0, CLOUD0_BYTES);
    gfx_overlay(t->fbuf + SPRITE_HDR, t->cloud1 + SPRITE_HDR, CLOUD1_PIX);
    gfx_blit_masked(t->fbuf + SPRITE_HDR + PATCH_AT, 320, t->patch);
    memcpy(scr->pix, t->fbuf + SPRITE_HDR, CLOUD_PIX);
}

intro1_t *intro1_create(screen_t *scr, int faithful_keys)
{
    intro1_t *t = calloc(1, sizeof *t);
    if (!t)
        return NULL;

    uint8_t *pic = malloc(PIC_BYTES);
    uint8_t *frames = malloc(4 * FACE_BYTES);
    uint8_t *tline = malloc(LINES_BYTES);
    int ok = pic && frames && tline;
    if (ok) {
        uint8_t *d1[1] = {pic};
        size_t w1[1] = {PIC_BYTES};
        uint8_t *d2[3] = {t->cloud0, t->cloud1, frames};
        size_t w2[3] = {CLOUD0_BYTES, CLOUD1_BYTES, 4 * FACE_BYTES};
        uint8_t *d3[1] = {tline};
        size_t w3[1] = {LINES_BYTES};
        ok = slide_load_chunks("story1.lcz", 1, d1, w1) == 0 && slide_load_chunks("story1_2.lcz", 3, d2, w2) == 0 &&
             slide_load_chunks("story1_t.lcz", 1, d3, w3) == 0 && slide_load_palettes(t->pal1, t->pal2) == 0;
    }
    if (ok && (sprite_w(t->cloud0) != 320 || sprite_h(t->cloud0) != 43 || sprite_w(t->cloud1) != 320 ||
               sprite_h(t->cloud1) != 39)) {
        fprintf(stderr, "intro1: story1_2.lcz strips have unexpected sizes\n");
        ok = 0;
    }
    if (ok) {
        t->subs = subs_create(tline, LINES_BYTES);
        if (t->subs && subs_load_voice(t->subs, 1) != 0)
            fprintf(stderr, "intro1: no voice-over, using the timed hold\n");
        ok = t->subs != NULL;
    }
    if (!ok) {
        free(pic);
        free(frames);
        free(tline);
        subs_free(t->subs);
        free(t);
        return NULL;
    }

    memcpy(t->face, frames, 4 * FACE_BYTES);
    memcpy(t->face + 4 * FACE_BYTES, t->face + 2 * FACE_BYTES, FACE_BYTES);   /* 0x10bcf */
    free(frames);
    free(tline);

    /* The picture goes straight onto the screen, then the patch is cut out of it. */
    memcpy(scr->pix, pic, PIC_BYTES);
    free(pic);
    sprite_set_size(t->patch, PATCH_W, PATCH_H);
    gfx_copy_rect(t->patch + SPRITE_HDR, scr->pix + PATCH_AT, SCREEN_W, PATCH_W, PATCH_H);
    draw_sky(t, scr);

    slide_start_palette(scr, t->cur, t->pal1);

    t->phase = INTRO1_FADE_DIM;
    t->faithful_keys = faithful_keys;
    /* Stack slots left over from slide 0: the first cloud counter ends at 0xef, the second at
     * 0x3c. Both exceed their limits, so both strips move on the first pass. */
    t->ctr_a = 0xef;
    t->ctr_b = 0x3c;
    return t;
}

void intro1_free(intro1_t *t)
{
    if (t) {
        subs_free(t->subs);
        free(t);
    }
}

intro1_phase intro1_phase_now(const intro1_t *t) { return t->phase; }
unsigned intro1_anim_done(const intro1_t *t) { return t->anim_done; }
const subs_t *intro1_subs(const intro1_t *t) { return t->subs; }

/* One pass of the animation loop (0x10c80..0x10db7). Returns non-zero when the loop is over. */
static int anim_pass(intro1_t *t, screen_t *scr, int skip)
{
    if (++t->ctr_a > CLOUD_A_EVERY) {
        t->ctr_a = 0;
        gfx_rotate_rows_left(t->cloud0);
    }
    if (++t->ctr_b > CLOUD_B_EVERY) {
        t->ctr_b = 0;
        gfx_rotate_rows_left(t->cloud1);
    }
    draw_sky(t, scr);
    gfx_blit_rect(scr, FACE_AT, t->face + t->face_at, FACE_W, FACE_H);
    if (++t->ctr_face > FACE_HOLD) {
        t->ctr_face = 0;
        t->face_at += FACE_BYTES;
        if (t->face_at > (FACE_FRAMES - 1) * FACE_BYTES)
            t->face_at = 0;
    }
    /* The original now reads the keyboard port into the second cloud counter's stack slot. With
     * no key down the port reads 0, so the counter restarts from 0 on every pass and that strip
     * never moves again; with a stale scan code of 2 or more it moves on every pass. Both are
     * accidents of the keyboard's state. The default here is the evident intent (every third
     * pass); `faithful_keys` reproduces the no-key behaviour, which is what the reference run
     * shows. */
    if (t->faithful_keys)
        t->ctr_b = 0;
    t->anim_done++;
    return skip || t->anim_done >= ANIM_PASSES;
}

/* A palette fade phase: one step, and the next phase after SLIDE_FADE_STEPS of them. */
static void fade_to(intro1_t *t, screen_t *scr, const uint8_t *target, intro1_phase next)
{
    slide_fade_step(scr, t->cur, target);
    if (++t->fade_step >= SLIDE_FADE_STEPS) {
        t->fade_step = 0;
        t->phase = next;
        if (next == INTRO1_SUBS)
            subs_begin(t->subs, scr);
    }
}

intro1_phase intro1_step(intro1_t *t, screen_t *scr, unsigned tick, int skip)
{
    switch (t->phase) {
    case INTRO1_FADE_DIM:
        fade_to(t, scr, t->pal2, INTRO1_FADE_BRIGHT);
        break;
    case INTRO1_FADE_BRIGHT:
        fade_to(t, scr, t->pal1, INTRO1_ANIM);
        break;
    case INTRO1_ANIM:
        if (anim_pass(t, scr, skip))
            t->phase = INTRO1_FADE_DIM2;
        break;
    case INTRO1_FADE_DIM2:
        fade_to(t, scr, t->pal2, INTRO1_SUBS);
        break;
    case INTRO1_SUBS:
        if (subs_step(t->subs, scr, tick, skip) == SUBS_DONE)
            t->phase = INTRO1_FADE_BLACK;
        break;
    case INTRO1_FADE_BLACK:
        fade_to(t, scr, t->black, INTRO1_DONE);
        break;
    case INTRO1_DONE:
        break;
    }
    return t->phase;
}

/* The uniform interface (slide.h). */
static void *ops_create(screen_t *scr, int faithful_keys) { return intro1_create(scr, faithful_keys); }
static void ops_destroy(void *t) { intro1_free(t); }
static void ops_step(void *t, screen_t *scr, unsigned tick, int skip) { intro1_step(t, scr, tick, skip); }

static void ops_probe(const void *p, slide_probe *out)
{
    const intro1_t *t = p;
    out->done = t->phase == INTRO1_DONE;
    out->anim = t->phase == INTRO1_ANIM;
    out->anim_done = t->anim_done;
    out->anim_hz = TK_INTRO1_PASS_HZ;
    out->subs = t->phase == INTRO1_SUBS ? t->subs : NULL;
    out->looks = out->anim || (out->subs && subs_state_now(t->subs) == SUBS_WAIT);
}

const slide_ops intro1_ops = {"intro1", ops_create, ops_destroy, ops_step, ops_probe};
