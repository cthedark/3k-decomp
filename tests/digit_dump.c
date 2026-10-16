/* Dump the intermediate buffers of the CPU label for comparison with the oracle:
 * build/digit_dump DATA_DIR OUTPREFIX writes OUTPREFIX.strip / .stretched / .frame.
 * (A debugging aid for the menu; not part of the game.) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"
#include "gfx.h"
#include "lcz.h"
#include "menu.h"

static uint8_t *block(const char *file, size_t off, size_t len)
{
    uint8_t *raw = malloc(len);
    size_t pos = 0;
    lcz_chunk c;
    if (!raw || assets_read_at(file, off, raw, len) != 0 || lcz_next_chunk(raw, len, &pos, &c) != LCZ_OK)
        exit(1);
    free(raw);
    return c.pixels;
}

int main(int argc, char **argv)
{
    if (argc != 3)
        return 2;
    assets_set_dir(argv[1]);
    uint8_t *atlas = block("RPG.LCP", 0x1643b, 1214);
    uint8_t strip[244], stretched[807];
    memset(strip, 0, sizeof strip);
    sprite_set_size(strip, 27, 8);
    const size_t tiles[3] = {2 * 64 + 0x80, 0x200, 0x180}, at[3] = {4, 13, 22};
    for (int i = 0; i < 3; i++)
        menu_tile_copy(strip, at[i], atlas + tiles[i], 27, 8, 8);
    for (unsigned k = 0; k < 12; k++)
        menu_shade(strip + 4 + 20 * k, 20, (uint8_t)(0xf0 + k));
    menu_stretch(stretched, strip, 53, 15);
    char path[512];
    FILE *f;
    snprintf(path, sizeof path, "%s.strip", argv[2]);
    f = fopen(path, "wb"); fwrite(strip, 1, 220, f); fclose(f);
    snprintf(path, sizeof path, "%s.stretched", argv[2]);
    f = fopen(path, "wb"); fwrite(stretched, 1, 4 + 53 * 15, f); fclose(f);
    free(atlas);
    return 0;
}
