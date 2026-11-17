#pragma bank 11

#include <stdint.h>
#include <gb/gb.h>
#include "ui.h"
#include "banked.h"

/* BATTLE/ITEM/SHOP entry invalidate + menu chrome stream (bank 5).
 *
 * Moved here from src/game/marks_banked.c (bank 8): the chrome sheet
 * (assets/menus-tile.png, 416 bytes) overflowed bank 8 in the debug
 * build, bank 2 (the riders' home) overflowed first, and bank 5 (the
 * next candidate) overflowed by 178 bytes.  Bank 11 has room in both
 * builds and already hosts the menu cursor loader.  screen_change()
 * (fixed bank) already pays one trampoline dispatch on these entries --
 * retargeting it costs zero fixed-bank bytes (same immediates).
 *
 * Entry re-arm flags (WRAM, defined in marks_banked.c): cleared
 * cross-bank here; set by the bank-8 tick loaders. */
extern uint8_t s_mark_tiles_loaded;
extern uint8_t s_battle_tiles_loaded;
extern uint8_t s_ap_pip_loaded;

/* Menu chrome bytes (menus-tile.png, gfx rule --tile-coords order: 12
 * row-0 singles then the 14 bottom-box frame cells).  Same-bank data. */
#include "gfx/menus_tile_tiles.h"

/* Menu chrome VRAM slots (ui.h): 12 singles to UI_TILE_MENUS_BASE+i,
 * the 14 bottom-box frame cells through this table (menu data order is
 * the gfx rule's --tile-coords order: singles first, then box scan minus
 * the two blank corners).  Slots dodge riders 228-232, AP pips 236-240
 * and the spin tile 241. */
static const uint8_t s_menus_box_slots[14] = {
    220u, 221u, 222u, 223u, 224u, 225u, 226u, 227u,
    233u, 234u, 235u, 242u, 243u, 244u
};

/* Compile-time pins: the chrome layout above must match ui.h. */
typedef char menus_chrome_ok[
    (UI_TILE_MENUS_BASE == 208u && UI_TILE_MENUS_COUNT == 12u &&
     UI_TILE_MENUS_BOX_COUNT == 14u) ? 1 : -1];

/* Stream the menu chrome sheet (26 cells, 416 bytes) to its VRAM slots.
 * Same LCD-safe pattern as the bank-8 OAM streamers (BANKED_VRAM_WRITE
 * per byte: the entry transition repaints with the LCD on). */
static void menus_chrome_stream(void)
{
    uint8_t i, j, k;
    volatile uint8_t *dst;
    const uint8_t *src;

    for (i = 0; i < UI_TILE_MENUS_COUNT; i++) {
        src = &menus_tile_tiles[((uint16_t)i << 4)];
        dst = (volatile uint8_t *)(0x8000u +
                                   ((uint16_t)(UI_TILE_MENUS_BASE + i) << 4));
        for (j = 0; j < 16; j++) {
            BANKED_VRAM_WRITE(dst, (uint8_t)src[j]);
            dst++;
        }
    }
    for (k = 0; k < UI_TILE_MENUS_BOX_COUNT; k++) {
        src = &menus_tile_tiles[((uint16_t)(UI_TILE_MENUS_COUNT + k) << 4)];
        dst = (volatile uint8_t *)(0x8000u +
                                   ((uint16_t)s_menus_box_slots[k] << 4));
        for (j = 0; j < 16; j++) {
            BANKED_VRAM_WRITE(dst, (uint8_t)src[j]);
            dst++;
        }
    }
}

/* Re-arm the battle tile stream after overworld entry restores the
 * time-shared OAM ids (AP pips 1-4 over hero idle/walk at 96-99, pip 5
 * plus riders over the blob tail at 122-125, material icons over blob
 * offsets 17-21 at 117-121), and stream the menu chrome sheet.
 * No-arg trampoline target for the fixed-bank screen_change() path.
 * No WRAM flag of its own: entry itself is the once-per-stay re-arm. */
void menus_entry_invalidate_banked(void)
{
    /* Re-arm both streams: AP pips/riders time-share OAM ids with
     * overworld art (96-99 hero, 122-125 blob tail), and the !/? marks
     * (126-127) are streamed once per boot.  The material icons (streamed
     * by the bank-2 menu render, restored with the rider tail below)
     * need no flag: renders always precede their stamps.  screen_change()
     * calls this on every BATTLE/ITEM/SHOP entry so the next tick
     * restreams before the first render -- even if the flag powered up
     * set (harness skips CRT0, so BSS is not cleared) or VRAM was
     * clobbered by a path that did not clear the flag, the pips would
     * otherwise show hero pixels with correct OAM ids (all id-only
     * asserts green). */
    s_battle_tiles_loaded = 0;
    s_mark_tiles_loaded = 0;
    s_ap_pip_loaded = 0;
    /* Card-menu rider icons (bank-2 streamer, plain WRAM flag): restream on
     * every ITEM/SHOP entry for the same reason as the pips -- the flag
     * may be boot garbage under the harness, and the menu must never stamp
     * an icon id whose VRAM holds something else. */
    g_menu_riders_loaded = 0;
    /* Menu chrome (208-219 + box slots): stream eagerly on every
     * BATTLE/ITEM/SHOP entry.  The slots belong to no tileset and no
     * battle art, so the 416 bytes survive until the next overworld
     * tileset reload.  Battle pays it too -- same dispatch, and its
     * entry loads dwarf it. */
    menus_chrome_stream();
}
