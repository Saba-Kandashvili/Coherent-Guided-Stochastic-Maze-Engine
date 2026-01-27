#ifndef __THREAD_RANDOM__
#define __THREAD_RANDOM__



#include <stdint.h>


// linear congruential generator
// Inline for performance (no function call overhead)
static inline uint32_t nextRandom(uint32_t *state) {
    *state = (*state * 1664525 + 1013904223); // modulo 2^32 is implicit with uint32 overflow
    return *state;
}


#endif
