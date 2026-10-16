#include "tkrand.h"

static uint32_t seed = 1; /* the seed in the original's data image */

void tk_srand(uint16_t s)
{
    seed = s;
}

void tk_set_state(uint32_t state)
{
    seed = state;
}

int tk_rand(void)
{
    seed = seed * 214013u + 2531011u;
    return (int)((seed >> 16) & 0x7fff);
}
