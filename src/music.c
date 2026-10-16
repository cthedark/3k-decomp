#include "music.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"
#include "opl.h"

/*
 * The player follows the original's FM driver ("FM-Music Functions, Copyright (c) Creative Technology Pte Ltd, 1990",
 * linked into MARK.EXE, STORY2.SL, WALK0.SL, CAST.SL ...). What is taken from it, and how it was established, is in
 * docs/findings.md ("The original's FM driver") and docs/port.md ("Milestone 5b"). Addresses below are CAST.SL's
 * (linear, load segment 0x1000); they are named so the code can be checked against the disassembly.
 */

enum {
    VOICES = 9,
    DRUMS = 5,            /* MIDI channels 11..15 in rhythm mode */
    DRUM_CHANNEL = 11,
    TAG_FREE = 0xff,      /* the voice's last owner is forgotten */
    TAG_RELEASED = 0x80   /* or'ed into the tag (the channel number) once the note was released */
};

/* Operator register offset of the modulator of each of the nine voices; the carrier is +3. (CAST.SL data 0x76) */
static const uint8_t k_mod_op[VOICES] = {0, 1, 2, 8, 9, 10, 16, 17, 18};

/* The twelve f-numbers of one octave (CAST.SL data 0x147): the standard AdLib set, used for every note. */
static const uint16_t k_fnum[12] = {0x157, 0x16b, 0x181, 0x198, 0x1b0, 0x1ca, 0x1e5, 0x202, 0x220, 0x241, 0x263, 0x287};

/* The five rhythm instruments, by MIDI channel 11..15: bass drum, snare, tom-tom, cymbal, hi-hat (CAST.SL data
 * 0x8a, 0x8f, 0x94): the operator, the bit in register 0xBD, and the voice whose pitch registers they use. */
static const uint8_t k_drum_op[DRUMS] = {0x10, 0x14, 0x12, 0x15, 0x11};
static const uint8_t k_drum_bit[DRUMS] = {0x10, 0x08, 0x04, 0x02, 0x01};
static const uint8_t k_drum_voice[DRUMS] = {6, 7, 8, 8, 7};

typedef struct {
    uint8_t tag;        /* TAG_FREE, the channel that holds the voice, or channel | TAG_RELEASED  (data 0x1e) */
    uint8_t note;       /* the note it plays or played (data 0x29) */
    uint16_t word;      /* pitch as written to 0xA0 (low) and 0xB0 (high, without key-on)  (data 0x4a) */
    uint16_t stamp;     /* when the note started (data 0x60), for stealing the oldest voice */
    uint8_t base;       /* 0x3f minus the patch's output level (data 0x34) */
    uint8_t ksl;        /* the patch's key-scale bits, 0xc0 mask (data 0x3f) */
} voice_t;

typedef struct {
    uint8_t base, ksl;
} drum_t;

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
    voice_t v[VOICES];
    drum_t drum[DRUMS];
    uint8_t program[16];    /* the program each channel selected (data 0x9b); 0 until one is */
    unsigned nvoices;       /* 9, or 6 in rhythm mode (data 0x18) */
    int rhythm;             /* (data 0x9a) */
    uint8_t bd;             /* register 0xBD as last written: depth bits 0xc0, rhythm 0x20, drum bits (data 0x99) */
    uint16_t now;           /* the song tick of the event being applied */
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

/* Forget every voice and program, as the driver does when a song selects its mode (controller 0x67, 0x135f3). */
static void reset_state(music_t *m)
{
    memset(m->program, 0, sizeof m->program);
    for (unsigned v = 0; v < VOICES; v++)
        m->v[v].tag = TAG_FREE;
}

static void set_mode(music_t *m, int rhythm)
{
    reset_state(m);
    m->rhythm = rhythm;
    m->nvoices = rhythm ? 6 : VOICES;
    m->bd = rhythm ? 0xe0 : 0xc0;
    wr(m, 0xbd, m->bd);
}

static void chip_init(music_t *m)
{
    wr(m, 0x01, 0x20);   /* waveform select enabled: the patches use registers 0xE0.. */
    wr(m, 0x08, 0x00);
    set_mode(m, 0);
}

/* The pitch word of a note: f-number low byte, then block << 2 | f-number high byte (0x138cc ..). The block follows the
 * octave one lower than the MIDI number (note 24 is block 1), is 0 below note 24 and stops at 7; notes above 119 all
 * give the same pitch as note 127. The driver adds a transposition that is 0 unless a program sets it; none does. */
static uint16_t pitch_word(unsigned note)
{
    unsigned n = note & 0x7f;
    unsigned block, pc;
    if (n >= 120) {
        block = 7;
        pc = 11;
    } else {
        pc = n % 12;
        block = n / 12;
        block = block ? block - 1 : 0;
        if (block > 7)
            block = 7;
    }
    return (uint16_t)(k_fnum[pc] | (block << 10));
}

/* The output level for a velocity: 0x3f minus the share (0x80 | velocity) / 256 of the patch's attenuation range
 * (0x138ac ..); the key-scale bits of the patch are kept. */
static uint8_t level_for(unsigned base, unsigned ksl, unsigned vel)
{
    return (uint8_t)((0x3fu - ((base * (vel | 0x80u)) >> 8)) | ksl);
}

/* Write the register pair that releases a voice: its pitch without the key-on bit (0x1384b ..). */
static void key_off_voice(music_t *m, unsigned v)
{
    wr(m, (uint8_t)(0xa0 + v), (uint8_t)(m->v[v].word & 0xff));
    wr(m, (uint8_t)(0xb0 + v), (uint8_t)(m->v[v].word >> 8));
}

/* Load a patch into a melodic voice (0x1364f). The carrier's level register is NOT written here (the note-on
 * writes it), and the modulator's is: seen in the driver's register log. */
static void load_voice(music_t *m, unsigned v, unsigned prog)
{
    const uint8_t *p = cmf_instrument(m->song, prog);
    if (!p) {
        m->ignored++;   /* the driver would read past its patch table; no song does */
        return;
    }
    unsigned mo = k_mod_op[v], ca = mo + 3;
    wr(m, (uint8_t)(0x20 + mo), p[0]);
    wr(m, (uint8_t)(0x20 + ca), p[1]);
    wr(m, (uint8_t)(0x40 + mo), p[2]);
    wr(m, (uint8_t)(0x60 + mo), p[4]);
    wr(m, (uint8_t)(0x60 + ca), p[5]);
    wr(m, (uint8_t)(0x80 + mo), p[6]);
    wr(m, (uint8_t)(0x80 + ca), p[7]);
    wr(m, (uint8_t)(0xe0 + mo), p[8]);
    wr(m, (uint8_t)(0xe0 + ca), p[9]);
    wr(m, (uint8_t)(0xc0 + v), p[10]);
    m->v[v].base = (uint8_t)(0x3f - (p[3] & 0x3f));
    m->v[v].ksl = p[3] & 0xc0;
}

/* Load a patch for a rhythm instrument (0x136d6 for the single-operator ones). The bass drum uses both operators
 * of voice 6; the others one operator, with the patch's MODULATOR bytes (and its modulator level for the volume). */
static void load_drum(music_t *m, unsigned d, unsigned prog)
{
    const uint8_t *p = cmf_instrument(m->song, prog);
    if (!p) {
        m->ignored++;
        return;
    }
    unsigned op = k_drum_op[d];
    if (d == 0) {
        wr(m, (uint8_t)(0x20 + op), p[0]);
        wr(m, (uint8_t)(0x23 + op), p[1]);
        wr(m, (uint8_t)(0x40 + op), p[2]);
        wr(m, (uint8_t)(0x60 + op), p[4]);
        wr(m, (uint8_t)(0x63 + op), p[5]);
        wr(m, (uint8_t)(0x80 + op), p[6]);
        wr(m, (uint8_t)(0x83 + op), p[7]);
        wr(m, (uint8_t)(0xe0 + op), p[8]);
        wr(m, (uint8_t)(0xe3 + op), p[9]);
        m->drum[d].base = (uint8_t)(0x3f - (p[3] & 0x3f));
        m->drum[d].ksl = p[3] & 0xc0;
    } else {
        wr(m, (uint8_t)(0x20 + op), p[0]);
        wr(m, (uint8_t)(0x60 + op), p[4]);
        wr(m, (uint8_t)(0x80 + op), p[6]);
        wr(m, (uint8_t)(0xe0 + op), p[8]);
        m->drum[d].base = (uint8_t)(0x3f - (p[2] & 0x3f));
        m->drum[d].ksl = p[2] & 0xc0;
    }
    wr(m, (uint8_t)(0xc0 + k_drum_voice[d]), p[10]);
}

/* Choose the voice for a note on `ch` (0x13708): a released voice of the same channel, else a free one, else a
 * released one of another channel, else the oldest one, which is cut. */
static unsigned allocate(music_t *m, unsigned ch)
{
    const unsigned n = m->nvoices;
    for (unsigned v = 0; v < n; v++)
        if (m->v[v].tag == (ch | TAG_RELEASED))
            return v;
    for (unsigned v = 0; v < n; v++)
        if (m->v[v].tag == TAG_FREE)
            return v;
    for (unsigned v = 0; v < n; v++)
        if (m->v[v].tag > 0x7f)
            return v;
    unsigned best = 0;
    uint16_t best_age = 0;
    for (unsigned v = 0; v < n; v++) {
        uint16_t age = (uint16_t)(m->now - m->v[v].stamp);
        if (age > best_age) {
            best_age = age;
            best = v;
        }
    }
    key_off_voice(m, best);
    return best;
}

static void drum_on(music_t *m, unsigned d, unsigned note, unsigned vel)
{
    m->bd |= k_drum_bit[d];
    unsigned reg = 0x40u + k_drum_op[d] + (d == 0 ? 3u : 0u);
    wr(m, (uint8_t)reg, level_for(m->drum[d].base, m->drum[d].ksl, vel));
    uint16_t w = pitch_word(note);
    unsigned v = k_drum_voice[d];
    wr(m, (uint8_t)(0xa0 + v), (uint8_t)(w & 0xff));
    wr(m, (uint8_t)(0xb0 + v), (uint8_t)(w >> 8));
    wr(m, 0xbd, m->bd);
}

static void note_on(music_t *m, unsigned ch, unsigned note, unsigned vel)
{
    if (m->rhythm && ch >= DRUM_CHANNEL) {
        drum_on(m, ch - DRUM_CHANNEL, note, vel);
        return;
    }
    unsigned v = allocate(m, ch);
    voice_t *vo = &m->v[v];
    unsigned old = vo->tag;
    vo->tag = (uint8_t)ch;
    if ((old & 0x7f) != ch)   /* the voice last played for another channel (or none): load this channel's patch */
        load_voice(m, v, m->program[ch]);
    wr(m, (uint8_t)(0x43 + k_mod_op[v]), level_for(vo->base, vo->ksl, vel));
    vo->note = (uint8_t)note;
    vo->word = pitch_word(note);
    vo->stamp = m->now;
    wr(m, (uint8_t)(0xa0 + v), (uint8_t)(vo->word & 0xff));
    wr(m, (uint8_t)(0xb0 + v), (uint8_t)(0x20 | (vo->word >> 8)));
}

static void note_off(music_t *m, unsigned ch, unsigned note)
{
    if (m->rhythm && ch >= DRUM_CHANNEL) {
        m->bd &= (uint8_t)~k_drum_bit[ch - DRUM_CHANNEL];
        wr(m, 0xbd, m->bd);
        return;
    }
    /* Only the note a voice of this channel is holding can be released; a stale off finds nothing. */
    for (unsigned v = 0; v < m->nvoices; v++)
        if (m->v[v].note == note && m->v[v].tag == ch) {
            m->v[v].tag = (uint8_t)(ch | TAG_RELEASED);
            key_off_voice(m, v);
            return;
        }
}

static void program_change(music_t *m, unsigned ch, unsigned prog)
{
    m->program[ch] = (uint8_t)prog;
    if (m->rhythm && ch >= DRUM_CHANNEL) {
        load_drum(m, ch - DRUM_CHANNEL, prog);
        return;
    }
    /* Released voices of the channel are forgotten (their patch is stale), held ones take the new patch now. */
    for (unsigned v = 0; v < VOICES; v++)
        if (m->v[v].tag == (ch | TAG_RELEASED))
            m->v[v].tag = TAG_FREE;
    for (unsigned v = 0; v < VOICES; v++)
        if (m->v[v].tag == ch)
            load_voice(m, v, prog);
}

static void apply(music_t *m, const cmf_event *e)
{
    unsigned kind = e->status & 0xf0, c = e->status & 0x0f;
    m->now = (uint16_t)e->tick;
    switch (kind) {
    case 0x90:
        if (e->b != 0) {
            note_on(m, c, e->a & 0x7f, e->b & 0x7f);
            break;
        }
        /* velocity 0 is a note-off */
        /* fall through */
    case 0x80:
        note_off(m, c, e->a & 0x7f);
        break;
    case 0xc0:
        program_change(m, c, e->a & 0x7f);
        break;
    case 0xb0:
        if (e->a == 0x67)
            set_mode(m, e->b != 0);
        else if (e->a != 0x66)   /* 0x66 is a song marker; the driver acts on no other controller */
            m->ignored++;
        break;
    default:   /* pitch bend, aftertouch, pressure: the driver does nothing with them */
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
    return music_load_at(MUSIC_INTRO_OFFSET, MUSIC_INTRO_LENGTH);
}

music_t *music_load_at(size_t offset, size_t length)
{
    uint8_t *buf = malloc(length);
    if (!buf)
        return NULL;
    if (assets_read_at("MUC.ALL", offset, buf, length) != 0) {
        fprintf(stderr, "music: cannot read MUC.ALL\n");
        free(buf);
        return NULL;
    }
    cmf_t *song = cmf_parse(buf, length);
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

/* Release every sounding note, melodic and rhythm, so the release stages play out; the song's own first
 * events then set the mode and forget the voices again. */
static void all_notes_off(music_t *m)
{
    for (unsigned v = 0; v < m->nvoices; v++)
        if (m->v[v].tag < 0x80) {
            m->v[v].tag = (uint8_t)(m->v[v].tag | TAG_RELEASED);
            key_off_voice(m, v);
        }
    if (m->bd & 0x1f) {
        m->bd &= 0xe0;
        wr(m, 0xbd, m->bd);
    }
}

static void restart(music_t *m)
{
    all_notes_off(m);
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
