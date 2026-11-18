#pragma bank 14
#pragma disable_warning 110

#include "battle.h"
#include "banked.h"
#include "rpg/status.h"
#include "rpg/cards.h"

/* Take pile[pile_idx] into hand[hand_idx]: rotate pile[draw_idx..pile_idx]
 * so the chosen card lands on top (skipped cards keep their relative
 * order for later draws), then deal it.  Field-wise copies only (no
 * struct assignment: SDCC lowers those to the RAM-resident __memcpy
 * helper, unreachable while a bank is mapped).  The CARD_COPY_FIELDS
 * `src` argument is hoisted: the macro names it once per field, so
 * passing `&cards[draw_idx++]` directly would advance the pile once
 * PER FIELD (8 draws per card) and corrupt every later refill. */
static void turn_draw_take(Battle *b, uint8_t hand_idx, uint8_t pile_idx)
{
    Card tmp;
    uint8_t k;
    CARD_COPY_FIELDS(&tmp, &b->deck.cards[pile_idx]);
    k = pile_idx;
    while (k > b->deck.draw_idx) {
        CARD_COPY_FIELDS(&b->deck.cards[k], &b->deck.cards[(uint8_t)(k - 1)]);
        k--;
    }
    /* tmp already holds the dealt card: straight to the hand, no
     * pile round-trip. */
    CARD_COPY_FIELDS(&b->hand[hand_idx], &tmp);
    b->deck.draw_idx++;
}

/* ── Banked hand-refill body (ROM bank 14) ───────────────────────────
 * Hosts battle_turn_draw()'s decision logic out of the fixed bank
 * (which sits hard against 0x8000, AGENTS.md 52.18).  The fixed
 * wrapper in battle.c keeps the signature and stages the Battle*
 * through the WRAM trampoline; the boolean result returns in
 * g_bk_byte_c.  The fixed wrapper also stages the live shared arrow
 * pool in g_bk_byte_d (limited-arrows), so this engine body stays
 * game-id-free.
 *
 * Self-contained (AGENTS.md 52.11.1): reads/writes only the staged
 * Battle* (hand + deck, both WRAM), never calls fixed-bank functions.
 *
 * Decision contract (unchanged from the fixed version): atomic -- a
 * partial pile plus a non-empty discard deals NOTHING and reports dry
 * so the caller reshuffles first; the short-pile case reports covered
 * only when the discard pile is also empty.  Drawable counts non-bows
 * plus bows up to the arrow backing -- bows without backing are
 * skipped, never drawn empty, so the raw pile size alone must not
 * gate the check.
 *
 * Limited-arrows draw (never-draw-empty): each open slot inspects the
 * pile top (draw_idx).  Non-bows deal in shuffled pile order; a bow at
 * draw_idx while backing lasts (arrows minus bows already in hand)
 * takes the cheapest pile bow first (AP order, ties broken by pile
 * order); once backing is exhausted, bows at draw_idx are skipped by
 * pulling the next non-bow past them (the rotation keeps skipped bows'
 * relative order for when arrows are restocked).  Unfillable slots
 * stay EMPTY (never filled with phantom swords that would pollute the
 * discard pile and overflow MAX_DECK_SIZE on reshuffle).
 */
void battle_turn_draw_banked(void)
{
    Battle *b = (Battle *)g_bk_ptr_a;
    uint8_t i, need = 0;
    uint8_t backing;
    uint8_t pile_nb = 0;
    uint8_t pile_nbow = 0;
    uint8_t j;
    uint8_t best;
    uint8_t best_cost;

    g_bk_byte_c = 0;
    if (!b) return;
    for (i = 0; i < BATTLE_HAND_SIZE; i++) {
        if (b->hand[i].type == BATTLE_CARD_TYPE_EMPTY) need++;
    }
    if (need == 0) {
        g_bk_byte_c = 1;
        return;
    }
    /* Backing for new bows: live arrows minus bows already held. */
    backing = g_bk_byte_d;
    for (i = 0; i < BATTLE_HAND_SIZE; i++) {
        if (b->hand[i].type == BATTLE_CARD_TYPE_BOW) {
            if (backing > 0) backing--;
        }
    }
    /* Drawable pile cards: non-bows plus bows up to the backing. */
    for (j = b->deck.draw_idx; j < b->deck.count; j++) {
        if (b->deck.cards[j].type == BATTLE_CARD_TYPE_BOW) pile_nbow++;
        else pile_nb++;
    }
    if (pile_nbow > backing) pile_nbow = backing;
    if ((uint8_t)(pile_nb + pile_nbow) < need) {
        /* Short: reshuffle first when the discard can feed one, else
         * deal what is drawable and leave the remaining slots EMPTY. */
        if (b->deck.discard_count != 0) return;
    }
    for (i = 0; i < BATTLE_HAND_SIZE && need > 0; i++) {
        if (b->hand[i].type != BATTLE_CARD_TYPE_EMPTY) continue;
        j = b->deck.draw_idx;
        if (j >= b->deck.count) break;
        if (b->deck.cards[j].type == BATTLE_CARD_TYPE_BOW) {
            if (backing > 0) {
                best = j;
                best_cost = b->deck.cards[j].cost;
                for (j = (uint8_t)(j + 1); j < b->deck.count; j++) {
                    if (b->deck.cards[j].type == BATTLE_CARD_TYPE_BOW &&
                        b->deck.cards[j].cost < best_cost) {
                        best_cost = b->deck.cards[j].cost;
                        best = j;
                    }
                }
                turn_draw_take(b, i, best);
                backing--;
                need--;
                continue;
            }
            while (j < b->deck.count &&
                   b->deck.cards[j].type == BATTLE_CARD_TYPE_BOW) {
                j++;
            }
            if (j >= b->deck.count) break;
        }
        turn_draw_take(b, i, j);
        need--;
    }
    g_bk_byte_c = 1;
}
