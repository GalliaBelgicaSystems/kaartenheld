#pragma bank 5

#include <stdint.h>
#include <gb/gb.h>
#include <gb/cgb.h>
#include "tile_palette.h"
#include "world/world.h"
#include "ui/ui.h"
#include "banked.h"
#include "gfx/rpg_tile_lookup.h"

/* CGB palette tables + NPC display slots, generated from assets/palette.txt
 * + tools/palette_slots.json by tools/palette_compiler.py (make manifest).
 * Artist ramps win: these bytes are verbatim artist colors per slot. */
#include "cgb_palettes.inc"

/* Backward compatibility alias */
#define cgb_bg_palettes_overworld cgb_bg_palettes_forest

void ui_load_cram_banked(void)
{
    uint8_t p, c;
    const uint8_t *pal_data;
    if (!g_is_cgb) return;
    switch (g_bk_byte_a) {
        case WORLD_TILESET_DESOLATE:
            pal_data = (const uint8_t *)cgb_bg_palettes_desolate;
            break;
        case WORLD_TILESET_CASTLE:
            pal_data = (const uint8_t *)cgb_bg_palettes_castle;
            break;
        case WORLD_TILESET_VILLAGE:
            pal_data = (const uint8_t *)cgb_bg_palettes_village;
            break;
        case WORLD_TILESET_FOREST:
        case 1: /* Legacy overworld mode */
            pal_data = (const uint8_t *)cgb_bg_palettes_forest;
            break;
        case 0: /* Battle / UI mode */
        default:
            pal_data = (const uint8_t *)cgb_bg_palettes;
            break;
    }
    for (p = 0; p < 8; p++) {
        const uint8_t *ramp = pal_data + ((uint16_t)p << 3);
        BCPS_REG = (uint8_t)(0x80 | (p << 3));
        for (c = 0; c < 8; c++) {
            BCPD_REG = ramp[c];
        }
    }
}

/* World background tiles extracted from assets/ by tools/png2gb.py (make gfx) */

const uint8_t g_tileset_forest[768] = {
    /* 48 forest tiles (768 bytes): All wall, floor, stump, tree, and exit tiles */
#include "gfx/rpg_forest_world_tiles.inc"
};

const uint8_t g_tileset_desolate[768] = {
    /* 48 desolate landscape tiles (768 bytes): All wall, floor, rock, tree, and prop tiles */
#include "gfx/rpg_desolate_world_tiles.inc"
};

const uint8_t g_tileset_castle[256] = {
    /* 16 castle tiles (256 bytes), VRAM-slot order (see palette_compiler) */
#include "gfx/rpg_castle_tiles.inc"
};

const uint8_t g_tileset_village[768] = {
    /* 48 village tiles (768 bytes): Walls, dirt floors, houses, wells,
     * roofs, NPC art, and the stairs exit tile */
#include "gfx/rpg_village_world_tiles.inc"
};

const uint8_t g_intrepid_font_tiles[1536] = {
#include "gfx/intrepid_font_tiles.inc"
};

#ifdef DEBUG_BUILD
/* Title logo (assets/title-brun.png, 16x3 tiles, make gfx).  Kept in bank 5
 * for the harness build (its layout is regression-pinned: moving it out
 * flips a layout-sensitive SDCC miscompile in the dialogue path,
 * AGENTS.md 52.19).  The release build places it in bank 4 instead, where
 * there is room for the expanded content (see title_logo_content.c). */
const uint8_t g_title_logo_tiles[768] = {
#include "gfx/title_logo_tiles.inc"
};
#endif

void ui_load_tileset_banked(void)
{
    uint8_t tileset = g_bk_byte_a;
    const uint8_t *src;
    const uint8_t *pal_src;
    uint8_t tile_count;
    uint8_t i;
    uint16_t n;
    volatile uint8_t *dst;

    switch (tileset) {
        case WORLD_TILESET_FOREST:
            src = g_tileset_forest;
            pal_src = g_tile_pal_forest;
            tile_count = 48;
            break;
        case WORLD_TILESET_DESOLATE:
            src = g_tileset_desolate;
            pal_src = g_tile_pal_desolate;
            tile_count = 48;
            break;
        case WORLD_TILESET_CASTLE:
            src = g_tileset_castle;
            pal_src = g_tile_pal_castle;
            tile_count = 16;
            break;
        case WORLD_TILESET_VILLAGE:
            src = g_tileset_village;
            pal_src = g_tile_pal_village;
            tile_count = 48;
            break;
        default:
            src = g_tileset_forest;
            pal_src = g_tile_pal_forest;
            tile_count = 48;
            break;
    }

    VBK_REG = 0;
    dst = (volatile uint8_t *)(0x8000u + ((uint16_t)RPG_TILE_BASE_WORLD << 4));
    n = (uint16_t)tile_count << 4;
    while (n--) {
        *dst++ = *src++;
    }
    for (i = 0; i < tile_count; i++) {
        g_active_tile_palette[i] = pal_src[i];
    }
    for (; i < 48; i++) {
        g_active_tile_palette[i] = 0;
    }

    /* Town NPCs (guard, wizard, merchant, mayor) render as OAM sprites
     * with exact OBJ ramps (screens/enemy_types/npc_*.json): no overlay.
     * The village sheet's old NPC cells are plain ground. */
}

#ifdef DEBUG_BUILD
/* Bank-5 title-logo loader for the fixed-bank title wrapper (harness
 * build).  The release build's copy lives in title_logo_content.c. */
void ui_title_logo_load_banked(void)
{
    const uint8_t *src = g_title_logo_tiles;
    volatile uint8_t *dst =
        (volatile uint8_t *)(0x8000u + ((uint16_t)RPG_TILE_BASE_WORLD << 4));
    uint16_t n = 768u;

    VBK_REG = 0;
    while (n--) {
        *dst++ = *src++;
    }
}
#endif