/*
 * Print the OPL register writes the music player makes for a song, with the song tick of each, in the
 * format tools/dosemu.py --opl-log writes ("tick register value", hex register and value), so the two can
 * be compared (tools/compare_opl.py).
 *
 *   build/music_regs DATA_DIR OFFSET LENGTH TICKS OUT.txt
 *
 * OFFSET and LENGTH are the byte range of MUC.ALL the original's program reads (decimal or 0x hex). TICKS is how
 * many song ticks to run (the player's own tick, 96 per second for these songs).
 */
#include <stdio.h>
#include <stdlib.h>

#include "assets.h"
#include "music.h"

typedef struct {
    FILE *f;
    unsigned rate, tps;
    uint64_t last_sample;
    int stop;
} sink;

static void trace(void *user, uint64_t sample, uint8_t reg, uint8_t value)
{
    sink *s = user;
    if (sample < s->last_sample)   /* the song started again: this is a second pass, not the song */
        s->stop = 1;
    s->last_sample = sample;
    if (s->stop)
        return;
    /* the player places events at sample = tick * rate / tps rounded down, so this recovers the tick */
    unsigned long tick = (unsigned long)((sample * s->tps + s->rate / 2) / s->rate);
    fprintf(s->f, "%lu %02x %02x\n", tick, reg, value);
}

int main(int argc, char **argv)
{
    if (argc < 6) {
        fprintf(stderr, "usage: %s DATA_DIR OFFSET LENGTH TICKS OUT.txt\n", argv[0]);
        return 2;
    }
    assets_set_dir(argv[1]);
    size_t offset = strtoul(argv[2], NULL, 0), length = strtoul(argv[3], NULL, 0);
    unsigned long ticks = strtoul(argv[4], NULL, 0);
    FILE *f = fopen(argv[5], "w");
    if (!f) {
        perror(argv[5]);
        return 1;
    }
    music_t *m = music_load_at(offset, length);
    if (!m) {
        fclose(f);
        return 1;
    }
    sink s = {f, music_rate(), 96, 0, 0};
    music_set_trace(m, trace, &s);
    int16_t buf[4096];
    unsigned long total = (unsigned long)((unsigned long long)ticks * s.rate / s.tps);
    /* One pass through the song: stop before it starts again (the ticks of a second pass would overlap the first's). */
    for (unsigned long done = 0; done < total && music_loops(m) == 0;) {
        size_t n = total - done < 256 ? total - done : 256;
        music_render(m, buf, n);
        done += n;
    }
    /* The restart happens inside the render call that crosses the end: its note-offs and the next pass's tick-0 events
     * are already in the output file, stamped with the new pass's samples. Drop them (everything after the first
     * write whose position went back). */

    fclose(f);
    printf("%lu ticks, %u loop(s), %u ignored event(s)\n", ticks, music_loops(m), music_ignored_events(m));
    music_free(m);
    return 0;
}
