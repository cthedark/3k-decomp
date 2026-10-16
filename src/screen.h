/*
 * The game's screen: 320x200 pixels, one byte per pixel, with a 256-entry
 * palette. The original programs set BIOS video mode 13h and write straight
 * into this linear buffer (see docs/findings.md, "CORRECTION: plain mode 13h").
 * Palette entries are VGA DAC values: 6 bits per channel (0..63).
 */
#ifndef TK_SCREEN_H
#define TK_SCREEN_H

#include <stddef.h>
#include <stdint.h>

#define SCREEN_W 320
#define SCREEN_H 200
#define SCREEN_PIXELS (SCREEN_W * SCREEN_H)

typedef struct {
    uint8_t pix[SCREEN_PIXELS];
    uint8_t pal[256][3];  /* 6-bit R, G, B */
} screen_t;

/* Load `count` palette entries from `rgb6` (3 bytes each, values 0..63) starting at `first`.
 * Values above 63 are masked to 6 bits, as the DAC does. */
void screen_set_palette(screen_t *s, const uint8_t *rgb6, int first, int count);

/* Convert to 8-bit RGB (3 bytes per pixel) with the scaling v * 255 / 63. */
void screen_to_rgb888(const screen_t *s, uint8_t *out);

/* Write a binary PPM (P6). Returns 0 on success. */
int screen_write_ppm(const screen_t *s, const char *path);

#endif
