/*
 * WALK.SL's Sound Blaster sounds (docs/findings.md, "WALK.SL step 25"). With a card the original reads all of SND_EF.1 at its start
 * and all of _1_.LOC when a battle starts; each sound is a bare VOC sound-data block (voc_block_decode) at an offset its code names,
 * started through 0x1c68e, which cuts any sound that plays. A sound is said to last sfx_passes passes of the world's pace: that is
 * how tests/walk_dump and tools/sb_stub.py model the card's busy word, since a stepped run has no real time.
 */
#ifndef TK_SFX_H
#define TK_SFX_H

#include <stddef.h>
#include <stdint.h>

#include "voc.h"

enum { SFX_EF = 0, SFX_LOC = 1, SFX_FILES = 2 };   /* SND_EF.1, _1_.LOC */

typedef struct sfx_bank sfx_bank_t;

/* Both files read whole; NULL with a message on stderr if either is missing. */
sfx_bank_t *sfx_bank_load(void);
void sfx_bank_free(sfx_bank_t *b);
const char *sfx_file_name(unsigned file);
/* The block at `off` in `file` and the bytes held from there (*avail); NULL past the end. */
const uint8_t *sfx_block(const sfx_bank_t *b, unsigned file, unsigned off, size_t *avail);
/* The passes a block lasts at TK_WALK_PASS_HZ: its samples (voc_block_frames) at its rate divisor, rounded up; at least 1. */
unsigned sfx_passes(const uint8_t *block, size_t avail);
/* The block decoded, kept by the bank until it is freed; NULL (with a message) if it does not decode. */
const voc_clip *sfx_clip(sfx_bank_t *b, unsigned file, unsigned off);

#endif
