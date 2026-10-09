#ifndef RNG_H
#define RNG_H

#include <stdint.h>

/* The generator state lives in WRAM and is exported by symbol so banked
 * code (e.g. deck_reshuffle_banked, ROM bank 7) can advance the SAME
 * stream with an inlined xorshift step — a banked body must never call
 * fixed-bank functions (see AGENTS.md 52.11.1).  Gameplay code should use
 * rng_next()/rng_set_seed() instead of touching this directly. */
extern uint16_t g_rng_state;

uint16_t rng_next(void);
void rng_set_seed(uint16_t seed);

#endif /* RNG_H */
