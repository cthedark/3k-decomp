/*
 * An OPL2 (YM3812, the AdLib chip) as a plain C interface.
 *
 * The chip itself is emulated by ymfm (third_party/ymfm, BSD-3-Clause, pinned commit in
 * third_party/ymfm/PINNED.txt). This header hides the C++ so the rest of the program stays C99.
 * The chip is clocked at 3,579,545 Hz like an AdLib card, which gives a native output rate of
 * 49,716 Hz (opl_native_rate).
 */
#ifndef TK_OPL_H
#define TK_OPL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct opl opl_t;

opl_t *opl_create(void);
void opl_free(opl_t *o);
void opl_reset(opl_t *o);

/* The rate, in Hz, at which opl_generate produces samples (the chip's own rate). */
unsigned opl_native_rate(void);

/* Write one chip register (the AdLib's address port 0x388 followed by data port 0x389). */
void opl_write(opl_t *o, uint8_t reg, uint8_t value);

/* Generate `frames` mono 16-bit samples at opl_native_rate(), overwriting out[]. */
void opl_generate(opl_t *o, int16_t *out, size_t frames);

#ifdef __cplusplus
}
#endif

#endif
