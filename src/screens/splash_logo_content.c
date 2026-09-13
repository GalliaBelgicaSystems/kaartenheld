#pragma bank 2

#include <stdint.h>
#include <gb/gb.h>
#include <gb/cgb.h>
#include "gfx/rpg_tile_lookup.h"

/* Studio splash logo (assets/gallia_belgica_systems.png, 13x5 cells,
 * make gfx -> src/gfx/splash_logo_tiles.h: a deduped tile blob + a raster
 * tile map).  Lives in bank 2 with its renderer (the world-tiles bank 5 and
 * the sfx/atlas bank 7 are both over budget in the release build); a banked
 * body may only read its own bank's data. */
#include "gfx/splash_logo_tiles.h"

/* Bitmap footprint on the 20x18 BG grid (centered: cols 3-15, rows 6-10). */
#define SPLASH_LOGO_X 3
#define SPLASH_LOGO_Y 6
#define SPLASH_LOGO_W 13
#define SPLASH_LOGO_H 5
#define SPLASH_LOGO_PALETTE 1

extern uint8_t g_is_cgb;

/* Bank-2 studio splash body for the fixed-bank splash wrapper.  Writes the
 * deduped tiles to VRAM at the world BG block (ids 128+, signed 0x8800
 * fetch: AGENTS.md 52.22), stamps the 13x5 map with CGB BG palette 1, and
 * programs that palette to the logo's fixed ramp
 * [white, red, blue, black].  Runs inside the LCD-off full redraw; the
 * overworld reloads the block on entry (ui_invalidate_tileset), so
 * nothing leaks. */
void ui_splash_logo_render_banked(void)
{
    static const palette_color_t ramp[4] = {
        RGB8(255, 255, 255), RGB8(184, 68, 68),
        RGB8(0, 60, 242), RGB8(0, 0, 0)
    };
    const uint8_t *bytes = (const uint8_t *)ramp;
    const uint8_t *src;
    const uint8_t *map;
    volatile uint8_t *dst;
    uint16_t n;
    uint8_t row, col, i;

    VBK_REG = 0;
    src = g_splash_logo_tiles;
    dst = (volatile uint8_t *)(0x8000u + ((uint16_t)RPG_TILE_BASE_WORLD << 4));
    n = (uint16_t)sizeof(g_splash_logo_tiles);
    while (n--) {
        *dst++ = *src++;
    }

    map = g_splash_logo_map;
    for (row = 0; row < SPLASH_LOGO_H; row++) {
        dst = (volatile uint8_t *)(0x9800 + ((uint16_t)(SPLASH_LOGO_Y + row) << 5) + SPLASH_LOGO_X);
        for (col = 0; col < SPLASH_LOGO_W; col++) {
            dst[col] = (uint8_t)(RPG_TILE_BASE_WORLD + *map++);
        }
    }
    VBK_REG = 1;
    for (row = 0; row < SPLASH_LOGO_H; row++) {
        dst = (volatile uint8_t *)(0x9800 + ((uint16_t)(SPLASH_LOGO_Y + row) << 5) + SPLASH_LOGO_X);
        for (col = 0; col < SPLASH_LOGO_W; col++) {
            dst[col] = SPLASH_LOGO_PALETTE;
        }
    }
    VBK_REG = 0;

    if (g_is_cgb) {
        BCPS_REG = (uint8_t)(0x80 | (SPLASH_LOGO_PALETTE << 3));
        for (i = 0; i < 8; i++) {
            BCPD_REG = bytes[i];
        }
    }
}
