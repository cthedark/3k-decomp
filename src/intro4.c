#include "intro4.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gfx.h"
#include "timing.h"
#include "tkrand.h"

/* Layout facts, each read from STORY2.SL's code (addresses are in that program):
 *  - the picture : STORY4.LCZ chunk 0, 64,000 bytes, decoded straight onto the screen (0x11549);
 *  - eyes A      : STORY4_2.LCZ chunk 0, seven headerless 79x99 frames (0x1159a);
 *  - eyes B      : STORY4_3.LCZ chunk 0, four headerless 79x99 frames (0x115eb);
 *  - sparkles    : STORY4_4.LCZ chunk 0, twenty headerless 23x19 frames (0x1163c): ten per bank,
 *                  bank 0 first, bank 1 at +0x1112;
 *  - patch       : the 100x81 screen rectangle at offset 187 (row 0, column 187), saved as a
 *                  sprite (0x137c8) and redrawn from a working copy every 47 passes;
 *  - lines       : STORY4_T.LCZ chunk 0, 5 lines of 600 bytes. */
enum {
    PIC_BYTES = 64000,
    EYE_W = 79,
    EYE_H = 99,
    EYE_BYTES = EYE_W * EYE_H,   /* 7,821 */
    EYES_A_FRAMES = 7,
    EYES_B_FRAMES = 4,
    EYES_AT = 0x2d50,            /* row 36, column 80 */
    SPARK_W = 23,
    SPARK_H = 19,
    SPARK_BYTES = SPARK_W * SPARK_H,   /* 437 = 0x1b5 */
    SPARK_BANK = 10 * SPARK_BYTES,     /* 0x1112 */
    SPARK_TOTAL = 2 * SPARK_BANK,
    NSPARK = 10,
    PATCH_AT = 187,
    PATCH_W = 100,
    PATCH_H = 81,
    PATCH_SPRITE = SPRITE_HDR + PATCH_W * PATCH_H,   /* 8,104 */
    SPARK_EVERY = 46,            /* the counter must pass 46: every 47th pass */
    BLINK_PERIOD = 60,           /* the blink counter wraps when it passes 60 */
    LOOP_END = 0x1900,           /* the pass counter at which the loop ends */
    LINES_BYTES = 3000
};

struct intro4 {
    uint8_t eyes_a[EYES_A_FRAMES * EYE_BYTES];
    uint8_t eyes_b[EYES_B_FRAMES * EYE_BYTES];
    uint8_t spark[SPARK_TOTAL];
    uint8_t patch[PATCH_SPRITE];   /* the saved background, untouched */
    uint8_t work[PATCH_SPRITE];    /* background plus sparkles */
    uint8_t pal1[768], pal2[768], cur[768], black[768];
    subs_t *subs;

    uint16_t pos[NSPARK];          /* sparkle position: index into `work`, header included */
    uint16_t frame[NSPARK];        /* offset of its current frame within its bank */
    uint8_t bank[NSPARK];          /* 1: bank 0, 0: bank 1 (the original draws bank 0 if non-zero) */

    intro4_phase phase;
    unsigned fade_step;

    unsigned anim_done;
    unsigned si;                   /* the pass counter of the original; held back during the pauses */
    uint8_t ctr_spark, ctr_blink;
    unsigned hold;                 /* position within a pause, 0..1000 */
    unsigned hold_round;           /* how many full cycles of the pause are done */
};

/* A sparkle's random position and bank (0x11662..0x116f2, 0x11811..0x1187a). */
static void place(intro4_t *t, int i)
{
    unsigned a = (unsigned)tk_rand() / 0x1b0;
    unsigned b = (unsigned)tk_rand() / 0x211;
    t->pos[i] = (uint16_t)(b * 100 + a + 4);
}

intro4_t *intro4_create(screen_t *scr, int faithful_keys)
{
    (void)faithful_keys;
    intro4_t *t = calloc(1, sizeof *t);
    if (!t)
        return NULL;

    uint8_t *pic = malloc(PIC_BYTES);
    uint8_t *lines = malloc(LINES_BYTES);
    int ok = pic && lines;
    if (ok) {
        uint8_t *d1[1] = {pic};
        size_t w1[1] = {PIC_BYTES};
        uint8_t *d2[1] = {t->eyes_a};
        size_t w2[1] = {sizeof t->eyes_a};
        uint8_t *d3[1] = {t->eyes_b};
        size_t w3[1] = {sizeof t->eyes_b};
        uint8_t *d4[1] = {t->spark};
        size_t w4[1] = {SPARK_TOTAL};
        uint8_t *d5[1] = {lines};
        size_t w5[1] = {LINES_BYTES};
        ok = slide_load_chunks("story4.lcz", 1, d1, w1) == 0 && slide_load_chunks("story4_2.lcz", 1, d2, w2) == 0 &&
             slide_load_chunks("story4_3.lcz", 1, d3, w3) == 0 && slide_load_chunks("story4_4.lcz", 1, d4, w4) == 0 &&
             slide_load_chunks("story4_t.lcz", 1, d5, w5) == 0 && slide_load_palettes(t->pal1, t->pal2) == 0;
    }
    if (ok) {
        t->subs = subs_create(lines, LINES_BYTES);
        if (t->subs && subs_load_voice(t->subs, 4) != 0)
            fprintf(stderr, "intro4: no voice-over, using the timed hold\n");
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
    sprite_set_size(t->patch, PATCH_W, PATCH_H);
    gfx_copy_rect(t->patch + SPRITE_HDR, scr->pix + PATCH_AT, SCREEN_W, PATCH_W, PATCH_H);   /* 0x11651 */
    memcpy(t->work, t->patch, PATCH_SPRITE);

    /* The ten sparkles (0x11659..0x116fb): a random place, a start frame staggered by one frame
     * per sparkle (the last one wraps to 0), and a random bank. */
    unsigned f = 0;
    for (int i = 0; i < NSPARK; i++) {
        place(t, i);
        f += SPARK_BYTES;
        if (f == SPARK_BANK)
            f = 0;
        t->frame[i] = (uint16_t)f;
        t->bank[i] = (uint8_t)((unsigned)tk_rand() / 0x4000);
    }

    slide_start_palette(scr, t->cur, t->pal1);
    t->phase = INTRO4_FADE_DIM;
    /* Stack slots left over from earlier slides: the sparkle counter holds slide 0's subtitle state
     * (3 when its loop ended); the blink counter holds slide 3's "every second pass" flag, 0 after
     * its 142 passes. */
    t->ctr_spark = 3;
    t->ctr_blink = 0;
    return t;
}

void intro4_free(intro4_t *t)
{
    if (t) {
        subs_free(t->subs);
        free(t);
    }
}

intro4_phase intro4_phase_now(const intro4_t *t) { return t->phase; }

/* Draw a headerless w x h block into `dst`, whose rows are `stride` bytes apart; 0 is transparent. */
static void blit_block(uint8_t *dst, unsigned stride, const uint8_t *src, unsigned w, unsigned h)
{
    for (unsigned r = 0; r < h; r++)
        for (unsigned c = 0; c < w; c++)
            if (src[r * w + c] != 0)
                dst[r * stride + c] = src[r * w + c];
}

/* One update of the sparkles (0x1173e..0x118b7), every 47th pass. */
static void sparkles(intro4_t *t, screen_t *scr)
{
    memcpy(t->work, t->patch, PATCH_SPRITE);
    for (int i = 0; i < NSPARK; i++) {
        const uint8_t *src = t->spark + t->frame[i] + (t->bank[i] ? 0 : SPARK_BANK);
        blit_block(t->work + t->pos[i], PATCH_W, src, SPARK_W, SPARK_H);
        t->frame[i] = (uint16_t)(t->frame[i] + SPARK_BYTES);
        if (t->frame[i] == SPARK_BANK) {   /* its animation is over: start another elsewhere */
            place(t, i);
            t->frame[i] = 0;
            t->bank[i] = (uint8_t)((unsigned)tk_rand() / 0x4000);
        }
    }
    gfx_draw_sprite(scr, PATCH_AT, t->work);
}

static void eyes(screen_t *scr, const uint8_t *frames, unsigned k)
{
    gfx_blit_rect(scr, EYES_AT, frames + (size_t)k * EYE_BYTES, EYE_W, EYE_H);
}

/* One pass of the animation loop (0x1172c..0x11bf0). The original's pass counter `si` is kept
 * so the sequence reads as it does there: the eye frame depends on it, and during the two pauses
 * it is pushed back to the same value on every pass. Returns non-zero when the loop is over. */
static int anim_pass(intro4_t *t, screen_t *scr, int skip)
{
    unsigned si = t->si;

    if (++t->ctr_spark > SPARK_EVERY) {
        t->ctr_spark = 0;
        sparkles(t, scr);
    }
    if (++t->ctr_blink > BLINK_PERIOD)
        t->ctr_blink = 0;
    unsigned blink = t->ctr_blink;

    /* First half: the eyes of STORY4_2, frames 0..5, then the first pause on frames 5 and 6. */
    if (si < 0x2bc)
        eyes(scr, t->eyes_a, 0);
    if (si > 0x2bc && si < 0x4b0)
        eyes(scr, t->eyes_a, 1);
    if (si > 0x4b0 && si < 0x6a4)
        eyes(scr, t->eyes_a, 2);
    if (si > 0x6a4 && si < 0x960)
        eyes(scr, t->eyes_a, 3);
    if (si > 0x960 && si < 0xc1c)
        eyes(scr, t->eyes_a, 4);
    if (si > 0xc1c && si < 0xed8)
        eyes(scr, t->eyes_a, 5);
    if (si == 0xed8) {
        t->hold = 0;
        t->hold_round = 0;
    }
    if (si > 0xed8 && si < 0xeda) {   /* the pause: 2 x 1001 passes, eyes blinking, then closed */
        si = 0xed8;
        if (t->hold < 500)
            eyes(scr, t->eyes_a, blink < 30 ? 5 : 6);
        else
            eyes(scr, t->eyes_a, 6);
        if (++t->hold > 1000) {
            t->hold = 0;
            if (++t->hold_round > 1)
                si = 0xeda;
        }
    }

    /* Second half: the eyes of STORY4_3. */
    if (si > 0xed9 && si < 0x11f8)
        eyes(scr, t->eyes_b, 0);
    if (si == 0x11f8) {
        t->hold = 0;
        t->hold_round = 0;
    }
    if (si > 0x11f8 && si < 0x11fa) {   /* the second pause: 3 x 1001 passes */
        si = 0x11f8;
        if (t->hold < 500)
            eyes(scr, t->eyes_b, blink < 30 ? 0 : 1);
        else
            eyes(scr, t->eyes_b, 1);
        if (++t->hold > 1000) {
            t->hold = 0;
            if (++t->hold_round > 2)
                si = 0x11fa;
        }
    }
    if (si > 0x11f9 && si < 0x157c)
        eyes(scr, t->eyes_b, 2);
    if (si > 0x157c && si < LOOP_END)
        eyes(scr, t->eyes_b, 3);

    t->si = si + 1;
    t->anim_done++;
    return skip || t->si >= LOOP_END;
}

static void fade_to(intro4_t *t, screen_t *scr, const uint8_t *target, intro4_phase next)
{
    slide_fade_step(scr, t->cur, target);
    if (++t->fade_step >= SLIDE_FADE_STEPS) {
        t->fade_step = 0;
        t->phase = next;
        if (next == INTRO4_SUBS)
            subs_begin(t->subs, scr);
    }
}

intro4_phase intro4_step(intro4_t *t, screen_t *scr, unsigned tick, int skip)
{
    switch (t->phase) {
    case INTRO4_FADE_DIM:
        fade_to(t, scr, t->pal2, INTRO4_FADE_BRIGHT);
        break;
    case INTRO4_FADE_BRIGHT:
        fade_to(t, scr, t->pal1, INTRO4_ANIM);
        break;
    case INTRO4_ANIM:
        if (anim_pass(t, scr, skip))
            t->phase = INTRO4_FADE_DIM2;
        break;
    case INTRO4_FADE_DIM2:
        fade_to(t, scr, t->pal2, INTRO4_SUBS);
        break;
    case INTRO4_SUBS:
        if (subs_step(t->subs, scr, tick, skip) == SUBS_DONE)
            t->phase = INTRO4_FADE_BLACK;
        break;
    case INTRO4_FADE_BLACK:
        fade_to(t, scr, t->black, INTRO4_DONE);
        break;
    case INTRO4_DONE:
        break;
    }
    return t->phase;
}

/* The uniform interface (slide.h). */
static void *ops_create(screen_t *scr, int faithful_keys) { return intro4_create(scr, faithful_keys); }
static void ops_destroy(void *t) { intro4_free(t); }
static void ops_step(void *t, screen_t *scr, unsigned tick, int skip) { intro4_step(t, scr, tick, skip); }

static void ops_probe(const void *p, slide_probe *out)
{
    const intro4_t *t = p;
    out->done = t->phase == INTRO4_DONE;
    out->anim = t->phase == INTRO4_ANIM;
    out->anim_done = t->anim_done;
    out->anim_hz = TK_INTRO4_PASS_HZ;
    out->subs = t->phase == INTRO4_SUBS ? t->subs : NULL;
    out->looks = out->anim || (out->subs && subs_state_now(t->subs) == SUBS_WAIT);
}

const slide_ops intro4_ops = {"intro4", ops_create, ops_destroy, ops_step, ops_probe};
