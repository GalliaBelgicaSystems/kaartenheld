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
 * g_bk_byte_c.
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
 * only when the discard pile is also empty. */
void battle_turn_draw_banked(void)
{
    Battle *b = (Battle *)g_bk_ptr_a;
    Card *src;
    uint8_t i, need = 0;

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
    for (i = 0; i < BATTLE_HAND_SIZE && need > 0; i++) {
        if (b->hand[i].type == BATTLE_CARD_TYPE_EMPTY) {
            if (b->deck.draw_idx >= b->deck.count) {
                b->hand[i].type = BATTLE_CARD_TYPE_SWORD;
                b->hand[i].value = 2;
                b->hand[i].uses_remaining = 0xFF;
                b->hand[i].cost = 1;
                b->hand[i].effect = CARD_EFFECT_DAMAGE_TARGET;
                b->hand[i].status_id = STATUS_NONE;
                b->hand[i].status_chance = 0;
            } else {
                /* Hoisted out of the macro call: CARD_COPY_FIELDS
                 * names its `src` argument once per field, so passing
                 * `&cards[draw_idx++]` directly would advance the pile
                 * once PER FIELD (8 draws per card) and corrupt every
                 * later refill. */
                src = &b->deck.cards[b->deck.draw_idx];
                CARD_COPY_FIELDS(&b->hand[i], src);
                b->deck.draw_idx++;
            }
            need--;
        }
    }
    g_bk_byte_c = 1;
}
