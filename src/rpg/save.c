#include "save.h"
#include "rpg/deck.h"
#include "banked.h"

/* Slot capacity mirror (WRAM): the bank-3 guard reads this instead of a
 * compile-time constant, which SDCC would constant-fold away. */
const uint16_t g_save_state_capacity =
    (uint16_t)(SAVE_SLOT_STRIDE - SAVE_SRAM_STATE_OFF);

/* Fixed-bank dispatchers for the bank-8 SRAM bodies
 * (src/rpg/save_banked.c), keeping the checksum/copy loops out of the
 * fixed-bank budget.  Results report through g_save_ok (shared WRAM
 * byte, written by the banked body).  See banked.h for the ABI. */

extern uint8_t g_save_ok;

static bool save_dispatch(uint8_t op, uint8_t slot, const GameState *state)
{
    g_bk_call_bank = 8;
    g_bk_call_target = (uint16_t)&save_op_banked;
    g_bk_ptr_a = (void *)state;
    g_bk_byte_a = slot;
    g_bk_byte_b = op;
    banked_call_run();
    return g_save_ok != 0;
}

void save_claim_active(void)
{
    g_bk_call_bank = 12;
    g_bk_call_target = (uint16_t)&save_commit_active_banked;
    banked_call_run();
}

bool save_game_slot(uint8_t slot, const GameState *state)
{
    return state ? save_dispatch(1, slot, state) : false;
}

bool load_game_slot(uint8_t slot, GameState *state)
{
    bool ok;
    /* Re-arm hook for state that the save does not cover: the chest
     * jingle edge (bank-8 BSS) keys off live loot flags, so a save
     * loaded mid-session with chests already open would replay on the
     * first overworld frame.  Clearing re-seeds from the live mask on
     * the next call (see jingle_full_banked).  Real saves and the
     * harness debug-load action share this path. */
    extern uint8_t g_jingle_done;
    if (!state) return false;
    ok = save_dispatch(2, slot, state);
    if (ok) {
        g_jingle_done = 0;
        /* Saves written before one-row-per-card can hold a curated card
         * and its loot twin as two collection rows: fold them so the
         * DECK menu lists the card once (and the deck cannot take a copy
         * from each row).  A capped fold also evicts decked copies above
         * the folded identity's cap (pre-fix saves may hold twins decked
         * past it; unreachable through current gameplay, where the gate
         * refuses the over-cap add). */
        deck_collection_merge_duplicates(&state->cards);
    }
    return ok;
}
