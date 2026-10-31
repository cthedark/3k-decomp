#include "walkscene.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"
#include "lcz.h"

static unsigned w16(const uint8_t *b, size_t o)
{
    return (unsigned)b[o] | ((unsigned)b[o + 1] << 8);
}

int walk_flag_test(const uint8_t *tab_b, unsigned idx, unsigned bit)
{
    unsigned n = (bit + 1) & 31;
    if (n == 0)
        return 1;
    return n <= 8 ? (tab_b[WS_FLAGS_AT + idx] >> (n - 1)) & 1 : 0;
}

void walk_flag_set(uint8_t *tab_b, unsigned idx, unsigned bit)
{
    tab_b[WS_FLAGS_AT + idx] |= (uint8_t)((1u << (bit & 31)) & 0xFF);
}

/* The original's map segment (the 0xfb70 bytes allocated at 0x1019c, at offset 0x16 of its segment; SEEN in the oracle at pass 5,
 * work/b8/probe_gmap.py, and compared with the port's buffers): 0x16 the ground map (0x6400), 0x6416 the villagers' sprite bank
 * (0x2800), 0x8c16 the villager table and records (0x6ea), 0x9300 the object map (0x3200), 0xc500 NUMBER.LCZ's 0x585 bytes, 0xca85 a 0,
 * 0xca86 the dark scenes' oval (0x3100), then the allocator's header of the next block and the map buffer [bp-0xba] from 0xfba6.
 * The two headers (0 .. 0x15 and 0xfb86 .. 0xfba5) hold the oracle's segment numbers and sizes: what DOS gave that run, a GUESS for
 * any other machine. */
static const uint8_t seg_head[0x16] = {0xC0, 0x6D, 0x03, 0x00, 0x90, 0xFB, 0x14, 0x00, 0x14, 0x00, 0x8E,
                                       0xFB, 0x00, 0x00, 0x79, 0x7D, 0x00, 0x00, 0xEB, 0x5D, 0x70, 0xFB};
static const uint8_t seg_next[0x20] = {0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFE, 0xFF, 0x79, 0x7D, 0x03, 0x00, 0x30, 0xEE,
                                       0x14, 0x00, 0x20, 0xEE, 0x2E, 0xEE, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC0, 0x6D, 0x0A, 0xEE};

unsigned walk_seg_byte(const walk_scene_t *s, unsigned off)
{
    off &= 0xFFFF;
    if (off < 0x16)
        return seg_head[off];
    if (off < 0x6416)
        return s->gmap[off - 0x16];
    if (off < 0x8C16)
        return s->bank1 ? s->bank1[off - 0x6416] : 0;
    if (off < 0x9300)
        return s->tab_a[off - 0x8C16];
    if (off < 0xC500)
        return s->omap[off - 0x9300];
    if (off < 0xCA85)
        return s->number_tail[off - 0xC500];
    if (off < 0xCA86)
        return 0;
    if (off < 0xFB86)
        return s->dark_mask && off - 0xCA86 < s->dark_mask_len ? s->dark_mask[off - 0xCA86] : 0;
    if (off < 0xFBA6)
        return seg_next[off - 0xFB86];
    return s->view_head ? s->view_head[off - 0xFBA6] : 0;   /* [bp-0xba]: only the rows above the map read it */
}

/* A write that stays inside its buffer; otherwise remember that the load must fail. */
static int g_oob;
static void put16(uint8_t *b, size_t len, unsigned o, unsigned v)
{
    if (o + 2 > len) {
        g_oob = 1;
        return;
    }
    b[o] = (uint8_t)v;
    b[o + 1] = (uint8_t)(v >> 8);
}

static void put8(uint8_t *b, size_t len, unsigned o, unsigned v)
{
    if (o + 1 > len) {
        g_oob = 1;
        return;
    }
    b[o] = (uint8_t)v;
}

/* Decode the run-length block that starts at `p` (escape byte, 16-bit size, data). Returns a malloc'ed buffer
 * with at least `pad` bytes (zero-filled beyond the data), and the data length in *len. */
static uint8_t *decode_block(const uint8_t *p, size_t avail, size_t pad, size_t *len)
{
    size_t pos = 0;
    lcz_chunk c;
    if (lcz_next_chunk(p, avail, &pos, &c) != LCZ_OK)
        return NULL;
    size_t cap = c.len > pad ? c.len : pad;
    uint8_t *out = calloc(cap, 1);
    if (out)
        memcpy(out, c.pixels, c.len);
    *len = c.len;
    lcz_chunk_free(&c);
    return out;
}

/* A 5-byte index entry of TAB.TAB: file offset (byte 2 << 16 | word 0) and length (word 3). */
static int tab_entry(const uint8_t *tab, size_t tab_len, size_t at, size_t *pos, size_t *len)
{
    if (at + 5 > tab_len)
        return -1;
    *pos = ((size_t)tab[at + 2] << 16) + w16(tab, at);
    *len = w16(tab, at + 3);
    return 0;
}

static uint8_t *read_range(const char *name, size_t pos, size_t len)
{
    uint8_t *b = malloc(len ? len : 1);
    if (!b || assets_read_at(name, pos, b, len) != 0) {
        free(b);
        fprintf(stderr, "walk: cannot read %zu bytes at %zu of %s\n", len, pos, name);
        return NULL;
    }
    return b;
}

void walk_scene_free(walk_scene_t *s)
{
    if (!s)
        return;
    free(s->tiles);
    free(s->sprites);
    free(s->bank1);
    free(s->bank2);
    free(s->puff);
    free(s->dark_mask);
    free(s);
}

int walk_scene_apply_records(walk_scene_t *s)
{
    return walk_scene_apply_records_cb(s, NULL, NULL);
}

int walk_scene_apply_records_cb(walk_scene_t *s, void (*fired)(void *ctx), void *ctx)
{
    /* The 50 scene records (the loop at 0x113c4 .. 0x11c0e). R(k) is byte k of the record, W(k) the word at k. The loader runs
     * this once; the main loop runs it again, on the live state, each time the hero stands on a record's position. */
    uint8_t *ta = s->tab_a, *tb = s->tab_b;
    g_oob = 0;
    for (int i = 0; i < WS_RECORDS; i++) {
        unsigned o = 1000 + WS_RECORD_SIZE * (unsigned)i;
#define R(k) ((unsigned)ta[o + (k)])
#define W(k) w16(ta, o + (k))
        unsigned kind = R(2);
        if (kind == 0x0B) {
            for (unsigned k = 3; k < 0x0F; k += 3) {
                unsigned value = R(k);
                for (unsigned di = 0; di < 0xC8; di += 0x28) {
                    if (tb[di + 3] > 10)
                        break;   /* a member who is not in the party ends the search (0x11430) */
                    int found = 0;
                    for (unsigned b = di + 0x14; b < di + 0x28; b++)
                        if (tb[b] == value) {
                            found = 1;
                            break;
                        }
                    if (found) {
                        walk_flag_set(tb, R(k + 1), R(k + 2));
                        break;
                    }
                }
            }
        }
        if (kind == 0x0A) {
            if (walk_flag_test(tb, R(7), R(8)))
                put16(s->gmap, sizeof s->gmap, W(3), W(5));
            if (walk_flag_test(tb, R(13), R(14)) && W(9) != 0)
                put16(s->gmap, sizeof s->gmap, W(9), W(11));
        }
        if (kind == 0x0C) {
            if (walk_flag_test(tb, R(6), R(7)))
                put8(s->omap, sizeof s->omap, (R(4) << 7) + R(3), R(5));
            if (walk_flag_test(tb, R(11), R(12)) && W(8) != 0)
                put8(s->omap, sizeof s->omap, (R(9) << 7) + R(8), R(10));
        }
        if (kind == 0x02 && R(13) == 0) {
            unsigned xx = R(0), yy = R(1);
            if (R(6) != 0) {
                put16(s->omap, sizeof s->omap, (yy << 7) + (xx >> 1), R(7) | (R(8) << 8));
            } else {
                put16(s->gmap, sizeof s->gmap, (yy << 8) + xx, R(7) | (R(8) << 8));
                put16(s->gmap, sizeof s->gmap, (yy << 8) + xx + 2, R(9) | (R(10) << 8));
            }
        }
        if (kind == 0x0D) {
            if (walk_flag_test(tb, R(3), R(4)))
                put16(ta, WS_TAB_A, W(5), W(7));
        }
        if (kind == 0x0F) {
            if (walk_flag_test(tb, R(3), R(4))) {
                for (unsigned di = 5; di < 0x0F; di += 2) {
                    unsigned v = W(di);
                    if (v > 0x226)
                        break;
                    unsigned j = 0;
                    while (j < 0xC8) {
                        unsigned wv = w16(s->list, j);
                        if (wv == v || wv > 0x226)
                            break;
                        j += 2;
                    }
                    if (j >= 0xC8) {
                        fprintf(stderr, "walk: the list of scene %d overflows\n", s->scene);
                        return -1;
                    }
                    put16(s->list, WS_LIST_BYTES, j, v);
                }
            }
        }
        if (kind == 0x10) {
            if (R(5) != 0 && walk_flag_test(tb, R(3), R(4))) {
                s->events++;
                ta[o + 5] = 0;
                if (fired)   /* 0x11b2b: here, with the records after this one not applied yet */
                    fired(ctx);
            }
        }
#undef R
#undef W
        if (g_oob) {
            fprintf(stderr, "walk: record %d of scene %d (kind 0x%02x) writes outside its map\n", i, s->scene, kind);
            return -1;
        }
    }
    return 0;
}

/* `party` NULL: the start (MAN_INFO.NOW read, its bytes 500..503 copied to 510..513, 0x10b17); else a scene change (0x10bf9 on): the
 * party table as it is in memory, 510..513 already set by the door. */
static walk_scene_t *scene_load(const uint8_t *party)
{
    walk_scene_t *s = calloc(1, sizeof *s);
    uint8_t *mi = NULL, *now = NULL, *tab = NULL, *raw = NULL, *map = NULL, *gt = NULL, *m1 = NULL, *rp = NULL;
    size_t mi_len = 0, now_len = 0, tab_len = 0, rp_len = 0;
    if (!s)
        return NULL;
    g_oob = 0;

    if (!party)
        mi = assets_read("MAN_INFO.NOW", &mi_len);
    now = assets_read("__1.NOW", &now_len);
    tab = assets_read("TAB.TAB", &tab_len);
    rp = assets_read("RPG.LCP", &rp_len);
    if ((!party && !mi) || !now || !tab || !rp || rp_len < 0x7ADD + 5 * 256) {
        fprintf(stderr, "walk: MAN_INFO.NOW, __1.NOW, TAB.TAB or RPG.LCP is missing or too short\n");
        goto fail;
    }
    if (tab_len > 1000)
        tab_len = 1000;   /* the original reads 1,000 bytes (0x3e8) */

    /* The party table: the file, then bytes 500..503 copied to 510..513 (0x10b17). */
    uint8_t *tb = s->tab_b;
    if (party) {
        memcpy(tb, party, WS_TAB_B);
    } else {
        memcpy(tb, mi, mi_len < WS_TAB_B ? mi_len : WS_TAB_B);
        memcpy(tb + 510, tb + 500, 4);
    }
    s->scene = tb[510];

    /* The scene's state block and the copy of each villager's byte 8. */
    uint8_t *ta = s->tab_a;
    size_t blk = (size_t)s->scene * WS_STATE;
    if (blk < now_len)
        memcpy(ta, now + blk, now_len - blk < WS_STATE ? now_len - blk : WS_STATE);
    for (int i = 0; i < WS_VILLAGERS; i++)
        ta[WS_STATE + i] = ta[8 + WS_VILLAGER_SIZE * i];

    /* The scene's block of __2.ALL: 6 header bytes, then run-length data (the same code as .LCZ). */
    size_t mpos, mlen;
    if (tab_entry(tab, tab_len, 5 * (size_t)s->scene, &mpos, &mlen) != 0 || mlen < 9) {
        fprintf(stderr, "walk: scene %d is not in TAB.TAB\n", s->scene);
        goto fail;
    }
    raw = read_range("__2.ALL", mpos, mlen);
    if (!raw)
        goto fail;
    memcpy(s->header, raw, 6);
    memcpy(s->raw_head, raw, mlen < sizeof s->raw_head ? mlen : sizeof s->raw_head);
    s->view_head = s->raw_head;
    s->weather = s->header[4];
    tb[518] = raw[1];
    size_t dlen = 0;
    map = decode_block(raw + 6, mlen - 6, 0, &dlen);
    if (!map || dlen < 39080) {
        fprintf(stderr, "walk: scene %d's map block does not decode to 39,080 bytes (%zu)\n", s->scene, dlen);
        goto fail;
    }
    memcpy(s->list, map, 100);
    memcpy(s->list + 100, map + 38740, 100);
    for (int k = 0; k < 12800; k++) {   /* two planes: low bytes, then high bytes */
        s->gmap[2 * k] = map[100 + k];
        s->gmap[2 * k + 1] = map[100 + 12800 + k];
    }
    memcpy(s->omap, map + 25700, WS_OMAP_BYTES);
    memcpy(s->block_x, map + 38500, WS_BLOCK_BYTES);
    memcpy(s->block_y, map + 38840, WS_BLOCK_BYTES);

    /* Tiles and object sprites: entry 0x109 + 10 * tile set; lengths at +3 and +8. */
    size_t ent = 0x109 + 10 * (size_t)s->header[0];
    if (ent + 10 > tab_len) {
        fprintf(stderr, "walk: tile set %d is not in TAB.TAB\n", s->header[0]);
        goto fail;
    }
    size_t gpos = ((size_t)tab[ent + 2] << 16) + w16(tab, ent);
    size_t len1 = w16(tab, ent + 3), len2 = w16(tab, ent + 8);
    gt = read_range("GTAB.ALL", gpos, len1 + len2);
    if (!gt)
        goto fail;
    s->tiles = calloc(len1 > WS_TILES_PAD ? len1 : WS_TILES_PAD, 1);
    s->sprites = calloc(len2 > WS_SPRITES_PAD ? len2 : WS_SPRITES_PAD, 1);
    if (!s->tiles || !s->sprites)
        goto fail;
    memcpy(s->tiles, gt, len1);
    s->tiles_len = len1;
    memcpy(s->sprites, gt + len1, len2);
    s->sprites_len = len2;

    /* Sprite bank 1 (M_M.ALL): number n = header byte 1, less 5 when above 0x13. */
    unsigned n = s->header[1] > 0x13 ? s->header[1] - 5u : s->header[1];
    size_t bpos, blen;
    if (tab_entry(tab, tab_len, 0x334 + 5 * (size_t)n, &bpos, &blen) != 0) {
        fprintf(stderr, "walk: sprite bank %u is not in TAB.TAB\n", n);
        goto fail;
    }
    m1 = read_range("M_M.ALL", bpos, blen);
    if (!m1)
        goto fail;
    s->bank1 = decode_block(m1, blen, WS_BANK1_PAD, &s->bank1_len);
    /* Bank 2 (the party's sprites): RPG.LCP 0x1ae37, 0x2696 bytes. */
    s->bank2 = decode_block(rp + 0x1AE37, rp_len - 0x1AE37 < 0x2696 ? rp_len - 0x1AE37 : 0x2696, WS_BANK2_PAD,
                            &s->bank2_len);
    /* The smoke (0x109cb): RPG.LCP 0x1a4cc, 0x1eb bytes. */
    if (rp_len > 0x1A4CC)
        s->puff = decode_block(rp + 0x1A4CC, rp_len - 0x1A4CC < 0x1EB ? rp_len - 0x1A4CC : 0x1EB, 0x500, &s->puff_len);
    /* The lit oval of the dark scenes (0x108cd, read in every scene): RPG.LCP 0x4350, 0x15a4 bytes. */
    if (rp_len > 0x4350)
        s->dark_mask = decode_block(rp + 0x4350, rp_len - 0x4350 < 0x15A4 ? rp_len - 0x4350 : 0x15A4, 0, &s->dark_mask_len);
    if (!s->dark_mask || s->dark_mask_len < WS_DARK_MASK_BYTES) {
        fprintf(stderr, "walk: the dark scenes' lit oval (RPG.LCP 0x4350) does not decode\n");
        goto fail;
    }
    if (!s->bank1 || !s->bank2 || !s->puff) {
        fprintf(stderr, "walk: a sprite bank does not decode\n");
        goto fail;
    }

    /* The song's place in MUC.ALL: RPG.LCP 0x7add + 5 * header byte 2. */
    {
        size_t at = 0x7ADD + 5 * (size_t)s->header[2];
        s->song_pos = ((size_t)rp[at + 2] << 16) + w16(rp, at);
        s->song_len = w16(rp, at + 3);
    }
    s->speed_class = rp[0];
    /* 0x10aaf: NUMBER.LCZ 0x359b, 0x585 bytes as they are, into [bp-0x28] (the map segment's 0xc500 ..); only the view past the
     * map's bottom reads it here */
    if (assets_read_at("NUMBER.LCZ", 0x359B, s->number_tail, sizeof s->number_tail) != 0)
        memset(s->number_tail, 0, sizeof s->number_tail);

    /* The party starts at (byte 512, 2 * byte 513); so does a trail of 13 entries. */
    unsigned x = tb[512], y = (tb[513] << 1) & 0xFF;
    s->start_x = (int)x;
    s->start_y = (int)y;
    for (unsigned di = 0; di < 0xC8; di += 0x28) {
        tb[di] = (uint8_t)x;
        tb[di + 1] = (uint8_t)y;
        tb[di + 2] = 1;
    }
    for (int k = 0; k < WS_TRAIL_BYTES; k += 3) {
        s->trail[k] = (uint8_t)x;
        s->trail[k + 1] = (uint8_t)y;
        s->trail[k + 2] = 1;
    }

    /* On a slow machine (speed class below 2) the villagers walk more slowly (0x11358). */
    if (s->speed_class < 2) {
        unsigned k = 2u - (unsigned)s->speed_class;
        for (int i = 0; i < WS_VILLAGERS; i++) {
            uint8_t *v = ta + WS_VILLAGER_SIZE * i;
            v[8] = v[8] > k ? (uint8_t)(v[8] - k) : 1;
            v[9] = 0;
        }
    }

    if (walk_scene_apply_records(s) != 0)
        goto fail;

    free(mi);
    free(now);
    free(tab);
    free(rp);
    free(raw);
    free(map);
    free(gt);
    free(m1);
    return s;

fail:
    free(mi);
    free(now);
    free(tab);
    free(rp);
    free(raw);
    free(map);
    free(gt);
    free(m1);
    walk_scene_free(s);
    return NULL;
}

walk_scene_t *walk_scene_load(void)
{
    return scene_load(NULL);
}

walk_scene_t *walk_scene_load_party(const uint8_t *party)
{
    return scene_load(party);
}

int walk_scene_dump(const walk_scene_t *s, const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    int ok = fwrite(s->gmap, 1, sizeof s->gmap, f) == sizeof s->gmap &&
             fwrite(s->omap, 1, sizeof s->omap, f) == sizeof s->omap &&
             fwrite(s->tab_a, 1, sizeof s->tab_a, f) == sizeof s->tab_a &&
             fwrite(s->tab_b, 1, sizeof s->tab_b, f) == sizeof s->tab_b &&
             fwrite(s->list, 1, sizeof s->list, f) == sizeof s->list &&
             fwrite(s->trail, 1, sizeof s->trail, f) == sizeof s->trail &&
             fwrite(s->tiles, 1, s->tiles_len, f) == s->tiles_len &&
             fwrite(s->sprites, 1, s->sprites_len, f) == s->sprites_len &&
             fwrite(s->bank1, 1, s->bank1_len, f) == s->bank1_len &&
             fwrite(s->bank2, 1, s->bank2_len, f) == s->bank2_len;
    if (ok)
        fprintf(f, "scene %d events %d speed %d start %d %d\n", s->scene, s->events, s->speed_class, s->start_x,
                s->start_y);
    return (fclose(f) == 0 && ok) ? 0 : -1;
}
