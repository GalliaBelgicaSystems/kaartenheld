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
 * Authored colors come from assets/palette.txt via tools/palette_txt.py
 * (`make palette-check` enforces parity); slots without an authored
 * counterpart keep their legacy gameplay values. */
const palette_color_t cgb_bg_palettes[8][4] = {
    /* 0 gray */     { RGB8(255,255,255), RGB8(170,170,170), RGB8(85,85,85),  RGB8(0,0,0)      },
    /* 1 fire */     { RGB8(255,255,224), RGB8(255,140,40),  RGB8(220,50,20), RGB8(100,10,0)   },
    /* 2 iron/ice */ { RGB8(235,242,250), RGB8(140,180,214), RGB8(70,105,138), RGB8(27,43,58) },
    /* 3 field: palette.txt FOREST grass/tree_leaves/dark_tree_leaves */
    { RGB8(123,182,96),  RGB8(42,79,26),    RGB8(29,62,15),   RGB8(0,0,0)     },
    /* 4 poison/mauve */{ RGB8(250,240,250), RGB8(190,140,200), RGB8(140,80,160), RGB8(60,30,80) },
    /* 5 wood */     { RGB8(245,230,210), RGB8(196,138,72),  RGB8(138,82,34), RGB8(61,32,10)  },
    /* 6 gold: slot 1 is palette.txt CASTLE gold */
    { RGB8(255,252,224), RGB8(215,167,38),   RGB8(200,140,8), RGB8(90,58,0)   },
    /* 7 dim */      { RGB8(200,200,200), RGB8(150,150,150), RGB8(90,90,90),   RGB8(40,40,40)   }
};

/* Forest / Field overworld palette set: Palette 5 (wood) Color 0 is
 * harmonized to palette.txt FOREST grass (RGB8(123,182,96)), eliminating
 * rectangular beige seams around tree trunks and stumps while preserving
 * wood details. */
const palette_color_t cgb_bg_palettes_forest[8][4] = {
    /* 0 gray */     { RGB8(255,255,255), RGB8(170,170,170), RGB8(85,85,85),  RGB8(0,0,0)      },
    /* 1 fire */     { RGB8(255,255,224), RGB8(255,140,40),  RGB8(220,50,20), RGB8(100,10,0)   },
    /* 2 iron/ice */ { RGB8(235,242,250), RGB8(140,180,214), RGB8(70,105,138), RGB8(27,43,58) },
    /* 3 field: palette.txt FOREST grass/tree_leaves/dark_tree_leaves */
    { RGB8(123,182,96),  RGB8(42,79,26),    RGB8(29,62,15),   RGB8(0,0,0)     },
    /* 4 poison/mauve */{ RGB8(250,240,250), RGB8(190,140,200), RGB8(140,80,160), RGB8(60,30,80) },
    /* 5 wood */     { RGB8(123,182,96),  RGB8(196,138,72),  RGB8(138,82,34), RGB8(61,32,10)  },
    /* 6 gold: slot 1 is palette.txt CASTLE gold */
    { RGB8(255,252,224), RGB8(215,167,38),   RGB8(200,140,8), RGB8(90,58,0)   },
    /* 7 dim: slot 3 is palette.txt FOREST rocks_outline */
    { RGB8(200,200,200), RGB8(150,150,150), RGB8(90,90,90),   RGB8(38,35,46)   }
};

/* Desolate landscape palette set: Anchor color 0 is slate rock (#938da1
 * -> RGB8(147,141,161)) across outdoor nature palettes. */
const palette_color_t cgb_bg_palettes_desolate[8][4] = {
    /* 0 gray */     { RGB8(255,255,255), RGB8(170,170,170), RGB8(85,85,85),  RGB8(0,0,0)      },
    /* 1 campfire */ { RGB8(147,141,161), RGB8(237,194,20),  RGB8(215,80,20), RGB8(80,10,0)   },
    /* 2 iron/ice */ { RGB8(147,141,161), RGB8(140,180,214), RGB8(70,105,138), RGB8(27,43,58) },
    /* 3 flora */    { RGB8(147,141,161), RGB8(116,111,128), RGB8(63,58,74),  RGB8(38,35,46)  },
    /* 4 poison/mauve */{ RGB8(147,141,161), RGB8(190,140,200), RGB8(140,80,160), RGB8(60,30,80) },
    /* 5 deadwood */ { RGB8(147,141,161), RGB8(141,117,74),  RGB8(111,90,52), RGB8(38,35,46)  },
    /* 6 gold */     { RGB8(147,141,161), RGB8(215,167,38),  RGB8(141,117,74),RGB8(50,30,10)  },
    /* 7 slate rock*/{ RGB8(147,141,161), RGB8(131,123,150), RGB8(63,58,74),  RGB8(38,35,46)  }
};

/* Castle palette set: Anchor color 0 is palette.txt CASTLE ground (#a99fc0 ->
 * RGB8(169,159,192)) across indoor stone and furniture palettes. */
const palette_color_t cgb_bg_palettes_castle[8][4] = {
    /* 0 stone */    { RGB8(169,159,192), RGB8(179,176,176), RGB8(130,130,130),RGB8(46,46,46)  },
    /* 1 curtain */  { RGB8(169,159,192), RGB8(139,27,27),   RGB8(98,18,18),  RGB8(30,0,0)    },
    /* 2 iron */     { RGB8(169,159,192), RGB8(140,160,180), RGB8(70,90,110), RGB8(30,40,50)  },
    /* 3 moss/green*/{ RGB8(169,159,192), RGB8(90,140,80),   RGB8(40,80,30),  RGB8(10,30,10)  },
    /* 4 poison/mauve */{ RGB8(169,159,192), RGB8(190,140,200), RGB8(140,80,160), RGB8(60,30,80) },
    /* 5 wood furn */{ RGB8(169,159,192), RGB8(158,142,113), RGB8(111,90,52), RGB8(40,25,10)  },
    /* 6 gold */     { RGB8(169,159,192), RGB8(215,167,38),  RGB8(162,146,113),RGB8(60,40,10) },
    /* 7 dim shadow*/{ RGB8(169,159,192), RGB8(130,130,130), RGB8(86,86,86),  RGB8(35,35,35)  }
};

/* Village palette set: Anchor color 0 is dirt (#b6a27e -> RGB8(182,162,126))
 * across outdoor nature palettes. */
const palette_color_t cgb_bg_palettes_village[8][4] = {
    /* 0 gray */     { RGB8(182,162,126), RGB8(200,200,200), RGB8(125,125,125),RGB8(30,30,30)  },
    /* 1 fire */     { RGB8(182,162,126), RGB8(255,196,96),  RGB8(220,110,32), RGB8(90,40,10)  },
    /* 2 iron */     { RGB8(182,162,126), RGB8(150,160,180), RGB8(85,105,130), RGB8(35,45,60)  },
    /* 3 dirt floor */{ RGB8(182,162,126), RGB8(140,120,88),  RGB8(96,78,52),  RGB8(48,36,24)  },
    /* 4 poison/mauve */{ RGB8(182,162,126), RGB8(190,140,200), RGB8(140,80,160), RGB8(60,30,80) },
    /* 5 wood: slots 1-2 are palette.txt TOWN barrel_wood_fence/wood_outlines */
    { RGB8(182,162,126), RGB8(141,117,74),  RGB8(100,82,51),   RGB8(38,24,10)  },
    /* 6 cream: slots 1-3 are palette.txt TOWN house_wall/roof_shade1/wood_outlines */
    { RGB8(182,162,126), RGB8(241,207,145), RGB8(204,170,108), RGB8(100,82,51) },
    /* 7 dim */      { RGB8(182,162,126), RGB8(158,148,128), RGB8(100,88,66),  RGB8(42,36,26)  }
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