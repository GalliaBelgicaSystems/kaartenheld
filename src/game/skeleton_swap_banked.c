#pragma bank 11

#include <stdint.h>
#include <gb/gb.h>
#include "banked.h"
#include "game.h"
#include "game_ids.h"
#include "ui/ui.h"
#include "world/world.h"
#include "gfx/rpg_tile_lookup.h"

/* Tunnel skeleton bone art (ROM bank 11: banks 5 and 8 are full in the
 * debug/release links).  With BONE_TAKEN set on an underground tileset,
 * VRAM slot 30
 * (skeleton with bone) is repainted with the slot-31 no-bone art.  Both
 * tiles share TILE_UNDERGROUND_1E = 254 -- the u8 TileType space is
 * spent (world.h) -- so the map keeps value 254 either way and only
 * the slot art flips.  VRAM-to-VRAM through BANKED_VRAM_READ/WRITE
 * (LCD-on safe, §52.11.3): the grant fires under a dialogue overlay,
 * loads repaint with the LCD on on gate crossings.  Idempotent:
 * pre-bone or a non-underground tileset is a no-op, so the loader
 * wrapper and the bone grant path share it.  Flag bit inline (a banked
 * body must never call the fixed-bank game_flag_is_set): flag N lives
 * in bit (N-1)%8 of flags.bytes[(N-1)/8]; BONE_TAKEN is 10. */
void skeleton_swap_banked(void)
{
    volatile uint8_t *dst;
    volatile uint8_t *src;
    uint8_t i;
    uint8_t v;

    if (g_loaded_tileset != (int8_t)WORLD_TILESET_UNDERGROUND) return;
    if (!(g_game.state.flags.bytes[1] & 0x02)) return;

    dst = (volatile uint8_t *)(0x8000u +
        ((uint16_t)(RPG_TILE_BASE_WORLD + 30) << 4));
    src = (volatile uint8_t *)(0x8000u +
        ((uint16_t)(RPG_TILE_BASE_WORLD + 31) << 4));
    for (i = 0; i < 16; i++) {
        BANKED_VRAM_READ(v, src);
        src++;
        BANKED_VRAM_WRITE(dst, v);
        dst++;
    }
}
