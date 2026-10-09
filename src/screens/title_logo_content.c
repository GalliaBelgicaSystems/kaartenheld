#pragma bank 2

#include <stdint.h>
#include <gb/gb.h>
#include "gfx/rpg_tile_lookup.h"

#ifndef DEBUG_BUILD
/* Release-only title-logo home.  The harness (debug) build must keep the
 * logo in bank 5 (tiles_content.c): moving it flips a layout-sensitive
 * SDCC miscompile in the dialogue path (AGENTS.md 52.19).  The release
 * build keeps it in bank 2 (spare after the actors moved to bank 4).  The
 * fixed title wrapper selects the matching bank. */
const uint8_t g_title_logo_tiles[768] = {
#include "gfx/title_logo_tiles.inc"
};

/* Bank-2 title-logo loader for the fixed-bank title wrapper.  Writes the
 * 48 tiles to VRAM at the world BG block (ids 128+, signed 0x8800 fetch:
 * AGENTS.md 52.22) inside the LCD-off full redraw. */
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
#endif /* !DEBUG_BUILD */
