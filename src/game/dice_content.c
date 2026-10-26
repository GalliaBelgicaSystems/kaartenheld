#pragma bank 8

#include <gb/gb.h>
#include "ui.h"

/* Chuck-a-luck dice digits (assets/dice.png via tools/compose_dice.py,
 * encoded with the wood ramp by palette_compiler into
 * src/gfx/dice_tiles.h).  Bank 8 with its only reader (this loader):
 * bank 8 has room (overflow bank, AGENTS.md 55.5) while the fixed bank
 * and bank 3 are full, and a banked body must share its table's bank
 * (AGENTS.md 52.11.1).  dice.png cell i = digit i (0-9). */
#include "gfx/dice_tiles.h"

/* Stream the 10 dice digits to UI_TILE_DICE_BASE (246-255, high end of
 * the battle-art block, clear of the 128+ enemy-art growth).  LCD-off
 * caller (gamble entry; future battle callers reuse the same ids).
 * Ids >= 128 fetch from the 0x8800 block, i.e. raw 0x8000 + id*16
 * like the battle-art loader.  Dispatched via banked_call_run(). */
void dice_tiles_load_banked(void)
{
    uint8_t d, j;
    volatile uint8_t *dst;
    const uint8_t *src;

    for (d = 0; d < 10; d++) {
        dst = (volatile uint8_t *)(0x8000u + (((uint16_t)UI_TILE_DICE_BASE + d) << 4));
        src = dice_tiles + ((uint16_t)d << 4);
        for (j = 0; j < 16; j++) {
            dst[j] = src[j];
        }
    }
}
