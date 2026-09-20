#pragma bank 3

#include <stdint.h>
#include <gb/gb.h>
#include "battle.h"
#include "banked.h"
#include "rpg/status.h"
#include "rider_tiles_generated.h"

/* Selected-card element riders (top-right OAM HUD, bank-3 local).
 * One sprite per element present anywhere in the pending combo (deduped:
 * at most fire/ice/poison, so three fixed entries).  Row 0 cols 17-19
 * (banner-row margins the banner text never reaches).  Tiles/slots are
 * compile-time constants (rider_tiles_generated.h: blob offsets +
 * slotted OBJ ramps): no staging, no cross-bank reads, no new WRAM.
 * Hidden (y = 0, tile cleared for deterministic harness reads) when the
 * element is absent; the combo reset on resolve clears them.  Entries
 * 19-21: the enemy strides own 1-18 and nothing else writes OAM during
 * battle.  No multiply/div/mod (AGENTS.md 52.18): running counters only.
 *
 * Called directly (plain C call, no trampoline dispatch) from
 * ui_update_battle_banked() at the end of every battle render -- same
 * bank, same gating as the BG stamper, so riders and boxes can never
 * desync.  This placement also keeps the fixed bank under 0x8000: a
 * third trampoline dispatch out of ui_update_battle() overflows the
 * debug fixed bank (AGENTS.md 52.18).  The undo body moved to bank 2 to
 * make room here (its dispatch retargeted, same size). */
void battle_rider_draw(const volatile Battle *battle)
{
    uint8_t rk, hi, st;
    uint8_t seen_burn = 0;
    uint8_t seen_freeze = 0;
    uint8_t seen_poison = 0;
    volatile uint8_t *re;

    if (!battle) return;
    for (rk = 0; rk < battle->combo_count; rk++) {
        hi = battle->selected_indices[rk];
        if (hi >= BATTLE_HAND_SIZE) continue;
        st = battle->hand[hi].status_id;
        if (st == STATUS_BURN) seen_burn = 1;
        else if (st == STATUS_FREEZE) seen_freeze = 1;
        else if (st == STATUS_POISON) seen_poison = 1;
    }
    re = (volatile uint8_t *)(0xC000u + ((uint16_t)19 << 2));
    if (seen_burn) {
        re[0] = 16; re[1] = 144; re[2] = RIDER_TILE_BURN; re[3] = RIDER_OBJ_BURN;
    } else {
        re[0] = 0; re[2] = 0;
    }
    re = (volatile uint8_t *)(0xC000u + ((uint16_t)20 << 2));
    if (seen_freeze) {
        re[0] = 16; re[1] = 152; re[2] = RIDER_TILE_FREEZE; re[3] = RIDER_OBJ_FREEZE;
    } else {
        re[0] = 0; re[2] = 0;
    }
    re = (volatile uint8_t *)(0xC000u + ((uint16_t)21 << 2));
    if (seen_poison) {
        re[0] = 16; re[1] = 160; re[2] = RIDER_TILE_POISON; re[3] = RIDER_OBJ_POISON;
    } else {
        re[0] = 0; re[2] = 0;
    }
}
