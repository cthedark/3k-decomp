#ifndef TK_GAMEOVER_H
#define TK_GAMEOVER_H
/*
 * MARK.EXE's game-over screen (0x101ed .. 0x1053d), which it shows when WALK.SL has left the one-byte message 0x3e in ANI.LCP byte
 * 0x2ff: the party fell in a battle (docs/findings.md, "WALK.SL step 16"). Checked pass by pass against the original by
 * tools/check_gameover.sh.
 *
 *   - the screen WALK.SL left (palette included: MARK sets none on this path) shaded four times through RPG.LCP 0x340, shrunk to
 *     80 x 50 (0x11a76, the same stepping as battle_shrink) and tiled 4 x 4 over the whole screen;
 *   - each pass the tiles roll up one row and left one pixel, both wrapping round, and the lettering 全體陣亡 ("the whole party has
 *     fallen", TITLE1.LCZ 0x3e73, colour 0 transparent) is drawn over them at (114, 87);
 *   - Space or Enter, or with a mouse driver any button, ends it; MARK then goes on to its title as usual. Its song is MUC.ALL 0x2d76b (the
 *     caller's: GAMEOVER_SONG_*).
 */
#include <stddef.h>

#include "screen.h"

enum { GAMEOVER_SONG_OFFSET = 0x2D76B, GAMEOVER_SONG_LENGTH = 0x7B1 };

typedef struct gameover gameover_t;

/* Take the screen as WALK.SL left it. NULL, with a message on stderr, if RPG.LCP or TITLE1.LCZ cannot be read. */
gameover_t *gameover_begin(const screen_t *scr);
/* One pass of the loop (0x1040a): roll the tiles and show them with the lettering. */
void gameover_pass(gameover_t *g, screen_t *scr);
/* Does this keyboard port value end the screen (Space or Enter)? */
int gameover_key(unsigned port60);
/* (0x10502 .. 0x10537) With a mouse driver (found at MARK's start, 0x10120), does this button state end it? Any button held, the
 * moment it is seen (no release is waited for). */
int gameover_mouse(unsigned buttons);
void gameover_free(gameover_t *g);

#endif
