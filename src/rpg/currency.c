#include "rpg/currency.h"
#include "banked.h"

int16_t currency_get(const GameState *state, CurrencyId currency)
{
    if (!state || currency < 1 || currency > MAX_CURRENCIES) return 0;
    return state->currency.amount[currency - 1];
}

/* Thin fixed-bank wrapper: the saturating core lives in the bank-11 body
 * (src/rpg/currency_banked.c) so the fixed bank stays under 0x8000
 * (AGENTS.md §52.18).  Validation lives in the body; this only stages. */
void currency_add_banked(void);

void currency_add(GameState *state, CurrencyId currency, int16_t amount)
{
    g_bk_call_bank = 11;
    g_bk_call_target = (uint16_t)&currency_add_banked;
    g_bk_ptr_a = (void *)state;
    g_bk_byte_a = (uint8_t)currency;
    g_bk_byte_b = (uint8_t)(amount & 0xFF);
    g_bk_byte_c = (uint8_t)((amount >> 8) & 0xFF);
    banked_call_run();
}
