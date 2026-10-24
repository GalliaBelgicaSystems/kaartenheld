#pragma bank 5

#include "battle.h"
#include "banked.h"

/* ── Banked dagger-counter prep (ROM bank 5, unconditionally) ────────
 * The defend body (battle_defend_content.c) splits bank 4 (release) /
 * bank 5 (debug) because the debug bank 4 is test-content-full; this
 * prep instead lives in bank 5 in BOTH builds (room in each), so the
 * fixed dispatch needs no bank ifdef.  When the pending defend selection
 * is daggers-only (at least one dagger; no shields, swords or bows;
 * rings allowed) AND the hero survived the swing AND the telegraphed
 * attacker still stands, compacts the dagger subset to the front of
 * last_combo.cards in selection order and stages:
 *   g_bk_byte_a = dagger count (0 = no counter),
 *   g_bk_byte_b = attacker index (clamped),
 *   g_bk_byte_c = leading dagger's on-hit status id (0 = none),
 *   g_bk_byte_d = shared enemy-deck count (poison grey-out width).
 * Otherwise stages 0 in byte_a and leaves everything else untouched.
 *
 * Runs AFTER battle_defend_resolve_banked in the same defend flow, so
 * last_combo.cards is free scratch: the block resolution already
 * consumed it (ring scan + block sum), and the coming discard reads
 * hand/selected_indices.  Partial compaction on a mixed abort is
 * therefore harmless.  Self-contained: WRAM only, no fixed-bank calls
 * (AGENTS.md §52.11.1).  Single pass: writes land at n <= i, always at
 * or below the slot being read.  The wide staging keeps the fixed caller
 * off the Battle struct (every b->field access costs ~10 fixed-bank
 * bytes under SDCC's 16-bit struct addressing, AGENTS.md 52.18). */
void battle_counter_prep_banked(void)
{
    Battle *b = (Battle *)g_bk_ptr_a;
    uint8_t i, n, atk;
    g_bk_byte_a = 0;
    if (!b || b->player.hp == 0 || b->combo_count == 0) return;
    atk = (b->attacking_enemy_idx < b->enemy_count) ? b->attacking_enemy_idx : 0;
    if (b->enemies[atk].hp == 0) return;
    n = 0;
    for (i = 0; i < b->combo_count; i++) {
        uint8_t t = b->last_combo.cards[i].type;
        if (t == BATTLE_CARD_TYPE_SHIELD || t == BATTLE_CARD_TYPE_SWORD ||
            t == BATTLE_CARD_TYPE_BOW) {
            g_bk_byte_a = 0;
            return;
        }
        if (t == BATTLE_CARD_TYPE_DAGGER) {
            /* Field-wise copy (no struct assignment: SDCC lowers
             * Card-sized copies to __memcpy, unreachable while a
             * switchable bank is mapped -- same rule as
             * card_copy_banked in deck_shuffle_banked.c, enforced by
             * tools/check_banked_copies.sh). */
            if (n != i) {
                b->last_combo.cards[n].type = b->last_combo.cards[i].type;
                b->last_combo.cards[n].value = b->last_combo.cards[i].value;
                b->last_combo.cards[n].uses_remaining = b->last_combo.cards[i].uses_remaining;
                b->last_combo.cards[n].cost = b->last_combo.cards[i].cost;
                b->last_combo.cards[n].effect = b->last_combo.cards[i].effect;
                b->last_combo.cards[n].status_id = b->last_combo.cards[i].status_id;
                b->last_combo.cards[n].status_chance = b->last_combo.cards[i].status_chance;
                b->last_combo.cards[n].ring = b->last_combo.cards[i].ring;
            }
            n++;
        }
    }
    if (n == 0) return;
    g_bk_byte_a = n;
    g_bk_byte_b = atk;
    g_bk_byte_c = b->last_combo.cards[0].status_id;
    g_bk_byte_d = b->enemy_deck.count;
}
