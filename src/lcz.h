/*
 * LCZ container decoder.
 *
 * A .LCZ file is a sequence of chunks:
 *     [esc : 1 byte] [size : 2 bytes, little endian, counts these 3 bytes] [data]
 * `data` is run-length coded: the byte `esc` introduces `esc, count, value`
 * (`count` copies of `value`, count may be 0); any other byte is a literal.
 *
 * Behaviour was derived from observation of the original program (see
 * docs/formats.md and docs/code.md) and checked against the original's own
 * decompression routine; this is a fresh implementation.
 */
#ifndef TK_LCZ_H
#define TK_LCZ_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t esc;          /* escape byte of this chunk */
    size_t file_offset;   /* where the chunk starts in the file */
    size_t packed_size;   /* size field, including the 3 header bytes */
    uint8_t *pixels;      /* decoded bytes, malloc'ed; free with lcz_chunk_free */
    size_t len;           /* number of decoded bytes */
} lcz_chunk;

enum { LCZ_OK = 0, LCZ_END = 1, LCZ_ERR = -1 };

/*
 * Decode the chunk that starts at *pos and advance *pos past it.
 * Returns LCZ_OK, LCZ_END when *pos is at the end of the file, or LCZ_ERR
 * for a truncated header, an impossible size, or a truncated escape sequence.
 */
int lcz_next_chunk(const uint8_t *file, size_t file_len, size_t *pos, lcz_chunk *out);

void lcz_chunk_free(lcz_chunk *c);

#endif
