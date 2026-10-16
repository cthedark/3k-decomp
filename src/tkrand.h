/*
 * The pseudo-random generator of the original programs.
 *
 * STORY2.SL's rand() (0x13f34) is the Microsoft C library generator:
 *     seed = seed * 214013 + 2531011   (32-bit, wrapping)
 *     result = (seed >> 16) & 0x7fff
 * and srand() (0x13f22) stores a 16-bit value as the seed with the high word cleared.
 * The program never calls srand, and the seed in its data image is 1, so every run of the
 * original sees the same sequence. There is one generator for the whole program; the port
 * keeps one global state, and restarts it at 1 where a new original program would start.
 */
#ifndef TK_RAND_H
#define TK_RAND_H
#include <stdint.h>

void tk_srand(uint16_t seed);
/* Set the whole 32-bit state. The original's state after a slide 0 run is not a 16-bit seed; this
 * lets a comparison start from the state read out of the reference run. */
void tk_set_state(uint32_t state);
/* Next value, 0..32767. */
int tk_rand(void);

#endif
