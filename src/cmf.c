#include "cmf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { HDR_V10 = 37, HDR_V11 = 40, PATCH_BYTES = 16 };

struct cmf {
    uint8_t *data;
    size_t len;
    size_t music_at;
    size_t inst_at;
    unsigned ticks_per_quarter, ticks_per_second, inst_count;
    uint8_t channels[16];
};

static unsigned rd16(const uint8_t *p) { return p[0] | p[1] << 8; }

cmf_t *cmf_parse(const uint8_t *data, size_t len)
{
    if (len < HDR_V10 || memcmp(data, "CTMF", 4) != 0) {
        fprintf(stderr, "cmf: not a CTMF file\n");
        return NULL;
    }
    unsigned minor = data[4], major = data[5];
    if (major != 1 || minor > 1) {
        fprintf(stderr, "cmf: unsupported version %u.%u\n", major, minor);
        return NULL;
    }
    size_t hdr = minor == 1 ? HDR_V11 : HDR_V10;
    if (len < hdr) {
        fprintf(stderr, "cmf: header is cut short\n");
        return NULL;
    }
    cmf_t *c = calloc(1, sizeof *c);
    if (!c)
        return NULL;
    c->inst_at = rd16(data + 6);
    c->music_at = rd16(data + 8);
    c->ticks_per_quarter = rd16(data + 10);
    c->ticks_per_second = rd16(data + 12);
    memcpy(c->channels, data + 20, 16);
    c->inst_count = minor == 1 ? rd16(data + 36) : data[36];
    if (c->ticks_per_second == 0 || c->music_at < hdr || c->music_at >= len || c->inst_at < hdr ||
        c->inst_at + (size_t)c->inst_count * PATCH_BYTES > len) {
        fprintf(stderr, "cmf: header offsets do not fit the data\n");
        free(c);
        return NULL;
    }
    c->data = malloc(len);
    if (!c->data) {
        free(c);
        return NULL;
    }
    memcpy(c->data, data, len);
    c->len = len;
    return c;
}

void cmf_free(cmf_t *c)
{
    if (c) {
        free(c->data);
        free(c);
    }
}

unsigned cmf_ticks_per_second(const cmf_t *c) { return c->ticks_per_second; }
unsigned cmf_ticks_per_quarter(const cmf_t *c) { return c->ticks_per_quarter; }
unsigned cmf_instrument_count(const cmf_t *c) { return c->inst_count; }
int cmf_channel_used(const cmf_t *c, unsigned ch) { return ch < 16 && c->channels[ch] != 0; }

const uint8_t *cmf_instrument(const cmf_t *c, unsigned i)
{
    return i < c->inst_count ? c->data + c->inst_at + (size_t)i * PATCH_BYTES : NULL;
}

void cmf_rewind(const cmf_t *c, cmf_cursor *cur)
{
    memset(cur, 0, sizeof *cur);
    cur->song = c;
    cur->pos = c->music_at;
}

/* A MIDI variable-length number. Returns 0 on success. */
static int vlq(const cmf_t *c, size_t *pos, uint32_t *out)
{
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) {
        if (*pos >= c->len)
            return -1;
        uint8_t b = c->data[(*pos)++];
        v = (v << 7) | (b & 0x7f);
        if (!(b & 0x80)) {
            *out = v;
            return 0;
        }
    }
    return -1;
}

static int fail(cmf_cursor *cur)
{
    cur->error = 1;
    cur->done = 1;
    return 0;
}

int cmf_next(cmf_cursor *cur, cmf_event *ev)
{
    const cmf_t *c = cur->song;
    while (!cur->done) {
        uint32_t dt;
        if (cur->pos >= c->len) {   /* no end marker: treat the end of the data as the end */
            cur->done = 1;
            return 0;
        }
        if (vlq(c, &cur->pos, &dt) != 0 || cur->pos >= c->len)
            return fail(cur);
        cur->tick += dt;
        uint8_t b = c->data[cur->pos];
        if (b == 0xff) {   /* meta event: type, length, data */
            uint32_t n;
            size_t q = cur->pos + 2;
            if (q > c->len || vlq(c, &q, &n) != 0 || q + n > c->len)
                return fail(cur);
            uint8_t type = c->data[cur->pos + 1];
            cur->pos = q + n;
            if (type == 0x2f) {
                cur->done = 1;
                return 0;
            }
            continue;
        }
        if (b == 0xf0 || b == 0xf7) {   /* system exclusive: length, data; ignored by the driver */
            uint32_t n;
            size_t q = cur->pos + 1;
            if (vlq(c, &q, &n) != 0 || q + n > c->len)
                return fail(cur);
            cur->pos = q + n;
            continue;
        }
        if (b & 0x80) {
            cur->running = b;
            cur->pos++;
        } else if (!(cur->running & 0x80)) {
            return fail(cur);   /* data byte with no status to apply it to */
        }
        unsigned hi = cur->running & 0xf0;
        if (hi == 0xf0)
            return fail(cur);
        unsigned n = (hi == 0xc0 || hi == 0xd0) ? 1 : 2;
        if (cur->pos + n > c->len)
            return fail(cur);
        ev->tick = cur->tick;
        ev->status = cur->running;
        ev->a = c->data[cur->pos];
        ev->b = n == 2 ? c->data[cur->pos + 1] : 0;
        cur->pos += n;
        return 1;
    }
    return 0;
}
