#pragma bank 5

#include <stdint.h>
#include <gb/gb.h>
#include <gb/cgb.h>
#include "tile_palette.h"
#include "world/world.h"
#include "ui/ui.h"
#include "banked.h"
#include "gfx/rpg_tile_lookup.h"

/* 8 CGB BG palettes, 4 colors each in RGB555 format.
 * Every value is resolved from assets/palette.txt via tools/palette_txt.py
 * (`make palette-check` enforces parity). Names in comments are the
 * palette.txt ramp names pinned by SLOTS/*; `unused` slots hold a magenta
 * canary, `dup X` slots duplicate a real ramp until the artist authors a
 * real one (both fail loudly in palette-check when consumed). */
const palette_color_t cgb_bg_palettes[8][4] = {
    /* 0 fight_card */ { RGB8(255,255,255), RGB8(223,189,141), RGB8(227,174,99), RGB8(176,146,102) },
    /* 1 fight_more */ { RGB8(255,255,255), RGB8(195,78,27), RGB8(132,79,79), RGB8(74,39,39) },
    /* 2 fight_again */ { RGB8(255,255,255), RGB8(143,143,143), RGB8(86,86,86), RGB8(90,74,61) },
    /* 3 fight_standard (DEV: slot 1 was ice blue -> slime green; FLORENT: revert if ice intended) */ { RGB8(255,255,255), RGB8(137,204,94), RGB8(92,144,58), RGB8(92,144,58) },
    /* 4 fight_forever */ { RGB8(255,255,255), RGB8(113,54,193), RGB8(86,86,86), RGB8(61,48,68) },
    /* 5 fight_still */ { RGB8(255,255,255), RGB8(141,117,74), RGB8(117,89,48), RGB8(111,90,52) },
    /* 6 dup fight_card (gold/bow MISSING) */ { RGB8(255,255,255), RGB8(223,189,141), RGB8(227,174,99), RGB8(176,146,102) },
    /* 7 fight_final */ { RGB8(255,255,255), RGB8(63,48,23), RGB8(0,0,0), RGB8(0,0,0) },
};

/* Forest / Field overworld palette set. */
const palette_color_t cgb_bg_palettes_forest[8][4] = {
    /* 0 unused */ { RGB8(255,0,255), RGB8(255,0,255), RGB8(255,0,255), RGB8(255,0,255) },
    /* 1 field_more (UNUSED slot 3 repeats grass, darkest by luminance) */ { RGB8(123,182,96), RGB8(237,194,20), RGB8(215,167,38), RGB8(123,182,96) },
    /* 2 unused */ { RGB8(255,0,255), RGB8(255,0,255), RGB8(255,0,255), RGB8(255,0,255) },
    /* 3 field */ { RGB8(123,182,96), RGB8(42,79,26), RGB8(29,62,15), RGB8(0,0,0) },
    /* 4 unused */ { RGB8(255,0,255), RGB8(255,0,255), RGB8(255,0,255), RGB8(255,0,255) },
    /* 5 field_too (stump browns as wood) */ { RGB8(123,182,96), RGB8(147,123,74), RGB8(97,78,39), RGB8(97,78,39) },
    /* 6 unused */ { RGB8(255,0,255), RGB8(255,0,255), RGB8(255,0,255), RGB8(255,0,255) },
    /* 7 field_again (rocks) */ { RGB8(123,182,96), RGB8(169,159,192), RGB8(74,59,28), RGB8(38,35,46) },
};

/* Desolate landscape palette set. */
const palette_color_t cgb_bg_palettes_desolate[8][4] = {
    /* 0 unused */ { RGB8(255,0,255), RGB8(255,0,255), RGB8(255,0,255), RGB8(255,0,255) },
    /* 1 Underworld_light (campfire; UNUSED slot 3 repeats ground) */ { RGB8(147,141,161), RGB8(237,194,20), RGB8(215,167,38), RGB8(147,141,161) },
    /* 2 unused */ { RGB8(255,0,255), RGB8(255,0,255), RGB8(255,0,255), RGB8(255,0,255) },
    /* 3 unused */ { RGB8(255,0,255), RGB8(255,0,255), RGB8(255,0,255), RGB8(255,0,255) },
    /* 4 unused */ { RGB8(255,0,255), RGB8(255,0,255), RGB8(255,0,255), RGB8(255,0,255) },
    /* 5 unused */ { RGB8(255,0,255), RGB8(255,0,255), RGB8(255,0,255), RGB8(255,0,255) },
    /* 6 unused */ { RGB8(255,0,255), RGB8(255,0,255), RGB8(255,0,255), RGB8(255,0,255) },
    /* 7 underworld (slate rock) */ { RGB8(147,141,161), RGB8(169,159,192), RGB8(131,123,150), RGB8(63,58,74) },
};

/* Castle palette set. */
const palette_color_t cgb_bg_palettes_castle[8][4] = {
    /* 0 castle (stone) */ { RGB8(215,215,215), RGB8(130,130,130), RGB8(86,86,86), RGB8(46,46,46) },
    /* 1 castle_room (curtains) */ { RGB8(215,215,215), RGB8(179,176,176), RGB8(139,27,27), RGB8(98,18,18) },
    /* 2 unused */ { RGB8(255,0,255), RGB8(255,0,255), RGB8(255,0,255), RGB8(255,0,255) },
    /* 3 unused */ { RGB8(255,0,255), RGB8(255,0,255), RGB8(255,0,255), RGB8(255,0,255) },
    /* 4 unused */ { RGB8(255,0,255), RGB8(255,0,255), RGB8(255,0,255), RGB8(255,0,255) },
    /* 5 unused */ { RGB8(255,0,255), RGB8(255,0,255), RGB8(255,0,255), RGB8(255,0,255) },
    /* 6 castle_hall (gold) */ { RGB8(215,215,215), RGB8(215,167,38), RGB8(141,117,74), RGB8(111,90,52) },
    /* 7 unused */ { RGB8(255,0,255), RGB8(255,0,255), RGB8(255,0,255), RGB8(255,0,255) },
};

/* Village palette set. */
const palette_color_t cgb_bg_palettes_village[8][4] = {
    /* 0 unused */ { RGB8(255,0,255), RGB8(255,0,255), RGB8(255,0,255), RGB8(255,0,255) },
    /* 1 city (braziers) */ { RGB8(182,162,126), RGB8(237,194,20), RGB8(215,167,38), RGB8(38,35,46) },
    /* 2 unused */ { RGB8(255,0,255), RGB8(255,0,255), RGB8(255,0,255), RGB8(255,0,255) },
    /* 3 dup city (dirt_floor MISSING) */ { RGB8(182,162,126), RGB8(237,194,20), RGB8(215,167,38), RGB8(38,35,46) },
    /* 4 unused */ { RGB8(255,0,255), RGB8(255,0,255), RGB8(255,0,255), RGB8(255,0,255) },
    /* 5 village (wood) */ { RGB8(182,162,126), RGB8(141,117,74), RGB8(121,100,62), RGB8(100,82,51) },
    /* 6 town (cream) */ { RGB8(182,162,126), RGB8(241,207,145), RGB8(182,182,182), RGB8(204,170,108) },
    /* 7 unused */ { RGB8(255,0,255), RGB8(255,0,255), RGB8(255,0,255), RGB8(255,0,255) },
};

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

const uint8_t g_tileset_castle[432] = {
    /* 27 castle tiles (432 bytes) */
#include "gfx/rpg_castle_tiles.inc"
};

const uint8_t g_tileset_village[768] = {
    /* 48 village tiles (768 bytes): Walls, dirt floors, houses, wells,
     * roofs, NPC art, and the stairs exit tile */
#include "gfx/rpg_village_world_tiles.inc"
};

/* NPC map art, sourced from the shared actors tileset (assets/
 * actor-sprites.png via tools/compose_npc_tiles.py).  The village
 * sheet's NPC cells were blanked when the art moved to the actors
 * tileset (assets/tilesets.md); ui_load_tileset_banked() overlays these
 * tiles into the village VRAM block slots the maps still reference.
 * Order mirrors compose_npc_tiles.py LAYOUT and the npc_slots[] table
 * below: guard, wizard, merchant, mayor, dog frame 1, dog frame 2. */
const uint8_t g_actor_npc_tiles[96] = {
#include "gfx/rpg_actor_npc_tiles.inc"
};

const uint8_t g_intrepid_font_tiles[1536] = {
#include "gfx/intrepid_font_tiles.inc"
};

#ifdef DEBUG_BUILD
/* Title logo (assets/title-red.png, 16x3 tiles, make gfx).  Kept in bank 5
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

    if (tileset == WORLD_TILESET_VILLAGE) {
        /* NPC map art moved to the shared actors tileset, so the village
         * sheet's NPC cells are blank.  Overlay the actor-sourced tiles
         * into the village block slots the maps reference.  Slot order
         * must match g_actor_npc_tiles (compose_npc_tiles.py LAYOUT):
         * guard, wizard, merchant, mayor, dog frame 1, dog frame 2. */
        static const uint8_t npc_slots[6] = { 35, 36, 39, 41, 4, 5 };
        /* CGB palette per NPC (UI_COLOR_* indices; the auto palette
         * manifest cannot know these slots are NPC overlays): guard,
         * wizard and dogs field green (3), merchant wood (5), mayor gold
         * (6).  Order mirrors npc_slots (compose_npc_tiles.py LAYOUT). */
        static const uint8_t npc_pals[6] = { 3, 3, 5, 6, 3, 3 };
        uint8_t s;
        uint8_t j;
        const uint8_t *tile_src;
        volatile uint8_t *tile_dst;

        for (s = 0; s < 6; s++) {
            tile_src = &g_actor_npc_tiles[(uint16_t)s << 4];
            tile_dst = (volatile uint8_t *)(
                0x8000u + ((uint16_t)(RPG_TILE_BASE_WORLD + npc_slots[s]) << 4));
            for (j = 0; j < 16; j++) {
                tile_dst[j] = tile_src[j];
            }
            g_active_tile_palette[npc_slots[s]] = npc_pals[s];
        }
    }
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