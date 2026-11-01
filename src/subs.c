#include "subs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "gfx.h"
#include "l10n.h"
#include "voice.h"

/* Layout facts, from STORY2.SL's player (addresses in that program):
 *  - the strip is the 320x14 screen rectangle at offset 0xe740 (row 185), saved as a sprite `bg`
 *    (0x137c8) and redrawn from it before every pass;
 *  - two mask sprites (320x14) hold the line being faded in and the one being faded out; a line is
 *    expanded into the one that is free (0x12984, set bit -> 0x10);
 *  - every pass stamps the masks twice (x offset 0 and 1) in colours 0xf0..0xfe of the palette,
 *    a grey ramp that the palette fades never touch. */
enum {
    LINE_STEP = 600,
    MASK_BYTES = 40 * 14,
    STRIP_AT = 0xe740,
    STRIP_W = 320,
    STRIP_H = 14,
    STRIP_PIX = STRIP_W * STRIP_H,
    STRIP_SPRITE = SPRITE_HDR + STRIP_PIX + 8,   /* + slack for the x = 1 stamp's last pixel */
    PASSES = 14,
    HOLD_TICKS = 74,                             /* 0x12811: the no-sound wait */
    COL_BRIGHT = 0xf0,
    COL_DARK = 0xfe
};

struct subs {
    uint8_t *lines;
    size_t len;
    uint8_t bg[STRIP_SPRITE];
    uint8_t work[STRIP_SPRITE];
    uint8_t mask[2][STRIP_SPRITE];

    subs_state state;
    unsigned pass_done, out_done;
    unsigned line_off;
    unsigned pass_in_line;
    unsigned cur_mask, old_mask;
    uint8_t col_a, col_b, out_col;
    uint16_t wait_end;

    voice_bank_t *voice;   /* NULL: the timed hold */
    int clip_started;      /* the current hold is waiting for a clip */
};

void subs_translate(uint8_t *lines, size_t len, unsigned slide)
{
    for (size_t k = 0; (k + 1) * LINE_STEP <= len; k++) {
        char id[24];
        snprintf(id, sizeof id, "story%u.%u", slide, (unsigned)k + 1);
        l10n_bitmap(id, lines + k * LINE_STEP, 320, 14, 40, 0);
    }
}

subs_t *subs_create(const uint8_t *lines, size_t len)
{
    if (len == 0 || len % LINE_STEP != 0)
        return NULL;
    subs_t *s = calloc(1, sizeof *s);
    if (!s)
        return NULL;
    s->lines = malloc(len);
    if (!s->lines) {
        free(s);
        return NULL;
    }
    memcpy(s->lines, lines, len);
    s->len = len;
    return s;
}

void subs_free(subs_t *s)
{
    if (s) {
        if (s->clip_started)
            audio_stop();
        voice_bank_free(s->voice);
        free(s->lines);
        free(s);
    }
}

int subs_load_voice(subs_t *s, unsigned slide)
{
    if (!audio_enabled())
        return 0;
    voice_bank_free(s->voice);
    s->voice = voice_bank_load(slide, (unsigned)(s->len / LINE_STEP));
    return s->voice ? 0 : -1;
}

subs_state subs_state_now(const subs_t *s) { return s->state; }
unsigned subs_pass_done(const subs_t *s) { return s->pass_done; }
unsigned subs_out_done(const subs_t *s) { return s->out_done; }

static void expand_line(subs_t *s)
{
    gfx_expand_bits(s->mask[s->cur_mask] + SPRITE_HDR, s->lines + s->line_off, MASK_BYTES, 0x10);
}

static void stamp2(uint8_t *work, const uint8_t *mask, uint8_t colour)
{
    gfx_stamp(work + SPRITE_HDR, STRIP_W, 0, mask, colour);
    gfx_stamp(work + SPRITE_HDR, STRIP_W, 1, mask, colour);
}

void subs_begin(subs_t *s, screen_t *scr)
{
    sprite_set_size(s->bg, STRIP_W, STRIP_H);
    gfx_copy_rect(s->bg + SPRITE_HDR, scr->pix + STRIP_AT, SCREEN_W, STRIP_W, STRIP_H);
    sprite_set_size(s->mask[0], STRIP_W, STRIP_H);
    sprite_set_size(s->mask[1], STRIP_W, STRIP_H);
    memset(s->mask[1] + SPRITE_HDR, 0, STRIP_PIX);
    s->cur_mask = 0;
    s->old_mask = 1;
    s->line_off = 0;
    s->pass_done = 0;
    s->out_done = 0;
    s->pass_in_line = 0;
    s->col_a = COL_BRIGHT;
    s->col_b = COL_DARK;
    expand_line(s);
    s->state = SUBS_PASS;
}

/* One cross-fade pass (0x1250e..0x12771). */
static void cross_fade_pass(subs_t *s, screen_t *scr)
{
    const uint8_t *cur = s->mask[s->cur_mask], *old = s->mask[s->old_mask];
    memcpy(s->work, s->bg, STRIP_SPRITE);
    if (s->col_a < s->col_b) {
        stamp2(s->work, cur, s->col_b);
        stamp2(s->work, old, s->col_a);
    } else {
        stamp2(s->work, old, s->col_a);
        stamp2(s->work, cur, s->col_b);
    }
    if (s->pass_in_line == PASSES - 1) {   /* last pass: only the new line, fully bright */
        memcpy(s->work, s->bg, STRIP_SPRITE);
        stamp2(s->work, cur, COL_BRIGHT);
    }
    gfx_draw_sprite(scr, STRIP_AT, s->work);
    s->col_a++;
    s->col_b--;
    s->pass_in_line++;
    s->pass_done++;
}

subs_state subs_step(subs_t *s, screen_t *scr, unsigned tick, int skip)
{
    switch (s->state) {
    case SUBS_PASS:
        cross_fade_pass(s, scr);
        if (s->pass_in_line >= PASSES) {
            unsigned t = s->cur_mask;   /* the new line becomes the old one */
            s->cur_mask = s->old_mask;
            s->old_mask = t;
            s->wait_end = (uint16_t)(tick + HOLD_TICKS);
            s->state = SUBS_WAIT;
            /* 0x1279f: with sound, start this line's clip now and wait for it instead. */
            const voc_clip *clip = audio_enabled() ? voice_bank_clip(s->voice, s->line_off / LINE_STEP) : NULL;
            if (clip) {
                audio_play(clip->pcm, clip->frames, clip->rate);
                s->clip_started = 1;
            }
        }
        break;
    case SUBS_WAIT: {
        int over;
        if (s->clip_started) {
            over = !audio_playing();
            if (skip) {   /* 0x127ee: Space ends the hold and silences the clip */
                audio_stop();
                over = 1;
            }
            if (over)
                s->clip_started = 0;
        } else {
            over = skip || (int16_t)((uint16_t)tick - s->wait_end) >= 0;
        }
        if (over) {
            s->line_off += LINE_STEP;
            if (s->line_off > s->len - 300) {
                s->out_col = COL_BRIGHT;
                s->state = SUBS_OUT;
            } else {
                s->pass_in_line = 0;
                s->col_a = COL_BRIGHT;
                s->col_b = COL_DARK;
                expand_line(s);
                s->state = SUBS_PASS;
            }
        }
        break;
    }
    case SUBS_OUT:
        if (s->out_done < PASSES) {   /* 0x12870..0x1291f: the last line, through the grey ramp */
            memcpy(s->work, s->bg, STRIP_SPRITE);
            stamp2(s->work, s->mask[s->old_mask], s->out_col);
            gfx_draw_sprite(scr, STRIP_AT, s->work);
            s->out_col++;
            s->out_done++;
        } else {
            gfx_draw_sprite(scr, STRIP_AT, s->bg);   /* 0x12928: put the strip back */
            s->state = SUBS_DONE;
        }
        break;
    case SUBS_DONE:
        break;
    }
    return s->state;
}
