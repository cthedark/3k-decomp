/* ENDING.SL, the game's end (docs/findings.md "ENDING.SL step 1" on). Built so far: the first scene (step 2), the villain rising
 * through the drifting clouds and her four lines of subtitles, and the second (step 3), five pictures of her with her voice, and the third (step 4), her
 * figure sinking into a drifting red sky full of sparks, and the fourth (step 5), a picture shaking, then six flickering into each other, and the fifth (step 6),
 * her face and eyes moving with two subtitle lines, and the sixth
 * (step 7), the sunset coming in over her and the palette fading to the second one, and the credits (step 8)
 * rising at the left. */
#ifndef TK_ENDING_H
#define TK_ENDING_H

#include "screen.h"

typedef struct ending ending_t;

/* The loops ending_step reports, numbered as tools/ending_check.py numbers the original's loop tops. */
enum { ENDING_SCENE1 = 1, ENDING_SCENE1_END = 2, ENDING_SCENE2 = 3, ENDING_SCENE2_END = 4, ENDING_SCENE3 = 5,
       ENDING_SCENE3_END = 6, ENDING_SCENE4_SHAKE = 7, ENDING_SCENE4_SHAKE_END = 8, ENDING_SCENE4_STROBE = 9, ENDING_SCENE4_END = 10,
       ENDING_SCENE5_FACE = 11, ENDING_SCENE5_FACE_END = 12, ENDING_SCENE5_EYES = 13, ENDING_SCENE5_END = 14,
       ENDING_SCENE6_WIPE = 15, ENDING_SCENE6_FADE = 16, ENDING_SCENE6_END = 17,
       ENDING_SCENE7 = 18, ENDING_SCENE7_END = 19 };

/* The set-up of main (0x10000 .. 0x1042a): the files read and unpacked, the palette (ANI.LCP's first 256 entries) set and the screen
 * cleared (mode 0x13). NULL when a file cannot be read. */
ending_t *ending_new(screen_t *scr);
void ending_free(ending_t *e);
/* One pass of the current loop, the screen drawn as the original draws it. Returns the loop the original is in after the pass (its
 * next loop top), or 0 once nothing more is built. */
int ending_step(ending_t *e, screen_t *scr);
/* After the credits (0x11384 .. 0x113ff) the original stops the music, sets text mode 3 and prints "THE END" in three lines of block
 * characters at column 20, rows 4 to 6 (colours 0xb, 9, 1), then exits to DOS. This draws those lines as the 80 x 25 text screen would
 * show them, each cell 4 x 8 pixels, with the text mode's 16 colours. */
void ending_text_end(screen_t *scr);
/* With a Sound Blaster: `play` plays the voice block at `off` in _1_.LOC (the sound before it cut), `cut` cuts the sound, `busy` says
 * whether one plays. */
void ending_set_voice(ending_t *e, void (*play)(void *ctx, unsigned off), void (*cut)(void *ctx), int (*busy)(void *ctx), void *ctx);

#endif
