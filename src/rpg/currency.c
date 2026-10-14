#include "rpg/currency.h"
#include "telemetry.h"

int16_t currency_get(const GameState *state, CurrencyId currency)
{
    if (!state || currency < 1 || currency > MAX_CURRENCIES) return 0;
    return state->currency.amount[currency - 1];
}

void currency_add(GameState *state, CurrencyId currency, int16_t amount)
{
    int16_t new_value;
    if (!state || currency < 1 || currency > MAX_CURRENCIES || amount == 0) return;
    new_value = (int16_t)(state->currency.amount[currency - 1] + amount);
    state->currency.amount[currency - 1] = new_value;
#ifdef DEBUG_BUILD
    if (amount > 0) {
        telemetry_emit(EVENT_CURRENCY_ADDED, (uint8_t)currency,
                       (uint8_t)(amount & 0xFF),
                       (uint8_t)((amount >> 8) & 0xFF), 0);
    } else {
        telemetry_emit(EVENT_CURRENCY_SPENT, (uint8_t)currency,
                       (uint8_t)((-amount) & 0xFF),
                       (uint8_t)((-amount >> 8) & 0xFF), 0);
    }
#endif
}
