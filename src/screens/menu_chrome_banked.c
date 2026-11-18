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

/* Menu chrome bytes (menus-tile.png, gfx rule --tile-coords order: 12
 * row-0 singles then the 16 hero-status portrait cells, row-major from
 * sheet (12,0), blank transparent corners included).  Same-bank data. */
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
      UI_TILE_MENUS_LIST_SCROLL_UP == 245u)
#error "menu chrome VRAM layout mismatch with ui.h"
#endif
#if !(UI_TILE_STATUS_HEART_FULL == 208u && UI_TILE_STATUS_ARROWS == 211u)
#error "status icon VRAM layout mismatch with ui.h"
#endif

/* Status icon header base (gfx rule --tile-coords order: 12 row-0
 * singles + 16 hero cells, then the 4 row-1 status icons appended).
 * Appended-last so the chrome indices above never move. */
#define MENUS_STATUS_CELL_BASE 28u

/* Paper-white document ramp (CRAM slot 4): pure white box, black font
 * ink, artist heart-red at index 1 (RGB8(139,27,27), the menus-tile
 * status hearts' own ink).  The bar + dialogue text + status icons use
 * only shades 0+3 (white/black); the status hearts use shades 0+1, so
 * index 1 serves nothing but hearts.  Same bytes as the dialogue paper
 * (ui_dialogue_box_banked.c); slot 4 is free in every world tileset. */
static const uint8_t s_status_paper[8] = {
    0xFF, 0x7F, /* RGB8(255,255,255) */
    0x71, 0x0C, /* RGB8(139,27,27) heart red */
    0x4A, 0x29, /* RGB8(85,85,85) */
    0x00, 0x00  /* RGB8(0,0,0) */
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

/* Stream the menu chrome sheet (28 cells, 448 bytes) to its VRAM slots.
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
    for (k = 0; k < UI_TILE_MENUS_HERO_COUNT; k++) {
        src = &menus_tile_tiles[((uint16_t)(UI_TILE_MENUS_COUNT + k) << 4)];
        dst = (volatile uint8_t *)(0x8000u +
                                   ((uint16_t)s_menus_hero_slots[k] << 4));
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

    for (i = 0; i < 4; i++) {
        src = &menus_tile_tiles[((uint16_t)(MENUS_STATUS_CELL_BASE + i) << 4)];
        dst = (volatile uint8_t *)(0x8000u +
                                   ((uint16_t)(UI_TILE_STATUS_HEART_FULL + i) << 4));
        for (j = 0; j < 16; j++) {
            BANKED_VRAM_WRITE(dst, (uint8_t)src[j]);
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
