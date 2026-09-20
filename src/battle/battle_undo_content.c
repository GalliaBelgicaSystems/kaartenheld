#pragma bank 2

#include "battle.h"
#include "banked.h"

/* Banked body of battle_card_undo (src/battle/battle.c, ROM bank 2).
 * Lives here (not bank 3: that room belongs to the rider HUD body).
 * Self-contained: reads/writes only Battle struct fields (WRAM)
 * and calls no fixed-bank functions.  For the flee path, it sets
 * the result fields directly; the fixed-bank wrapper emits telemetry
 * after the call returns. */

void battle_card_undo_banked(void)
{
    Battle *b = (Battle *)g_bk_ptr_a;
    if (!b) return;
    g_bk_byte_c = 0;
    if (b->phase != BATTLE_PHASE_PLAYER_SELECT && b->phase != BATTLE_PHASE_PLAYER_DEFEND) {
        return;
    }

    if (b->combo_count > 0) {
        b->cursor_pos = b->selected_indices[--b->combo_count];
        /* BATTLE_DIRTY_HERO: the deck/AP line (row 7) shows the un-reserved
         * energy, so unselecting a card must redraw it too, or the AP badge
         * stays stale while the combo is being built. */
        b->dirty |= (BATTLE_DIRTY_COMBO | BATTLE_DIRTY_HAND | BATTLE_DIRTY_DESC | BATTLE_DIRTY_HERO);
        /* Report an actual unselect so the fixed wrapper plays the back
         * bloup only when a card left the combo (not on flee). */
        g_bk_byte_c = 1;
    } else if (b->phase == BATTLE_PHASE_PLAYER_SELECT) {
        g_bk_byte_c = 0;
        b->result = BATTLE_RESULT_FLED;
        b->phase = BATTLE_PHASE_RESULT;
        b->turn = BATTLE_TURN_RESULT;
        b->battle_over = true;
        b->dirty = BATTLE_DIRTY_ALL;
    }
}
