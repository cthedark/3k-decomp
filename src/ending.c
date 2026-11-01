#include "ending.h"

#include <stdlib.h>
#include <string.h>

#include "assets.h"

enum { BUF = 0xFFDC };

/* The buffers of main, by the stack places that hold them. The file buffer [bp-0x12] (each .LCZ read whole into it, 0xffdc bytes at
 * most) is also the frame the scene is built in before it is copied to the screen. */
struct ending {
    /* main's seven far buffers of 0xffdc bytes, by their stack places (the eighth, 0x5420 bytes, holds the palettes and the song) */
    uint8_t file[BUF];                /* [bp-0x12]: each .LCZ read whole; also the frame the first scene is built in */
    uint8_t b22[BUF];                 /* [bp-0x22]: ENDING12.LCZ's clouds (320 x 200, header), then ENDING2.LCZ's background */
    uint8_t be[BUF];                  /* [bp-0xe]: ENDING1.LCZ's five 41 x 44 pictures of her face, with headers */
    uint8_t b36[BUF], b46[BUF];       /* ENDING1.LCZ's two 320 x 147 pictures of her (no header); then ENDING2.LCZ's five pictures
                                       * and the second scene's work picture */
    uint8_t b1e[BUF];                 /* [bp-0x1e]: ENDING1T.LCZ, four 236 x 15 subtitle lines */
    uint8_t b32[BUF];                 /* [bp-0x32]: _1_.LOC, the voice (0xee48 bytes read) */
    uint8_t pal[0x600];               /* [bp-0x18]: ANI.LCP's two palettes; the first is the one in the DAC */
    int loop;
    /* the first scene's locals */
    unsigned face;                    /* [bp-0x14]: the face picture's offset, 0x710 a picture */
    unsigned rounds;                  /* [bp-4]: how many times the face pictures went round for this line */
    unsigned sub;                     /* [bp-0x3a]: the subtitle's offset, 0xdd4 a line */
    unsigned wait;                    /* di: the passes before she rises */
    unsigned count, at;               /* [bp-0x24], [bp-0x28]: the bytes of her picture shown and where they start in the frame */
    unsigned swap, which;             /* [bp-0x48], [bp-0x1a]: the pass counter of her two pictures, which one is shown */
    unsigned risen;                   /* [bp-0x2a] */
    unsigned talk;                    /* [bp-0x3e]: the passes since the face last changed */
    /* the second scene's: [bp-0x14] (face) the picture's offset, [bp-0x1a] (which) set once its size is read, di (wait), [bp-0x24]
     * and [bp-0x28] (count, at) its width and height, [bp-0x48] (swap) where it goes on the screen */
    void (*voice)(void *ctx, unsigned off);   /* ending_set_voice */
    void (*cut)(void *ctx);
    int (*busy)(void *ctx);
    void *vctx;
};

/* 0x115fd: the .LCZ run-length code (byte 0 the escape, word 1 the end; escape, count, byte for a run) */
static size_t unpack(const uint8_t *in, size_t n, uint8_t *out, size_t cap)
{
    if (n < 3)
        return 0;
    size_t end = (size_t)in[1] | (size_t)in[2] << 8, si = 3, o = 0;
    if (end > n)
        end = n;
    while (si < end) {
        if (in[si] != in[0]) {
            if (o < cap)
                out[o] = in[si];
            o++, si++;
            continue;
        }
        if (si + 2 >= n)
            break;
        for (unsigned k = 0; k < in[si + 1]; k++, o++)
            if (o < cap)
                out[o] = in[si + 2];
        si += 3;
    }
    return o;
}

/* fopen, fread up to 0xffdc bytes into the file buffer, fclose; the length read */
static size_t read_file(ending_t *e, const char *name)
{
    size_t n = 0;
    uint8_t *b = assets_read(name, &n);
    if (!b)
        return 0;
    if (n > sizeof e->file)
        n = sizeof e->file;
    memcpy(e->file, b, n);
    free(b);
    return n;
}

ending_t *ending_new(screen_t *scr)
{
    ending_t *e = calloc(1, sizeof *e);
    if (!e)
        return NULL;
    if (assets_read_at("ANI.LCP", 0, e->pal, 0x300) != 0 || assets_read_at("ANI.LCP", 0x300, e->pal + 0x300, 0x300) != 0)
        goto fail;
    memset(scr->pix, 0, sizeof scr->pix);   /* 0x204:0x163(0x13): the mode set */
    screen_set_palette(scr, e->pal, 0, 256);   /* 0x22c:0x14e */
    size_t n = read_file(e, "ENDING12.LCZ");
    if (!n)
        goto fail;
    unpack(e->file, n, e->b22, sizeof e->b22);
    if (!(n = read_file(e, "ENDING1.LCZ")) || n < 0x40D1)
        goto fail;
    unpack(e->file, n, e->be, sizeof e->be);
    unpack(e->file + 0x1925, n - 0x1925, e->b36, sizeof e->b36);
    unpack(e->file + 0x40D1, n - 0x40D1, e->b46, sizeof e->b46);
    if (!(n = read_file(e, "ENDING1T.LCZ")))
        goto fail;
    unpack(e->file, n, e->b1e, sizeof e->b1e);
    e->count = 0x140;
    e->at = 0xFA00;
    e->swap = 0xA;    /* [bp-0x48] is the last file handle (ENDING1T.LCZ's), 0xa in the original (SEEN) */
    e->talk = 0;      /* [bp-0x3e] is never set first: 0 (SEEN) */
    e->loop = ENDING_SCENE1;
    return e;
fail:
    free(e);
    return NULL;
}

void ending_free(ending_t *e)
{
    free(e);
}

/* 0x1252d mode 1: each row of a picture with its header turned a pixel to the left (the first goes to the row's end) */
static void turn_rows(uint8_t *pic)
{
    unsigned w = pic[0] | pic[1] << 8, h = pic[2] | pic[3] << 8;
    for (unsigned r = 0; r < h; r++) {
        uint8_t *p = pic + 4 + (size_t)r * w, a = p[0];
        memmove(p, p + 1, w - 1);
        p[w - 1] = a;
    }
}

/* 0x1252d mode 2: the same, two pixels (the first two go to the row's end) */
static void turn_rows2(uint8_t *pic)
{
    unsigned w = pic[0] | pic[1] << 8, h = pic[2] | pic[3] << 8;
    for (unsigned r = 0; r < h; r++) {
        uint8_t *p = pic + 4 + (size_t)r * w, a = p[0], b = p[1];
        memmove(p, p + 2, w - 2);
        p[w - 2] = a;
        p[w - 1] = b;
    }
}

/* 0x11e7c: a picture with its header, opaque, at `at` in a picture `stride` wide */
static void put_hdr(uint8_t *dst, size_t cap, unsigned at, unsigned stride, const uint8_t *pic)
{
    unsigned w = pic[0] | pic[1] << 8, h = pic[2] | pic[3] << 8;
    for (unsigned r = 0; r < h; r++)
        for (unsigned c = 0; c < w; c++) {
            size_t o = (size_t)at + (size_t)r * stride + c;
            if (o < cap)
                dst[o] = pic[4 + (size_t)r * w + c];
        }
}

/* 0x11ff8: a w x h picture with no header, colour 0 clear, at `at` in a picture `stride` wide */
static void put_clear(uint8_t *dst, size_t cap, unsigned at, unsigned stride, const uint8_t *pic, unsigned w, unsigned h)
{
    for (unsigned r = 0; r < h; r++)
        for (unsigned c = 0; c < w; c++) {
            size_t o = (size_t)at + (size_t)r * stride + c;
            uint8_t v = pic[(size_t)r * w + c];
            if (v && o < cap)
                dst[o] = v;
        }
}

/* 0x1042d .. 0x10600, a pass of the first scene. The clouds turn a pixel; her face picture goes into both pictures of her at
 * (143, 9); the clouds into the frame; every fifth pass her other picture; the first `count` bytes of it (colour 0 clear, from its
 * first byte) at `at`, so she comes up from the bottom a row a pass after the first 51 passes, until all 147 rows show; then her face
 * changes every sixth pass, round its four pictures twice for each of the four subtitle lines (236 x 15 at (42, 184)); the frame is
 * shown. After the fourth line the loop ends before the frame is shown. */
static int scene1(ending_t *e, screen_t *scr)
{
    turn_rows(e->b22);
    put_hdr(e->b36, sizeof e->b36, 0xBCF, 0x140, e->be + e->face);
    put_hdr(e->b46, sizeof e->b46, 0xBCF, 0x140, e->be + e->face);
    /* 0x19c:0x102: with a Sound Blaster a sound that ended is started again (not built: no sound card) */
    memcpy(e->file, e->b22 + 4, 0xFA00);
    if (++e->swap > 4) {
        e->swap = 0;
        e->which = !e->which;
    }
    const uint8_t *b = e->which ? e->b36 : e->b46;
    for (unsigned i = 0; i < e->count; i++) {   /* 0x12146 */
        size_t o = (size_t)e->at + i;
        if (b[i] && o < sizeof e->file)
            e->file[o] = b[i];
    }
    if (!e->risen && ++e->wait > 0x32) {
        e->count += 0x140;
        e->at -= 0x140;
        if (e->count > 0xB7C0) {
            e->count -= 0x140;
            e->at += 0x140;
            e->risen = 1;
        }
    }
    if (e->risen) {
        if (++e->talk > 5) {
            e->talk = 0;
            e->face += 0x710;
            if (e->face == 0x1C40) {
                e->face = 0;
                if (++e->rounds > 1) {
                    e->rounds = 0;
                    e->sub += 0xDD4;
                    if (e->sub == 0x3750)
                        return ENDING_SCENE1_END;
                }
            }
        }
        put_clear(e->file, sizeof e->file, 0xE62A, 0x140, e->b1e + e->sub, 0xEC, 0xF);
    }
    memcpy(scr->pix, e->file, 0xFA00);
    return ENDING_SCENE1;
}

/* 0x10603 .. 0x106ce: ENDING2.LCZ unpacked, at 0 the background (142 x 138, header) over the clouds and at 0x46c five pictures with
 * headers (0x0 90 x 138, 0x3088 76 x 79, 0x4800 101 x 92, 0x6c50 142 x 64, 0x8fd4 97 x 87); _1_.LOC read (0xee48 bytes at most); the
 * screen cleared (0x22c:0xd8); the first picture to go at 0x286c, (108, 32). */
static int scene2_begin(ending_t *e, screen_t *scr)
{
    size_t n = read_file(e, "ENDING2.LCZ");
    if (n < 0x46C)
        return 0;
    unpack(e->file, n, e->b22, BUF);
    unpack(e->file + 0x46C, n - 0x46C, e->b36, BUF);
    size_t v = 0;
    uint8_t *loc = assets_read("_1_.LOC", &v);
    if (loc) {
        memcpy(e->b32, loc, v < 0xEE48 ? v : 0xEE48);
        free(loc);
    }
    memset(scr->pix, 0, sizeof scr->pix);
    e->face = 0;
    e->which = 0;
    e->swap = 0x286C;
    return ENDING_SCENE2;
}

static unsigned w16(const uint8_t *b, size_t o)
{
    return b[o] | b[o + 1] << 8;
}

/* 0x106d1 .. 0x108cd, a pass of the second scene. The background's rows turn two pixels (0x1252d mode 2). A new picture: its width and
 * height read, the pass count (di) 0, and at the picture's start its voice line (0x11400: with a Sound Blaster, the sound cut and the
 * block at _1_.LOC 0x6771, 0x7742, 0x8849, 0x8e01 or 0xa1bb played). Each pass: the background's top-left w x h grabbed (0x1222d, 142
 * wide) into the work picture, the picture over it (colour 0 clear, 0x11ec6), the work picture on the screen at its place (0x122c8);
 * after the 100th pass the sound cut and the next picture (to 0x76cb, (11, 95); 0x1732, (178, 18); 0x8e00, (192, 113)); after the fifth the
 * loop ends. The pictures stay on the screen, so the five build up a collage. */
static int scene2(ending_t *e, screen_t *scr)
{
    static const struct { unsigned face, voice; } line[5] = {{0, 0x6771}, {0x3088, 0x7742}, {0x4800, 0x8849}, {0x6C50, 0x8E01},
                                                              {0x8FD4, 0xA1BB}};
    turn_rows2(e->b22);
    if (!e->which) {
        e->count = w16(e->b36, e->face);
        e->at = w16(e->b36, e->face + 2);
        e->which = 1;
        e->wait = 0;
        for (int k = 0; k < 5; k++)
            if (e->face == line[k].face && e->voice)
                e->voice(e->vctx, line[k].voice);
    }
    uint8_t *g = e->b46;   /* 0x1222d: the grab with its header */
    g[0] = (uint8_t)e->count, g[1] = (uint8_t)(e->count >> 8), g[2] = (uint8_t)e->at, g[3] = (uint8_t)(e->at >> 8);
    for (unsigned r = 0; r < e->at; r++)
        for (unsigned c = 0; c < e->count; c++) {
            size_t s = 4 + (size_t)r * 0x8E + c, d = 4 + (size_t)r * e->count + c;
            if (s < BUF && d < BUF)
                g[d] = e->b22[s];
        }
    const uint8_t *f = e->b36 + e->face;   /* 0x11ec6 at 4 in the grab, its width the stride */
    unsigned fw = w16(f, 0), fh = w16(f, 2);
    for (unsigned r = 0; r < fh; r++)
        for (unsigned c = 0; c < fw; c++) {
            size_t d = 4 + (size_t)r * e->count + c;
            uint8_t v = f[4 + (size_t)r * fw + c];
            if (v && d < BUF)
                g[d] = v;
        }
    for (unsigned r = 0; r < e->at; r++)   /* 0x122c8 */
        for (unsigned c = 0; c < e->count; c++) {
            size_t o = (size_t)e->swap + (size_t)r * 320 + c;
            if (o < sizeof scr->pix)
                scr->pix[o] = g[4 + (size_t)r * e->count + c];
        }
    if (++e->wait <= 0x63)
        return ENDING_SCENE2;
    if (e->cut)
        e->cut(e->vctx);
    if (e->face == 0x8FD4)
        return ENDING_SCENE2_END;
    if (e->face == 0x6C50)
        e->face = 0x8FD4, e->swap = 0x8E00;
    if (e->face == 0x4800)
        e->face = 0x6C50, e->swap = 0x1732;
    if (e->face == 0x3088)
        e->face = 0x4800, e->swap = 0x76CB;
    if (e->face == 0)
        e->face = 0x3088, e->swap = 0x2A;
    e->which = 0;
    return ENDING_SCENE2;
}

/* 0x108d0 .. 0x10965: ENDING3.LCZ unpacked, at 0 two 151 x 192 pictures of her with headers (0 and 0x7144) into [bp-0x22], at 0x3e6b
 * the red sky and at 0x4a52 the sparks, both 320 x 200 with headers, into [bp-0x36] and [bp-0x46]; her place [bp-0x48] 0xa46, (70, 8);
 * the picture shown [bp-0x14] 0, the pass counter [bp-0x1a] 0. */
static int scene3_begin(ending_t *e)
{
    size_t n = read_file(e, "ENDING3.LCZ");
    if (n < 0x4A52)
        return 0;
    unpack(e->file, n, e->b22, BUF);
    unpack(e->file + 0x3E6B, n - 0x3E6B, e->b36, BUF);
    unpack(e->file + 0x4A52, n - 0x4A52, e->b46, BUF);
    e->swap = 0xA46;
    e->face = 0;
    e->which = 0;
    return ENDING_SCENE3;
}

/* A layer drifts (0x1252d mode 1, 0x11917): its rows turned a pixel left, then the whole moved up a row with the first row put last */
static void drift(ending_t *e, uint8_t *pic)
{
    turn_rows(pic);
    memcpy(e->file, pic + 4, 0x140);
    memmove(pic + 4, pic + 4 + 0x140, 0xFA00 - 0x140);   /* 0x11917(pic, 1): w x (h - 1) from row 1 */
    memcpy(pic + 0xF8C4, e->file, 0x140);
}

/* 0x10968 .. 0x10b69, a pass of the third scene. With a Sound Blaster and no sound playing, _1_.LOC 0x37a3 (the sound cut first). The
 * red sky drifts every second pass ([bp-0x3e], left from the first scene), the sparks every pass; the sky into the frame; her picture
 * (the two by turns, colour 0 clear) at her place; the sparks over all (colour 0 clear); she goes a row down and both pictures lose
 * their last row, so she sinks; past 0x88b8 the loop ends before the frame is shown; every seventh pass she moves a pixel right; the
 * frame shown. At its end (0x10b6c) the sound is cut and the screen cleared. */
static int scene3(ending_t *e, screen_t *scr)
{
    if (e->busy && !e->busy(e->vctx))
        e->voice(e->vctx, 0x37A3);
    if (e->talk) {
        drift(e, e->b36);
        e->talk = 0;
    } else {
        e->talk = 1;
    }
    drift(e, e->b46);
    memcpy(e->file, e->b36 + 4, 0xFA00);
    const uint8_t *f = e->b22 + e->face;   /* 0x11ec6 */
    unsigned fw = w16(f, 0), fh = w16(f, 2);
    for (unsigned r = 0; r < fh; r++)
        for (unsigned c = 0; c < fw; c++) {
            size_t o = (size_t)e->swap + (size_t)r * 0x140 + c;
            uint8_t v = f[4 + (size_t)r * fw + c];
            if (v && o < BUF)
                e->file[o] = v;
        }
    e->face = e->face ? 0 : 0x7144;
    for (unsigned i = 0; i < 0xFA00; i++)   /* 0x12146 */
        if (e->b46[4 + i])
            e->file[i] = e->b46[4 + i];
    e->swap += 0x140;
    uint8_t h = (uint8_t)(e->b22[2] - 1);   /* [bp-0x2a]: the first picture's height less one, for both */
    e->b22[0x7146] = h;
    e->b22[2] = h;
    if (e->swap > 0x88B8) {
        if (e->cut)
            e->cut(e->vctx);
        memset(scr->pix, 0, sizeof scr->pix);
        return ENDING_SCENE3_END;
    }
    if (++e->which > 6) {
        e->which = 0;
        e->swap++;
    }
    memcpy(scr->pix, e->file, 0xFA00);
    return ENDING_SCENE3;
}

/* 0x10ba1 .. 0x10ccb: ENDING4.LCZ read in seven blocks one after another (0xa5ec, 0x8788, 0x899c, 0x8f1a, 0x913a, 0x8e56, 0x853e
 * bytes): the first unpacked straight onto the screen from row 1 (320 x 198), the second unpacked into [bp-0x22], the other five kept
 * packed in [bp-0x36], [bp-0x46], [bp-0xe], [bp-0x1e] and [bp-0x32] (over the voice); the screen copied into the frame. Each block is
 * a 320 x 165 picture without a header but the first. */
static int scene4_begin(ending_t *e, screen_t *scr)
{
    static const unsigned len[7] = {0xA5EC, 0x8788, 0x899C, 0x8F1A, 0x913A, 0x8E56, 0x853E};
    uint8_t *keep[5] = {e->b36, e->b46, e->be, e->b1e, e->b32};
    size_t n = 0, at = 0;
    uint8_t *all = assets_read("ENDING4.LCZ", &n);
    if (!all)
        return 0;
    for (int k = 0; k < 7; k++) {
        size_t m = at < n ? (n - at < len[k] ? n - at : len[k]) : 0;
        uint8_t *dst = k == 0 || k == 1 ? e->file : keep[k - 2];
        memcpy(dst, all + at, m);
        at += m;
        if (k == 0) {
            static uint8_t pic[0x10000];
            size_t o = unpack(e->file, m, pic, sizeof pic);
            for (size_t i = 0; i < o && 0x140 + i < sizeof scr->pix; i++)
                scr->pix[0x140 + i] = pic[i];
        }
        if (k == 1)
            unpack(e->file, m, e->b22, BUF);
    }
    free(all);
    memcpy(e->file, scr->pix, 0xFA00);
    e->which = 0;
    e->wait = 0;
    return ENDING_SCENE4_SHAKE;
}

/* 0x10cd1 .. 0x10d27, 80 passes: the frame on the screen at row 0 and at row 1 by turns (the copy one row down runs 320 bytes past the
 * screen's end, which the port drops), so the picture shakes. */
static int scene4_shake(ending_t *e, screen_t *scr)
{
    if (e->which) {
        memcpy(scr->pix, e->file, 0xFA00);
        e->which = 0;
    } else {
        memcpy(scr->pix + 0x140, e->file, 0xFA00 - 0x140);
        e->which = 1;
    }
    return ++e->wait < 0x50 ? ENDING_SCENE4_SHAKE : ENDING_SCENE4_SHAKE_END;
}

/* 0x10d2a .. 0x10d55: the screen's rows 20 .. 184 into the frame; the pass count [bp-0x14] 0, the next block [bp-0x26] 1, [bp-0x1a] 1 */
static int scene4_strobe_begin(ending_t *e, screen_t *scr)
{
    memcpy(e->file, scr->pix + 0x1900, 0xCE40);
    e->face = 0;
    e->rounds = 1;
    e->which = 1;
    return ENDING_SCENE4_STROBE;
}

/* 0x10d58 .. 0x10e9d, a pass: the song kept going; for 49 passes the frame (the picture before) and [bp-0x22] (the next) by turns on the
 * screen's rows 20 .. 184, so the one flickers into the other; on the 50th the next becomes the frame and the next block is unpacked
 * into [bp-0x22]; after the sixth picture the loop ends. */
static int scene4_strobe(ending_t *e, screen_t *scr)
{
    uint8_t *keep[5] = {e->b36, e->b46, e->be, e->b1e, e->b32};
    if (++e->face < 0x32) {
        if (e->which) {
            memcpy(scr->pix + 0x1900, e->file, 0xCE40);
            e->which = 0;
        } else {
            memcpy(scr->pix + 0x1900, e->b22, 0xCE40);
            e->which = 1;
        }
        return ENDING_SCENE4_STROBE;
    }
    e->face = 0;
    memcpy(e->file, e->b22, 0xCE40);
    if (e->rounds >= 1 && e->rounds <= 5)
        unpack(keep[e->rounds - 1], BUF, e->b22, BUF);
    if (e->rounds == 6)
        return ENDING_SCENE4_END;
    e->rounds++;
    return ENDING_SCENE4_STROBE;
}

/* 0x122c8 for a picture with its header, opaque, on the screen at `at` */
static void show_hdr(screen_t *scr, unsigned at, const uint8_t *pic)
{
    unsigned w = w16(pic, 0), h = w16(pic, 2);
    for (unsigned r = 0; r < h; r++)
        for (unsigned c = 0; c < w; c++) {
            size_t o = (size_t)at + (size_t)r * 320 + c;
            if (o < sizeof scr->pix)
                scr->pix[o] = pic[4 + (size_t)r * w + c];
        }
}

/* 0x11462: a w x h picture with no header, opaque, on the screen at `at` */
static void show_raw(screen_t *scr, unsigned at, const uint8_t *pic, unsigned w, unsigned h)
{
    for (unsigned r = 0; r < h; r++)
        for (unsigned c = 0; c < w; c++) {
            size_t o = (size_t)at + (size_t)r * 320 + c;
            if (o < sizeof scr->pix)
                scr->pix[o] = pic[(size_t)r * w + c];
        }
}

/* The subtitle line `k` of ENDING4T.LCZ (220 x 15, colour 0 clear) over the kept grab of the screen's (50, 184), shown there */
static void scene5_subtitle(ending_t *e, screen_t *scr, unsigned k)
{
    memcpy(e->be, e->file, 0xCE8);
    for (unsigned i = 0; i < 0xCE4; i++)   /* 0x12146 */
        if (e->b46[k * 0xCE4 + i])
            e->be[4 + i] = e->b46[k * 0xCE4 + i];
    show_hdr(scr, 0xE632, e->be);
}

/* 0x10ea0 .. 0x10fd3: ENDING4.LCZ again from 0x3eaf8: at 0 three 104 x 94 pictures of her face (no header, 0x2630 apart) into
 * [bp-0x22], at 0x5bb1 eight 32 x 26 pictures of her eyes (0x340 apart) into [bp-0x36]; ENDING4T.LCZ, two 220 x 15 subtitle lines,
 * into [bp-0x46]; the screen's 220 x 15 at (50, 184) grabbed with its header into the frame (0x121dc) and the first line shown over it. */
static int scene5_begin(ending_t *e, screen_t *scr)
{
    size_t n = 0;
    uint8_t *all = assets_read("ENDING4.LCZ", &n);
    if (!all || n <= 0x3EAF8) {
        free(all);
        return 0;
    }
    size_t m = n - 0x3EAF8 < BUF ? n - 0x3EAF8 : BUF;
    memcpy(e->file, all + 0x3EAF8, m);
    free(all);
    unpack(e->file, m, e->b22, BUF);
    if (m > 0x5BB1)
        unpack(e->file + 0x5BB1, m - 0x5BB1, e->b36, BUF);
    if (!(n = read_file(e, "ENDING4T.LCZ")))
        return 0;
    unpack(e->file, n, e->b46, BUF);
    e->wait = 0;
    e->face = 0;
    uint8_t *g = e->file;   /* 0x121dc */
    g[0] = 0xDC, g[1] = 0, g[2] = 0xF, g[3] = 0;
    for (unsigned r = 0; r < 0xF; r++)
        memcpy(g + 4 + r * 0xDC, scr->pix + 0xE632 + r * 320, 0xDC);
    scene5_subtitle(e, scr, 0);
    return ENDING_SCENE5_FACE;
}

/* 0x10fd6 .. 0x11017: her face's three pictures in turn at (105, 16), each for 81 passes; then the second subtitle line, and the eyes */
static int scene5_face(ending_t *e, screen_t *scr)
{
    show_raw(scr, 0x1469, e->b22 + e->face, 0x68, 0x5E);
    if (++e->wait > 0x50) {
        e->wait = 0;
        e->face += 0x2630;
        if (e->face == 0x7290)
            return ENDING_SCENE5_FACE_END;
    }
    return ENDING_SCENE5_FACE;
}

static int scene5_eyes_begin(ending_t *e, screen_t *scr)
{
    scene5_subtitle(e, scr, 1);
    e->rounds = 0;   /* di */
    e->wait = 0;
    e->face = 0;
    return ENDING_SCENE5_EYES;
}

/* 0x11078 .. 0x110c5: her eyes' eight pictures in turn at (144, 37), each for 701 passes, twice round; then the loop ends */
static int scene5_eyes(ending_t *e, screen_t *scr)
{
    show_raw(scr, 0x2ED0, e->b36 + e->face, 0x20, 0x1A);
    if (++e->wait > 0x2BC) {
        e->wait = 0;
        e->face += 0x340;
        if (e->face == 0x1A00) {
            e->face = 0;
            if (++e->rounds >= 2)
                return ENDING_SCENE5_END;
        }
    }
    return ENDING_SCENE5_EYES;
}

/* 0x110c8 .. 0x11126: ENDING5.LCZ, the sunset (320 x 200, no header), unpacked into [bp-0x22]; [bp-0x24] 0 and [bp-0x28] 0xf8c0 */
static int scene6_begin(ending_t *e)
{
    size_t n = read_file(e, "ENDING5.LCZ");
    if (!n)
        return 0;
    unpack(e->file, n, e->b22, BUF);
    e->count = 0;
    e->at = 0xF8C0;
    e->wait = 0;
    return ENDING_SCENE6_WIPE;
}

/* 0x11129 .. 0x11196, 100 steps: a row of the sunset put on the screen at [bp-0x24] and one at [bp-0x28] (99 times over, a delay), then
 * the first two rows down and the second two rows up; so the even rows come in from the top and the bottom both, each twice, and the
 * odd rows keep the woman in blue. */
static int scene6_wipe(ending_t *e, screen_t *scr)
{
    memcpy(scr->pix + e->count, e->b22 + e->count, 0x140);
    memcpy(scr->pix + e->at, e->b22 + e->at, 0x140);
    e->count += 0x280;
    e->at -= 0x280;
    if (++e->wait < 0x64)
        return ENDING_SCENE6_WIPE;
    e->wait = 0;
    return ENDING_SCENE6_FADE;
}

/* 0x1119f .. 0x11215, 64 steps: each byte of the palette one nearer ANI.LCP's second palette (0x12610), the DAC set from it, two delay
 * loops of 50,000 rounds */
static int scene6_fade(ending_t *e, screen_t *scr)
{
    for (unsigned i = 0; i < 0x300; i++) {
        if (e->pal[i] > e->pal[0x300 + i])
            e->pal[i]--;
        else if (e->pal[i] < e->pal[0x300 + i])
            e->pal[i]++;
    }
    screen_set_palette(scr, e->pal, 0, 256);
    return ++e->wait < 0x40 ? ENDING_SCENE6_FADE : ENDING_SCENE6_END;
}

/* 0x11218 .. 0x112be: CAST2.LCZ, the credits as a 1-bit picture 120 wide (15 bytes a row, 0x6144 bytes: 1,660 rows), unpacked into
 * [bp-0x1e] after it is cleared (0xffdc bytes); the screen's 120 x 200 at (2, 0) grabbed with its header into the frame buffer
 * (0x121dc); [bp-0x22] cleared (0x5dc4 bytes) and given the grab's header: the credits' own 120 x 200 picture; [bp-0x14] 0. */
static int scene7_begin(ending_t *e, screen_t *scr)
{
    size_t n = read_file(e, "CAST2.LCZ");
    if (!n)
        return 0;
    memset(e->b1e, 0, BUF);
    unpack(e->file, n, e->b1e, BUF);
    uint8_t *g = e->file;
    g[0] = 0x78, g[1] = 0, g[2] = 0xC8, g[3] = 0;
    for (unsigned r = 0; r < 0xC8; r++)
        memcpy(g + 4 + r * 0x78, scr->pix + 2 + r * 320, 0x78);
    memset(e->b22, 0, 0x5DC4);
    memcpy(e->b22, e->file, 4);
    e->face = 0;
    return ENDING_SCENE7;
}

/* 0x112c1 .. 0x11381, a pass of the credits: their picture moves up a row (0x11917) and the next row of the credits comes in at the
 * bottom (0x114a8: 15 bytes, the high bit first, set bits colour 0xf0, the others 0); the grab copied into [bp-0x36]; the credits put
 * on it as colour 0 at (4, 1) and (5, 1) (0x11f05, a shadow) and as they are at (4, 0) (0x11ec6, colour 0 clear); that shown at (2, 0).
 * After 1,861 passes (the rows past the credits' end are the cleared buffer's zeros, so they scroll away) the loop ends. */
static int scene7(ending_t *e, screen_t *scr)
{
    uint8_t *c = e->b22;
    memmove(c + 4, c + 4 + 0x78, 0x78 * 199);
    for (unsigned b = 0; b < 0xF; b++)
        for (unsigned k = 0; k < 8; k++)
            c[0x5D4C + b * 8 + k] = (e->b1e[e->face + b] << k) & 0x80 ? 0xF0 : 0;
    memcpy(e->b36, e->file, 0x5DC4);
    static const unsigned shadow[2] = {0x7C, 0x7D};
    for (int s = 0; s < 3; s++) {
        unsigned at = s < 2 ? shadow[s] : 4;
        for (unsigned r = 0; r < 0xC8; r++)
            for (unsigned x = 0; x < 0x78; x++) {
                uint8_t v = c[4 + r * 0x78 + x];
                size_t o = (size_t)at + r * 0x78 + x;
                if (v && o < BUF)
                    e->b36[o] = s < 2 ? 0 : v;
            }
    }
    e->face += 0xF;
    show_hdr(scr, 2, e->b36);
    return e->face > 0x6CFC ? ENDING_SCENE7_END : ENDING_SCENE7;
}

void ending_text_end(screen_t *scr)
{
    /* data segment 0xf4, 0x116, 0x138: 0xdb a full cell, 0xdf its upper half, 0xdc its lower half */
    static const char *const line[3] = {
        "\xdf\xdf\xdb\xdf\xdf \xdb  \xdb \xdb\xdf\xdf\xdf    \xdb\xdf\xdf\xdf \xdb\xdc \xdb \xdb\xdf\xdf\xdc",
        "  \xdb   \xdb\xdf\xdf\xdb \xdb\xdf\xdf     \xdb\xdf\xdf  \xdb \xdb\xdb \xdb  \xdb",
        "  \xdf   \xdf  \xdf \xdf\xdf\xdf\xdf    \xdf\xdf\xdf\xdf \xdf  \xdf \xdf\xdf\xdf "};
    static const uint8_t colour[3] = {0xB, 9, 1};
    static const uint8_t cga[16][3] = {{0, 0, 0}, {0, 0, 42}, {0, 42, 0}, {0, 42, 42}, {42, 0, 0}, {42, 0, 42}, {42, 21, 0},
                                       {42, 42, 42}, {21, 21, 21}, {21, 21, 63}, {21, 63, 21}, {21, 63, 63}, {63, 21, 21},
                                       {63, 21, 63}, {63, 63, 21}, {63, 63, 63}};
    memset(scr->pix, 0, sizeof scr->pix);
    screen_set_palette(scr, &cga[0][0], 0, 16);
    for (int l = 0; l < 3; l++)
        for (int i = 0; line[l][i]; i++) {
            uint8_t ch = (uint8_t)line[l][i];
            unsigned x0 = (20u + (unsigned)i) * 4, y0 = (4u + (unsigned)l) * 8;
            for (unsigned y = 0; y < 8; y++) {
                int on = ch == 0xDB || (ch == 0xDF && y < 4) || (ch == 0xDC && y >= 4);
                if (on)
                    memset(scr->pix + (y0 + y) * 320 + x0, colour[l], 4);
            }
        }
}

void ending_set_voice(ending_t *e, void (*play)(void *ctx, unsigned off), void (*cut)(void *ctx), int (*busy)(void *ctx), void *ctx)
{
    e->voice = play;
    e->cut = cut;
    e->busy = busy;
    e->vctx = ctx;
}

int ending_step(ending_t *e, screen_t *scr)
{
    switch (e->loop) {
    case ENDING_SCENE1: e->loop = scene1(e, scr); break;
    case ENDING_SCENE1_END: e->loop = scene2_begin(e, scr); break;
    case ENDING_SCENE2: e->loop = scene2(e, scr); break;
    case ENDING_SCENE2_END: e->loop = scene3_begin(e); break;
    case ENDING_SCENE3: e->loop = scene3(e, scr); break;
    case ENDING_SCENE3_END: e->loop = scene4_begin(e, scr); break;
    case ENDING_SCENE4_SHAKE: e->loop = scene4_shake(e, scr); break;
    case ENDING_SCENE4_SHAKE_END: e->loop = scene4_strobe_begin(e, scr); break;
    case ENDING_SCENE4_STROBE: e->loop = scene4_strobe(e, scr); break;
    case ENDING_SCENE4_END: e->loop = scene5_begin(e, scr); break;
    case ENDING_SCENE5_FACE: e->loop = scene5_face(e, scr); break;
    case ENDING_SCENE5_FACE_END: e->loop = scene5_eyes_begin(e, scr); break;
    case ENDING_SCENE5_EYES: e->loop = scene5_eyes(e, scr); break;
    case ENDING_SCENE5_END: e->loop = scene6_begin(e); break;
    case ENDING_SCENE6_WIPE: e->loop = scene6_wipe(e, scr); break;
    case ENDING_SCENE6_FADE: e->loop = scene6_fade(e, scr); break;
    case ENDING_SCENE6_END: e->loop = scene7_begin(e, scr); break;
    case ENDING_SCENE7: e->loop = scene7(e, scr); break;
    default: e->loop = 0; break;
    }
    return e->loop;
}
