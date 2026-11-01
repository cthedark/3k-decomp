/* ENDING.SL, the game's end (docs/findings.md "ENDING.SL step 1" on). Built so far: the first scene (step 2), the villain rising
 * through the drifting clouds and her four lines of subtitles, and the second (step 3), five pictures of her with her voice. */
#ifndef TK_ENDING_H
#define TK_ENDING_H

#include "screen.h"

typedef struct ending ending_t;

/* The loops ending_step reports, numbered as tools/ending_check.py numbers the original's loop tops. */
enum { ENDING_SCENE1 = 1, ENDING_SCENE1_END = 2, ENDING_SCENE2 = 3, ENDING_SCENE2_END = 4 };

/* The set-up of main (0x10000 .. 0x1042a): the files read and unpacked, the palette (ANI.LCP's first 256 entries) set and the screen
 * cleared (mode 0x13). NULL when a file cannot be read. */
ending_t *ending_new(screen_t *scr);
void ending_free(ending_t *e);
/* One pass of the current loop, the screen drawn as the original draws it. Returns the loop the original is in after the pass (its
 * next loop top), or 0 once nothing more is built. */
int ending_step(ending_t *e, screen_t *scr);
/* With a Sound Blaster: `play` plays the voice block at `off` in _1_.LOC (the sound before it cut), `cut` cuts the sound. */
void ending_set_voice(ending_t *e, void (*play)(void *ctx, unsigned off), void (*cut)(void *ctx), void *ctx);

#endif
