#include "lcz.h"

#include <stdlib.h>
#include <string.h>

int lcz_next_chunk(const uint8_t *file, size_t file_len, size_t *pos, lcz_chunk *out)
{
    size_t p = *pos;
    if (p >= file_len)
        return LCZ_END;
    if (p + 3 > file_len)
        return LCZ_ERR;

    uint8_t esc = file[p];
    size_t size = (size_t)file[p + 1] | ((size_t)file[p + 2] << 8);
    if (size < 3 || p + size > file_len)
        return LCZ_ERR;

    const uint8_t *body = file + p + 3;
    size_t body_len = size - 3;

    /* Pass 1: measure the decoded length and validate escape sequences. */
    size_t n = 0;
    for (size_t i = 0; i < body_len;) {
        if (body[i] == esc) {
            if (i + 2 >= body_len)
                return LCZ_ERR;
            n += body[i + 1];
            i += 3;
        } else {
            n++;
            i++;
        }
    }

    uint8_t *px = malloc(n ? n : 1);
    if (!px)
        return LCZ_ERR;

    /* Pass 2: expand. */
    size_t o = 0;
    for (size_t i = 0; i < body_len;) {
        if (body[i] == esc) {
            memset(px + o, body[i + 2], body[i + 1]);
            o += body[i + 1];
            i += 3;
        } else {
            px[o++] = body[i++];
        }
    }

    out->esc = esc;
    out->file_offset = p;
    out->packed_size = size;
    out->pixels = px;
    out->len = n;
    *pos = p + size;
    return LCZ_OK;
}

void lcz_chunk_free(lcz_chunk *c)
{
    free(c->pixels);
    c->pixels = NULL;
    c->len = 0;
}
