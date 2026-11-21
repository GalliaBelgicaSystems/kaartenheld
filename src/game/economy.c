#include "economy.h"
#include "banked.h"

/* Fixed-bank economy surface: staging wrappers only (docs/economy.md §9,
 * AGENTS.md §52.18).  All arithmetic lives in the bank-11 bodies. */

uint8_t economy_quote(uint8_t price, int8_t bias, uint8_t use_market)
{
    g_bk_call_bank = 11;
    g_bk_call_target = (uint16_t)&economy_quote_banked;
    g_bk_byte_a = price;
    g_bk_byte_b = (uint8_t)bias;
    g_bk_byte_c = use_market;
    banked_call_run();
    return g_bk_byte_b;
}

void economy_note_spent(GameState *state, uint8_t amount)
{
    uint16_t v;
    if (!state || amount == 0) return;
    v = (uint16_t)(state->economy.gold_out + amount);
    if (v < state->economy.gold_out) v = 0xFFFF;
    state->economy.gold_out = v;
}
