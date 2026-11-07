#ifndef RPG_CURRENCY_H
#define RPG_CURRENCY_H

#include <stdint.h>
#include "rpg/state.h"

/* Generalized currency primitive.  Currencies are dense slots indexed by
 * CurrencyId - 1 inside GameState.currency.amount[].  A game may use any
 * number of currencies; only the ones actually used occupy slots. */

int16_t currency_get(const GameState *state, CurrencyId currency);
void currency_add(GameState *state, CurrencyId currency, int16_t amount);

/* Bank-11 saturating core (src/rpg/currency_banked.c), dispatched by the
 * fixed wrapper above. */
void currency_add_banked(void);

#endif /* RPG_CURRENCY_H */
