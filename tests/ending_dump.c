/* Dump what the ENDING.SL port draws, for tools/ending_check.py:
 *
 *   build/ending_dump DATA_DIR N OUT
 *
 * OUT gets N records: the loop number (1 byte), the screen (64,000 bytes) and the palette (768 bytes). Record 0 is the state when
 * the first loop is reached, each next one the state after a pass. It stops early when the port has nothing more built. */
#include <stdio.h>
#include <stdlib.h>

#include "assets.h"
#include "ending.h"

static void put(FILE *f, int loop, const screen_t *scr)
{
    fputc(loop, f);
    fwrite(scr->pix, 1, sizeof scr->pix, f);
    fwrite(scr->pal, 1, sizeof scr->pal, f);
}

int main(int argc, char **argv)
{
    if (argc != 4) {
        fprintf(stderr, "usage: %s DATA_DIR N OUT\n", argv[0]);
        return 2;
    }
    assets_set_dir(argv[1]);
    int n = atoi(argv[2]);
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
