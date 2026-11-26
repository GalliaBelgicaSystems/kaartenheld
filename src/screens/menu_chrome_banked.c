#pragma bank 11

#include <stdint.h>
#include <gb/gb.h>
#include "ui.h"
#include "banked.h"

/* BATTLE/ITEM/SHOP entry invalidate + menu chrome stream (bank 11).
 *
 * Moved here from src/game/marks_banked.c (bank 8): the chrome sheet
 * (assets/menus-tile.png) overflowed bank 8 in the debug build, bank 2
 * (the riders' home) overflowed first, and bank 5 (the next candidate)
 * overflowed by 178 bytes.  Bank 11 has room in both builds and already
 * hosts the menu cursor loader.  screen_change() (fixed bank) already pays one trampoline dispatch on these entries --
 * retargeting it costs zero fixed-bank bytes (same immediates).
 *
 * Entry re-arm flags (WRAM, defined in marks_banked.c): cleared
 * cross-bank here; set by the bank-8 tick loaders. */
extern uint8_t s_mark_tiles_loaded;
extern uint8_t s_battle_tiles_loaded;
extern uint8_t s_ap_pip_loaded;
uint8_t g_menus_chrome_loaded;

/* Menu chrome bytes (menus-tile.png, gfx rule --tile-coords order: 12
 * row-0 singles, then the 16 hero-status portrait cells, row-major from
 * sheet (12,0), blank transparent corners included, then the 4
 * status-bar icons from sheet row 1, cols 0-3.  Same-bank data. */
#include "gfx/menus_tile_tiles.h"

/* Hero-status portrait VRAM slots (ui.h): 16 cells in header order
 * (row-major from sheet (12,0)): the 15 orphaned box slots around
 * riders 228-231, AP pips 236-240 and the spin tile 241, plus dice
 * slot 246 (the gamble entry restreams all dice, and every ITEM entry
 * restreams hero, so the time-share is safe).
 * The bank-13 STATUS renderer carries a copy of this table (it cannot
 * read bank-11 data); KEEP IN SYNC. */
static const uint8_t s_menus_hero_slots[16] = {
    220u, 221u, 222u, 223u, 224u, 225u, 226u, 227u,
    232u, 233u, 234u, 235u, 242u, 243u, 244u, 246u
};

/* Compile-time pins: the chrome layout above must match ui.h.
 * Preprocessor #if/#error (SDCC silently accepts typedef char[-1]). */
#if !(UI_TILE_MENUS_BASE == 208u && UI_TILE_MENUS_COUNT == 12u && \
      UI_TILE_MENUS_HERO_COUNT == 16u && \
      UI_TILE_MENUS_STATUS_COUNT == 4u && \
      UI_TILE_MENUS_STATUS_BASE == 248u && \
      UI_TILE_MENUS_LIST_SCROLL_UP == 245u && \
      UI_TILE_BUTTON_COUNT == 10u && \
      UI_TILE_BUTTON_A == 176u && \
      UI_TILE_BUTTON_SELECT_3 == 185u)
#error "menu chrome VRAM layout mismatch with ui.h"
#endif
#if !(UI_TILE_STATUS_HEART_FULL == 208u && UI_TILE_STATUS_ARROWS == 211u)
#error "status icon VRAM layout mismatch with ui.h"
#endif

/* Status icon header base (gfx rule --tile-coords order: 12 row-0
 * singles + 16 hero cells, then the 4 row-1 status icons appended).
 * Appended-last so the chrome indices above never move. */
#define MENUS_STATUS_CELL_BASE 28u

/* Button header base (gfx rule --tile-coords order: 12 row-0
 * singles + 16 hero cells + 4 status icons, then the 10 button cells
 * appended).  Appended-last so the chrome indices above never move. */
#define MENUS_BUTTON_CELL_BASE 32u

/* Paper-white document ramp (CRAM slot 4): byte-identical to the
 * battle fight3 ramp (white, tan, dark red, dark bronze).  The HUD
 * status art uses one ink shade each: hearts shade 2 (red), AP/arrow
 * icons shade 3 (brown), so each colored index serves exactly one
 * element; font text/digits/chevron use shades 0+3 (white bg, bronze
 * ink -- the same colour as battle numbers).  Same bytes in the
 * dialogue paper (ui_dialogue_box_banked.c); slot 4 is free in every
 * world tileset. */
static const uint8_t s_status_paper[8] = {
    0xFF, 0x7F, /* RGB8(255,255,255) */
    0x56, 0x32, /* RGB8(176,146,102) tan (fight3 spare) */
    0x38, 0x0D, /* RGB8(195,78,27) heart red */
    0x6E, 0x19  /* RGB8(117,89,48) icon bronze */
};

/* Bit-reverse one byte (horizontal mirror for the 180-degree rotation
 * of the list scroll arrow, whose sheet art points down).  No table
 * (bank 11 has room, WRAM does not grow). */
static uint8_t menus_bit_rev(uint8_t b)
{
    uint8_t r = 0;
    uint8_t i;
    for (i = 0; i < 8; i++) {
        r = (uint8_t)((r << 1) | (b & 1u));
        b >>= 1;
    }
    return r;
}

/* Stream the menu chrome sheet (28 + 4 + 10 cells, 672 bytes) to its VRAM
 * slots.  Same LCD-safe pattern as the bank-8 OAM streamers
 * (BANKED_VRAM_WRITE per byte: the entry transition repaints with the
 * LCD on). */
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
    for (k = 0; k < UI_TILE_MENUS_STATUS_COUNT; k++) {
        src = &menus_tile_tiles[((uint16_t)(UI_TILE_MENUS_COUNT + UI_TILE_MENUS_HERO_COUNT + k) << 4)];
        dst = (volatile uint8_t *)(0x8000u +
                                   ((uint16_t)(UI_TILE_MENUS_STATUS_BASE + k) << 4));
        for (j = 0; j < 16; j++) {
            BANKED_VRAM_WRITE(dst, (uint8_t)src[j]);
            dst++;
        }
    }
    for (k = 0; k < UI_TILE_MENUS_HERO_COUNT; k++) {
        src = &menus_tile_tiles[((uint16_t)(UI_TILE_MENUS_COUNT + k) << 4)];
        dst = (volatile uint8_t *)(0x8000u +
                                   ((uint16_t)s_menus_hero_slots[k] << 4));
        for (j = 0; j < 16; j++) {
            BANKED_VRAM_WRITE(dst, (uint8_t)src[j]);
            dst++;
        }
    }
    for (k = 0; k < UI_TILE_BUTTON_COUNT; k++) {
        src = &menus_tile_tiles[((uint16_t)(MENUS_BUTTON_CELL_BASE + k) << 4)];
        dst = (volatile uint8_t *)(0x8000u +
                                   ((uint16_t)(UI_TILE_BUTTON_A + k) << 4));
        for (j = 0; j < 16; j++) {
            BANKED_VRAM_WRITE(dst, (uint8_t)src[j]);
            dst++;
        }
    }
    /* Rotated copy of the list scroll arrow (sheet (0,7), header tile
     * index 7) into slot 245 for the up carets: 180 degrees = row
     * order reversed + each byte bit-mirrored.  Row pairs are 2 bytes
     * (lo/hi, monochrome doubles). */
    {
        uint8_t r;
        src = &menus_tile_tiles[((uint16_t)7 << 4)];
        dst = (volatile uint8_t *)(0x8000u +
                                   ((uint16_t)UI_TILE_MENUS_LIST_SCROLL_UP << 4));
        for (r = 0; r < 8; r++) {
            uint8_t lo = src[((uint16_t)(7 - r) << 1)];
            uint8_t hi = src[((uint16_t)((7 - r) << 1) + 1)];
            BANKED_VRAM_WRITE(dst, menus_bit_rev(lo));
            dst++;
            BANKED_VRAM_WRITE(dst, menus_bit_rev(hi));
            dst++;
        }
    }
}

/* Overworld status-bar stream (bank 11): restreams the 4 status
 * icons (header cells 28-31: full heart, half heart, AP, arrows) to
 * VRAM 208-211 and programs the paper-white ramp into CGB slot 4.
 * Dispatched from ui_world_tail() on every overworld full redraw.
 * Gate crossings run the tail LCD-on (curtain wipe), so every store
 * is mode-gated like the chrome stream above.  The slots time-share
 * with the menu chrome singles (each entry restores its own); the 4
 * icon bytes survive every overworld stay (world tilesets end at 207,
 * battle art stays below, nothing else writes 208+), and the next
 * full redraw restores whatever a menu/battle entry clobbered.
 * DMG-safe: BCPS/BCPD are unmapped no-ops there (AGENTS.md 38.1). */
void menus_status_stream_banked(void)
{
    uint8_t i, j;
    const uint8_t *src;
    volatile uint8_t *dst;

    /* Stream the 4 icons: hearts (0-1) drop bitplane 0 so shade 3
     * (battle fight4 red) maps onto shade 2 (fight3 paper red) here
     * while AP/arrows (2-3) keep shade 3 (fight3 paper bronze). */
    for (i = 0; i < 4; i++) {
        src = &menus_tile_tiles[((uint16_t)(MENUS_STATUS_CELL_BASE + i) << 4)];
        dst = (volatile uint8_t *)(0x8000u +
                                   ((uint16_t)(UI_TILE_STATUS_HEART_FULL + i) << 4));
        for (j = 0; j < 16; j++) {
            uint8_t b = (i < 2 && !(j & 1)) ? 0u : (uint8_t)src[j];
            BANKED_VRAM_WRITE(dst, b);
            dst++;
        }
    }
    if (g_is_cgb) {
        BCPS_REG = (uint8_t)(0x80u | (UI_COLOR_PAPER << 3));
        for (i = 0; i < 8; i++) {
            BANKED_VRAM_WRITE(&BCPD_REG, s_status_paper[i]);
        }
    }
}

/* Stream menu chrome + button tiles during an LCD-off transition render
 * (called from game_render() via the trampoline). */
void menus_chrome_stream_banked(void)
{
    if (g_menus_chrome_loaded) return;
    g_menus_chrome_loaded = 1;
    menus_chrome_stream();
}

/* Re-arm the battle/menu tile streams on screen entry (WRAM flags only).
 * Actual VRAM streaming runs later in game_render() once ui_lcd_off()
 * has blanked the display, so the outgoing overworld never flashes
 * overwritten tiles for 1-2 frames. */
void menus_entry_invalidate_banked(void)
{
    s_battle_tiles_loaded = 0;
    s_mark_tiles_loaded = 0;
    s_ap_pip_loaded = 0;
    g_menu_riders_loaded = 0;
    g_menus_chrome_loaded = 0;
}
