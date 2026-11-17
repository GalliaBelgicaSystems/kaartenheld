#pragma bank 2

#include <stdint.h>
#include <gb/gb.h>
#include "ui.h"
#include "banked.h"

/* Card-menu element rider icons (assets/menu_riders.png, cells in
 * fire/ice/poison/health order; make gfx -> gfx/menu_rider_tiles.h).
 *
 * The menu used to recycle enemy-sprite OAM art for these (an 8x8 crop of
 * a larger frame, off-center under the card's element cell).  The menu
 * now stamps dedicated BG tiles over that cell instead: one encoding per
 * icon, colored by the cell's own element palette slot (fire/ice/poison,
 * health -> UI_COLOR_HEALTH), so no OAM entry, palette slot or per-row
 * hide/sweep is involved.
 *
 * Bank 2 with the menu render bodies that call it (plain same-bank call,
 * no trampoline).  Ids sit in the free high block-1 window
 * (UI_TILE_MENU_RIDER_BASE 228-231): ids >= 128 are fetched by the PPU
 * from 0x8800 + (id-128)*16 == 0x8000 + id*16, which is what the stream
 * below targets (AGENTS.md 52.22).  The world tileset only writes
 * 128..127+count (max 207), so nothing else ever owns those slots.
 *
 * Streaming runs with the LCD on (a menu full redraw), so every byte goes
 * through BANKED_VRAM_WRITE.  One-shot per menu stay: the flag is cleared
 * on every ITEM/SHOP entry by menus_entry_invalidate_banked
 * (bank 8, WRAM write) -- the same re-arm contract as the AP pip stream,
 * which also keeps the harness (CRT0-skipping, uninitialized BSS) from
 * ever trusting a stale flag value it never set. */

#include "gfx/menu_rider_tiles.h"

/* Scroll-down caret tile (streamed as rider slot 4 below):
 * menu_cursor tile 1 (finger up: rows 30 30 30 3C 7E 7E 3C 3C)
 * rotated 180 degrees, i.e. reversed row order with each row's bits
 * mirrored (0x30 <-> 0x0C; 0x3C/0x7E are horizontally symmetric).
 * Monochrome doubles like the source (lo == hi per row). */
static const uint8_t s_scroll_down_tile[16] = {
    0x3C, 0x3C,
    0x3C, 0x3C,
    0x7E, 0x7E,
    0x7E, 0x7E,
    0x3C, 0x3C,
    0x0C, 0x0C,
    0x0C, 0x0C,
    0x0C, 0x0C
};

/* Compile-time pin: the caret rides rider slot 4 (UI_TILE_SCROLL_DOWN
 * == BASE + 4), streamed by the same loop below at zero extra
 * dispatch cost. */
typedef char scroll_down_slot_ok[
    UI_TILE_SCROLL_DOWN == (UI_TILE_MENU_RIDER_BASE + 4) ? 1 : -1];

uint8_t g_menu_riders_loaded;

void ic_menu_riders_load(void)
{
    uint8_t i, j;
    volatile uint8_t *dst;
    const uint8_t *src;

    if (g_menu_riders_loaded) return;
    g_menu_riders_loaded = 1;
    VBK_REG = 0;
    /* Rider icons plus the scroll-down caret (slot 4: the menu
     * finger-up bitmap rotated 180 degrees, so the down caret is the
     * same symbol as the '^' up caret, not the font 'v').  One loop,
     * one LCD-on window, same re-arm contract as the AP pip stream. */
    for (i = 0; i < UI_TILE_MENU_RIDER_COUNT + 1; i++) {
        src = (i < UI_TILE_MENU_RIDER_COUNT) ?
            &menu_rider_tiles[((uint16_t)i << 4)] : s_scroll_down_tile;
        dst = (volatile uint8_t *)(0x8000u +
                                   ((uint16_t)(UI_TILE_MENU_RIDER_BASE + i) << 4));
        for (j = 0; j < 16; j++) {
            BANKED_VRAM_WRITE(dst, src[j]);
            dst++;
        }
    }
}
