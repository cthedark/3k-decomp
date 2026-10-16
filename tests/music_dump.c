/*
 * Render the intro song to a WAV file, to listen to it without the game.
 *
 *   build/music_dump DATA_DIR OUT.WAV [SECONDS]
 *
 * 16-bit mono at the OPL chip's own rate (49,716 Hz). SECONDS defaults to one pass of the song
 * (132 s).
 */
#include <stdio.h>
#include <stdlib.h>

#include "assets.h"
#include "music.h"

static void put16(FILE *f, unsigned v) { fputc((int)(v & 255), f); fputc((int)((v >> 8) & 255), f); }
static void put32(FILE *f, unsigned long v) { put16(f, (unsigned)(v & 0xffff)); put16(f, (unsigned)((v >> 16) & 0xffff)); }

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s DATA_DIR OUT.WAV [SECONDS]\n", argv[0]);
        return 2;
    }
    assets_set_dir(argv[1]);
    double seconds = argc > 3 ? atof(argv[3]) : 132.0;
    if (seconds <= 0 || seconds > 3600) {
        fprintf(stderr, "SECONDS must be between 0 and 3600\n");
        return 2;
    }
    music_t *m = music_load_intro();
    if (!m)
        return 1;
    unsigned rate = music_rate();
    size_t total = (size_t)(seconds * rate);
    FILE *f = fopen(argv[2], "wb");
    if (!f) {
        perror(argv[2]);
        music_free(m);
        return 1;
    }
    unsigned long bytes = (unsigned long)total * 2;
    fputs("RIFF", f); put32(f, 36 + bytes); fputs("WAVEfmt ", f);
    put32(f, 16); put16(f, 1); put16(f, 1); put32(f, rate); put32(f, rate * 2ul); put16(f, 2); put16(f, 16);
    fputs("data", f); put32(f, bytes);
    int16_t buf[4096];
    for (size_t done = 0; done < total;) {
        size_t n = total - done < 4096 ? total - done : 4096;
        music_render(m, buf, n);
        for (size_t i = 0; i < n; i++)
            put16(f, (unsigned)(uint16_t)buf[i]);
        done += n;
    }
    fclose(f);
    printf("wrote %s: %.1f s at %u Hz, %u loop(s), %u ignored event(s)\n", argv[2], seconds, rate,
           music_loops(m), music_ignored_events(m));
    music_free(m);
    return 0;
}
