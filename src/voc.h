/*
 * Creative Voice File (VOC) decoder for the game's sound: the STORYn_V voice-over clips (8-bit
 * PCM) and STORY0_1.LCZ, which is a VOC file too (4-bit ADPCM). See docs/formats.md.
 *
 * Output is mono 16-bit signed. Only what the game's files use is supported: sound-data blocks
 * (type 1) and their continuations (type 2) with codec 0 (unsigned 8-bit PCM) or codec 1 (Sound
 * Blaster 4-bit ADPCM), and the terminator (type 0). Anything else is an error, not guessed at.
 */
#ifndef TK_VOC_H
#define TK_VOC_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    int16_t *pcm;      /* mono samples */
    size_t frames;
    unsigned rate;     /* samples per second */
} voc_clip;

/* Decode one VOC file held in memory. Returns 0, or -1 with a message on stderr. On success the
 * caller owns clip->pcm (voc_free). */
int voc_decode(const uint8_t *data, size_t len, voc_clip *clip);
void voc_free(voc_clip *clip);

/* The 4-bit ADPCM decoder alone, for tests. `in` is the data after the block's two header bytes:
 * the first byte is the starting level, each later byte holds two nibbles (high one first).
 * Writes 2 * (n - 1) unsigned 8-bit samples to `out` and returns that count; 0 if n < 1. */
size_t voc_adpcm4_decode(const uint8_t *in, size_t n, uint8_t *out);

#endif
