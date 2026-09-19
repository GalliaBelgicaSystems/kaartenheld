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

void deck_draw(Deck *d, Card *out_card)
{
    if (!out_card) return;
    if (!d || d->draw_idx >= d->count) {
        /* Dry pile.  Reshuffling is now an explicit battle-level event (the
         * reshuffle turn), so a draw from a dry pile yields the phantom card
         * safety net instead of silently cycling the discard pile. */
        out_card->type = BATTLE_CARD_TYPE_SWORD;
        out_card->value = 2;
        out_card->uses_remaining = 0xFF;
        out_card->cost = 1;
        out_card->effect = CARD_EFFECT_DAMAGE_TARGET;
        out_card->status_id = STATUS_NONE;
        out_card->status_chance = 0;
        return;
    }

    *out_card = d->cards[d->draw_idx++];
}

/* Banked dispatch: the Fisher-Yates body lives in ROM bank 7
 * (src/battle/deck_shuffle_banked.c) to keep the fixed-bank _CODE budget
 * small.  Bank 7 has room in both builds; banks 2 and 3 do not fit the
 * remainder-preserving reshuffle. */
void deck_reshuffle(Deck *d)
{
    g_bk_call_bank = 7;
    g_bk_call_target = (uint16_t)&deck_reshuffle_banked;
    g_bk_ptr_a = (void *)d;
    banked_call_run();
}

void deck_discard(Deck *d, const Card *c)
{
    /* Body runs banked (src/battle/deck_banked.c): pure WRAM writes,
     * staged Deck/card pointers, keeps fixed-bank budget small. */
    if (!d || !c) return;
    g_bk_call_bank = 3;
    g_bk_call_target = (uint16_t)&deck_discard_banked;
    g_bk_ptr_a = (void *)d;
    g_bk_ptr_b = (void *)c;
    banked_call_run();
}
