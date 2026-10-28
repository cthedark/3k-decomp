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
/* walk_begin with the start's zoom shown: `show` (when not NULL) is called after each of its WALK_ZOOM_STEPS pictures. */
int walk_begin_shown(walk_t *w, screen_t *scr, uint32_t seed, uint32_t ticks, void (*show)(void *ctx, screen_t *scr), void *ctx);

/* The start's zoom (0x11c25, 0x19ab2): the world is drawn as the F-keys draw it and its 192 x 168 window grabbed; then in 13 steps a
 * middle part of it, 24 x 24 growing by 14 x 12 a step to the whole window, is scaled up to the window (walk_zoom_scale) and shown at
 * (64, 16). The original waits for nothing between the steps. walk_zoom_step draws step k (0 .. 12; step 0 also draws and grabs). */
enum { WALK_ZOOM_W = 192, WALK_ZOOM_H = 168, WALK_ZOOM_STEPS = 13 };
void walk_zoom_step(walk_t *w, screen_t *scr, int k);
/* Step 0's drawing and grab alone (0x172df: into the map buffer [bp-0xba], which the view reads above the map's top), nothing shown:
 * for a start without the zoom on screen (tests/walk_dump.c). */
void walk_zoom_keep(walk_t *w);
/* 0x26e5f: `src` (sw x sh, one byte more readable) scaled to WALK_ZOOM_W x WALK_ZOOM_H in `dst`, which is cleared first. Steps of
 * 0xffff * sw / 192 across and 0xffff * sh / 168 down, added from 1: each source pixel is written only at the destination pixel where
 * the step carries, every other pixel and row stays 0, so a small part comes out as spaced dots. */
void walk_zoom_scale(uint8_t *dst, const uint8_t *src, unsigned sw, unsigned sh);
/* What walk_begin's first pass returned (walk_pass's mask): WALK_PASS_BATTLE when the party starts on an enemy. */
int walk_begin_result(const walk_t *w);

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
 * 0x4d right) walk the hero (see walk_keys in walk.c) and Space (0x39) fires the party's shots; no other key and no mouse is read yet.
 * Returns a mask: WALK_PASS_UNBUILT when the original did something that is not built (walk_unbuilt says what).
 * WALK_PASS_VILLAGER_EVENT is no longer returned: the case it reported (0x13356) is an enemy coming back, now built. */
enum { WALK_PASS_VILLAGER_EVENT = 1, WALK_PASS_UNBUILT = 2, WALK_PASS_BATTLE = 4 };
int walk_pass(walk_t *w, screen_t *scr, uint32_t ticks, unsigned port60);

/* ---- for battle.c: a pass that returns WALK_PASS_BATTLE has left the loop where the original leaves it for a battle (after the
 * window copy, before the villagers' steps), with the touched enemy already moved away when its byte 0x2f is 0. ---- */
uint32_t *walk_rng(walk_t *w);
/* The mouse: `on` when a driver is there (the original asks INT 33h once at its start), and what it reports (function 3: x and y in
 * screen pixels, the buttons, bit 0 the left one). The battle reads it through walk_mouse, which returns `on`. */
void walk_set_mouse(walk_t *w, int on, int x, int y, unsigned buttons);
int walk_mouse(const walk_t *w, int *x, int *y, unsigned *buttons);
/* The pointer's 6 x 10 pictures: grab from the screen, put (with `clear`, colour 0 transparent); the shared buffer kept under it, the
 * pointer itself (RPG.LCP 0x300); hide puts back what is under it (0x1287a, at a touch); reset keeps the corner (0x132b0, after a
 * battle). Pixels past the screen's end are dropped (they are read as 0). */
void walk_mouse_grab(const screen_t *scr, unsigned at, uint8_t out[60]);
void walk_mouse_put(screen_t *scr, unsigned at, const uint8_t px[60], int clear);
uint8_t *walk_mouse_under(walk_t *w);
const uint8_t *walk_mouse_pic(const walk_t *w);
void walk_mouse_hide(walk_t *w, screen_t *scr);
void walk_mouse_reset(walk_t *w, screen_t *scr);
/* (0x2c8a0) The key a left press at (x, y) stands for in the world, `last` the key of the pass before. */
unsigned walk_mouse_key(unsigned x, unsigned y, unsigned last);
/* Which loop the world's next pass is in, for comparing with the original's loop tops: 0 the world (0x11d36), 4 a message box
 * (0x1b506), 8 a talk's yes / no (0x18d23), 9 its number (0x19166), 10 F3's wait after a take (0x160b0, 0x164ad). */
int walk_kind(const walk_t *w);
/* The palette cycle of 0x1c284 (the battle runs it too, on the same counters). */
void walk_palette_step(walk_t *w, screen_t *scr);
/* The same on the dimmed copy of the palette the F-keys make ([bp-0xb0]): what F4's picker and magic list step. */
void walk_palette_step_dim(walk_t *w, screen_t *scr);
/* With `show` set, F4's picker shows the steps of a helped member's face flash through it (battle_set_show). */
void walk_set_show(walk_t *w, void (*show)(void *ctx, screen_t *scr), void *ctx);
/* F4's picker and magic list while they run (a battle_t in its world mode, battle.h), else NULL. */
struct battle;
struct battle *walk_cast(const walk_t *w);
/* The touch: the member's offset in the party table (0, 0x28, ...), the monster group, the enemy record's byte 0x2f and the record's
 * offset in the villager table. */
void walk_battle_info(const walk_t *w, unsigned *member, unsigned *group, unsigned *boss, unsigned *rec);
/* The battle routine's [bp-0x84] is never set before its loop's first monster pass, so a battle starts with what the last one left
 * there (0 before any): the battle keeps it here when it is freed. */
unsigned *walk_battle_left(walk_t *w);
/* The game's sound flags (0x2cc5:5, FM music; 0x2cc5:0xc, the Sound Blaster's sounds): 0 when there is no such card, 0x81 on, 0x80
 * off. Both 0 after walk_create; the caller sets what it has. The battle's keys 1 and 2 switch them (the caller then stops or starts
 * the song). */
void walk_set_sound(walk_t *w, unsigned fm, unsigned sb);
/* (0x106c0 .. 0x10730) At the start, after the interface: the music button pressed when there is an FM card, the sound button when
 * there is a Sound Blaster (walk_begin does this). */
void walk_sound_buttons(const walk_t *w, screen_t *scr);
unsigned *walk_fm(walk_t *w);
unsigned *walk_sb(walk_t *w);
/* The Sound Blaster's voice driver as the world and the battle use it (docs/findings.md, "WALK.SL step 25"): play the sound-data block
 * at `off` in `file` (SFX_EF, SFX_LOC of sfx.h), cutting any that plays; whether one plays (the busy word [0x24e]); cut it. With none
 * set (walk_create) nothing plays and nothing is busy. */
typedef struct {
    void (*play)(void *ctx, unsigned file, unsigned off);
    int (*busy)(void *ctx);
    void (*stop)(void *ctx);
    void *ctx;
    void (*wait)(void *ctx);   /* the loops that spin until the busy word is 0 (0x1609e, 0x1649b); NULL: nothing waits */
} walk_sfx_t;
void walk_set_sfx(walk_t *w, const walk_sfx_t *sfx);
/* (0x1c68e) A sound started, when the sound flag is 0x81. */
void walk_sfx_play(walk_t *w, unsigned file, unsigned off);
/* (cmp word [0x24e], 0) Whether a sound still plays. */
int walk_sfx_busy(walk_t *w);
/* (0x2914d) The sound cut, when the flag is 0x81 (the callers test it). */
void walk_sfx_stop(walk_t *w);
/* A loop that waits until the sound has played (0x1609e, 0x1649b), with the sounds on. */
void walk_sfx_wait(walk_t *w);
/* (0x1c3ba) A button pressed or let go: the w x h picture at screen offset `at` turned round, its inside put back two in. */
void walk_button_toggle(screen_t *scr, unsigned at, unsigned w, unsigned h);
/* The world drawn again at the hero with walk frame 0 and its middle 96 x 84 doubled through `table` (0x2c658): the 192 x 168 picture
 * the battle's backdrop is made from. Uses (and overwrites) the world buffer. */
void walk_battle_view(walk_t *w, const uint8_t table[256], uint8_t window[192 * 168]);
/* (0x1bd6e) The coin count (party table word 0x122) drawn again. */
void walk_show_gold(walk_t *w, screen_t *scr);
/* After a won battle (0x13300 on): the rest of the pass that touched the enemy, from the villagers' steps (0x13318), with the clock
 * `ticks` and the key `port60` of the pass in which the battle ended. `result` is what the battle returned (the monsters it killed);
 * like the original, it is left in the hero's walk counter, and the enemy record's byte 0x2f in his frame. After a story enemy (byte
 * 0x2f set) whose five monsters were all killed the pass ends instead (0x172f0), the scene records run again (0x18b82). Returns as
 * walk_pass. */
int walk_resume(walk_t *w, uint32_t ticks, unsigned port60, unsigned result);
/* (0x12fb5 .. 0x130e2) After a battle with a story enemy, before the level-ups: `result` 5 (all killed) takes the enemy away
 * (position 0) and sets its flag (bytes 0xc, 0xd); otherwise the whole party and its trail go to the record's bytes 0x28, 0x29.
 * Nothing for other enemies. */
void walk_boss_after(walk_t *w, unsigned result);

/* Shots ([bp-0x62]: 5 of x, y, direction, zigzag phase) and sparks ([bp-0xcc]: 10 of frame, x, y, direction), kept as one list. */
enum { WALK_SHOTS = 5, WALK_SPARKS = 10, WALK_FX_BYTES = 4 * (WALK_SHOTS + WALK_SPARKS), WALK_ART_BYTES = 0x15E + 0x44 + 0x140,
       WALK_GOLD_AT = 0x122 };   /* the coins: a word in the party table, at most 60,000 */
/* (0x2bac3) Each shot steps 2 along its direction and 2 across, alternately to either side. */
void walk_shots_move(uint8_t fx[WALK_FX_BYTES]);
/* (0x2bb5e) Has `shot` reached a live enemy (byte 0x31 set, not in direction 0x61)? 0: no; 1: hit, the enemy turns the shot's
 * way; 2: its hit counter (byte 0x28) reached twice its sprite set, and it starts going up in smoke (direction 0x61). */
int walk_shot_hit(const uint8_t shot[4], uint8_t *tab_a);
/* (0x1bd6e) The coin count at the bottom right of the screen. */
void walk_draw_gold(screen_t *scr, unsigned value, const uint8_t digits[350]);

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
/* (0x2c28f) F2: the offset of the first scene record at (x, y) in the records' units whose kind is 9 (something to look at), 0x2ee if
 * there is none. */
unsigned walk_look_record(const uint8_t *tab_a, unsigned x, unsigned y);
/* (0x2c28f) The same for any kind: F3 looks for kind 2 (something to take). */
unsigned walk_record_of_kind(const uint8_t *tab_a, unsigned x, unsigned y, unsigned kind);
/* (0x15dfa .. 0x15eaa) F3: the party table offset of the empty item place a taken item goes to: the first of the 20 (0x14 .. 0x27) of
 * the first member whose number is below 5 and whose HP is not 0; 0 when there is none. */
unsigned walk_take_slot(const uint8_t *party);
/* (0x15959 .. 0x15a09) The offset in kind 9 record `r` just past the message F2 shows, which is the word 2 bytes before it: entries of
 * 4 bytes from byte 3 (a flag, its bit, the message) are passed while their flag is set and the next one has a message, at most 3. */
unsigned walk_look_entry(const uint8_t *party, const uint8_t *r);
/* (0x1695b .. 0x16afc, 0x16f6a .. 0x170b6) F7: the scene record (offset from 1000, 0x2ee none) its item (`coins` 0) or coins (1) are
 * used on: the hero's cell and the one beside it (right facing up or down, below facing left or right), then each a step the way he
 * faces, four times, for kind 6 with byte 3 = `coins`'s truth, and for an item kind 0x14 (*scene set). The last looked at is
 * returned; cell[] gets the first cell's x and y and the second's x as they were left. */
unsigned walk_use_find(const uint8_t *tab_a, const uint8_t *party, int coins, int *scene, uint8_t cell[3]);
/* (0x16ce0) The blink's count after its 1200 rounds from `c` (each +1, 0 after 0x190): the name is shown when it ends at 0xc8 or more. */
/* (0x15aeb .. 0x15b42) Whether one of the five party records' 20 item places (0x14 .. 0x27 of each) holds the map, item 0x13; *v96
 * ([bp-0x96]) is the record's place + 0x28 where the search stopped (0xc8 when none has it). */
int walk_map_has(const uint8_t *party, unsigned *v96);
/* (0x1c843 .. 0x1c8f8) A row of F8's map: the 128 cells' pixels `map` stretched to `w` (0x26de8: step 0xffff * 128 / w, from 1)
 * between two 0xb0 edges, w + 2 bytes in `out`; not shaded. */
void walk_map_row(const uint8_t *map, unsigned w, uint8_t *out);
/* (0x1b7d4 .. 0x1b80e) F9: how many party records come before the first whose number (byte 3) is above 4, at most 5. */
unsigned walk_order_members(const uint8_t *party);
/* (0x25c8e) F9: the party records at `at` and `at` + 0x28 swap their bytes 3 .. 0x27 (place and facing stay). */
void walk_order_swap(uint8_t *party, unsigned at);
/* (0x1a7c8) F10: `v` in 4 x 7 digits (`digits`, 28 bytes each) 4 apart at `at` of a 320-wide screen, colour 0 clear; leading zeros
 * left out, the units always drawn. */
void walk_data_number(uint8_t *pix, unsigned at, unsigned v, const uint8_t *digits);
unsigned walk_use_blink(unsigned c);
/* (0x18723 .. 0x18877) A sell counter's text: m's first 21 bytes become 原價:NNNNN 半價:NNNNN (price, then half of it, each number's
 * leading zeros spaces up to its last digit; a 0 after each number, the first then a space). Returns where the second number's spaces
 * stop ([bp-0x96]). */
unsigned walk_sell_text(uint8_t m[22], unsigned price);
/* (0x1252c) How far into the buffer the window is read for a bump of strength `v48` (2, then 1) given the random value `r` (0 .. 0x7fff):
 * t = 4 * v48, d = 0x7fff / t + 1, a = r / d; when a is above v48 the offset is (a - v48) * 224 (1 to 5 rows), else a (0 to 2 pixels). */
unsigned walk_shake_offset(unsigned v48, unsigned r);
/* (0x1b634) A house's roof: `alt` holds the roof flag in byte 0 and the saved part of the object map after it. See walk.c. */
int walk_roof(uint8_t *omap, uint8_t *alt, const uint8_t *rec, int enter);
/* 0x2c658: the 96 x 84 pixels of `src` (WALK_W wide) from (64, 57) doubled into `dst` from (16, 16): each pixel p becomes
 * p, t[p] over t[p], t[t[p]] (t = `table`, the shading table). The F-keys' world drawing does this in a dark scene. */
void walk_double(uint8_t *dst, const uint8_t *src, const uint8_t table[256]);
/* 0x2c581, a dark scene's (map header byte 5 set) world window: the same doubling, but only source rows 34 .. 60 are shaded that way;
 * the rows above and below become p, 0 over 0, 0. Then WS_DARK_MASK_BYTES of `dst` from row 83 are ANDed with `mask` (the lit oval). */
void walk_double_dark(uint8_t *dst, const uint8_t *src, const uint8_t table[256], const uint8_t *mask);

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
 * the first 48 bytes of the palette buffer and the two animation tables (240 bytes each), then the party (200), trail (39),
 * six locals, the roof flag and weather (2), the shots and sparks (60) and the coins (2). */
enum { WALK_STATE_TAIL = 60 + 2,
       WALK_STATE_BYTES = 1000 + 4 + 150 + 600 + 48 + 240 + 240 + 200 + 39 + 6 + 2 + WALK_STATE_TAIL };
void walk_state_dump(const walk_t *w, uint8_t out[WALK_STATE_BYTES]);

/* The game's rand() on a state: advances it and returns state >> 16 & 0x7fff. */
unsigned walk_rand(uint32_t *state);

/* (0x2c362) 25 x 23 ground tiles from map tile (x/2 - 11, y/2 - 10); the first at buffer (16, 16), 4 pixels
 * further left for an odd x and 4 rows further up for an odd y. A tile number is a 16-bit word shifted left by 6
 * (in 16 bits) into the tile data. The cell's offset is worked out in bytes and 16 bits as the original's is, so past the map's
 * edges it reads what follows the map in the original's segment (walk_seg_byte), and a tile offset past the tile data wraps into
 * the object sprites (findings.md "WALK.SL step 27"). */
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
