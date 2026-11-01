#pragma bank 2

#include "deck.h"
#include "rng.h"

/* Battle-start deck shuffle (see deck.h).  Atomic Fisher-Yates over the
 * draw pile, keeping draw_idx at 0 so the next BATTLE_HAND_SIZE draws are
 * a random subset of the deck instead of the DECK-menu order.
 *
 * Lives in ROM bank 2 (not the fixed bank) because the debug link's
 * fixed bank is hard against 0x8000 (AGENTS.md 52.18/55.5); both callers
 * (deck_init.c and battle_init_content.c) are bank-2 bodies that run with
 * this bank mapped, so they call it directly -- no trampoline, no
 * fixed-bank wrapper.
 *
 * Self-contained per the banked-body rules (AGENTS.md 52.11.1): field-wise
 * card copies only (CARD_COPY_FIELDS; no struct assignment -> __memcpy) and
 * an inlined xorshift step that must stay byte-identical to rng_next().
 * Consumes the SAME g_rng_state stream as the mid-battle reshuffle
 * (deck_shuffle_banked.c), so a battle's deal and its reshuffle advance one
 * sequence. */

/* One CARD_COPY_FIELDS expansion per function (SDCC rejects a second
 * typedef 'card_copy_fields_ok' in the same scope), same shape as
 * card_copy_banked in deck_shuffle_banked.c. */
static void card_copy_here(Card *dst, const Card *src)
{
    CARD_COPY_FIELDS(dst, src);
}

static void card_swap_here(Card *a, Card *b)
{
    Card t;

    card_copy_here(&t, a);
    card_copy_here(a, b);
    card_copy_here(b, &t);
}

void deck_shuffle_pile(Deck *d)
{
    uint8_t i, j;

    if (!d || d->count < 2) return;
    for (i = (uint8_t)(d->count - 1); i > 0; i--) {
        /* Rejection-sample j in [0, i] (mask 0x1F covers MAX_DECK_SIZE-1). */
        do {
            g_rng_state ^= g_rng_state << 7;
            g_rng_state ^= g_rng_state >> 9;
            g_rng_state ^= g_rng_state << 8;
            j = (uint8_t)(g_rng_state & 0x1F);
        } while (j > i);
        card_swap_here(&d->cards[i], &d->cards[j]);
    }
}
