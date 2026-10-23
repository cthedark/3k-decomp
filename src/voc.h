/*
 * Creative Voice File (VOC) decoder for the game's sound: the STORYn_V voice-over clips (8-bit
 * PCM) and STORY0_1.LCZ, which is a VOC file too (4-bit ADPCM). See docs/formats.md.
 *
 * Output is mono 16-bit signed. Only what the game's files use is supported: sound-data blocks
 * (type 1) and their continuations (type 2) with codec 0 (unsigned 8-bit PCM) or the Sound
 * Blaster's ADPCM codecs 1 (4-bit), 2 (2.6-bit) and 3 (2-bit), and the terminator (type 0).
 * Anything else is an error, not guessed at. WALK.SL's sound effects (SND_EF.1, _1_.LOC) are
 * bare sound-data blocks with no file header: voc_block_decode.
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
/* The same for codec 1, 2 (three samples a byte) or 3 (four): returns the samples written; 0 for another codec or n < 1. */
size_t voc_adpcm_decode(unsigned codec, const uint8_t *in, size_t n, uint8_t *out);

/* One sound-data block alone (type byte 1, a 3-byte length, the rate divisor, the codec 0 .. 3, the data), `avail` bytes held from
 * `block` on: the samples it gives, 0 if it is not such a block or runs past `avail`; and the block decoded (0, or -1 with a message
 * on stderr; the caller owns clip->pcm). */
size_t voc_block_frames(const uint8_t *block, size_t avail);
int voc_block_decode(const uint8_t *block, size_t avail, voc_clip *clip);

#endif
