/*
 * WALK.SL, the game world: the first screen.
 *
 * What this reproduces, from the original's code and the oracle (docs/findings.md, "WALK.SL step 1" to "step 4"):
 *   - the 224 x 200 world buffer: ground tiles, the villagers / party / hero, the object sprites, the rain;
 *   - its middle 192 x 168 pixels copied to screen (64, 16);
 *   - the interface around the window: sixteen button labels in the font of NUMBER.LCZ, each drawn three times
 *     for the engraved look, and two small pieces at the bottom right.
 *
 * `walk_first_screen` produces the screen the original shows right after its first window copy (BIOS tick 21):
 * it draws over the picture the menu left, which must hold the interface palette (the original never changes it
 * before that copy). `walk_pass` then runs the main loop (0x11d36) one pass at a time with nothing pressed: animated
 * tiles, the palette shimmer, the weather clock (rain, dry, dark specks), the villagers walking and the hero's walk
 * counter, all checked pass by pass against the original (tools/check_walk.sh, tools/walk_idle.py). No key or mouse is
 * read yet, nothing is played, and the ten actions are not built.
 *
 * The first frame and the idle passes are checked against the original. The drawing routines are general (any view
 * centre, any hero frame), but odd view centres, other scenes and weather mode 2 are READ from the code, not seen
 * (see findings.md).
 */
#ifndef TK_WALK_H
#define TK_WALK_H

#include <stddef.h>
#include <stdint.h>

#include "screen.h"
#include "walkscene.h"

enum {
    WALK_W = 224,           /* world buffer width */
    WALK_H = 200,           /* world buffer height */
    WALK_RAIN_DROPS = 75,   /* 40 short drops then 35 long ones */
    WALK_ENTITIES = 25      /* 20 villagers then 5 party members */
};

typedef struct walk walk_t;

/* Loads the scene (walkscene.h) and prepares the world state: the rain list from srand(0). Returns NULL, with a
 * message on stderr, if a file is missing or malformed. */
walk_t *walk_create(void);
void walk_free(walk_t *w);
const walk_scene_t *walk_scene(const walk_t *w);

/* The first screen. `scr` holds the picture the menu left (and its palette); the interface and the world window
 * are drawn over it. Resets the rain list, so it can be called again. Returns 0, or -1 if NUMBER.LCZ or MES_TXT.LCC
 * cannot be read (the screen is then unchanged). */
int walk_first_screen(walk_t *w, screen_t *scr);

/* The same, with the clock the original reads at the start: `seed` is srand's argument (the original passes the BIOS
 * tick count it reads first; the shipped checks use 0), `ticks` the clock at the first pass. The interface is drawn, the
 * moving world is reset and pass 0 is run, so later walk_pass calls continue from there. */
int walk_begin(walk_t *w, screen_t *scr, uint32_t seed, uint32_t ticks);

/* The interface: sixteen labels and the two pieces at the bottom right. Returns 0, or -1 if NUMBER.LCZ or
 * MES_TXT.LCC cannot be read. */
int walk_draw_interface(screen_t *scr);

/* Copy the world buffer's middle 192 x 168 pixels to screen (64, 16). */
void walk_copy_window(screen_t *scr, const uint8_t *buf);

/* ---- the pieces, exposed for tests ---- */

/* ---- the world in motion (checked against the original's idle main loop, tools/walk_idle.py) ----
 *
 * One pass of WALK.SL's main loop (0x11d36) with no key and no mouse: the animated tiles step, the palette cycles,
 * the world buffer is drawn and its window copied, every villager takes a step (and sometimes picks a new direction), and
 * the hero's walk counter counts. Everything the keys and the mouse do in that loop is not built; a pass in which
 * the original would do something that is not built reports it (see walk_pass). */

enum {
    WALK_SPECKS = 300,       /* the second random list, used in the last part of each hour */
    WALK_PALBUF = 768
};

/* Start (or restart) the moving world: reloads the scene, then does what the original does before its first pass:
 * srand(seed) (the original seeds with the BIOS tick count it reads first thing), 75 rain values, then 300 distinct
 * words (rand() + 0xe10, a repeat is drawn again; 303 draws for the shipped seed 0). `screen` palette entries
 * 0..15 follow the cycle from the first pass on. Returns 0, or -1 if the scene cannot be loaded. */
int walk_reset(walk_t *w, uint32_t seed);

/* One pass of the main loop at BIOS clock `ticks` (only the low 16 bits are used, like the original) with the keyboard port
 * (0x60) reading `port60`: 0 for no key, a make code while a key is held, the make code + 0x80 after its release. Draws the world
 * into `scr` (window only, plus palette entries 0..15) and advances all moving state. The arrow keys (0x48 up, 0x50 down, 0x4b left,
 * 0x4d right) walk the hero (see walk_keys in walk.c); no other key and no mouse is read yet.
 * Returns a mask: WALK_PASS_VILLAGER_EVENT when a villager reached the special case at 0x13356 (direction 0x61 timing out: the
 * original starts an event there), WALK_PASS_UNBUILT when the original did something that is not built (walk_unbuilt says what). */
enum { WALK_PASS_VILLAGER_EVENT = 1, WALK_PASS_UNBUILT = 2 };
int walk_pass(walk_t *w, screen_t *scr, uint32_t ticks, unsigned port60);

/* What the last pass that reported WALK_PASS_UNBUILT could not do, or NULL if nothing so far. */
const char *walk_unbuilt(const walk_t *w);

/* (0x2bf02) Can the hero stand at (x, y)? The cells under him must all be on the scene's list of walkable tiles: two in the row
 * (y >> 1) + 1, a third when x is odd, the next row's two when y is odd (and a third of them when x is odd too). A tile in the
 * list's last place does not count (the original's scan leaves a count of 0). */
int walk_foot_ok(const uint8_t *gmap, const uint8_t *list, unsigned x, unsigned y);
/* (0x2bfd5) A step: every trail entry (13 of x, y, direction) moves one place on, the hero's new (x, y, direction) goes in front, party record 0
 * gets (x, y) and records 1 to 4 get trail entries 3, 6, 9 and 12. */
void walk_trail_step(uint8_t trail[WS_TRAIL_BYTES], uint8_t *party, unsigned x, unsigned y);
/* (0x2c098) The offset in the villager table's records of the first scene record at the hero's position (x with its low bit cleared,
 * y >> 1), 0x2ee if there is none (or 0x320 for a kind 5 record under odd coordinates). */
unsigned walk_find_record(const uint8_t *tab_a, unsigned x, unsigned y);
/* (0x1252c) How far into the buffer the window is read for a bump of strength `v48` (2, then 1) given the random value `r` (0 .. 0x7fff):
 * t = 4 * v48, d = 0x7fff / t + 1, a = r / d; when a is above v48 the offset is (a - v48) * 224 (1 to 5 rows), else a (0 to 2 pixels). */
unsigned walk_shake_offset(unsigned v48, unsigned r);
/* Copy the world buffer's window to screen (64, 16), read `shift` bytes into the buffer (the bump shake). */
void walk_copy_window_at(screen_t *scr, const uint8_t *buf, unsigned shift);

/* The rain's stride for the weather mode (scene header byte 4: 1 rain, 2 something similar) at a clock reading:
 * 0 when there is none. The first 0x5000 ticks of each 0x10000 (18.8 min of 60) rain, with a stride that grows
 * for 10 steps of 1024 ticks and falls again; the second 0x5000 are dry; after 0xa000 there is no rain either. */
unsigned walk_rain_stride(int weather, uint32_t ticks);

/* (0x2b898) Up to 30 entries of 8 bytes: ground-map byte offset (0 ends the list), period, counter, first tile,
 * last tile (words). Every period + 1 passes the tile steps on; after the last it goes back to the first. */
void walk_animate_ground(uint8_t table[WS_BLOCK_BYTES], uint8_t *gmap);
/* (0x2b99a) Up to 40 entries of 6 bytes: object-map offset (0 ends), period, counter, first id, last id. */
void walk_animate_objects(uint8_t table[WS_BLOCK_BYTES], uint8_t *omap);

/* (0x2c71d / 0x2c795) rotate `n` palette-buffer entries from `first`: forward gives entry k the colour of k + 1, backward of k - 1. */
void walk_pal_rotate(uint8_t *pal, int first, int n, int forward);
/* (0x2c82c) Entry 15 of the buffer: its green byte steps by 7 up (down when the red byte is not 1). */
void walk_pal_ramp(uint8_t *pal);

/* (0x2c2c2) 300 positions in a world buffer of `len` bytes: each darkens its pixel by 2 and moves on (see walk.c). */
void walk_draw_specks(uint8_t *buf, size_t len, uint16_t pos[WALK_SPECKS]);

/* (0x2bccd) One villager's step. Returns the value the original leaves in AL for the caller: 0xa asks for a new
 * direction (walk_villager_decide), 0xc is the unbuilt special case, -1 means "nothing the caller looks at". */
int walk_villager_step(uint8_t rec[WS_VILLAGER_SIZE], const uint8_t *gmap, const uint8_t *list);

/* (0x134ba) The new direction: byte 7 = clock & 7, then one or two rand() values. Returns how many it drew. */
int walk_villager_decide(uint8_t rec[WS_VILLAGER_SIZE], uint32_t *rng, uint32_t ticks, unsigned hero_x, unsigned hero_y,
                         unsigned stride);

/* The state that does not show on the screen, for comparing with the original's (tools/walk_idle.py cross): the 20
 * villager records (1,000 bytes), the rand state (4, little-endian), the rain list (75 words), the specks list (300 words),
 * the first 48 bytes of the palette buffer and the two animation tables (240 bytes each). */
enum { WALK_STATE_BYTES = 1000 + 4 + 150 + 600 + 48 + 240 + 240 + 200 + 39 + 6 };
void walk_state_dump(const walk_t *w, uint8_t out[WALK_STATE_BYTES]);

/* The game's rand() on a state: advances it and returns state >> 16 & 0x7fff. */
unsigned walk_rand(uint32_t *state);

/* (0x2c362) 25 x 23 ground tiles from map tile (x/2 - 11, y/2 - 10); the first at buffer (16, 16), 4 pixels
 * further left for an odd x and 4 rows further up for an odd y. A tile number is a 16-bit word shifted left by 6
 * (in 16 bits) into the tile data. Map cells outside the map are taken as tile 0. */
void walk_draw_ground(uint8_t *buf, const walk_scene_t *s, unsigned x, unsigned y);

/* (0x2c456) The same grid over the object map; a number below 0xff is blitted as (screen & mask) | pixel. */
void walk_draw_objects(uint8_t *buf, const walk_scene_t *s, unsigned x, unsigned y);

/* (0x2c05a + 0x2be49) The draw order of the 25 entities: 0..19 villagers, 20..24 party members, sorted by y with
 * a stable bubble sort (24 passes of 24 compares; a swap needs strictly greater). */
void walk_entity_order(const walk_scene_t *s, uint8_t order[WALK_ENTITIES]);

/* (0x2bbec) The buffer offset of a 16 x 16 sprite's top-left corner, or WALK_HIDDEN (>= 0xc350 is skipped).
 * flag 0: villagers, visible only inside a box around the view, and `vis` (the sprite set) must be 1..5;
 * flag 1: party, no box. Positions are in half tiles (4 pixels). */
enum { WALK_HIDDEN = 0xEA60 };
unsigned walk_screen_pos(unsigned px, unsigned py, unsigned x, unsigned y, int flag, unsigned vis);

/* (0x2bc7c) The offset of a villager's sprite: bank 1 below 0x2800, else bank 2 at offset - 0x9470. */
unsigned walk_sprite_off_villager(unsigned direction, unsigned frame, unsigned vis);
/* (0x2beca) The offset of a party member's sprite in bank 2. `frame` is the walk counter 0..3 (3 is taken as 1). */
unsigned walk_sprite_off_party(unsigned direction, unsigned frame, unsigned number);

/* The entity pass of the frame routine (0x19680): the draw order, then each visible entity as a 16 x 16 sprite with
 * colour 0 transparent. `hero_frame` is the party members' walk counter. */
void walk_draw_entities(uint8_t *buf, const walk_scene_t *s, unsigned x, unsigned y, unsigned hero_frame);

/* The game's rand(): state = state * 0x343FD + 0x269EC3; result = state >> 16 & 0x7fff. The first 75 values
 * after srand(0) are the rain list. */
void walk_rain_initial(uint16_t drops[WALK_RAIN_DROPS]);

/* (0x2c1ae, mode 1) Draw the drops: 40 of 3 pixels (colours 0xae 0xad 0xac) and 35 of 6 (0xad .. 0xa8), each row one
 * pixel to the right (offset + 0xe1), then move each on by stride / 2 (short) or stride (long), modulo 0xa100. */
void walk_draw_rain(uint8_t *buf, uint16_t drops[WALK_RAIN_DROPS], unsigned stride);

/* A label's sprite: width 16 * chars, height 14, one pixel byte each; `big5` has two bytes per character. The
 * renderer (0x243ec) shades glyph rows 0-4 with 0x89, 5-9 with 0x8a, 10-13 with 0x88; two-character labels are
 * then recoloured over three overlapping byte ranges (0x103ab). Returns 0, or -1 if the font cannot be read.
 * `out` needs 16 * chars * 14 bytes. */
int walk_label_sprite(const uint8_t *big5, int chars, uint8_t *out);

/* The offset in NUMBER.LCZ of a character's 28-byte glyph (30-byte cells in Big5 order, from lead byte 0xa4). */
size_t walk_glyph_offset(unsigned hi, unsigned lo);

#endif
