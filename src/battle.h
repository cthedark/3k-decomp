/*
 * WALK.SL's battle, stages 1 to 4: the hand-over from the world and the battle's first screen, its idle passes, choosing a command and
 * a target, whole rounds of attacks up to the end of the battle, and what follows a win.
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
 *     its frame now and then from rand()), the window copy to (64, 74);
 *   - stage 2, the keys of the party's turn (0x1e7e3 .. 0x1f1c7, docs/findings.md "WALK.SL step 14"): Left and Right move along the
 *     four command tabs (round), Space or Enter confirms; the first command then marks the first live front monster as the target
 *     (a box of three outlines), Left and Right move it to the nearest live front monster, Esc goes back. A key counts only after a
 *     release has been seen;
 *   - stage 3, a round (docs/findings.md "WALK.SL step 15"): the party's attack (the blow from experience and two stats, the hit
 *     pictures grown over the target with the member's attack picture, the target flashing, dying behind six masks, a monster from the
 *     back stepping forward, the floating numbers, the shouts), the monsters' turn (each live one doubled in close-up for three frames,
 *     then a blow on a member: the field flashes and jumps, the member's HP and face in the strip), and the turn passing back. Ends
 *     when the monsters or the party are all down (battle_over);
 *   - what follows (docs/findings.md "WALK.SL step 16"): after a win the rewards bar (0x1f900 .. 0x1fcb5: "kills x coins each =
 *     coins" in NUMBER.LCZ 0x359b's 13 x 13 digits, bobbing over the swaying field until Space, Esc or Enter; the coins added, at most
 *     50,000), then the world's level-ups (0x130e2 .. 0x13291) with a message box each (0x1afb0: a MES_TXT.LCC text over the darkened
 *     bottom of the screen, waiting for a key); after a loss nothing more here (the caller shows gameover.h);
 *   - the other three commands (docs/findings.md "WALK.SL step 17"): flee (0x1f86b: one try in two goes to the rewards bar), the magic
 *     list (0x21486, key 1 casts) with the member picker (0x1a8a4) for the spells that help, and the item list (0x20412: key 1 uses,
 *     key 2 turns the page). battle_kind then says which of their loops the next pass is in.
 *
 * The mouse (findings "WALK.SL step 18"): the pointer and the buttons in the fight, the lists, the picker, the bar and the message box.
 * Story enemies, keys 1 and 2 with their cards, the picker face's 48-step flash (findings "WALK.SL step 24"). The Sound Blaster's
 * sounds from SND_EF.1 and _1_.LOC through walk_sfx_play (findings "WALK.SL step 25").
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
     * monster table, then 16 locals (see battle_state_dump), the party table (200) and the floating numbers' list (20 x 3) */
    BATTLE_STATE_BYTES = 4 + BATTLE_MON_BYTES + 16 + 200 + 60
};

/* Start the battle the last walk_pass reported (WALK_PASS_BATTLE): does what the original does between the touch and the battle loop's
 * first pass, drawing into `scr`. Returns NULL, with a message on stderr, if a file cannot be read. `w` must outlive the battle. */
battle_t *battle_begin(walk_t *w, screen_t *scr);
/* F4 in the world (0x15549 .. 0x155d4): the member picker for who casts (faces drawn, a fallen one not taken), then his magic list as
 * the world has it (0x21486 with its buffer's byte 0 = 0: spells on monsters do nothing, palette steps on the dimmed copy), drawing
 * over `scr`. Run it with battle_pass until battle_over says BATTLE_BACK, then battle_free. NULL (said on stderr) when RPG.LCP falls
 * short. */
battle_t *battle_world_cast(walk_t *w, screen_t *scr);
/* F5 in the world (0x156c7 .. 0x1572d): the same picker, then his item list as the world has it (three icons; key 3 gives the lit item
 * to another member through a picker without faces; F6 or its button opens the equipment screen). Run as battle_world_cast. */
battle_t *battle_world_items(walk_t *w, screen_t *scr);
/* F6 (0x15748 .. 0x1577b): the same picker, then his equipment screen (0x221d2: Enter takes the lit thing off; F5 or its button opens
 * the item list). F5 and F6 go back and forth until Esc. */
battle_t *battle_world_equip(walk_t *w, screen_t *scr);
/* F7's item (0x16898 .. 0x16919): the same picker, then his item list with its buffer's byte 0 = 2 (two icons, no giving): key 1 on an
 * item ends it with that item. Run as battle_world_cast; then battle_use_item says what was chosen. */
battle_t *battle_world_use(walk_t *w, screen_t *scr);
/* A shop's member picker alone (0x1a8a4 with faces; `dead`: a fallen member may be chosen), stepping the palette itself, its count
 * starting at `count`. Run as battle_world_cast; then battle_picked gives the member's party offset (0xc8: none). */
battle_t *battle_world_pick(walk_t *w, screen_t *scr, int dead, unsigned count);
unsigned battle_picked(const battle_t *b);
/* The picker's count when it ended (its [bp-0x16], left on the stack for the next picker). */
unsigned battle_pick_count(const battle_t *b);
/* What F7's list returned: the item (its OBJECT.LCC record, 30 bytes, in `rec`), 0xff for F6 (rec untouched), 0 for none; the member
 * picked (his party offset) in *member. */
unsigned battle_use_item(const battle_t *b, uint8_t rec[30], unsigned *member);
void battle_free(battle_t *b);
/* With `show` set, it is called after each of the 48 steps of a helped member's face flash in the picker (three times 16 colours
 * down); the original draws each step 100 times and does not wait, so how long it showed depended on the machine. Without it only the
 * end is seen, as at a loop top. */
void battle_set_show(battle_t *b, void (*show)(void *ctx, screen_t *scr), void *ctx);

/* One pass of the battle loop with the keyboard port reading `port60` (as for walk_pass). Returns WALK_PASS_UNBUILT when the original
 * would have done something not built (battle_unbuilt says what), else 0. */
int battle_pass(battle_t *b, screen_t *scr, unsigned port60);
const char *battle_unbuilt(const battle_t *b);
/* 0 while the battle goes on; BATTLE_LOST when the whole party has fallen (the original then leaves WALK.SL for MARK.EXE's game-over
 * screen); BATTLE_BACK once a won battle is over (rewards and level-ups seen): the caller then plays the scene's song again and ends
 * the pass with walk_resume(w, ticks, port60, battle_result(b)), in the same pass. */
enum { BATTLE_LOST = 2, BATTLE_BACK = 3 };
int battle_over(const battle_t *b);
/* Which loop the next pass is in, for comparing with the original's loop tops: the fight (0x1d418), the rewards bar (0x1fb7d), a
 * message box's wait (0x1b506), the magic list (0x217a6), the member picker (0x1aa30) or the item list (0x207b2). */
enum { BATTLE_KIND_FIGHT = 1, BATTLE_KIND_REWARDS = 3, BATTLE_KIND_DIALOG = 4, BATTLE_KIND_MAGIC = 5, BATTLE_KIND_PICK = 6,
       BATTLE_KIND_ITEMS = 7, BATTLE_KIND_EQUIP = 11 };
int battle_kind(const battle_t *b);
/* What the battle returns: the number of slots whose monster died (0 after a loss). */
unsigned battle_result(const battle_t *b);
/* (0x2464a) A Big5 character's 28-byte glyph in NUMBER.LCZ; (size_t)-1 when its lead byte has none. */
size_t battle_glyph_offset(unsigned hi, unsigned lo);
/* 0x243ec: `n` bytes of text as an (8 n) x 14 picture `px` in `colour` (font8: RPG.LCP 0x1a6b7; Big5 pairs from NUMBER.LCZ); the last
 * character's width in *last_gw. Returns 1 when a character the font does not have was drawn blank. See battle.c. */
int battle_text_pic(const uint8_t *font8, const uint8_t *s, unsigned n, uint8_t colour, uint8_t *px, unsigned *last_gw);
/* N bytes at POS of the data file NAME run-length decoded (0x25eeb); malloc'd, its length in *len; NULL (and a message) on failure. */
uint8_t *battle_read_unpack(const char *name, size_t pos, size_t n, size_t *len);
/* (0x130e2 .. 0x13246) A party record's level-up after a won battle: when its experience (word 4) has reached the next level's
 * (word 6) and is below 60,000, the experience becomes that, the next level 500 higher, the most HP (word 0xa) a twelfth more and the
 * HP (word 8) full, each at most 60,000; returns 1. Otherwise nothing changes and it returns 0. */
int battle_level_up(uint8_t member[0x28]);
/* (0x1fb02 .. 0x1fb3b) The coins after a win: `kills` times `per` more (16-bit), at most 50,000. */
unsigned battle_add_coins(unsigned coins, unsigned kills, unsigned per);

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
/* (0x1ec8f, a party attack) The blow from experience (word 4), byte 0x13 and word 0x10; `back` for the last two members. */
unsigned battle_blow(unsigned exp, unsigned stat13, unsigned word10, int back);
/* (0x1d70f) A monster's blow from its attack word 0x29, halved for the back row, less the member's defence byte 0x12. */
unsigned battle_monster_blow(unsigned atk, int back, unsigned def);
/* (0x1dfc1) The front slot (2, 3 or 4) under a hit picture's column. */
unsigned battle_hit_slot(unsigned col);

#endif
