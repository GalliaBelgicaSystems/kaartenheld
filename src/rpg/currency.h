#ifndef RPG_CURRENCY_H
#define RPG_CURRENCY_H

#include <stdint.h>
#include "rpg/state.h"

/* Generalized currency primitive.  Currencies are dense slots indexed by
 * CurrencyId - 1 inside GameState.currency.amount[].  A game may use any
 * number of currencies; only the ones actually used occupy slots. */

int16_t currency_get(const GameState *state, CurrencyId currency);
/* Adds (or, when negative, spends) currency, saturating at 9999 / 0.
 * Staging hazard: the bank-11 dispatch reuses g_bk_ptr_a / g_bk_byte_*
 * for its own arguments, so any values the caller staged BEFORE this
 * call are gone afterwards -- snapshot staged ids/prices into locals
 * first.  The shop paths do (shop_sell / shop_confirm snapshot the
 * quote before spending); every other current caller (event.c
 * ADD_CURRENCY, scenarios.c BUY_ITEM, world.c victory grant) stages
 * fresh per call and reads no staged values after it, so they need no
 * snapshot -- re-check this if a new caller is added. */
void currency_add(GameState *state, CurrencyId currency, int16_t amount);

/* Bank-11 saturating core (src/rpg/currency_banked.c), dispatched by the
 * fixed wrapper above. */
void currency_add_banked(void);

#endif /* RPG_CURRENCY_H */
