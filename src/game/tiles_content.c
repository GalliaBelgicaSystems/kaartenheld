#pragma bank 5

#include <stdint.h>
#include <gb/gb.h>
#include <gb/cgb.h>
#include "tile_palette.h"
#include "world/world.h"
#include "ui/ui.h"
#include "banked.h"
#include "gfx/rpg_tile_lookup.h"

/* Overworld OBJ ramps (same generated source as the fixed-bank copy in
 * ui.c, but static so this bank carries its own: a banked body can only
 * read its own bank plus WRAM, never fixed-bank ROM above 0x4000). */
#include "cgb_obj_palettes.inc"

#ifdef DEBUG_BUILD
/* Tilemap mirrors (defined in ui.c, pinned WRAM): kept in sync by the
 * wipe so debugger reads stay sane between wipe and reveal. */
extern uint8_t g_tilemap_mirror[32 * 32];
extern volatile uint8_t g_tilemap_attr_mirror[32 * 32];
#endif

/* CGB palette tables + NPC display slots, generated from assets/palette.txt
 * + tools/palette_slots.json by tools/palette_compiler.py (make manifest).
 * Artist ramps win: these bytes are verbatim artist colors per slot. */
#include "cgb_palettes.inc"

/* Backward compatibility alias */
#define cgb_bg_palettes_overworld cgb_bg_palettes_forest

/* KEEP_SLOT4 travels in bit 7 of the staged kind byte (ui.h): every real
 * tileset kind and mode must stay below 0x80 so no plain caller can ever
 * trip it.  Zero ROM cost (same pattern as g_tiletype_fits_u8). */
static const int g_cram_keep_kind_ok[((int)WORLD_TILESET_FOREST < 0x80 &&
                                      (int)WORLD_TILESET_VILLAGE < 0x80 &&
                                      (int)WORLD_TILESET_DESOLATE < 0x80 &&
                                      (int)WORLD_TILESET_CASTLE < 0x80) ? 1 : 0];

void ui_load_cram_banked(void)
{
    uint8_t p, c;
    uint8_t kind;
    uint8_t keep;
    const uint8_t *pal_data;
    /* Bit 7 of the staged kind is KEEP_SLOT4: skip CGB slot PAPER so a
     * scene-transition wipe holding it black survives the load (slot 4
     * is free in every world tileset).  Safe by construction: every
     * tileset kind is < 0x80, so existing callers staging a plain kind
     * can never trip it -- no shared-scratch staleness possible. */
    kind = (uint8_t)(g_bk_byte_a & 0x7F);
    keep = (uint8_t)(g_bk_byte_a & 0x80);
    if (!g_is_cgb) return;
    switch (kind) {
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
        if (keep && p == UI_COLOR_PAPER) continue;
        BCPS_REG = (uint8_t)(0x80 | (p << 3));
        for (c = 0; c < 8; c++) {
            /* Per-store di + bounded Mode wait (BANKED_VRAM_WRITE):
             * gate crossings repaint with the LCD ON (curtain wipe), so
             * the timer ISR could otherwise eclipse the wait->store
             * window into Mode 3 -- where the write drops while BCPS
             * auto-increment still advances, splicing a stale byte
             * into the ramp for the entire map stay.  LCD-off callers
             * take the macro's direct-store fast path. */
            BANKED_VRAM_WRITE(&BCPD_REG, ramp[c]);
        }
    }
}

/* World background tiles extracted from assets/ by tools/png2gb.py (make gfx) */

const uint8_t g_tileset_forest[1280] = {
    /* 80 forest tiles (1280 bytes): walls, floors, stumps, trees, water
     * bodies, exit tiles, and the row-4 hut/mailbox extension (slots
     * 64..68; slots 69..79 are plain-floor duplicates) */
#include "gfx/rpg_forest_world_tiles.inc"
};

const uint8_t g_tileset_desolate[1024] = {
    /* 64 desolate landscape tiles (1024 bytes): walls, floors, rocks,
     * trees, water bodies, and prop tiles */
#include "gfx/rpg_desolate_world_tiles.inc"
};

const uint8_t g_tileset_castle[576] = {
    /* 36 castle tiles (576 bytes) in sheet scan order, VRAM slot == scan
     * index (see world.h TILE_CASTLE_* and the gfx rule's --tile-coords):
     * slots 0..7 base walls, 8..16 row-1 walls/furniture, 17..19 throne
     * + rugs, 20..22 chandeliers + vase, 23..25 table half + plates,
     * 26 exit, 27..30 table top + vase + columns, 31..32 rotated rugs,
     * 33 red carpet, 34..35 empty (shared tile) */
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
    uint8_t val;
    uint16_t n;
    volatile uint8_t *dst;

    switch (tileset) {
        case WORLD_TILESET_FOREST:
            src = g_tileset_forest;
            pal_src = g_tile_pal_forest;
            tile_count = 80;
            break;
        case WORLD_TILESET_DESOLATE:
            src = g_tileset_desolate;
            pal_src = g_tile_pal_desolate;
            tile_count = 64;
            break;
        case WORLD_TILESET_CASTLE:
            src = g_tileset_castle;
            pal_src = g_tile_pal_castle;
            tile_count = 36;
            break;
        case WORLD_TILESET_VILLAGE:
            src = g_tileset_village;
            pal_src = g_tile_pal_village;
            tile_count = 48;
            break;
        default:
            src = g_tileset_forest;
            pal_src = g_tile_pal_forest;
            tile_count = 80;
            break;
    }

    VBK_REG = 0;
    dst = (volatile uint8_t *)(0x8000u + ((uint16_t)RPG_TILE_BASE_WORLD << 4));
    n = (uint16_t)tile_count << 4;
    while (n--) {
        /* Same LCD-on hazard as the CRAM loop above: one-shot tile
         * bitmap load, so a Mode-3-eclipsed byte corrupts the tile for
         * the entire map stay. */
        val = *src++;
        BANKED_VRAM_WRITE(dst, val);
        dst++;
    }
    for (i = 0; i < 80; i++) {
        /* Nibble-pack into g_active_tile_palette (two 0-7 indices per
         * byte): even slots assign the low nibble first, odd slots OR
         * in the high nibble (ascending i guarantees the order). */
        uint8_t v = (i < tile_count) ? pal_src[i] : 0;
        if ((i & 1) == 0) {
            g_active_tile_palette[i >> 1] = v;
        } else {
            g_active_tile_palette[i >> 1] |= (uint8_t)(v << 4);
        }
    }

    /* Town NPCs (guard, wizard, merchant, mayor) render as OAM sprites
     * with exact OBJ ramps (screens/enemy_types/npc_*.json): no overlay.
     * The village sheet's old NPC cells are plain ground. */

    /* Battles program the scratch OBJ slot for OAM enemy art; restore
     * the NPC ramps (slots 5-7) on every tileset load so town sprites
     * keep exact colors after a fight.  Lives here (not fixed bank) for
     * ROM budget (AGENTS.md 55.5); PPU-safe like the CRAM loop above. */
    if (g_is_cgb) {
        OCPS_REG = (uint8_t)(0x80 | (5 << 3));
        for (i = 0; i < 24; i++) {
            BANKED_VRAM_WRITE(&OCPD_REG,
                              ((const uint8_t *)cgb_obj_palettes)[40 + i]);
        }
    }
}

/* Full-ring black wipe for scene transitions (Bank 5).  Blackens CGB
 * slot 4, then paints all 18 ring rows x 32 cols with blank tiles +
 * slot 4 so the viewport is covered whatever SCX shows.  Slot 4 (PAPER)
 * is free in every world tileset, so no map row can need it during the
 * reveal.  All writes go through BANKED_VRAM_WRITE: gate crossings run
 * this wipe with the LCD ON (curtain path skips ui_lcd_off), where an
 * ISR-eclipsed wait would drop the write into Mode 3.  Sole caller is
 * ui_draw_world_transition (always the full ring). */
void ui_wipe_black_banked(void)
{
    uint8_t r;
    uint8_t c;
    uint16_t off;
    volatile uint8_t *dst;
    if (g_is_cgb) {
        BCPS_REG = (uint8_t)(0x80 | (UI_COLOR_PAPER << 3));
        for (c = 0; c < 8; c++) {
            BANKED_VRAM_WRITE(&BCPD_REG, 0);
        }
    }
    for (r = 0; r < 18; r++) {
        off = (uint16_t)r << 5;
        VBK_REG = 0;
        dst = (volatile uint8_t *)(0x9800u + off);
        for (c = 0; c < 32; c++) {
            BANKED_VRAM_WRITE(dst, 0);
            dst++;
        }
        if (g_is_cgb) {
            VBK_REG = 1;
            dst = (volatile uint8_t *)(0x9800u + off);
            for (c = 0; c < 32; c++) {
                BANKED_VRAM_WRITE(dst, (uint8_t)(UI_COLOR_PAPER & 0x07));
                dst++;
            }
            VBK_REG = 0;
        }
#ifdef DEBUG_BUILD
        for (c = 0; c < 32; c++) {
            g_tilemap_mirror[off + c] = 0;
            g_tilemap_attr_mirror[off + c] = UI_COLOR_PAPER;
        }
#endif
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
        /* Macro form even though title entry is LCD-off (direct-store
         * fast path there): identical bytes, immune if ever reused
         * with the LCD on. */
        BANKED_VRAM_WRITE(dst, *src);
        dst++;
        src++;
    }
}
#endif