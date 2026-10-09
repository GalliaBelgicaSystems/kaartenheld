#pragma bank 3

#include <stdint.h>
#include <gb/gb.h>
#include "battle.h"
#include "battle_data.h"
#include "banked.h"
#include "rpg/status.h"
#include "rpg/cards.h"
#include "rider_tiles_generated.h"

/* Per-card element riders (bank-3 local).  Every hand card carrying a
 * rider (STATUS_POISON/BURN/FREEZE) shows its icon as an OAM sprite inset
 * over its own top-right frame corner -- from the moment the card is
 * dealt, whether selected or not.  Hand slot i owns OAM entry 19+i
 * (entries 19-23: the enemy strides own 1-18 and nothing else writes OAM
 * during battle).  Tiles/slots are compile-time constants
 * (rider_tiles_generated.h: blob offsets + slotted OBJ ramps): no
 * staging, no cross-bank reads, no new WRAM.  Riderless/empty slots are
 * hidden (y = 0, tile cleared for deterministic harness reads).  No
 * multiply/div/mod (AGENTS.md 52.18): shifts and adds only.
 *
 * Loot reveal (msg_id 4) clears the hand boxes, so the hand loop would
 * paint stale riders over it: in loot mode entries 20-23 hide and entry
 * 19 carries the revealed card's rider (if any) over the row-11 icon
 * cell, mirroring the BG layout in ui_update_battle_banked below.
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
    uint8_t cards_row = g_battle_hud.cards_row;
    uint8_t bh = g_card_skin_wram.box_h;
    uint8_t top;
    uint8_t i;
    uint8_t ctype, cstat;
    uint8_t tile, slot;
    uint8_t col;
    uint8_t len, block, lx;
    volatile uint8_t *re;

    if (!battle) return;
    if (bh < 3 || bh > 5) bh = 4;
    top = (uint8_t)(cards_row - (bh - 1));

    if (battle->msg_id == 4) {
        /* Loot reveal: hand zone cleared; show only the revealed card's
         * rider over the row-11 elem cell (same x math as the BG row). */
        for (i = 1; i < BATTLE_HAND_SIZE; i++) {
            re = (volatile uint8_t *)(0xC000u + ((uint16_t)(19 + i) << 2));
            re[0] = 0;
            re[2] = 0;
        }
        tile = 0;
        slot = 0;
        cstat = g_card_scratch.status_id;
        if (cstat == STATUS_BURN) {
            tile = RIDER_TILE_BURN;
            slot = RIDER_OBJ_BURN;
        } else if (cstat == STATUS_FREEZE) {
            tile = RIDER_TILE_FREEZE;
            slot = RIDER_OBJ_FREEZE;
        } else if (cstat == STATUS_POISON) {
            tile = RIDER_TILE_POISON;
            slot = RIDER_OBJ_POISON;
        }
        re = (volatile uint8_t *)(0xC000u + ((uint16_t)19 << 2));
        if (tile) {
            len = 0;
            while (len < 20 && g_card_scratch.name[len]) len++;
            block = (uint8_t)(3 + len);
            lx = (uint8_t)((20 - block) / 2);
            re[0] = (uint8_t)((11 << 3) + 16);
            re[1] = (uint8_t)((lx << 3) + 8);
            re[2] = tile;
            re[3] = slot;
        } else {
            re[0] = 0;
            re[2] = 0;
        }
        return;
    }

    for (i = 0; i < BATTLE_HAND_SIZE; i++) {
        ctype = battle->hand[i].type;
        cstat = battle->hand[i].status_id;
        tile = 0;
        slot = 0;
        if (ctype != BATTLE_CARD_TYPE_EMPTY) {
            if (cstat == STATUS_BURN) {
                tile = RIDER_TILE_BURN;
                slot = RIDER_OBJ_BURN;
            } else if (cstat == STATUS_FREEZE) {
                tile = RIDER_TILE_FREEZE;
                slot = RIDER_OBJ_FREEZE;
            } else if (cstat == STATUS_POISON) {
                tile = RIDER_TILE_POISON;
                slot = RIDER_OBJ_POISON;
            }
        }
        col = (uint8_t)(i << 2);
        re = (volatile uint8_t *)(0xC000u + ((uint16_t)(19 + i) << 2));
        if (tile) {
            re[0] = (uint8_t)((top << 3) + 16);
            re[1] = (uint8_t)(((col + 2) << 3) + 8);
            re[2] = tile;
            re[3] = slot;
        } else {
            re[0] = 0;
            re[2] = 0;
        }
    }
}
