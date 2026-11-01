#pragma bank 4
#pragma disable_warning 110

#include "battle.h"
#include "banked.h"

/* ── Banked combo-staging body (ROM bank 4) ──────────────────────────
 * Hosts battle_play_hand()'s hand -> last_combo copy loop out of the
 * fixed bank (which sits hard against 0x8000, AGENTS.md §52.18).  The
 * fixed wrapper keeps the signature and stages the Battle* through the
 * WRAM trampoline; nothing is reported back (the staged combo is the
 * result).
 *
 * Self-contained (AGENTS.md §52.11.1): reads/writes only the staged
 * Battle* (hand + selected_indices + last_combo, all WRAM), never calls
 * fixed-bank functions.  Field-wise copies only (no struct assignment:
 * SDCC lowers those to the RAM-resident __memcpy helper, unreachable
 * while a bank is mapped). */
void battle_stage_combo_banked(void)
{
    Battle *b = (Battle *)g_bk_ptr_a;
    uint8_t i;

    if (!b) return;
    for (i = 0; i < b->combo_count; i++) {
        CARD_COPY_FIELDS(&b->last_combo.cards[i],
                         &b->hand[b->selected_indices[i]]);
    }
}
