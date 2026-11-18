#ifndef MENU_TABS_SNIPPET_H
#define MENU_TABS_SNIPPET_H

/* Shared DECK/QUEST/HERO/GEMS tab chrome -- single source of truth.
 *
 * Included once by each banked tab renderer (item_content_banked.c bank 2,
 * item_quest_banked.c / item_gem_banked.c / item_status_banked.c bank 13): a banked body must share
 * its tables' bank (AGENTS.md 52.11.1), so one compiled copy cannot serve
 * all tabs -- but the SOURCE is written once here and compiled per bank at
 * zero runtime/fixed-bank cost.  Each includer defines MENU_TAB_DRAW_TEXT
 * to its own skip-guarded text writer before including.
 *
 * No pragma here: inherits the includer's bank.  Uses only always-mapped
 * WRAM scratch (s_ic_* globals owned by item_content_banked.c and shared
 * while one tab renders) plus UI_TILE_MENUS_* ids, so it is safe in any
 * bank.  LCD-on safe throughout (BANKED_VRAM_WRITE).  Buffer keeps ' ' for
 * chrome and '^' at the cursor so harness caret asserts are unchanged.
 * No multiply (would pull the SDCC mult lib into fixed _CODE).
 */

#include <stdint.h>

extern char g_ui_screen_buf[18][21];
extern uint8_t g_is_cgb;
extern volatile uint8_t *s_ic_dst;
extern char *s_ic_buf;
#ifdef DEBUG_BUILD
extern volatile uint8_t g_tilemap_attr_mirror[32 * 32];
#endif

static void menu_stamp_chrome(uint8_t y, uint8_t x, uint8_t tile)
{
    s_ic_buf = &g_ui_screen_buf[y][x];
    *s_ic_buf = ' ';
    s_ic_dst = (volatile uint8_t *)(0x9800u + ((uint16_t)y << 5) + x);
    VBK_REG = 0;
    BANKED_VRAM_WRITE(s_ic_dst, tile);
}

#ifdef MENU_NEED_ATTR_CLEAR
/* Clear CGB palette attributes on the content rows that can carry
 * non-zero spans (row 3 detail/quest header, rows 5-12 card/quest rows,
 * cols 1-17) when repainting in-place over an already-open SCREEN_ITEM.
 * First entry from another screen runs under ui_lcd_off(), which already
 * wiped all 1024 attribute bytes, so this is skipped there.  For the
 * DECK tab this runs in bank 15 (item_view_build_banked) to keep bank 2
 * lean. */
static void menu_clear_content_attrs(void)
{
    uint8_t x;
    uint8_t y;
    if (!g_is_cgb || g_game.render_cache.prev_screen != SCREEN_ITEM) return;
    VBK_REG = 1;
    for (y = 3; y <= 12; y++) {
        s_ic_dst = (volatile uint8_t *)(0x9800u + ((uint16_t)y << 5) + 1);
        for (x = 1; x < 18; x++) {
            BANKED_VRAM_WRITE(s_ic_dst, 0);
#ifdef DEBUG_BUILD
            g_tilemap_attr_mirror[((uint16_t)y << 5) + x] = 0;
#endif
            s_ic_dst++;
        }
    }
    VBK_REG = 0;
}
#endif

/* Row 0: the four tabs with category separators in the three gaps
 * (cols 4/10/15).  Row 1: category border everywhere, category corner
 * under each separator, cursor-category tile at cursor_x (never a bare
 * border under the cursor).  cursor_x: 0 DECK, 5 QUEST, 11 HERO, 16 GEMS.
 * On first entry (prev_screen != SCREEN_ITEM) paints the full 2-row bar;
 * on intra-menu tab/mode switches updates only the moved caret on row 1. */
static void menu_draw_tabs(uint8_t cursor_x)
{
    uint8_t x;
    if (g_game.render_cache.prev_screen != SCREEN_ITEM) {
        MENU_TAB_DRAW_TEXT(0, 0, "DECK QUEST HERO GEMS", 20);
        menu_stamp_chrome(0, 4, UI_TILE_MENUS_CAT_SEPARATOR);
        menu_stamp_chrome(0, 10, UI_TILE_MENUS_CAT_SEPARATOR);
        menu_stamp_chrome(0, 15, UI_TILE_MENUS_CAT_SEPARATOR);
        for (x = 0; x < 20; x++) {
            if (x == 4 || x == 10 || x == 15) continue;
            if (x == cursor_x) continue;
            menu_stamp_chrome(1, x, UI_TILE_MENUS_CAT_BORDER);
        }
        menu_stamp_chrome(1, 4, UI_TILE_MENUS_CAT_CORNER);
        menu_stamp_chrome(1, 10, UI_TILE_MENUS_CAT_CORNER);
        menu_stamp_chrome(1, 15, UI_TILE_MENUS_CAT_CORNER);
    } else {
        for (x = 0; x < 20; x++) {
            if (x != cursor_x && g_ui_screen_buf[1][x] == '^') {
                menu_stamp_chrome(1, x, UI_TILE_MENUS_CAT_BORDER);
            }
        }
        if (g_ui_screen_buf[1][cursor_x] == '^') return;
    }
    s_ic_buf = &g_ui_screen_buf[1][cursor_x];
    *s_ic_buf = '^';
    s_ic_dst = (volatile uint8_t *)(0x9800u + ((uint16_t)1 << 5) + cursor_x);
    VBK_REG = 0;
    BANKED_VRAM_WRITE(s_ic_dst, UI_TILE_MENUS_CURSOR_CAT);
}

#endif /* MENU_TABS_SNIPPET_H */
