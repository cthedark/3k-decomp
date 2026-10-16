/*
 * Creative Music Format (CMF) songs, as stored back to back in MUC.ALL.
 *
 * A CMF is a header, a block of 16-byte OPL instrument patches, and a MIDI-style track. This
 * module parses the header and walks the track one event at a time with absolute tick times; it
 * knows nothing about sound chips. See docs/formats.md ("CMF songs in MUC.ALL").
 *
 * What the game does with it (STORY2.SL 0x12fec): it sets the driver clock to
 * 1,193,180 / ticks-per-second (the header's field at offset 0x0c: 96, so the song runs at 96
 * ticks per second whatever the machine), gives the driver the patch block and starts the track.
 * When the driver reports the song has ended, the next poll starts it again (0x130b0).
 */
#ifndef TK_CMF_H
#define TK_CMF_H

#include <stddef.h>
#include <stdint.h>

typedef struct cmf cmf_t;

/* Parse a song held in memory (copied). Returns NULL, with a message on stderr, if it is not a
 * version 1.0 or 1.1 CMF or its offsets do not fit the data. */
cmf_t *cmf_parse(const uint8_t *data, size_t len);
void cmf_free(cmf_t *c);

unsigned cmf_ticks_per_second(const cmf_t *c);   /* the clock the song is timed by */
unsigned cmf_ticks_per_quarter(const cmf_t *c);
unsigned cmf_instrument_count(const cmf_t *c);
const uint8_t *cmf_instrument(const cmf_t *c, unsigned i);   /* 16 bytes, NULL if out of range */
int cmf_channel_used(const cmf_t *c, unsigned ch);           /* the header's channel table */

typedef struct {
    uint32_t tick;     /* absolute time of the event */
    uint8_t status;    /* 0x80..0xef: kind in the high nibble, channel in the low one */
    uint8_t a, b;      /* data bytes (b is 0 for program change and channel pressure) */
} cmf_event;

typedef struct {
    const cmf_t *song;
    size_t pos;
    uint32_t tick;     /* time of the last event read, and of the end marker once done */
    uint8_t running;   /* running status */
    int done;          /* the end-of-track marker (or the end of the data) was reached */
    int error;         /* the track is malformed; done is set too */
} cmf_cursor;

void cmf_rewind(const cmf_t *c, cmf_cursor *cur);

/* The next channel event, in order. Meta events other than end-of-track and system-exclusive data
 * are skipped. Returns 1 with *ev filled, or 0 at the end of the song. */
int cmf_next(cmf_cursor *cur, cmf_event *ev);

#endif
