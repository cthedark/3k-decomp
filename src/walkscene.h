/*
 * WALK.SL's start-up scene loader: what the game builds in memory, from its files, before the first frame.
 *
 * This is the C version of the loader that tools/walk_scene.py models and that the oracle confirmed
 * (docs/findings.md, "WALK.SL step 4"; the original's code is WALK.SL 0x10b00 .. 0x11c17).
 * It has no clock, no input and no SDL.
 *
 * Inputs (all read from the data directory): MAN_INFO.NOW (the party and the flags), __1.NOW (the villagers and
 * the scene records, 1,750 bytes per scene), TAB.TAB (an index of 5-byte entries), __2.ALL (the maps), GTAB.ALL
 * (tiles and object sprites), M_M.ALL and RPG.LCP (sprite banks, the speed class).
 *
 * Where the original would read memory it did not set (a tile or sprite number outside the data read, a record
 * pointing outside its map), this module does not guess: the tile and sprite buffers are zero-padded so that a
 * stray number reads zeros, and a record that writes outside its map makes the load fail (walk_scene_load returns
 * NULL and says why on stderr). The shipped data does neither. One exception is copied from the original on purpose: the
 * view past the map's edges reads the original's segment layout (walk_seg_byte; findings.md "WALK.SL step 27").
 */
#ifndef TK_WALKSCENE_H
#define TK_WALKSCENE_H

#include <stddef.h>
#include <stdint.h>

enum {
    WS_STATE = 1750,        /* bytes per scene in __1.NOW */
    WS_VILLAGERS = 20,      /* villager records: __1.NOW bytes 0..999 of a scene, 50 bytes each */
    WS_VILLAGER_SIZE = 50,
    WS_RECORDS = 50,        /* scene records: bytes 1000..1749 of a scene, 15 bytes each */
    WS_RECORD_SIZE = 15,
    WS_TAB_A = WS_STATE + WS_VILLAGERS,   /* the villager table plus a copy of byte 8 of each villager */
    WS_TAB_B = 520,         /* MAN_INFO.NOW: 13 party records of 40 bytes, a flag table at 200.. */
    WS_PARTY_SIZE = 40,
    WS_FLAGS_AT = 200,      /* the flag table in tab_b */
    WS_MAP_W = 128,
    WS_GMAP_BYTES = 128 * 100 * 2,   /* ground map: 128 x 100 little-endian words */
    WS_OMAP_BYTES = 128 * 100,       /* object map: 128 x 100 bytes, 0xff = empty */
    WS_LIST_BYTES = 200,    /* the list of 100 words that scene records of kind 0x0f add to */
    WS_BLOCK_BYTES = 240,   /* the two animation tables */
    WS_TRAIL_BYTES = 39,    /* the party's trail: 13 entries of (x, y, 1) */
    WS_TILES_PAD = 0x10000, /* tile data is padded to this: a tile number is a 16-bit word shifted left by 6 */
    WS_SPRITES_PAD = 0x8000 + 256,
    WS_BANK1_PAD = 0x2800 + 256,
    WS_BANK2_PAD = 0x10000 + 256,
    WS_DARK_MASK_BYTES = 0x187A * 2   /* what the dark view (0x2c581) ANDs of the oval: 6266 words */
};

typedef struct {
    int scene;                       /* MAN_INFO.NOW byte 500 */
    uint8_t header[6];               /* the first 6 bytes of the scene's block in __2.ALL */
    int weather;                     /* header[4]: 1 = rain */
    int speed_class;                 /* RPG.LCP byte 0 */
    int start_x, start_y;            /* the party's start, in half tiles */
    uint8_t gmap[WS_GMAP_BYTES];
    uint8_t omap[WS_OMAP_BYTES];
    uint8_t tab_a[WS_TAB_A];
    uint8_t tab_b[WS_TAB_B];
    uint8_t list[WS_LIST_BYTES];
    uint8_t block_x[WS_BLOCK_BYTES]; /* the animated ground tiles' table (walk.h, 0x2b898); decoded map bytes 38500.. */
    uint8_t block_y[WS_BLOCK_BYTES]; /* the animated object ids' table (0x2b99a); decoded map bytes 38840.. */
    uint8_t trail[WS_TRAIL_BYTES];
    uint8_t *tiles;                  /* WS_TILES_PAD bytes: 8 x 8 tiles of 64 bytes; tiles_len of them are data */
    size_t tiles_len;
    uint8_t *sprites;                /* WS_SPRITES_PAD bytes: 8 x 8 masked sprites of 128 bytes (64 pixels, 64 mask) */
    size_t sprites_len;
    uint8_t *bank1;                  /* WS_BANK1_PAD bytes: the villagers' 16 x 16 sprites */
    size_t bank1_len;
    uint8_t *bank2;                  /* WS_BANK2_PAD bytes: the party's 16 x 16 sprites */
    size_t bank2_len;
    uint8_t *puff;                   /* RPG.LCP 0x1a4cc decoded: five 16 x 16 frames of an enemy going up in smoke, at [bp-0xba] + 0xe90a */
    size_t puff_len;
    uint8_t *dark_mask;              /* RPG.LCP 0x4350 decoded (12,544 bytes, 224 wide): the lit oval of a dark scene, 0xff in it, 0 out */
    size_t dark_mask_len;
    size_t song_pos, song_len;       /* where the scene's song is in MUC.ALL (read, not played here) */
    int events;                      /* scene records of kind 0x10 that fired (a cut scene starts; not modelled) */
    uint8_t number_tail[0x585];      /* NUMBER.LCZ 0x359b, 0x585 bytes as read ([bp-0x28], after the object map in its segment) */
    const uint8_t *view_head;        /* the map buffer [bp-0xba]'s first 0x45a bytes as walk.c keeps them (the start zoom's window), or
                                      * raw_head before it: what the view reads above the map's top */
    uint8_t raw_head[0x45A];         /* the scene's block of __2.ALL as read into [bp-0xba] at the start (0 past its end, a GUESS) */
} walk_scene_t;

/* A byte of the 64 KB segment the original keeps the ground map in (at offset 0x16), as the view's drawing reads it past the map's
 * ends (0x2c362, 0x2c456: their offsets wrap in 16 bits). See walk.c. */
unsigned walk_seg_byte(const walk_scene_t *s, unsigned off);

/* Load the scene named by MAN_INFO.NOW from the data directory. Returns NULL (and a message on stderr) if a file
 * is missing or malformed, or a record writes outside its map. */
walk_scene_t *walk_scene_load(void);
/* The same after a scene change (0x10bf9 ..): the scene and the party's place are bytes 510 .. 513 of `party` (the table in memory,
 * WS_TAB_B bytes), and the scene's block of __1.NOW is read as the door just wrote it. */
walk_scene_t *walk_scene_load_party(const uint8_t *party);
void walk_scene_free(walk_scene_t *s);

/* The loop over the 50 scene records (what the loader runs once, and the main loop runs again on the live state whenever the hero stands on a
 * record's position): patches the maps, the villager table, the flags and the list of walkable tiles. Returns 0, or -1 (with a message on stderr)
 * if a record writes outside its map or the list overflows. */
int walk_scene_apply_records(walk_scene_t *s);
/* The same, calling `fired` where a kind 0x10 record fires (the records after it not yet applied), when it is not NULL. */
int walk_scene_apply_records_cb(walk_scene_t *s, void (*fired)(void *ctx), void *ctx);

/* The flag test of the original (0x2c158, mode 0): bit `bit` of byte `idx` of the flag table, with the shift count
 * bit + 1 masked to 5 bits as the CPU does; a count of 0 leaves the carry as the compare before it set it, i.e. 1. */
int walk_flag_test(const uint8_t *tab_b, unsigned idx, unsigned bit);
/* (0x2c158, mode 1) Set flag bit `bit` of byte `idx` of the flag table (a bit number of 8 or more sets nothing). */
void walk_flag_set(uint8_t *tab_b, unsigned idx, unsigned bit);

/* Write the whole loaded state to `path` as the concatenation, in this order: gmap, omap, tab_a, tab_b, list, trail,
 * tiles (tiles_len), sprites (sprites_len), bank1 (bank1_len), bank2 (bank2_len), then a text line with
 * "scene N events E speed S start X Y". For comparing with tools/walk_scene.py. Returns 0 on success. */
int walk_scene_dump(const walk_scene_t *s, const char *path);

#endif
