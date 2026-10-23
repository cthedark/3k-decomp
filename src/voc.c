#include "voc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SIG "Creative Voice File\x1a"
#define SIG_LEN 20

/* Sound Blaster 4-bit ADPCM. Each byte holds two nibbles; a nibble is a sign bit (0x8) and a
 * 3-bit magnitude m. The output level moves by sign * ((m << r) + ((1 << r) >> 1)) where r is the
 * current step size 0..3, clamped to 0..255. After a nibble the step grows when m >= 5 (up to 3)
 * and shrinks when m == 0 (down to 0). The first byte of the data is the starting level and
 * produces no sample of its own. This is the behaviour of the DSP as emulated by DOSBox-X's
 * Sound Blaster (src/hardware/sblaster.cpp), written out as a rule instead of its tables; it
 * has not been compared with real hardware. */
static uint8_t adpcm4_nibble(unsigned nib, unsigned *level, unsigned *r)
{
    unsigned m = nib & 7;
    int delta = (int)((m << *r) + ((1u << *r) >> 1));
    int v = (int)*level + ((nib & 8) ? -delta : delta);
    *level = (unsigned)(v < 0 ? 0 : v > 255 ? 255 : v);
    if (m >= 5 && *r < 3)
        (*r)++;
    else if (m == 0 && *r > 0)
        (*r)--;
    return (uint8_t)*level;
}

size_t voc_adpcm4_decode(const uint8_t *in, size_t n, uint8_t *out)
{
    return voc_adpcm_decode(1, in, n, out);
}

/* Sound Blaster 2.6-bit (codec 2) and 2-bit (codec 3) ADPCM, the same scheme with shorter codes. A 3-bit code is a sign (4) and a
 * magnitude m of 0 .. 3, the step r runs 0 .. 4, the level moves by (m << r) + ((1 << r) >> 1) but by 10m + 5 at r = 4, and r grows
 * when m = 3; a byte holds three codes, bits 7-5, 4-2, and bits 1-0 with a 0 below them. A 2-bit code is a sign (2) and m of 0 or 1,
 * r runs 0 .. 5 and grows when m = 1; a byte holds four, high bits first. In both r shrinks when m = 0. As DOSBox-X emulates the DSP
 * (src/hardware/sblaster.cpp), written out as a rule; not compared with real hardware. */
static uint8_t adpcm_code(unsigned codec, unsigned code, unsigned *level, unsigned *r)
{
    if (codec == 1)
        return adpcm4_nibble(code, level, r);
    unsigned sign = codec == 2 ? 4 : 2, m = code & (sign - 1), top = codec == 2 ? 4 : 5, grow = sign - 1;
    int delta = codec == 2 && *r == 4 ? (int)(10 * m + 5) : (int)((m << *r) + ((1u << *r) >> 1));
    int v = (int)*level + ((code & sign) ? -delta : delta);
    *level = (unsigned)(v < 0 ? 0 : v > 255 ? 255 : v);
    if (m == grow && *r < top)
        (*r)++;
    else if (m == 0 && *r > 0)
        (*r)--;
    return (uint8_t)*level;
}

size_t voc_adpcm_decode(unsigned codec, const uint8_t *in, size_t n, uint8_t *out)
{
    if (n < 1 || codec < 1 || codec > 3)
        return 0;
    unsigned level = in[0], r = 0;
    size_t o = 0;
    for (size_t i = 1; i < n; i++) {
        unsigned b = in[i];
        if (codec == 1) {
            out[o++] = adpcm_code(1, b >> 4, &level, &r);
            out[o++] = adpcm_code(1, b & 15, &level, &r);
        } else if (codec == 2) {
            out[o++] = adpcm_code(2, (b >> 5) & 7, &level, &r);
            out[o++] = adpcm_code(2, (b >> 2) & 7, &level, &r);
            out[o++] = adpcm_code(2, (b & 3) << 1, &level, &r);
        } else {
            for (int s = 6; s >= 0; s -= 2)
                out[o++] = adpcm_code(3, (b >> s) & 3, &level, &r);
        }
    }
    return o;
}

/* Samples one data byte gives (after the starting level of an ADPCM block) */
static size_t per_byte(unsigned codec)
{
    return codec == 0 ? 1 : codec + 1;
}

static size_t block_frames(unsigned codec, size_t data)
{
    if (codec == 0)
        return data;
    return data >= 1 ? per_byte(codec) * (data - 1) : 0;
}

static unsigned rd24(const uint8_t *p) { return p[0] | p[1] << 8 | (unsigned)p[2] << 16; }

int voc_decode(const uint8_t *data, size_t len, voc_clip *clip)
{
    memset(clip, 0, sizeof *clip);
    if (len < 26 || memcmp(data, SIG, SIG_LEN) != 0) {
        fprintf(stderr, "voc: not a Creative Voice File\n");
        return -1;
    }
    size_t pos = data[20] | data[21] << 8;   /* header length, 26 in every file here */
    if (pos < SIG_LEN + 2 || pos > len) {
        fprintf(stderr, "voc: bad header length %zu\n", pos);
        return -1;
    }

    /* First pass: size the output and check the blocks. */
    size_t frames = 0;
    unsigned rate = 0;
    int codec = -1;
    for (size_t p = pos; p < len && data[p] != 0;) {
        if (p + 4 > len) {
            fprintf(stderr, "voc: truncated block header at %zu\n", p);
            return -1;
        }
        unsigned type = data[p], size = rd24(data + p + 1);
        if (p + 4 + size > len) {
            fprintf(stderr, "voc: block at %zu runs past the end\n", p);
            return -1;
        }
        const uint8_t *body = data + p + 4;
        if (type == 1) {
            if (size < 2) {
                fprintf(stderr, "voc: short sound block at %zu\n", p);
                return -1;
            }
            if (body[1] > 3) {
                fprintf(stderr, "voc: unsupported codec %u\n", body[1]);
                return -1;
            }
            unsigned div = 256u - body[0];
            unsigned r = (1000000u + div / 2) / div;   /* rounded: 13,157.9 Hz is 13,158 */
            if (rate && r != rate) {
                fprintf(stderr, "voc: sample rate changes inside a clip\n");
                return -1;
            }
            rate = r;
            codec = body[1];
            frames += block_frames(codec, size - 2);
        } else if (type == 2) {
            if (codec != 0) {
                fprintf(stderr, "voc: continuation block of an ADPCM clip is not supported\n");
                return -1;
            }
            frames += size;
        } else {
            fprintf(stderr, "voc: unsupported block type %u\n", type);
            return -1;
        }
        p += 4 + size;
    }
    if (rate == 0 || frames == 0) {
        fprintf(stderr, "voc: no sound data\n");
        return -1;
    }

    int16_t *pcm = malloc(frames * sizeof *pcm);
    uint8_t *tmp = malloc(frames);
    if (!pcm || !tmp) {
        free(pcm);
        free(tmp);
        return -1;
    }
    size_t o = 0;
    for (size_t p = pos; p < len && data[p] != 0;) {
        unsigned type = data[p], size = rd24(data + p + 1);
        const uint8_t *body = data + p + 4;
        size_t n = 0;
        if (type == 1 && body[1] == 0) {
            memcpy(tmp, body + 2, size - 2);
            n = size - 2;
        } else if (type == 1) {
            n = voc_adpcm_decode(body[1], body + 2, size - 2, tmp);
        } else {
            memcpy(tmp, body, size);
            n = size;
        }
        for (size_t i = 0; i < n; i++)
            pcm[o++] = (int16_t)(((int)tmp[i] - 128) * 256);   /* unsigned 8-bit to signed 16-bit */
        p += 4 + size;
    }
    free(tmp);
    clip->pcm = pcm;
    clip->frames = o;
    clip->rate = rate;
    return 0;
}

size_t voc_block_frames(const uint8_t *block, size_t avail)
{
    if (avail < 6 || block[0] != 1 || block[5] > 3)
        return 0;
    size_t size = rd24(block + 1);
    if (size < 2 || 4 + size > avail)
        return 0;
    return block_frames(block[5], size - 2);
}

int voc_block_decode(const uint8_t *block, size_t avail, voc_clip *clip)
{
    memset(clip, 0, sizeof *clip);
    size_t frames = voc_block_frames(block, avail);
    if (frames == 0) {
        fprintf(stderr, "voc: not a sound-data block of codec 0 .. 3\n");
        return -1;
    }
    size_t size = rd24(block + 1);
    uint8_t *tmp = malloc(frames);
    int16_t *pcm = malloc(frames * sizeof *pcm);
    if (!tmp || !pcm) {
        free(tmp);
        free(pcm);
        return -1;
    }
    size_t n = block[5] == 0 ? (memcpy(tmp, block + 6, size - 2), size - 2) : voc_adpcm_decode(block[5], block + 6, size - 2, tmp);
    for (size_t i = 0; i < n; i++)
        pcm[i] = (int16_t)(((int)tmp[i] - 128) * 256);
    free(tmp);
    unsigned div = 256u - block[4];
    clip->pcm = pcm;
    clip->frames = n;
    clip->rate = (1000000u + div / 2) / div;
    return 0;
}

void voc_free(voc_clip *clip)
{
    free(clip->pcm);
    clip->pcm = NULL;
    clip->frames = 0;
}
