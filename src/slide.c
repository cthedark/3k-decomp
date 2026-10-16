#include "slide.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"
#include "gfx.h"
#include "lcz.h"

int slide_load_chunks(const char *file, int n, uint8_t *const *dst, const size_t *want)
{
    size_t len = 0;
    uint8_t *data = assets_read(file, &len);
    if (!data) {
        fprintf(stderr, "intro: cannot read %s (data dir: %s)\n", file, assets_dir());
        return -1;
    }
    size_t pos = 0;
    int rc = 0;
    for (int i = 0; i < n && rc == 0; i++) {
        lcz_chunk c;
        if (lcz_next_chunk(data, len, &pos, &c) != LCZ_OK) {
            fprintf(stderr, "intro: %s has no chunk %d\n", file, i);
            rc = -1;
        } else {
            if (c.len != want[i]) {
                fprintf(stderr, "intro: %s chunk %d is %zu bytes, expected %zu\n", file, i, c.len, want[i]);
                rc = -1;
            } else {
                memcpy(dst[i], c.pixels, c.len);
            }
            lcz_chunk_free(&c);
        }
    }
    free(data);
    return rc;
}

int slide_load_palettes(uint8_t bright[768], uint8_t dim[768])
{
    uint8_t pal[1536];
    if (assets_read_at("ani.lcp", 0, pal, sizeof pal) != 0) {
        fprintf(stderr, "intro: cannot read ani.lcp (data dir: %s)\n", assets_dir());
        return -1;
    }
    for (int i = 0; i < 1536; i++)
        if (pal[i] > 63) {
            fprintf(stderr, "intro: ani.lcp does not start with two 6-bit palettes\n");
            return -1;
        }
    memcpy(bright, pal, 768);
    memcpy(dim, pal + 768, 768);
    return 0;
}

void slide_start_palette(screen_t *scr, uint8_t cur[768], const uint8_t bright[768])
{
    memcpy(scr->pal, bright, 768);
    memset(scr->pal, 0, SLIDE_FADE_BYTES);
    memset(cur, 0, 768);
}

void slide_fade_step(screen_t *scr, uint8_t cur[768], const uint8_t target[768])
{
    gfx_palette_step(cur, target, SLIDE_FADE_BYTES);
    screen_set_palette(scr, cur, 0, 240);
}
