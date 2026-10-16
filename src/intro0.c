#include "intro0.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"
#include "audio.h"
#include "gfx.h"
#include "lcz.h"
#include "tkrand.h"
#include "voc.h"

/* Layout facts, each read from STORY2.SL's code (addresses are in that program):
 *  - W      : STORY0.LCZ chunk 0, a 320 x 200 sprite (64,004 bytes) in a 64 KB block. The
 *             stars are indexed from the start of the block, header included (0x10692).
 *  - strip  : rows 185..199 of W, from W index 0xe744, 4,800 bytes (0x106de).
 *  - lines  : STORY0_T.LCZ chunk 0, 7 lines of 600 bytes; the first 560 bytes of a line are
 *             14 rows of 40 bytes. The buffer is zeroed up to 0x1298 bytes, so a read past the
 *             7th line gives a blank line (0x10424).
 *  - masks  : two 320 x 14 sprites in one block, at offsets 0 and 0x1388 (0x10451..0x104a9). */
enum {
    W_BLOCK = 65536,
    W_PIXELS = 64000,
    W_BYTES = SPRITE_HDR + W_PIXELS,
    STRIP_AT = 0xe744,
    STRIP_BYTES = 4800,
    LINES_BYTES = 0x1298,
    LINE_STEP = 600,
    LINE_END = 4000,        /* the loop ends when the line offset has passed this (0x10900) */
    MASK_ROWS = 14,
    MASK_BYTES = 40 * MASK_ROWS,
    MASK_PIXELS = 320 * MASK_ROWS,
    MASK_BLOCK = 0x1388 + SPRITE_HDR + MASK_PIXELS,
    MASK_ALT = 0x1388,
    NSTARS = 20,
    STAR_STEP = 0x282,      /* 642: two rows and two pixels up per pass */
    HOLD_TICKS = 0x38,      /* 56 BIOS ticks */
    MUSIC_GAP = 20,         /* passes between a clip ending and its restart (0x105e6) */
    FADE_STEPS = 64,
    FADE_BYTES = 0xf0 * 3,  /* palette entries 0..239 take part in fades */
    COL_BRIGHT = 0xf0,
    COL_DARK = 0xfe,
    RESPAWN_BASE = 0xf8c0
};

typedef enum { ST_LOAD = 1, ST_FADE, ST_HOLD } line_state;

struct intro0 {
    uint8_t w[W_BLOCK];
    uint8_t temp[STRIP_BYTES];
    uint8_t mask[MASK_BLOCK];
    uint8_t lines[LINES_BYTES];
    uint16_t star_pos[NSTARS];
    uint8_t star_under[NSTARS];
    uint8_t pal_cur[768];
    uint8_t pal_target[768];
    uint8_t pal_black[768];

    intro0_phase phase;
    unsigned fade_step;
    unsigned cur_mask;   /* offset of the mask being faded in / held */
    unsigned old_mask;   /* offset of the previous line's mask */
    unsigned line_off;   /* offset of the current line in `lines` */
    line_state state;
    unsigned col_a;      /* colour of the old line: rises 0xf0 -> 0xfe */
    unsigned col_b;      /* colour of the new line: falls 0xfe -> 0xf0 */
    uint16_t hold_until;

    voc_clip music;          /* STORY0_1.LCZ: the sound that goes with this slide; empty if none */
    unsigned music_latch;    /* the byte at [bp-4] in the original: counts the gap before a (re)start */
    unsigned music_plays;    /* how many times the clip has been started */
    int music_replay;        /* restart the clip whenever it ends, as the original's code does */
};

/* Decode chunk 0 of `file` and require exactly `want` bytes. */
static int load_chunk0(const char *file, size_t want, uint8_t *dst)
{
    size_t len = 0;
    uint8_t *data = assets_read(file, &len);
    if (!data) {
        fprintf(stderr, "intro: cannot read %s (data dir: %s)\n", file, assets_dir());
        return -1;
    }
    size_t pos = 0;
    lcz_chunk c;
    int rc = -1;
    if (lcz_next_chunk(data, len, &pos, &c) != LCZ_OK) {
        fprintf(stderr, "intro: %s has no chunk 0\n", file);
    } else {
        if (c.len != want)
            fprintf(stderr, "intro: %s chunk 0 is %zu bytes, expected %zu\n", file, c.len, want);
        else {
            memcpy(dst, c.pixels, want);
            rc = 0;
        }
        lcz_chunk_free(&c);
    }
    free(data);
    return rc;
}

intro0_t *intro0_create(screen_t *scr)
{
    intro0_t *t = calloc(1, sizeof *t);
    if (!t)
        return NULL;
    if (load_chunk0("story0.lcz", W_BYTES, t->w) != 0 || load_chunk0("story0_t.lcz", 4200, t->lines) != 0 ||
        assets_read_at("ani.lcp", 0, t->pal_target, sizeof t->pal_target) != 0) {
        free(t);
        return NULL;
    }
    for (int i = 0; i < 768; i++)
        if (t->pal_target[i] > 63) {
            fprintf(stderr, "intro: ani.lcp does not start with a 6-bit palette\n");
            free(t);
            return NULL;
        }
    if (sprite_w(t->w) != 320 || sprite_h(t->w) != 200) {
        fprintf(stderr, "intro: story0.lcz chunk 0 has unexpected size %ux%u\n", sprite_w(t->w), sprite_h(t->w));
        free(t);
        return NULL;
    }

    tk_srand(1);   /* a fresh program: the seed in its data image (docs/code.md) */

    sprite_set_size(t->mask, 320, MASK_ROWS);
    sprite_set_size(t->mask + MASK_ALT, 320, MASK_ROWS);

    /* Entries 240..255 keep the file's colours; 0..239 start black (0x1032d, 0x1033e). */
    memcpy(scr->pal, t->pal_target, 768);
    memset(scr->pal, 0, FADE_BYTES);
    memset(t->pal_cur, 0, sizeof t->pal_cur);
    memcpy(scr->pix, t->w + SPRITE_HDR, W_PIXELS);   /* 0x103a0 */

    /* Star start positions (0x104b8..0x10504): rand(), minus 0x7d00 if above it, doubled, plus 0x320. */
    for (int i = 0; i < NSTARS; i++) {
        unsigned r = (unsigned)tk_rand();
        if (r > 0x7d00)
            r -= 0x7d00;
        t->star_pos[i] = (uint16_t)((r << 1) + 0x320);
    }

    /* STORY0_1.LCZ is one VOC file (4-bit ADPCM, 20.9 s), read whole by the original (0x10370) and
     * played from byte 0x1a, after the header. Only loaded when there is somewhere to play it. */
    if (audio_enabled()) {
        size_t len = 0;
        uint8_t *snd = assets_read("story0_1.lcz", &len);
        if (!snd || voc_decode(snd, len, &t->music) != 0)
            fprintf(stderr, "intro: no sound for slide 0, continuing silently\n");
        free(snd);
    }

    t->phase = INTRO0_FADE_IN;
    t->cur_mask = 0;
    t->old_mask = MASK_ALT;
    t->line_off = 0;
    t->state = ST_LOAD;
    t->col_a = COL_BRIGHT;
    t->col_b = COL_DARK;
    return t;
}

void intro0_free(intro0_t *t)
{
    if (t) {
        if (t->music.pcm)
            audio_stop();
        voc_free(&t->music);
    }
    free(t);
}

void intro0_set_music_replay(intro0_t *t, int on)
{
    t->music_replay = on;
}

/* The sound handling at the top of every pass (0x10590..0x1060e). When the clip is not playing
 * and the gap counter is idle, the counter starts; it counts passes, and when it passes
 * MUSIC_GAP the clip is started. While it plays nothing happens. So the clip first starts on the
 * 20th pass, and, as the original's code reads, starts again 20 passes after it ends. */
static void music_pass(intro0_t *t)
{
    if (!t->music.pcm)
        return;
    if (!audio_playing() && t->music_latch == 0 && (t->music_plays == 0 || t->music_replay)) {
        t->music_latch = 1;
        audio_stop();
    }
    if (t->music_latch != 0 && ++t->music_latch > MUSIC_GAP) {
        t->music_latch = 0;
        t->music_plays++;
        audio_play(t->music.pcm, t->music.frames, t->music.rate);
    }
}

intro0_phase intro0_phase_now(const intro0_t *t)
{
    return t->phase;
}

static void fade_step(intro0_t *t, screen_t *scr, const uint8_t *target)
{
    gfx_palette_step(t->pal_cur, target, FADE_BYTES);
    screen_set_palette(scr, t->pal_cur, 0, 240);
    t->fade_step++;
}

/* The subtitle state machine (0x10705 .. 0x10990). Returns non-zero when the loop ends. */
static int subtitles(intro0_t *t, unsigned tick)
{
    uint8_t *strip = t->w + STRIP_AT;
    const uint8_t *cur = t->mask + t->cur_mask, *old = t->mask + t->old_mask;

    if (t->state == ST_LOAD) {
        gfx_expand_bits(t->mask + t->cur_mask + SPRITE_HDR, t->lines + t->line_off, MASK_BYTES, 0x10);
        t->state = ST_FADE;
        t->col_b = COL_DARK;
        t->col_a = COL_BRIGHT;
    }
    if (t->state == ST_FADE) {
        if (t->col_a < t->col_b) {
            gfx_stamp(strip, SCREEN_W, 0, cur, (uint8_t)t->col_b);
            gfx_stamp(strip, SCREEN_W, 1, cur, (uint8_t)t->col_b);
            gfx_stamp(strip, SCREEN_W, 0, old, (uint8_t)t->col_a);
            gfx_stamp(strip, SCREEN_W, 1, old, (uint8_t)t->col_a);
        } else {
            gfx_stamp(strip, SCREEN_W, 0, old, (uint8_t)t->col_a);
            gfx_stamp(strip, SCREEN_W, 1, old, (uint8_t)t->col_a);
            gfx_stamp(strip, SCREEN_W, 0, cur, (uint8_t)t->col_b);
            gfx_stamp(strip, SCREEN_W, 1, cur, (uint8_t)t->col_b);
        }
        t->col_a++;
        t->col_b--;
        if (t->col_a == 0xff) {
            t->state = ST_HOLD;
            t->hold_until = (uint16_t)(tick + HOLD_TICKS);
            if (t->line_off > LINE_END)
                return 1;   /* the blank 8th line is in: leave without a final copy (0x1090a) */
        }
    }
    if (t->state == ST_HOLD) {
        gfx_stamp(strip, SCREEN_W, 0, cur, COL_BRIGHT);
        gfx_stamp(strip, SCREEN_W, 1, cur, COL_BRIGHT);
        if ((uint16_t)tick >= t->hold_until) {
            unsigned swap = t->cur_mask;
            t->cur_mask = t->old_mask;
            t->old_mask = swap;
            t->line_off += LINE_STEP;
            t->state = ST_LOAD;
        }
    }
    return 0;
}

static void run_pass(intro0_t *t, screen_t *scr, unsigned tick)
{
    uint8_t *w = t->w;

    music_pass(t);

    /* 1. rotate left, scroll up, wrap the old top row to the bottom (0x10619..0x1066a) */
    gfx_rotate_rows_left(w);
    memcpy(t->temp, w + SPRITE_HDR, SCREEN_W);
    gfx_scroll_up(w, 1);
    memcpy(w + SPRITE_HDR + (size_t)199 * SCREEN_W, t->temp, SCREEN_W);

    /* 2. stars: remember what they cover, draw them with their table offset as colour */
    for (int i = 0; i < NSTARS; i++) {
        t->star_under[i] = w[t->star_pos[i]];
        w[t->star_pos[i]] = (uint8_t)(i * 3);
    }

    /* 3. save the rows under the subtitle strip */
    memcpy(t->temp, w + STRIP_AT, STRIP_BYTES);

    /* 4. subtitles */
    int finished = subtitles(t, tick);
    if (finished) {
        /* 0x10a59: with sound, wait until the clip is over before fading out. The original does
         * not look at the keyboard here. */
        t->phase = t->music.pcm ? INTRO0_MUSIC_WAIT : INTRO0_FADE_OUT;
        t->fade_step = 0;
        return;
    }

    /* 5. to the screen */
    memcpy(scr->pix, w + SPRITE_HDR, W_PIXELS);

    /* 6. restore the strip, restore the star pixels, move the stars */
    memcpy(w + STRIP_AT, t->temp, STRIP_BYTES);
    for (int i = 0; i < NSTARS; i++) {
        uint16_t pos = t->star_pos[i];
        w[pos] = t->star_under[i];
        pos = (uint16_t)(pos - STAR_STEP);
        if (pos < STAR_STEP)
            pos = (uint16_t)(tk_rand() / 0x67 + RESPAWN_BASE);
        t->star_pos[i] = pos;
    }
}

intro0_phase intro0_step(intro0_t *t, screen_t *scr, unsigned tick)
{
    switch (t->phase) {
    case INTRO0_FADE_IN:
        fade_step(t, scr, t->pal_target);
        if (t->fade_step >= FADE_STEPS)
            t->phase = INTRO0_RUN;
        break;
    case INTRO0_RUN:
        run_pass(t, scr, tick);
        break;
    case INTRO0_MUSIC_WAIT:
        if (!audio_playing()) {
            audio_stop();   /* 0x10a79 */
            t->phase = INTRO0_FADE_OUT;
        }
        break;
    case INTRO0_FADE_OUT:
        fade_step(t, scr, t->pal_black);
        if (t->fade_step >= FADE_STEPS)
            t->phase = INTRO0_DONE;
        break;
    case INTRO0_DONE:
        break;
    }
    return t->phase;
}
