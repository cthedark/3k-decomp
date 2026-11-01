/* Dump what the ENDING.SL port draws, for tools/ending_check.py:
 *
 *   build/ending_dump DATA_DIR N OUT
 *
 * OUT gets N records: the loop number (1 byte), the screen's CRC-32 (zlib's, 4 bytes little-endian) and the palette (768 bytes).
 * Record 0 is the state when the first loop is reached, each next one the state after a pass. It stops early when the port has
 * nothing more built. With ENDING_FULL=i in the environment record i's whole screen and palette also go to ENDING_FULL_OUT. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "assets.h"
#include "ending.h"

static uint32_t crc32(const uint8_t *p, size_t n)
{
    static uint32_t table[256];
    if (!table[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++)
                c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
            table[i] = c;
        }
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++)
        c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return ~c;
}

static long full_at = -1;
static long count;

static void put(FILE *f, int loop, const screen_t *scr)
{
    fputc(loop, f);
    uint32_t c = crc32(scr->pix, sizeof scr->pix);
    uint8_t b[4] = {(uint8_t)c, (uint8_t)(c >> 8), (uint8_t)(c >> 16), (uint8_t)(c >> 24)};
    fwrite(b, 1, 4, f);
    fwrite(scr->pal, 1, sizeof scr->pal, f);
    if (count++ == full_at && getenv("ENDING_FULL_OUT")) {
        FILE *g = fopen(getenv("ENDING_FULL_OUT"), "wb");
        if (g) {
            fwrite(scr->pix, 1, sizeof scr->pix, g);
            fwrite(scr->pal, 1, sizeof scr->pal, g);
            fclose(g);
        }
    }
}

int main(int argc, char **argv)
{
    if (argc != 4) {
        fprintf(stderr, "usage: %s DATA_DIR N OUT\n", argv[0]);
        return 2;
    }
    assets_set_dir(argv[1]);
    int n = atoi(argv[2]);
    if (getenv("ENDING_FULL"))
        full_at = atol(getenv("ENDING_FULL"));
    static screen_t scr;
    ending_t *e = ending_new(&scr);
    if (!e) {
        fprintf(stderr, "ending: the data files cannot be read\n");
        return 1;
    }
    FILE *f = fopen(argv[3], "wb");
    if (!f)
        return 1;
    put(f, ENDING_SCENE1, &scr);
    for (int i = 1; i < n; i++) {
        int loop = ending_step(e, &scr);
        if (!loop)
            break;
        put(f, loop, &scr);
    }
    fclose(f);
    ending_free(e);
    return 0;
}
