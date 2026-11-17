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

/* Default-sword fill for the covered case (see below): one copy of the
 * phantom fallback deck_draw() mints, so the three call sites share it. */
static void turn_fill_default_sword(Battle *b, uint8_t hand_idx)
{
    b->hand[hand_idx].type = BATTLE_CARD_TYPE_SWORD;
    b->hand[hand_idx].value = 2;
    b->hand[hand_idx].uses_remaining = 0xFF;
    b->hand[hand_idx].cost = 1;
    b->hand[hand_idx].effect = CARD_EFFECT_DAMAGE_TARGET;
    b->hand[hand_idx].status_id = STATUS_NONE;
    b->hand[hand_idx].status_chance = 0;
    b->hand[hand_idx].ring = 0;
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
 * so the caller reshuffles first; the fully-dry case reports covered
 * only when the discard pile is also empty (remaining slots take the
 * default sword, exactly like the fixed deck_draw() fallback in
 * docs/deck.md).  Drawable counts non-bows plus bows up to the arrow
 * backing -- bows without backing are skipped, never drawn empty, so
 * the raw pile size alone must not gate the check.
 *
 * Limited-arrows draw (never-draw-empty): bows are drawn only while
 * backing lasts (arrows minus bows already in hand).  Pass 1 pulls
 * past bows to the next non-bow in pile order; pass 2 then takes the
 * cheapest pile bows first (AP order) up to the remaining backing.
 * Skipped bows stay in the pile (the take rotates the segment so
 * their relative order survives) and become drawable again once
 * arrows are restocked.
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
         * the covered case (deal what is drawable, sword-fill the
         * rest, report covered). */
        if (b->deck.discard_count != 0) return;
    }
    /* Pass 1: non-bows in pile order, pulling past bows. */
    for (i = 0; i < BATTLE_HAND_SIZE && need > 0; i++) {
        if (b->hand[i].type != BATTLE_CARD_TYPE_EMPTY) continue;
        j = b->deck.draw_idx;
        while (j < b->deck.count &&
               b->deck.cards[j].type == BATTLE_CARD_TYPE_BOW) {
            j++;
        }
        if (j >= b->deck.count) continue;
        turn_draw_take(b, i, j);
        need--;
    }
    /* Pass 2: cheapest pile bows first, up to the backing left. */
    for (i = 0; i < BATTLE_HAND_SIZE && need > 0 && backing > 0; i++) {
        if (b->hand[i].type != BATTLE_CARD_TYPE_EMPTY) continue;
        best = b->deck.count;
        best_cost = 0xFF;
        for (j = b->deck.draw_idx; j < b->deck.count; j++) {
            if (b->deck.cards[j].type != BATTLE_CARD_TYPE_BOW) continue;
            if (b->deck.cards[j].cost < best_cost) {
                best_cost = b->deck.cards[j].cost;
                best = j;
            }
        }
        if (best >= b->deck.count) break;
        turn_draw_take(b, i, best);
        backing--;
        need--;
    }
    /* Covered case only (short pile + empty discard, per the check
     * above): sword-fill whatever is still open.  A short pile with a
     * non-empty discard returned dry before dealing anything. */
    if (need > 0 && b->deck.discard_count == 0) {
        for (i = 0; i < BATTLE_HAND_SIZE && need > 0; i++) {
            if (b->hand[i].type != BATTLE_CARD_TYPE_EMPTY) continue;
            turn_fill_default_sword(b, i);
            need--;
        }
    }
    g_bk_byte_c = 1;
}
