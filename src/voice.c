#include "voice.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"

#define MAX_LINES 16

struct voice_bank {
    unsigned n;
    voc_clip clip[MAX_LINES];
};

/* Offsets of each slide's table in TAB.TAB: the constants the player seeks to. */
static const unsigned table_at[7] = {0, 0x3a7, 0x3cf, 0x3de, 0x410, 0x429, 0x451};

int voice_table_entries(const uint8_t *tab, size_t tab_len, unsigned slide, unsigned lines, size_t voice_len,
                        size_t *offs, size_t *lens)
{
    if (slide < 1 || slide > 6 || lines < 1 || lines > MAX_LINES)
        return -1;
    size_t at = table_at[slide];
    if (at + 5u * lines > tab_len)
        return -1;
    size_t expect = 0;
    for (unsigned i = 0; i < lines; i++) {
        const uint8_t *e = tab + at + 5u * i;
        offs[i] = e[0] | e[1] << 8 | (size_t)e[2] << 16;
        lens[i] = e[3] | e[4] << 8;
        if (offs[i] != expect || lens[i] == 0)
            return -1;
        expect += lens[i];
    }
    return expect == voice_len ? 0 : -1;
}

voice_bank_t *voice_bank_load(unsigned slide, unsigned lines)
{
    char name[16];
    snprintf(name, sizeof name, "story%u_v", slide);
    size_t tab_len = 0, voice_len = 0;
    uint8_t *tab = assets_read("tab.tab", &tab_len);
    uint8_t *voice = assets_read(name, &voice_len);
    voice_bank_t *b = NULL;
    size_t offs[MAX_LINES], lens[MAX_LINES];
    if (!tab || !voice) {
        fprintf(stderr, "voice: cannot read tab.tab or %s (data dir: %s)\n", name, assets_dir());
    } else if (voice_table_entries(tab, tab_len, slide, lines, voice_len, offs, lens) != 0) {
        fprintf(stderr, "voice: tab.tab entries for slide %u do not match %s\n", slide, name);
    } else if ((b = calloc(1, sizeof *b)) != NULL) {
        for (unsigned i = 0; i < lines; i++) {
            if (voc_decode(voice + offs[i], lens[i], &b->clip[i]) != 0) {
                fprintf(stderr, "voice: %s clip %u does not decode\n", name, i + 1);
                voice_bank_free(b);
                b = NULL;
                break;
            }
            b->n = i + 1;
        }
    }
    free(tab);
    free(voice);
    return b;
}

void voice_bank_free(voice_bank_t *b)
{
    if (!b)
        return;
    for (unsigned i = 0; i < b->n; i++)
        voc_free(&b->clip[i]);
    free(b);
}

unsigned voice_bank_count(const voice_bank_t *b) { return b ? b->n : 0; }

const voc_clip *voice_bank_clip(const voice_bank_t *b, unsigned line)
{
    return b && line < b->n ? &b->clip[line] : NULL;
}
