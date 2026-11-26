#include "travel.h"
#include "banked.h"

/* ── Travel chrome tile data (travel-demo ROM only, bank 13) ───────
 * assets/travel-tile.png via tools/png2gb.py --shade-map
 * assets/travel-chrome-shades.json (see Makefile gfx rule).  Lives in
 * the same bank as the paint body (AGENTS.md 52.11.1: a banked body
 * may only read same-bank data).  Stamped at
 * UI_TILE_TRAVEL_CHROME_BASE + sheet index (sheet row-major =
 * assets/travel-tileset.csv order = TCHROME_* order).
 */

#pragma bank 13

const uint8_t travel_chrome_tiles[640] = {
#include "gfx/travel_chrome_tiles.inc"
};

/* One-shot streamer: LCD is off on journey first-entry (full redraw),
 * so plain writes are safe; BANKED_VRAM_WRITE keeps same-screen
 * restreams (battle->journey) safe too. */
void travel_chrome_load_banked(void)
{
    uint8_t i;
    uint16_t n;
    volatile uint8_t *dst;
    const uint8_t *src;
    for (i = 0; i < UI_TILE_TRAVEL_CHROME_COUNT; i++) {
        dst = (volatile uint8_t *)(0x8000u + ((uint16_t)(UI_TILE_TRAVEL_CHROME_BASE + i) << 4));
        src = &travel_chrome_tiles[(uint16_t)i << 4];
        for (n = 0; n < 16; n++) {
            BANKED_VRAM_WRITE(dst, *src);
            dst++;
            src++;
        }
    }
}
