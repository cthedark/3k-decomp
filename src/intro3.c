#include "intro3.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gfx.h"
#include "timing.h"

/* Layout facts, each read from STORY2.SL's code (addresses are in that program):
 *  - bg      : STORY3.LCZ chunk 0, 64,000 bytes (0x11247). The sparkles are drawn into it for good,
 *              and every pass copies it, shifted down, into the work picture;
 *  - figure  : STORY3_1.LCZ chunk 0, a 168x143 sprite with header (0x11298), drawn into the work
 *              picture at 0x478c (row 57, column 76);
 *  - sparkle : STORY3_1.LCZ chunk 1, three headerless 10x11 frames (0x112ba, at +0x61a8 in the
 *              figure's buffer), drawn twice into bg at 0x57a (row 4, column 122) and 0x6fb (row 5,
 *              column 187), 0 transparent (0x135e4);
 *  - lines   : STORY3_T.LCZ chunk 0, 10 lines of 600 bytes. */
enum {
    BG_BYTES = 64000,
    FIG_W = 168,
    FIG_H = 143,
    FIG_BYTES = SPRITE_HDR + FIG_W * FIG_H,   /* 24,028 */
    FIG_AT = 0x478c,
    FIG_END = 0xa04c,                         /* the loop ends when the figure's offset reaches this */
    SPARK_W = 10,
    SPARK_H = 11,
    SPARK_BYTES = SPARK_W * SPARK_H,
    SPARK_FRAMES = 3,
    SPARK_A = 0x57a,
    SPARK_B = 0x6fb,
    SCROLL_EVERY = 5,                         /* the update counter must pass 5: every sixth update */
    LINES_BYTES = 6000
};

struct intro3 {
    uint8_t bg[BG_BYTES];
    uint8_t work[65536];
    uint8_t fig[FIG_BYTES + 8];
    uint8_t spark[SPARK_FRAMES * SPARK_BYTES];
    uint8_t pal1[768], pal2[768], cur[768], black[768];
    subs_t *subs;

    intro3_phase phase;
    unsigned fade_step;

    unsigned anim_done;
    unsigned spark_at;     /* offset of the sparkle frame in its buffer, 0, 110, 220 */
    unsigned fig_off;      /* offset of the figure in the work picture */
    unsigned shift;        /* rows the picture has scrolled down, as a byte offset in `bg` terms */
    uint8_t half;          /* the figure and the scroll move on every second pass */
    uint8_t ctr;
};

intro3_t *intro3_create(screen_t *scr, int faithful_keys)
{
    (void)faithful_keys;
    intro3_t *t = calloc(1, sizeof *t);
    if (!t)
        return NULL;

    uint8_t *lines = malloc(LINES_BYTES);
    int ok = lines != NULL;
    if (ok) {
        uint8_t *d1[1] = {t->bg};
        size_t w1[1] = {BG_BYTES};
        uint8_t *d2[2] = {t->fig, t->spark};
        size_t w2[2] = {FIG_BYTES, SPARK_FRAMES * SPARK_BYTES};
        uint8_t *d3[1] = {lines};
        size_t w3[1] = {LINES_BYTES};
        ok = slide_load_chunks("story3.lcz", 1, d1, w1) == 0 && slide_load_chunks("story3_1.lcz", 2, d2, w2) == 0 &&
             slide_load_chunks("story3_t.lcz", 1, d3, w3) == 0 && slide_load_palettes(t->pal1, t->pal2) == 0;
    }
    if (ok && (sprite_w(t->fig) != FIG_W || sprite_h(t->fig) != FIG_H)) {
        fprintf(stderr, "intro3: story3_1.lcz chunk 0 is %ux%u, expected %dx%d\n", sprite_w(t->fig), sprite_h(t->fig),
                FIG_W, FIG_H);
        ok = 0;
    }
    if (ok) {
        subs_translate(lines, LINES_BYTES, 3);
        t->subs = subs_create(lines, LINES_BYTES);
        if (t->subs && subs_load_voice(t->subs, 3) != 0)
            fprintf(stderr, "intro3: no voice-over, using the timed hold\n");
        ok = t->subs != NULL;
    }
    free(lines);
    if (!ok) {
        subs_free(t->subs);
        free(t);
        return NULL;
    }

    /* 0x112d1, 0x112e3: the picture, then the figure, straight onto the screen. */
    memcpy(scr->pix, t->bg, BG_BYTES);
    {
        const uint8_t *px = t->fig + SPRITE_HDR;
        for (unsigned r = 0; r < FIG_H; r++)
            for (unsigned c = 0; c < FIG_W; c++) {
                size_t o = (size_t)FIG_AT + r * SCREEN_W + c;
                if (o < SCREEN_PIXELS && px[r * FIG_W + c] != 0)
                    scr->pix[o] = px[r * FIG_W + c];
            }
    }
    slide_start_palette(scr, t->cur, t->pal1);

    t->phase = INTRO3_FADE_DIM;
    t->fig_off = FIG_AT;
    return t;
}

void intro3_free(intro3_t *t)
{
    if (t) {
        subs_free(t->subs);
        free(t);
    }
}

intro3_phase intro3_phase_now(const intro3_t *t) { return t->phase; }

/* Draw a headerless w x h block into `dst` (row stride 320), skipping 0 pixels (0x135e4). */
static void blit_block(uint8_t *dst, const uint8_t *src, unsigned w, unsigned h)
{
    for (unsigned r = 0; r < h; r++)
        for (unsigned c = 0; c < w; c++)
            if (src[r * w + c] != 0)
                dst[r * SCREEN_W + c] = src[r * w + c];
}

/* One pass of the animation loop (0x1132e..0x1145a). Returns non-zero when the loop is over. */
static int anim_pass(intro3_t *t, screen_t *scr, int skip)
{
    /* The sparkles go into the picture itself, so they stay; the frame changes every pass. */
    blit_block(t->bg + SPARK_A, t->spark + t->spark_at, SPARK_W, SPARK_H);
    blit_block(t->bg + SPARK_B, t->spark + t->spark_at, SPARK_W, SPARK_H);
    t->spark_at += SPARK_BYTES;
    if (t->spark_at > (SPARK_FRAMES - 1) * SPARK_BYTES)
        t->spark_at = 0;

    /* The picture, `shift` bytes lower. The top rows are not rewritten: they keep the copy of the
     * picture's first row that an earlier, smaller shift left there. */
    memcpy(t->work + t->shift, t->bg, BG_BYTES - t->shift);
    gfx_blit_masked(t->work + t->fig_off, SCREEN_W, t->fig);
    memcpy(scr->pix, t->work, BG_BYTES);

    if (t->half == 0) {
        t->half = 1;
    } else {
        t->half = 0;
        t->fig_off += SCREEN_W;
        t->fig[2]--;   /* the figure's height, low byte: one row less (0x113f1) */
        if (++t->ctr > SCROLL_EVERY) {
            t->ctr = 0;
            t->shift += SCREEN_W;
        }
    }
    t->anim_done++;
    return skip || t->fig_off >= FIG_END;
}

static void fade_to(intro3_t *t, screen_t *scr, const uint8_t *target, intro3_phase next)
{
    slide_fade_step(scr, t->cur, target);
    if (++t->fade_step >= SLIDE_FADE_STEPS) {
        t->fade_step = 0;
        t->phase = next;
        if (next == INTRO3_SUBS)
            subs_begin(t->subs, scr);
    }
}

intro3_phase intro3_step(intro3_t *t, screen_t *scr, unsigned tick, int skip)
{
    switch (t->phase) {
    case INTRO3_FADE_DIM:
        fade_to(t, scr, t->pal2, INTRO3_FADE_BRIGHT);
        break;
    case INTRO3_FADE_BRIGHT:
        fade_to(t, scr, t->pal1, INTRO3_ANIM);
        break;
    case INTRO3_ANIM:
        if (anim_pass(t, scr, skip))
            t->phase = INTRO3_FADE_DIM2;
        break;
    case INTRO3_FADE_DIM2:
        fade_to(t, scr, t->pal2, INTRO3_SUBS);
        break;
    case INTRO3_SUBS:
        if (subs_step(t->subs, scr, tick, skip) == SUBS_DONE)
            t->phase = INTRO3_FADE_BLACK;
        break;
    case INTRO3_FADE_BLACK:
        fade_to(t, scr, t->black, INTRO3_DONE);
        break;
    case INTRO3_DONE:
        break;
    }
    return t->phase;
}

/* The uniform interface (slide.h). */
static void *ops_create(screen_t *scr, int faithful_keys) { return intro3_create(scr, faithful_keys); }
static void ops_destroy(void *t) { intro3_free(t); }
static void ops_step(void *t, screen_t *scr, unsigned tick, int skip) { intro3_step(t, scr, tick, skip); }

static void ops_probe(const void *p, slide_probe *out)
{
    const intro3_t *t = p;
    out->done = t->phase == INTRO3_DONE;
    out->anim = t->phase == INTRO3_ANIM;
    out->anim_done = t->anim_done;
    out->anim_hz = TK_INTRO3_PASS_HZ;
    out->subs = t->phase == INTRO3_SUBS ? t->subs : NULL;
    out->looks = out->anim || (out->subs && subs_state_now(t->subs) == SUBS_WAIT);
}

const slide_ops intro3_ops = {"intro3", ops_create, ops_destroy, ops_step, ops_probe};
