/*
 * WALK.SL's battle, stage 1: the hand-over from the world and the battle's first screen, then its idle passes.
 *
 * What this reproduces (docs/findings.md, "WALK.SL step 13"; checked pass by pass against the original by tools/battle_check.py):
 *   - the world's side of a touch (0x1275e .. 0x12e4e): the enemy moved away (record byte 0x2f = 0), the monster group number,
 *     the world drawn once more and its middle doubled (0x2c658), squeezed into a 195 x 110 picture in perspective (0x26de8 per row),
 *     shaded towards the top (0x25dca four times), the six progress squares, the party strip (RPG.LCP 0x18691) with each member's face,
 *     HP and MP (0x1c47c);
 *   - the battle's set-up (0x1cbb6 .. 0x1d415): five monster slots from rand(), the group's three 60 x 60 pictures from MON.ALL, its
 *     stats, who acts first (rand()), the mark on the first member able to act;
 *   - its idle pass (0x1d418 loop): the palette cycle the world uses, the four command tabs, the picture swaying row by row
 *     (0x2c6b9), the monsters (0x1fd06: the two at the back drawn at half size and shaded, each one jittering a pixel and changing
 *     its frame now and then from rand()), the window copy to (64, 74).
 *
 * Not built (reported through battle_unbuilt): keys and the mouse (commands, targets), the monsters acting first, the effects
 * list, the dialog some enemies start with (record word 0x11), the Sound Blaster parts, winning, losing and the way back.
 */
#ifndef TK_BATTLE_H
#define TK_BATTLE_H

#include <stdint.h>

#include "screen.h"
#include "walk.h"

typedef struct battle battle_t;

enum {
    BATTLE_SLOTS = 5,          /* monster slots: 0 and 1 at the back, 2 to 4 in front */
    BATTLE_MON_BYTES = 0x3C,   /* [bp-0x7c]: per slot (position word, frame, state), then the stats (0x28..0x31) and HPs (0x32..) */
    BATTLE_FIELD_W = 192,
    BATTLE_FIELD_H = 110,
    BATTLE_BACK_W = 195,       /* the backdrop is 3 pixels wider than the field: the sway reads 0 .. 3 pixels into each row */
    /* the state that does not show on the screen, for comparing with the original: rand state (4, little-endian), the
     * monster table, sway count and direction, who acts ([bp-0x44]) and the turn counter ([bp-0x5e]) */
    BATTLE_STATE_BYTES = 4 + BATTLE_MON_BYTES + 4
};

/* Start the battle the last walk_pass reported (WALK_PASS_BATTLE): does what the original does between the touch and the battle loop's
 * first pass, drawing into `scr`. Returns NULL, with a message on stderr, if a file cannot be read. `w` must outlive the battle. */
battle_t *battle_begin(walk_t *w, screen_t *scr);
void battle_free(battle_t *b);

/* One pass of the battle loop with the keyboard port reading `port60` (as for walk_pass). Returns WALK_PASS_UNBUILT when the original
 * would have done something not built (battle_unbuilt says what), else 0. */
int battle_pass(battle_t *b, screen_t *scr, unsigned port60);
const char *battle_unbuilt(const battle_t *b);

void battle_state_dump(const battle_t *b, uint8_t out[BATTLE_STATE_BYTES]);

/* ---- the pieces, exposed for tests ---- */

/* (0x26de8, one row) Stretch `sw` pixels to `dw` (>= sw): a 16-bit step 0xffff * sw / dw, starting at 1; the source moves on at each carry. */
void battle_stretch_row(const uint8_t *src, unsigned sw, uint8_t *dst, unsigned dw);
/* (0x26f72) Shrink a sw x sh picture to dw x dh (dw <= sw, dh <= sh) with whole and fractional steps; the fraction across a row is
 * not reset between rows, as in the original. */
void battle_shrink(const uint8_t *src, unsigned sw, unsigned sh, uint8_t *dst, unsigned dw, unsigned dh);
/* (0x2c6b9) The field's 110 rows from the 195-wide backdrop, row r starting `offset` pixels in; the offset starts at `count` and
 * bounces between 0 and 3 (up while `dir` is set). */
void battle_sway(const uint8_t *back, uint8_t *field, unsigned count, unsigned dir);
/* (0x1c47c, mode 0) A number as a 20 x 5 picture of 3 x 5 digits 4 pixels apart, leading zeros left out, digit pixels in `colour`. */
void battle_number(unsigned value, const uint8_t font[150], uint8_t out[100], uint8_t colour);

#endif
