#pragma bank 7

#include "deck.h"
#include "banked.h"
#include "rng.h"

/* Banked body of deck_reshuffle() (see deck.c).  Lives in ROM bank 7:
 * bank 2 ran out of room for the remainder fold and bank 3 (home of
 * deck_discard_banked) overflows in the release build, while bank 7 has
 * room in both builds.  The deck module is therefore split by dispatch
 * (init in bank 2, discard in bank 3, reshuffle here) -- each body is
 * self-contained and entered through the WRAM trampoline, so shared
 * banks are not required.
 *
 * Self-contained: reads only the staged Deck pointer (ptr_a) and advances
 * the shared RNG stream via an inlined xorshift on g_rng_state (banked code
 * must not call fixed-bank functions; see AGENTS.md 52.11.1).  The step
 * must stay byte-identical to rng_next(). */

/* Field-wise card copy/swap helpers.  Banked bodies MUST NOT use struct
 * assignment: SDCC lowers Card-sized copies to __memcpy, which links into
 * the switchable home bank -- unreachable while bank 7 is mapped
 * (AGENTS.md 52.11.1). */
static void card_copy_banked(Card *dst, const Card *src)
{
    dst->type = src->type;
    dst->value = src->value;
    dst->uses_remaining = src->uses_remaining;
    dst->cost = src->cost;
    dst->effect = src->effect;
    dst->status_id = src->status_id;
    dst->status_chance = src->status_chance;
    dst->ring = src->ring;
}

static void card_swap_banked(Card *a, Card *b)
{
    /* Swap via three field-wise copies through a stack temp: 24 direct
     * field assignments blow up to ~300 instructions under SDCC's
     * far-struct spilling.  The temp is an 8-byte stack local (well under
     * the harness SP budget, AGENTS.md 52.14), deliberately NOT a function
     * static: adding an 8-byte _DATA scratch here coincided with battle
     * input stalling under the harness, so banked-body scratch stays off
     * _DATA. */
    Card t;

    card_copy_banked(&t, a);
    card_copy_banked(a, b);
    card_copy_banked(b, &t);
}

void deck_reshuffle_banked(void)
{
    Deck *d = (Deck *)g_bk_ptr_a;
    uint8_t i, j, rem;
    Card *dp;
    const Card *sp;

    if (!d || d->discard_count == 0) return;

    /* Preserve the undrawn pile remainder: the atomic refill
     * (battle_turn_draw in battle.c) can trigger a reshuffle while
     * draw_idx < count.  Fold cards[draw_idx..count) into the discard
     * pile first so no real card is lost.  Remainder + discard are
     * disjoint subsets of one deck, so their sum cannot exceed
     * MAX_DECK_SIZE (same trust level as the copy loop below and the
     * MAX_DECK_SIZE cap in deck_discard_banked); draw_idx <= count
     * always holds (draw_idx advances only inside deck_draw's guarded
     * draw), so the subtraction cannot wrap.  Base pointers are
     * hoisted out of the loop (far-struct address math is expensive);
     * field-wise copies only -- no struct assignment across banks
     * (AGENTS.md 52.11.1). */
    rem = (uint8_t)(d->count - d->draw_idx);
    dp = &d->discard[d->discard_count];
    sp = &d->cards[d->draw_idx];
    for (i = 0; i < rem; i++) {
        card_copy_banked(dp, sp);
        dp++;
        sp++;
    }
    d->discard_count = (uint8_t)(d->discard_count + rem);

    for (i = 0; i < d->discard_count; i++) {
        card_copy_banked(&d->cards[i], &d->discard[i]);
    }
    d->count = d->discard_count;
    d->discard_count = 0;
    d->draw_idx = 0;

    /* Fisher-Yates: rejection-sample j in [0, i] (mask 0x1F covers
     * MAX_DECK_SIZE - 1). */
    for (i = (uint8_t)(d->count - 1); i > 0; i--) {
        do {
            g_rng_state ^= g_rng_state << 7;
            g_rng_state ^= g_rng_state >> 9;
            g_rng_state ^= g_rng_state << 8;
            j = (uint8_t)(g_rng_state & 0x1F);
        } while (j > i);
        card_swap_banked(&d->cards[i], &d->cards[j]);
    }
}
