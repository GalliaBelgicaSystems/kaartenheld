#pragma bank 2

#include "rng.h"
#include "banked.h"
#include "rpg/loot.h"

/* Banked RNG bodies (see AGENTS.md 52.11.1): the xorshift step lives here
 * so the fixed-bank _CODE budget keeps room for rendering.  Self-contained:
 * WRAM globals + own arithmetic only -- no fixed-bank calls (the bank is
 * switched away while running), no di/ei (bare interrupt ops are lint-
 * forbidden in banked files, and a held di would stall the music clock;
 * none is needed anyway since the timer ISR never touches RNG state).
 * Logic is identical to the old fixed-bank bodies (and to the inlined
 * xorshift steps in deck_shuffle_banked / battle_defend_content, which
 * cannot call across banks and stay duplicated by design). */

void rng_next_banked(void)
{
    uint16_t s;

    s = g_rng_state;
    s ^= (uint16_t)(s << 7);
    s ^= (uint16_t)(s >> 9);
    s ^= (uint16_t)(s << 8);
    g_rng_state = s;
    g_bk_byte_a = (uint8_t)s;
    g_bk_byte_b = (uint8_t)(s >> 8);
}

void rng_set_seed_banked(void)
{
    uint16_t seed;

    seed = (uint16_t)(((uint16_t)g_bk_byte_b << 8) | g_bk_byte_a);
    g_rng_state = seed ? seed : 1;
    /* Derive the isolated loot stream from the same seed so drops are
     * deterministic per scenario while never consuming the shared
     * stream (docs/loot.md 34.5). */
    g_loot_rng_state = (uint16_t)(g_rng_state ^ 0x1B3C);
    if (!g_loot_rng_state) g_loot_rng_state = 1;
}
