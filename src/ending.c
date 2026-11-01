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
    uint8_t pal[0x300];
    if (assets_read_at("ANI.LCP", 0, pal, sizeof pal) != 0)
        goto fail;
    memset(scr->pix, 0, sizeof scr->pix);   /* 0x204:0x163(0x13): the mode set */
    screen_set_palette(scr, pal, 0, 256);   /* 0x22c:0x14e */
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

void ending_set_voice(ending_t *e, void (*play)(void *ctx, unsigned off), void (*cut)(void *ctx), void *ctx)
{
    e->voice = play;
    e->cut = cut;
    e->vctx = ctx;
}

int ending_step(ending_t *e, screen_t *scr)
{
    switch (e->loop) {
    case ENDING_SCENE1: e->loop = scene1(e, scr); break;
    case ENDING_SCENE1_END: e->loop = scene2_begin(e, scr); break;
    case ENDING_SCENE2: e->loop = scene2(e, scr); break;
    default: e->loop = 0; break;
    }
    return e->loop;
}
