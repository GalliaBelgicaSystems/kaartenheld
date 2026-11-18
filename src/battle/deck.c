#include "deck.h"
#include "banked.h"
#include "rng.h"
#include "rpg/cards.h"
#include "rpg/status.h"

void deck_init_default(Deck *d)
{
    g_bk_call_bank = 2;
    g_bk_call_target = (uint16_t)&deck_init_default_banked;
    g_bk_ptr_a = (void *)d;
    banked_call_run();
}

void enemy_deck_setup(void)
{
    g_bk_call_bank = 2;
    g_bk_call_target = (uint16_t)&enemy_deck_setup_banked;
    banked_call_run();
}

/* Banked dispatch: the Fisher-Yates body lives in ROM bank 8
 * (src/battle/deck_shuffle_banked.c) to keep the fixed-bank _CODE budget
 * small.  Bank 8 has room in both builds; banks 2, 3 and 7 do not fit
 * the remainder-preserving reshuffle. */
void deck_reshuffle(Deck *d)
{
    g_bk_call_bank = 8;
    g_bk_call_target = (uint16_t)&deck_reshuffle_banked;
    g_bk_ptr_a = (void *)d;
    banked_call_run();
}

void deck_discard(Deck *d, const Card *c)
{
    Card *slot;
    /* Inline (not banked): pure WRAM field writes, and the fixed bank has
     * room again.  Field-wise (no struct assignment: keep the banked-ABI
     * habit -- one copy idiom everywhere). */
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
