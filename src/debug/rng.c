#include "rng.h"
#include "rpg/loot.h"
#include "banked.h"

uint16_t g_rng_state = 1;

/* Thin fixed-bank wrappers: stage args/target into the g_bk_call_*
 * globals, then run the WRAM trampoline (see banked.h / AGENTS.md
 * 52.11.1).  The xorshift bodies live in ROM bank 2 (rng_banked.c) so
 * the fixed-bank _CODE budget keeps room for rendering. */

uint16_t rng_next(void)
{
    uint16_t lo;
    uint16_t hi;

    g_bk_call_bank = 2;
    g_bk_call_target = (uint16_t)&rng_next_banked;
    banked_call_run();
    lo = g_bk_byte_a;
    hi = g_bk_byte_b;
    return (uint16_t)((hi << 8) | lo);
}

void rng_set_seed(uint16_t seed)
{
    g_bk_byte_a = (uint8_t)seed;
    g_bk_byte_b = (uint8_t)(seed >> 8);
    g_bk_call_bank = 2;
    g_bk_call_target = (uint16_t)&rng_set_seed_banked;
    banked_call_run();
}
