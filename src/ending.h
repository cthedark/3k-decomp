/* ENDING.SL, the game's end (docs/findings.md "ENDING.SL step 1" on). Built so far: the first scene (step 2), the villain rising
 * through the drifting clouds and her four lines of subtitles. */
#ifndef TK_ENDING_H
#define TK_ENDING_H

#include "screen.h"

typedef struct ending ending_t;

/* The loops ending_step reports, numbered as tools/ending_check.py numbers the original's loop tops. */
enum { ENDING_SCENE1 = 1, ENDING_SCENE1_END = 2 };

/* The set-up of main (0x10000 .. 0x1042a): the files read and unpacked, the palette (ANI.LCP's first 256 entries) set and the screen
 * cleared (mode 0x13). NULL when a file cannot be read. */
ending_t *ending_new(screen_t *scr);
void ending_free(ending_t *e);
/* One pass of the current loop, the screen drawn as the original draws it. Returns the loop the original is in after the pass (its
 * next loop top), or 0 once nothing more is built. */
int ending_step(ending_t *e, screen_t *scr);

#endif
