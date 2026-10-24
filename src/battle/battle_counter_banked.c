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
 * Contract: byte_a MUST still hold the count when the fixed caller runs
 * the bank-3 eval dispatch next -- the eval consumes it as its input
 * count, so no staging write may touch byte_a in between (the caller
 * copies b/c/d to locals first for exactly this reason).
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
        /* Whitelist, not blacklist: only daggers and rings (HEAL) may
         * stand next to daggers.  A future weapon type must consciously
         * opt into counter play here instead of silently riding along
         * as ring-like.  EMPTY slots can never be selected, but they
         * abort too rather than passing as harmless.  Ring ⟺ HEAL type
         * holds both ways (battle_init_content: every HEAL card gets
         * ring=1, and every loot/curated ring is typed HEAL), so the
         * type check covers the flag. */
        if (t != BATTLE_CARD_TYPE_DAGGER && t != BATTLE_CARD_TYPE_HEAL) {
            g_bk_byte_a = 0;
            return;
        }
        if (t == BATTLE_CARD_TYPE_DAGGER) {
            /* Shared field-wise copy (CARD_COPY_FIELDS in card.h): no
             * struct assignment, so SDCC never lowers a RAM-resident
             * __memcpy call here (AGENTS.md §52.11.1,
             * tools/check_banked_copies.sh).  Pointers hoisted: the
             * macro would otherwise recompute the indexed address on
             * all eight field assignments. */
            if (n != i) {
                Card *d = &b->last_combo.cards[n];
                const Card *s = &b->last_combo.cards[i];
                CARD_COPY_FIELDS(d, s);
            }
            n++;
        }
    }
    if (n == 0) return;
    /* Stage the coming bank-3 eval dispatch's pointers here (banked
     * bytes are cheap; the same computations cost ~10 B each in the
     * fixed bank): ptr_a = compacted subset, ptr_b = result struct.
     * Like byte_a, these must survive untouched until the fixed caller
     * dispatches the eval -- it only reads byte_b/c/d and writes the
     * bank/target pair in between.  byte_b/byte_d keep their prep
     * meanings (attacker/rider staging) until the caller overwrites
     * them with the eval pack/mode. */
    g_bk_ptr_a = (void *)b->last_combo.cards;
    g_bk_ptr_b = (void *)&b->last_combo;
    g_bk_byte_a = n;
    g_bk_byte_b = atk;
    g_bk_byte_c = b->last_combo.cards[0].status_id;
    g_bk_byte_d = b->enemy_deck.count;
}
