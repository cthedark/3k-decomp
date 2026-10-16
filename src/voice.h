/*
 * The voice-over of one slide: the clips of STORYn_V, located through TAB.TAB.
 *
 * STORYn_V is the clips back to back, each a complete VOC file. TAB.TAB holds, for each slide, a
 * run of 5-byte entries (3-byte offset into STORYn_V, 2-byte length, little endian), one per
 * subtitle line. The player in STORY2.SL seeks to the slide's table (file offsets 0x3a7, 0x3cf,
 * 0x3de, 0x410, 0x429, 0x451 for slides 1..6), reads entry k and then that many bytes of the voice
 * file (0x1243a..0x12512). The entries tile each voice file exactly; see docs/formats.md.
 */
#ifndef TK_VOICE_H
#define TK_VOICE_H

#include <stddef.h>
#include <stdint.h>

#include "voc.h"

typedef struct voice_bank voice_bank_t;

/* Load and decode the `lines` clips of slide `slide` (1..6). Returns NULL, with a message on
 * stderr, if a file is missing or does not have the expected shape. */
voice_bank_t *voice_bank_load(unsigned slide, unsigned lines);
void voice_bank_free(voice_bank_t *b);

unsigned voice_bank_count(const voice_bank_t *b);
const voc_clip *voice_bank_clip(const voice_bank_t *b, unsigned line);   /* NULL if out of range */

/* The same lookup on data already in memory (for tests): fills offs/lens with `lines` entries of
 * the slide's table in `tab`. Returns 0 if the entries lie inside a voice file of `voice_len`
 * bytes and tile it from 0 to voice_len without a gap. */
int voice_table_entries(const uint8_t *tab, size_t tab_len, unsigned slide, unsigned lines, size_t voice_len,
                        size_t *offs, size_t *lens);

#endif
