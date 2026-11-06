#pragma bank 11
#pragma disable_warning 110

#include <gb/gb.h>
#include <gb/cgb.h>
#include "game.h"
#include "ui.h"
#include "banked.h"
#include "shops.h"

/* Shared ITEM/SHOP cursor repaint (AGENTS.md 36): a cursor-only UP/DOWN
 * repaints just the `>` glyph instead of invalidating the render cache.
 *
 * Lives in bank 11 (not bank 2 with the renders that call it): bank 2's
 * switchable area is full, and this body is pure WRAM math -- mode/row
 * computation plus two VRAM cell writes -- needing no bank-2 ROM tables
 * (AGENTS.md §52.11.1).  Staging: g_bk_ptr_a = Game*,
 * g_bk_byte_a = old index, g_bk_byte_b = new index; the body owns the
 * mutation (scroll + caret rows), callers keep their single-call shape
 * so the fixed bank stays under 0x8000 (AGENTS.md §52.18).
 *
 * Moved verbatim from src/screens/screen_content.c; keep the row
 * geometry in sync with the bank-2 renders (bm_cursor_row comment). */

extern char g_ui_screen_buf[18][21];
extern uint8_t ui_font_tile_base;

/* Item menu navigation layout constants (mirror item_screen.c). */
#define CUR_FIRST_CARD 1
#define CUR_VISIBLE_CARD 11

static Game *s_cu_game;
static uint8_t s_cu_pos;
static uint8_t s_cu_mode;
static uint8_t s_cu_old_index;
static uint8_t s_cu_new_index;
static uint8_t s_cu_old_row;
static uint8_t s_cu_new_row;
static uint8_t s_cu_need_scroll;

static void cu_put_char(uint8_t x, uint8_t y, char ch)
{
    uint8_t tile;

    if (y >= 18 || x >= 20) return;
    if (g_ui_screen_buf[y][x] == ch) return;
    tile = (uint8_t)(ui_font_tile_base + (uint8_t)(ch - ' '));
    VBK_REG = 0;
    BANKED_VRAM_WRITE((volatile uint8_t *)(0x9800 + ((uint16_t)y << 5) + x), tile);
    g_ui_screen_buf[y][x] = ch;
}

static uint8_t cu_mode(const Game *g)
{
    return (g->screen == SCREEN_SHOP) ? 2 : (uint8_t)g->item_menu_tab;
}

static uint8_t cu_cursor_row(uint8_t mode, uint8_t index, uint8_t scroll)
{
    if (mode == 2) return (uint8_t)(5u + (uint8_t)(index - scroll));
    /* DECK tab: row 0 tabs, row 1 caret, row 2 FILTER/SORT, row 3
     * column header, one card per row at rows 4-14. */
    if (index == 0) return 2;
    s_cu_pos = (uint8_t)(index - CUR_FIRST_CARD);
    return (uint8_t)(4u + (uint8_t)(s_cu_pos - scroll));
}

/* Shared tail: scroll-window + caret repaint for staged old/new. */
static void cu_apply(void)
{
    s_cu_mode = cu_mode(s_cu_game);
    s_cu_game->item_menu_index = s_cu_new_index;

    if (s_cu_mode == 0) {
        s_cu_need_scroll = s_cu_game->item_menu_scroll;
        if (s_cu_new_index <= CUR_FIRST_CARD)
            s_cu_need_scroll = 0;
        else {
            s_cu_pos = (uint8_t)(s_cu_new_index - CUR_FIRST_CARD);
            if (s_cu_pos < s_cu_need_scroll)
                s_cu_need_scroll = s_cu_pos;
            else if (s_cu_pos > (uint8_t)(s_cu_need_scroll + CUR_VISIBLE_CARD - 1))
                s_cu_need_scroll = (uint8_t)(s_cu_pos - (CUR_VISIBLE_CARD - 1));
        }
        if (s_cu_need_scroll != s_cu_game->item_menu_scroll) {
            s_cu_game->item_menu_scroll = s_cu_need_scroll;
            s_cu_game->render_cache.valid = false;
            return;
        }
    } else if (s_cu_mode == 2) {
        /* Shop list: index is a direct stock position (0-based).  Keep the
         * cursor inside the SHOP_VISIBLE window; a window shift needs a
         * full redraw (the banked body cannot repaint scrolled rows). */
        s_cu_need_scroll = s_cu_game->item_menu_scroll;
        if (s_cu_new_index < s_cu_need_scroll)
            s_cu_need_scroll = s_cu_new_index;
        else if (s_cu_new_index > (uint8_t)(s_cu_need_scroll + SHOP_VISIBLE - 1))
            s_cu_need_scroll = (uint8_t)(s_cu_new_index - (SHOP_VISIBLE - 1));
        if (s_cu_need_scroll != s_cu_game->item_menu_scroll) {
            s_cu_game->item_menu_scroll = s_cu_need_scroll;
            s_cu_game->render_cache.valid = false;
            return;
        }
    }

    s_cu_old_row = cu_cursor_row(s_cu_mode, s_cu_old_index, s_cu_game->item_menu_scroll);
    s_cu_new_row = cu_cursor_row(s_cu_mode, s_cu_new_index, s_cu_game->item_menu_scroll);
    if (s_cu_old_row != s_cu_new_row) {
        cu_put_char(0, s_cu_old_row, ' ');
        cu_put_char(0, s_cu_new_row, '>');
    }
}

void item_menu_cursor_banked(void)
{
    s_cu_game = (Game *)g_bk_ptr_a;

    if (!s_cu_game) return;
    s_cu_old_index = g_bk_byte_a;
    s_cu_new_index = g_bk_byte_b;
    cu_apply();
}

/* Direction step for the shop list (g_bk_byte_b 0 = up, 1 = down,
 * g_bk_byte_c = visible count): clamp the new index banked so the
 * fixed update pays no bounds branch.  Shop mode geometry (mode 2)
 * is shared with the cursor body above. */
void item_menu_step_banked(void)
{
    s_cu_game = (Game *)g_bk_ptr_a;

    if (!s_cu_game) return;
    s_cu_old_index = g_bk_byte_a;
    s_cu_new_index = s_cu_old_index;
    if (g_bk_byte_b == 0) {
        if (s_cu_old_index > 0)
            s_cu_new_index = (uint8_t)(s_cu_old_index - 1);
    } else if ((uint8_t)(s_cu_old_index + 1) < g_bk_byte_c) {
        s_cu_new_index = (uint8_t)(s_cu_old_index + 1);
    }
    cu_apply();
}
