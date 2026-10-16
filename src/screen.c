#include "screen.h"

#include <stdio.h>
#include <string.h>

void screen_set_palette(screen_t *s, const uint8_t *rgb6, int first, int count)
{
    for (int i = 0; i < count && first + i < 256; i++)
        for (int c = 0; c < 3; c++)
            s->pal[first + i][c] = rgb6[i * 3 + c] & 0x3f;
}

void screen_to_rgb888(const screen_t *s, uint8_t *out)
{
    uint8_t lut[64];
    for (int v = 0; v < 64; v++)
        lut[v] = (uint8_t)(v * 255 / 63);
    for (int i = 0; i < SCREEN_PIXELS; i++) {
        const uint8_t *p = s->pal[s->pix[i]];
        out[i * 3 + 0] = lut[p[0]];
        out[i * 3 + 1] = lut[p[1]];
        out[i * 3 + 2] = lut[p[2]];
    }
}

int screen_write_ppm(const screen_t *s, const char *path)
{
    static uint8_t rgb[SCREEN_PIXELS * 3];
    screen_to_rgb888(s, rgb);
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    fprintf(f, "P6\n%d %d\n255\n", SCREEN_W, SCREEN_H);
    size_t n = fwrite(rgb, 1, sizeof rgb, f);
    int rc = (fclose(f) == 0 && n == sizeof rgb) ? 0 : -1;
    return rc;
}
