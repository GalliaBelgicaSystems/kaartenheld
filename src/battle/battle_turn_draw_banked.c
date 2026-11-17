#pragma bank 4
#pragma disable_warning 110

#include "battle.h"
#include "banked.h"
#include "rpg/status.h"
#include "rpg/cards.h"

/* ── Banked hand-refill body (ROM bank 4) ────────────────────────────
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
 * A dry pile mid-refill mints the phantom sword, exactly like the
 * fixed deck_draw() fallback (docs/deck.md): exiled finite-use cards
 * can shrink the live total below the open slots, so the pile can run
 * out even when it covered `need` at entry.  Without this the draw
 * index would overrun count and the next reshuffle would underflow
 * its remainder into a wild copy (WRAM smash, hung battle).
 * Field-wise copies only (no struct assignment: SDCC lowers those to
 * the RAM-resident __memcpy helper, unreachable while a bank is
 * mapped).
 *
 * Decision contract (unchanged from the fixed version): atomic -- a
 * partial pile plus a non-empty discard deals NOTHING and reports dry
 * so the caller reshuffles first; the fully-dry case reports covered
 * only when the discard pile is also empty.
 *
 * Limited-arrows draw (never-draw-empty): bows are drawn only while
 * backing lasts (arrows minus bows already in hand).  Pass 1 pulls
 * past bows to the next non-bow in pile order; pass 2 then takes the
 * cheapest pile bows first (AP order) up to the remaining backing.
 * Skipped bows stay in the pile (the take rotates the segment so
 * their relative order survives) and become drawable again once
 * arrows are restocked.  Slots that cannot be filled stay EMPTY. */
static void turn_draw_take(Battle *b, uint8_t hand_idx, uint8_t pile_idx)
{
    Card tmp;
    uint8_t k;
    /* Rotate pile[draw_idx..pile_idx] so the chosen card lands on top. */
    CARD_COPY_FIELDS(&tmp, &b->deck.cards[pile_idx]);
    k = pile_idx;
    while (k > b->deck.draw_idx) {
        CARD_COPY_FIELDS(&b->deck.cards[k], &b->deck.cards[(uint8_t)(k - 1)]);
        k--;
    }
    CARD_COPY_FIELDS(&b->deck.cards[b->deck.draw_idx], &tmp);
    /* Hoisted out of the macro call: CARD_COPY_FIELDS names its `src`
     * argument once per field, so passing `&cards[draw_idx++]`
     * directly would advance the pile once PER FIELD (8 draws per
     * card) and corrupt every later refill. */
    CARD_COPY_FIELDS(&b->hand[hand_idx], &b->deck.cards[b->deck.draw_idx]);
    b->deck.draw_idx++;
}

void battle_turn_draw_banked(void)
{
    Battle *b = (Battle *)g_bk_ptr_a;
    uint8_t i, need = 0;
    uint8_t backing;
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
    if (b->deck.draw_idx >= b->deck.count) {
        if (b->deck.discard_count == 0) g_bk_byte_c = 1;
        return;
    }
    if (b->deck.discard_count != 0 &&
        (uint8_t)(b->deck.draw_idx + need) > b->deck.count) {
        return;
    }
    /* Backing for new bows: live arrows minus bows already held. */
    backing = g_bk_byte_d;
    for (i = 0; i < BATTLE_HAND_SIZE; i++) {
        if (b->hand[i].type == BATTLE_CARD_TYPE_BOW) {
            if (backing > 0) backing--;
        }
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
        if (b->deck.draw_idx >= b->deck.count) {
            b->hand[i].type = BATTLE_CARD_TYPE_SWORD;
            b->hand[i].value = 2;
            b->hand[i].uses_remaining = 0xFF;
            b->hand[i].cost = 1;
            b->hand[i].effect = CARD_EFFECT_DAMAGE_TARGET;
            b->hand[i].status_id = STATUS_NONE;
            b->hand[i].status_chance = 0;
            b->hand[i].ring = 0;
        } else {
            turn_draw_take(b, i, j);
        }
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
        if (b->deck.draw_idx >= b->deck.count) {
            b->hand[i].type = BATTLE_CARD_TYPE_SWORD;
            b->hand[i].value = 2;
            b->hand[i].uses_remaining = 0xFF;
            b->hand[i].cost = 1;
            b->hand[i].effect = CARD_EFFECT_DAMAGE_TARGET;
            b->hand[i].status_id = STATUS_NONE;
            b->hand[i].status_chance = 0;
            b->hand[i].ring = 0;
        } else {
            turn_draw_take(b, i, best);
            backing--;
        }
        need--;
    }
    g_bk_byte_c = 1;
}
