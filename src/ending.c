#include "ending.h"

#include <stdlib.h>
#include <string.h>

#include "assets.h"

/* The buffers of main, by the stack places that hold them. The file buffer [bp-0x12] (each .LCZ read whole into it, 0xffdc bytes at
 * most) is also the frame the scene is built in before it is copied to the screen. */
struct ending {
    uint8_t file[0xFFDC];
    uint8_t sky[4 + 320 * 200];       /* [bp-0x22]: ENDING12.LCZ, the clouds, 320 x 200 with its header */
    uint8_t faces[0x2350];            /* [bp-0xe]: ENDING1.LCZ's first part: five 41 x 44 pictures of her face with headers */
    uint8_t body[2][320 * 147];       /* [bp-0x36], [bp-0x46]: ENDING1.LCZ's two pictures of her (no header), 320 x 147 */
    uint8_t subs[0x3750];             /* [bp-0x1e]: ENDING1T.LCZ, four 236 x 15 subtitle lines */
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
    unpack(e->file, n, e->sky, sizeof e->sky);
    if (!(n = read_file(e, "ENDING1.LCZ")) || n < 0x40D1)
        goto fail;
    unpack(e->file, n, e->faces, sizeof e->faces);
    unpack(e->file + 0x1925, n - 0x1925, e->body[0], sizeof e->body[0]);
    unpack(e->file + 0x40D1, n - 0x40D1, e->body[1], sizeof e->body[1]);
    if (!(n = read_file(e, "ENDING1T.LCZ")))
        goto fail;
    unpack(e->file, n, e->subs, sizeof e->subs);
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
    turn_rows(e->sky);
    put_hdr(e->body[0], sizeof e->body[0], 0xBCF, 0x140, e->faces + e->face);
    put_hdr(e->body[1], sizeof e->body[1], 0xBCF, 0x140, e->faces + e->face);
    /* 0x19c:0x102: with a Sound Blaster a sound that ended is started again (not built: no sound card) */
    memcpy(e->file, e->sky + 4, 0xFA00);
    if (++e->swap > 4) {
        e->swap = 0;
        e->which = !e->which;
    }
    const uint8_t *b = e->body[e->which ? 0 : 1];
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
        put_clear(e->file, sizeof e->file, 0xE62A, 0x140, e->subs + e->sub, 0xEC, 0xF);
    }
    memcpy(scr->pix, e->file, 0xFA00);
    return ENDING_SCENE1;
}

int ending_step(ending_t *e, screen_t *scr)
{
    if (e->loop == ENDING_SCENE1)
        e->loop = scene1(e, scr);
    else
        e->loop = 0;
    return e->loop;
}
