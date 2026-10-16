#include "music.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"
#include "opl.h"

enum { OPL_CHANNELS = 9 };

/* Operator register offsets of the modulator of each of the nine channels; the carrier is +3. */
static const uint8_t k_mod_op[OPL_CHANNELS] = {0, 1, 2, 8, 9, 10, 16, 17, 18};

typedef struct {
    int sounding;       /* a note is held on this channel */
    uint8_t note;       /* the held note (MIDI number, before transposition) */
    int patch;          /* the loaded patch number, -1 if none */
    int transpose;      /* controller 0x68/0x69: 1/128 semitones, signed */
    uint8_t fnum_hi;    /* register 0xB0 value without the key-on bit, as last written */
} channel_t;

struct music {
    cmf_t *song;
    opl_t *chip;
    cmf_cursor cur;
    cmf_event ev;
    int have_ev;
    int at_end;
    uint64_t end_sample;
    uint64_t pos;           /* samples generated in this pass through the song */
    uint64_t ev_sample;
    unsigned tps;
    unsigned gain;          /* percent */
    unsigned loops;
    unsigned ignored;
    int warned_rhythm;
    channel_t ch[OPL_CHANNELS];
    uint8_t depth;          /* register 0xBD */
    music_trace_fn trace;
    void *trace_user;
};

unsigned music_rate(void) { return opl_native_rate(); }

static void wr(music_t *m, uint8_t reg, uint8_t val)
{
    if (m->trace)
        m->trace(m->trace_user, m->pos, reg, val);
    opl_write(m->chip, reg, val);
}

/* The chip set-up the songs rely on: waveform select enabled (the patches use registers 0xE0..),
 * melody mode, AM and vibrato depth at the CMF default (both on). */
static void chip_init(music_t *m)
{
    wr(m, 0x01, 0x20);
    wr(m, 0x08, 0x00);
    m->depth = 0xc0;
    wr(m, 0xbd, m->depth);
}

/* Release every sounding note. The register values stay, so the release stages play out. */
static void all_keys_off(music_t *m)
{
    for (int c = 0; c < OPL_CHANNELS; c++) {
        if (m->ch[c].sounding)
            wr(m, (uint8_t)(0xb0 + c), m->ch[c].fnum_hi);
        m->ch[c].sounding = 0;
    }
}

static void load_patch(music_t *m, unsigned c, unsigned prog)
{
    const uint8_t *p = cmf_instrument(m->song, prog);
    if (!p) {
        m->ignored++;   /* ASSUMED: a program the song does not define leaves the channel as it is */
        return;
    }
    unsigned mo = k_mod_op[c], ca = mo + 3;
    wr(m, (uint8_t)(0x20 + mo), p[0]);
    wr(m, (uint8_t)(0x20 + ca), p[1]);
    wr(m, (uint8_t)(0x40 + mo), p[2]);
    wr(m, (uint8_t)(0x40 + ca), p[3]);
    wr(m, (uint8_t)(0x60 + mo), p[4]);
    wr(m, (uint8_t)(0x60 + ca), p[5]);
    wr(m, (uint8_t)(0x80 + mo), p[6]);
    wr(m, (uint8_t)(0x80 + ca), p[7]);
    wr(m, (uint8_t)(0xe0 + mo), p[8]);
    wr(m, (uint8_t)(0xe0 + ca), p[9]);
    wr(m, (uint8_t)(0xc0 + c), p[10] & 0x0f);
    m->ch[c].patch = (int)prog;
}

/* F-number and block for a pitch given in semitones (MIDI numbering, A4 = 69 = 440 Hz; ASSUMED,
 * the original driver's tuning was not read). The lowest block whose f-number fits in 10 bits is
 * used, for the best resolution; out-of-range pitches are clamped to the chip's range. */
static void pitch_to_fnum(double semitones, unsigned *fnum, unsigned *block)
{
    double hz = 440.0 * pow(2.0, (semitones - 69.0) / 12.0);
    double rate = (double)opl_native_rate();
    for (unsigned b = 0; b < 8; b++) {
        double f = hz * (double)(1u << (20 - b)) / rate;
        if (f < 1024.0 || b == 7) {
            long v = lround(f);
            if (v < 0)
                v = 0;
            if (v > 1023)
                v = 1023;
            *fnum = (unsigned)v;
            *block = b;
            return;
        }
    }
}

static void key_off(music_t *m, unsigned c)
{
    if (m->ch[c].sounding)
        wr(m, (uint8_t)(0xb0 + c), m->ch[c].fnum_hi);
    m->ch[c].sounding = 0;
}

static void note_on(music_t *m, unsigned c, unsigned note, unsigned vel)
{
    channel_t *ch = &m->ch[c];
    key_off(m, c);   /* a note-on on a sounding channel releases the old note first */

    /* ASSUMED: velocity goes straight into the carrier's output level (0 = loudest), keeping the
     * patch's key-scale bits; in the additive connection both operators are carriers. */
    unsigned level = 63u - (vel >> 1);
    const uint8_t *p = ch->patch >= 0 ? cmf_instrument(m->song, (unsigned)ch->patch) : NULL;
    if (p) {
        unsigned mo = k_mod_op[c], ca = mo + 3;
        wr(m, (uint8_t)(0x40 + ca), (uint8_t)((p[3] & 0xc0) | level));
        if (p[10] & 1)
            wr(m, (uint8_t)(0x40 + mo), (uint8_t)((p[2] & 0xc0) | level));
    }

    unsigned fnum, block;
    pitch_to_fnum((double)note + (double)ch->transpose / 128.0, &fnum, &block);
    wr(m, (uint8_t)(0xa0 + c), (uint8_t)(fnum & 0xff));
    ch->fnum_hi = (uint8_t)((block << 2) | (fnum >> 8));
    wr(m, (uint8_t)(0xb0 + c), (uint8_t)(0x20 | ch->fnum_hi));
    ch->sounding = 1;
    ch->note = (uint8_t)note;
}

static void apply(music_t *m, const cmf_event *e)
{
    unsigned kind = e->status & 0xf0, c = e->status & 0x0f;
    if (c >= OPL_CHANNELS) {
        m->ignored++;   /* ASSUMED: melody mode, MIDI channel n = OPL channel n */
        return;
    }
    switch (kind) {
    case 0x90:
        if (e->b != 0) {
            note_on(m, c, e->a & 0x7f, e->b & 0x7f);
            break;
        }
        /* velocity 0 is a note-off */
        /* fall through */
    case 0x80:
        /* Only the note the channel is playing can be released. */
        if (m->ch[c].sounding && m->ch[c].note == (e->a & 0x7f))
            key_off(m, c);
        break;
    case 0xc0:
        load_patch(m, c, e->a & 0x7f);
        break;
    case 0xb0:
        switch (e->a) {
        case 0x63:   /* AM / vibrato depth: bit 0 = vibrato (0xBD bit 6), bit 1 = tremolo (bit 7) */
            m->depth = (uint8_t)((m->depth & 0x3f) | ((e->b & 1) ? 0x40 : 0) | ((e->b & 2) ? 0x80 : 0));
            wr(m, 0xbd, m->depth);
            break;
        case 0x67:
            if (e->b != 0 && !m->warned_rhythm) {
                fprintf(stderr, "music: rhythm mode (controller 0x67) is not supported, ignored\n");
                m->warned_rhythm = 1;
            }
            if (e->b != 0)
                m->ignored++;
            break;
        case 0x68:
            m->ch[c].transpose = (int)(e->b & 0x7f);
            break;
        case 0x69:
            m->ch[c].transpose = -(int)(e->b & 0x7f);
            break;
        case 0x66:   /* song marker: only a status flag for a music player */
            break;
        default:
            m->ignored++;
            break;
        }
        break;
    default:   /* pitch bend (ignored by the original CMF player), aftertouch, pressure */
        m->ignored++;
        break;
    }
}

music_t *music_create(cmf_t *song)
{
    if (!song)
        return NULL;
    music_t *m = calloc(1, sizeof *m);
    if (!m)
        return NULL;
    m->chip = opl_create();
    if (!m->chip) {
        free(m);
        return NULL;
    }
    m->song = song;
    m->gain = 100;
    m->tps = cmf_ticks_per_second(song);
    if (m->tps == 0)
        m->tps = 96;
    for (int c = 0; c < OPL_CHANNELS; c++)
        m->ch[c].patch = -1;
    chip_init(m);
    cmf_rewind(song, &m->cur);
    return m;
}

void music_free(music_t *m)
{
    if (!m)
        return;
    opl_free(m->chip);
    cmf_free(m->song);
    free(m);
}

music_t *music_load_intro(void)
{
    uint8_t *buf = malloc(MUSIC_INTRO_LENGTH);
    if (!buf)
        return NULL;
    if (assets_read_at("MUC.ALL", MUSIC_INTRO_OFFSET, buf, MUSIC_INTRO_LENGTH) != 0) {
        fprintf(stderr, "music: cannot read MUC.ALL\n");
        free(buf);
        return NULL;
    }
    cmf_t *song = cmf_parse(buf, MUSIC_INTRO_LENGTH);
    free(buf);
    music_t *m = music_create(song);
    if (!m)
        cmf_free(song);
    return m;
}

void music_set_gain(music_t *m, unsigned percent) { m->gain = percent; }
unsigned music_loops(const music_t *m) { return m->loops; }
unsigned music_ignored_events(const music_t *m) { return m->ignored; }

void music_set_trace(music_t *m, music_trace_fn fn, void *user)
{
    m->trace = fn;
    m->trace_user = user;
}

static uint64_t tick_to_sample(const music_t *m, uint32_t tick)
{
    return (uint64_t)tick * opl_native_rate() / m->tps;
}

static void restart(music_t *m)
{
    all_keys_off(m);
    cmf_rewind(m->song, &m->cur);
    m->have_ev = 0;
    m->at_end = 0;
    m->pos = 0;
    m->loops++;
}

void music_render(music_t *m, int16_t *out, size_t frames)
{
    while (frames > 0) {
        if (!m->have_ev && !m->at_end) {
            if (cmf_next(&m->cur, &m->ev)) {
                m->have_ev = 1;
                m->ev_sample = tick_to_sample(m, m->ev.tick);
            } else {
                m->at_end = 1;
                m->end_sample = tick_to_sample(m, m->cur.tick);
            }
        }
        uint64_t target = m->have_ev ? m->ev_sample : m->end_sample;
        if (m->pos < target) {
            uint64_t left = target - m->pos;
            size_t n = left < frames ? (size_t)left : frames;
            opl_generate(m->chip, out, n);
            m->pos += n;
            out += n;
            frames -= n;
            continue;
        }
        if (m->have_ev) {
            apply(m, &m->ev);
            m->have_ev = 0;
        } else if (m->end_sample == 0) {
            /* A song of zero length (no events, or all at tick 0) would restart without ever
             * producing a sample, so it never loops: the chip just rings out. This runs in the
             * audio callback, where spinning would freeze the sound. */
            opl_generate(m->chip, out, frames);
            m->pos += frames;
            return;
        } else {
            restart(m);
        }
    }
}

void music_render_cb(void *user, int16_t *out, size_t frames)
{
    music_t *m = user;
    music_render(m, out, frames);
    if (m->gain != 100)
        for (size_t i = 0; i < frames; i++) {
            int32_t v = (int32_t)out[i] * (int32_t)m->gain / 100;
            out[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
        }
}
