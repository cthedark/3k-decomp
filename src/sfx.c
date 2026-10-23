#include "sfx.h"

#include <stdio.h>
#include <stdlib.h>

#include "assets.h"
#include "timing.h"

enum { CACHE = 32 };

struct sfx_bank {
    uint8_t *data[SFX_FILES];
    size_t len[SFX_FILES];
    struct {
        unsigned file, off;
        voc_clip clip;
    } cache[CACHE];
    int ncache;
};

static const char *const NAMES[SFX_FILES] = {"SND_EF.1", "_1_.LOC"};

const char *sfx_file_name(unsigned file)
{
    return file < SFX_FILES ? NAMES[file] : "?";
}

sfx_bank_t *sfx_bank_load(void)
{
    sfx_bank_t *b = calloc(1, sizeof *b);
    if (!b)
        return NULL;
    for (int f = 0; f < SFX_FILES; f++) {
        b->data[f] = assets_read(NAMES[f], &b->len[f]);
        if (!b->data[f]) {
            fprintf(stderr, "sfx: %s is missing\n", NAMES[f]);
            sfx_bank_free(b);
            return NULL;
        }
    }
    return b;
}

void sfx_bank_free(sfx_bank_t *b)
{
    if (!b)
        return;
    for (int f = 0; f < SFX_FILES; f++)
        free(b->data[f]);
    for (int i = 0; i < b->ncache; i++)
        voc_free(&b->cache[i].clip);
    free(b);
}

const uint8_t *sfx_block(const sfx_bank_t *b, unsigned file, unsigned off, size_t *avail)
{
    if (!b || file >= SFX_FILES || off >= b->len[file])
        return NULL;
    *avail = b->len[file] - off;
    return b->data[file] + off;
}

unsigned sfx_passes(const uint8_t *block, size_t avail)
{
    uint64_t n = voc_block_frames(block, avail);
    if (n == 0)
        return 1;
    uint64_t p = (n * TK_WALK_PASS_HZ * (256u - block[4]) + 999999u) / 1000000u;
    return p < 1 ? 1 : (unsigned)p;
}

const voc_clip *sfx_clip(sfx_bank_t *b, unsigned file, unsigned off)
{
    for (int i = 0; i < b->ncache; i++)
        if (b->cache[i].file == file && b->cache[i].off == off)
            return &b->cache[i].clip;
    size_t avail = 0;
    const uint8_t *blk = sfx_block(b, file, off, &avail);
    if (!blk || b->ncache >= CACHE) {
        fprintf(stderr, "sfx: no sound at %s %#x\n", sfx_file_name(file), off);
        return NULL;
    }
    voc_clip *c = &b->cache[b->ncache].clip;
    if (voc_block_decode(blk, avail, c) != 0) {
        fprintf(stderr, "sfx: the sound at %s %#x does not decode\n", sfx_file_name(file), off);
        return NULL;
    }
    b->cache[b->ncache].file = file;
    b->cache[b->ncache].off = off;
    return &b->cache[b->ncache++].clip;
}
