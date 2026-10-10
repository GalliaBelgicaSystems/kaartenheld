#pragma bank 7

#include "deck.h"
#include "banked.h"

/* Banked body of deck_discard() (src/battle/deck.c).  Pure WRAM writes:
 * staged Deck (ptr_a) and card (ptr_b); field-wise copy only -- struct
 * assignment lowers to __memcpy in the fixed bank (AGENTS.md 52.11.1).
 * Lives in bank 7 (not bank 3) to relieve the fixed-adjacent bank-3
 * budget; deck_reshuffle_banked already lives here. */

void deck_discard_banked(void)
{
    Deck *d = (Deck *)g_bk_ptr_a;
    const Card *c = (const Card *)g_bk_ptr_b;
    Card *slot;

    if (!d || !c || d->discard_count >= MAX_DECK_SIZE) return;
    slot = &d->discard[d->discard_count++];
    slot->type = c->type;
    slot->value = c->value;
    slot->uses_remaining = c->uses_remaining;
    slot->cost = c->cost;
    slot->effect = c->effect;
    slot->status_id = c->status_id;
    slot->status_chance = c->status_chance;
    slot->ring = c->ring;
}

