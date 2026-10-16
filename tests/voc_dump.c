/*
 * Decode a VOC file (or one clip at an offset inside a file such as STORY1_V) with src/voc.c and
 * write it as a 16-bit mono WAV, to listen to it or to compare with another decoder.
 *
 *   build/voc_dump IN OUT.wav [OFFSET LENGTH]
 *
 * Output is derived from game data: keep it under work/.
 */
#include <stdio.h>
#include <stdlib.h>

#include "voc.h"

static void put16(FILE *f, unsigned v) { fputc(v & 255, f); fputc(v >> 8 & 255, f); }
static void put32(FILE *f, unsigned v) { put16(f, v & 0xffff); put16(f, v >> 16); }

int main(int argc, char **argv)
{
    if (argc != 3 && argc != 5) {
        fprintf(stderr, "usage: %s IN OUT.wav [OFFSET LENGTH]\n", argv[0]);
        return 2;
    }
    FILE *in = fopen(argv[1], "rb");
    if (!in) {
        perror(argv[1]);
        return 1;
    }
    fseek(in, 0, SEEK_END);
    long size = ftell(in);
    fseek(in, 0, SEEK_SET);
    unsigned char *buf = malloc((size_t)size);
    if (!buf || fread(buf, 1, (size_t)size, in) != (size_t)size) {
        fprintf(stderr, "cannot read %s\n", argv[1]);
        return 1;
    }
    fclose(in);
    size_t off = 0, len = (size_t)size;
    if (argc == 5) {
        off = strtoul(argv[3], NULL, 0);
        len = strtoul(argv[4], NULL, 0);
        if (off + len > (size_t)size) {
            fprintf(stderr, "range is outside the file\n");
            return 1;
        }
    }
    voc_clip c;
    if (voc_decode(buf + off, len, &c) != 0)
        return 1;
    FILE *out = fopen(argv[2], "wb");
    if (!out) {
        perror(argv[2]);
        return 1;
    }
    unsigned bytes = (unsigned)(c.frames * 2);
    fwrite("RIFF", 1, 4, out); put32(out, 36 + bytes); fwrite("WAVEfmt ", 1, 8, out);
    put32(out, 16); put16(out, 1); put16(out, 1); put32(out, c.rate); put32(out, c.rate * 2); put16(out, 2); put16(out, 16);
    fwrite("data", 1, 4, out); put32(out, bytes);
    for (size_t i = 0; i < c.frames; i++)
        put16(out, (unsigned)(uint16_t)c.pcm[i]);
    fclose(out);
    printf("%zu samples at %u Hz = %.3f s\n", c.frames, c.rate, (double)c.frames / c.rate);
    voc_free(&c);
    free(buf);
    return 0;
}
