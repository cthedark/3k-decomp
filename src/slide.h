/*
 * Helpers shared by the slides of the intro (the scenes in intro1.c, intro2.c, ...), which
 * all start the same way: decode a few LCZ chunks of known size, take the two palettes of
 * ANI.LCP, and fade entries 0..239 between black, the dim palette and the bright palette.
 */
#ifndef TK_SLIDE_H
#define TK_SLIDE_H

#include <stddef.h>
#include <stdint.h>

#include "screen.h"
#include "subs.h"

enum { SLIDE_FADE_STEPS = 64, SLIDE_FADE_BYTES = 0xf0 * 3 };

/* Decode the first `n` chunks of `file` into dst[0..n-1], each of which must decode to exactly
 * want[i] bytes. Returns 0, or -1 with a message on stderr. */
int slide_load_chunks(const char *file, int n, uint8_t *const *dst, const size_t *want);

/* The two palettes at the start of ANI.LCP: `bright` (bytes 0..767, the normal look) and `dim`
 * (bytes 768..1535). Returns 0, or -1 with a message on stderr. */
int slide_load_palettes(uint8_t bright[768], uint8_t dim[768]);

/* Slide 0 leaves palette entries 0..239 black and 240..255 (the subtitle ramp) as in `bright`.
 * Set the screen and the fade state `cur` to that. */
void slide_start_palette(screen_t *scr, uint8_t cur[768], const uint8_t bright[768]);

/* One step of the 64-step fade of entries 0..239 (STORY2.SL 0x121ce): every byte of `cur`
 * moves one step toward `target`, then the entries are loaded into the screen's palette. */
void slide_fade_step(screen_t *scr, uint8_t cur[768], const uint8_t target[768]);

/* A uniform view of a slide scene, so that one driver in main.c can play, step and chain them.
 * `step` advances the scene by one step of its current phase; `tick` is the BIOS clock and `skip`
 * is non-zero if Space was hit since the last step that looked at it (see slide_probe.looks). */
typedef struct {
    int done;             /* the scene has finished */
    int anim;             /* in the animation loop */
    unsigned anim_done;   /* passes completed in the animation loop */
    unsigned anim_hz;     /* passes per second to run the animation loop at (src/timing.h) */
    const subs_t *subs;   /* non-NULL while the subtitle player is the current phase */
    int looks;            /* the original looks at the keyboard in the current phase */
} slide_probe;

typedef struct {
    const char *name;
    void *(*create)(screen_t *scr, int faithful_keys);
    void (*destroy)(void *scene);
    void (*step)(void *scene, screen_t *scr, unsigned tick, int skip);
    void (*probe)(const void *scene, slide_probe *out);
} slide_ops;

#endif
