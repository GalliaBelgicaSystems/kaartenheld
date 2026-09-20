#pragma bank 2
#pragma disable_warning 110

#include <gb/gb.h>
#include <gb/cgb.h>
#include "game.h"
#include "ui.h"
#include "banked.h"
#include "battle/card.h"
#include "rpg/cards.h"
#include "rpg/status.h"
#include "shops.h"
#include "menu.h"
#include "rpg/save.h"
#include "game_ids.h"

/* Bank-2 self-contained shop / save-load render bodies (AGENTS.md 52.11.1):
 * the fixed _CODE/_HOME area is at its size limit (see make memmap), so the
 * full shop and save-load content draws live here, writing VRAM directly with
 * bank-local helpers (a banked body must never call fixed-bank functions --
 * see ui_battle_content.c / title_content.c for the same pattern).
 *
 * The fixed wrapper (shop_screen.c / save_load_screen.c) runs menu_draw_frame
 * (fixed: centered title + separator + full clear) and THIS body draws only
 * the dynamic content below the frame, so no bank-local clear is needed.
 *
 * Staged input: g_bk_ptr_a = Game*.
 */

extern char g_ui_screen_buf[18][21];
extern uint8_t ui_font_tile_base;
extern uint8_t g_is_cgb;

extern const CardDefinition g_cards[];
extern const ShopDefinition g_shops[];
extern const uint8_t g_shop_count;

/* Shop message states (mirror shop_screen.c). */
#define SC_SHOP_MSG_NONE 0
#define SC_SHOP_MSG_BOUGHT 1
#define SC_SHOP_MSG_NO_GOLD 2
#define SC_SHOP_MSG_MAX_COPIES 3

/* Item menu navigation layout constants (mirror item_screen.c). */
#define ITEM_FIRST_CARD 1
#define ITEM_VISIBLE_CARD 5

static uint8_t s_sc_txt_i;
static uint8_t s_sc_col_i;
static uint8_t s_sc_shop_i;
static uint8_t s_sc_shop_scroll;
static uint8_t s_sc_def_i;
static uint8_t s_sc_shop_pos;
static uint8_t s_sc_save_pos;

static uint8_t s_sc_y;
static uint8_t s_sc_pos;
static uint8_t s_sc_mode;
static uint8_t s_sc_old_index;
static uint8_t s_sc_new_index;
static uint8_t s_sc_old_row;
static uint8_t s_sc_new_row;
static uint8_t s_sc_need_scroll;
static uint8_t s_sc_present;
static uint8_t s_sc_tile_elem;
static uint8_t s_sc_tile_wpn;
static uint16_t s_sc_gold;
static const ShopDefinition *s_sc_shop_def;
static const CardDefinition *s_sc_card_def;
static char s_sc_str[8];
static char s_sc_code[8];
static volatile uint8_t *s_sc_dst;
static char *s_sc_buf;
static Game *s_sc_game;

static const uint16_t s_sc_p10[4] = { 10000, 1000, 100, 10 };

static void sc_format_uint(uint16_t uval, char *out)
{
    uint8_t i, started = 0;
    for (i = 0; i < 4; i++) {
        uint16_t p = s_sc_p10[i];
        uint8_t d = 0;
        while (uval >= p) {
            uval -= p;
            d++;
        }
        if (d != 0 || started) {
            *out++ = (char)('0' + d);
            started = 1;
        }
    }
    *out++ = (char)('0' + (uint8_t)uval);
    *out = '\0';
}

static void sc_vram_sync_write(volatile uint8_t *dst, uint8_t tile)
{
    if (LCDC_REG & 0x80) {
        while (STAT_REG & 0x02);
        *dst = tile;
    } else {
        *dst = tile;
    }
}

static void sc_put_char(uint8_t x, uint8_t y, char ch)
{
    uint8_t tile;

    if (y >= 18 || x >= 20) return;
    if (g_ui_screen_buf[y][x] == ch) return;
    tile = (uint8_t)(ui_font_tile_base + (uint8_t)(ch - ' '));
    VBK_REG = 0;
    sc_vram_sync_write((volatile uint8_t *)(0x9800 + ((uint16_t)y << 5) + x), tile);
    g_ui_screen_buf[y][x] = ch;
}

static void sc_draw_text(uint8_t x, uint8_t y, const char *text, uint8_t max_chars)
{
    char ch;
    uint8_t ended;

    if (y >= 18) return;
    if ((uint8_t)(x + max_chars) > 20) max_chars = (uint8_t)(20 - x);

    VBK_REG = 0;
    ended = (text == NULL);
    s_sc_dst = (volatile uint8_t *)(0x9800 + ((uint16_t)y << 5) + x);
    s_sc_buf = &g_ui_screen_buf[y][x];

    for (s_sc_txt_i = 0; s_sc_txt_i < max_chars; s_sc_txt_i++) {
        if (!ended) {
            ch = text[s_sc_txt_i];
            if (ch == '\0') {
                ended = 1;
                ch = ' ';
            }
        } else {
            ch = ' ';
        }
        if (*s_sc_buf != ch) {
            uint8_t tile = (uint8_t)(ui_font_tile_base + (uint8_t)(ch - ' '));
            sc_vram_sync_write(s_sc_dst, tile);
            *s_sc_buf = ch;
        }
        s_sc_dst++;
        s_sc_buf++;
    }
}

static void sc_color_span(uint8_t x, uint8_t y, uint8_t len, uint8_t palette)
{
    if (!g_is_cgb) return;
    if (y >= 18 || x >= 32) return;
    if ((uint8_t)(x + len) > 32) len = (uint8_t)(32 - x);

    s_sc_dst = (volatile uint8_t *)(0x9800 + ((uint16_t)y << 5) + x);
    VBK_REG = 1;
    for (s_sc_col_i = 0; s_sc_col_i < len; s_sc_col_i++) {
        sc_vram_sync_write(&s_sc_dst[s_sc_col_i], (uint8_t)(palette & 0x07));
    }
    VBK_REG = 0;
}

static const ShopDefinition *sc_shop_active(const Game *g)
{
    for (s_sc_shop_i = 0; s_sc_shop_i < g_shop_count; s_sc_shop_i++) {
        if (g_shops[s_sc_shop_i].id == g->shop_id) return &g_shops[s_sc_shop_i];
    }
    return NULL;
}

static const CardDefinition *sc_card_get_def(CardId id)
{
    for (s_sc_def_i = 0; s_sc_def_i < GAME_CARD_COUNT; s_sc_def_i++) {
        if (g_cards[s_sc_def_i].id == id) return &g_cards[s_sc_def_i];
    }
    return NULL;
}

static const char s_card_bt_codes[] = "SW\0SH\0BO\0RG\0DA";

static void sc_card_code_str(uint8_t battle_type, uint8_t power, char *out)
{
    const char *bt = (battle_type <= BATTLE_CARD_TYPE_DAGGER) ?
        (s_card_bt_codes + (battle_type * 3)) : "??";
    uint8_t tens = 0;
    uint8_t ones = power;
    while (ones >= 10) {
        ones = (uint8_t)(ones - 10);
        tens++;
    }
    out[0] = bt[0];
    out[1] = bt[1];
    if (power >= 10) {
        out[2] = (char)('0' + tens);
        out[3] = (char)('0' + ones);
        out[4] = '\0';
    } else {
        out[2] = (char)('0' + ones);
        out[3] = '\0';
    }
}

static uint8_t bm_mode(const Game *g)
{
    return (g->screen == SCREEN_SHOP) ? 2 : (uint8_t)g->item_menu_tab;
}

static uint8_t bm_cursor_row(uint8_t mode, uint8_t index, uint8_t scroll)
{
    if (mode == 2) return (uint8_t)(5u + (uint8_t)(index - scroll));
    if (mode == 1) return (uint8_t)(5 + (index << 1));
    if (index == 0) return 5;
    s_sc_pos = (uint8_t)(index - ITEM_FIRST_CARD);
    if (scroll == 0) return (uint8_t)(6 + (s_sc_pos << 1));
    return (uint8_t)(5 + ((s_sc_pos - scroll) << 1));
}

void item_menu_cursor_banked(void)
{
    s_sc_game = (Game *)g_bk_ptr_a;

    if (!s_sc_game) return;
    s_sc_mode = bm_mode(s_sc_game);
    s_sc_old_index = g_bk_byte_a;
    s_sc_new_index = g_bk_byte_b;
    s_sc_game->item_menu_index = s_sc_new_index;

    if (s_sc_mode == 0) {
        s_sc_need_scroll = s_sc_game->item_menu_scroll;
        if (s_sc_new_index <= ITEM_FIRST_CARD)
            s_sc_need_scroll = 0;
        else {
            s_sc_pos = (uint8_t)(s_sc_new_index - ITEM_FIRST_CARD);
            if (s_sc_pos < s_sc_need_scroll)
                s_sc_need_scroll = s_sc_pos;
            else if (s_sc_pos > (uint8_t)(s_sc_need_scroll + ITEM_VISIBLE_CARD - 1))
                s_sc_need_scroll = (uint8_t)(s_sc_pos - (ITEM_VISIBLE_CARD - 1));
        }
        if (s_sc_need_scroll != s_sc_game->item_menu_scroll) {
            s_sc_game->item_menu_scroll = s_sc_need_scroll;
            s_sc_game->render_cache.valid = false;
            return;
        }
    } else if (s_sc_mode == 2) {
        /* Shop list: index is a direct stock position (0-based).  Keep the
         * cursor inside the SHOP_VISIBLE window; a window shift needs a
         * full redraw (the banked body cannot repaint scrolled rows). */
        s_sc_need_scroll = s_sc_game->item_menu_scroll;
        if (s_sc_new_index < s_sc_need_scroll)
            s_sc_need_scroll = s_sc_new_index;
        else if (s_sc_new_index > (uint8_t)(s_sc_need_scroll + SHOP_VISIBLE - 1))
            s_sc_need_scroll = (uint8_t)(s_sc_new_index - (SHOP_VISIBLE - 1));
        if (s_sc_need_scroll != s_sc_game->item_menu_scroll) {
            s_sc_game->item_menu_scroll = s_sc_need_scroll;
            s_sc_game->render_cache.valid = false;
            return;
        }
    }

    s_sc_old_row = bm_cursor_row(s_sc_mode, s_sc_old_index, s_sc_game->item_menu_scroll);
    s_sc_new_row = bm_cursor_row(s_sc_mode, s_sc_new_index, s_sc_game->item_menu_scroll);
    if (s_sc_old_row != s_sc_new_row) {
        sc_put_char(0, s_sc_old_row, ' ');
        sc_put_char(0, s_sc_new_row, '>');
    }
}

void shop_content_render(void)
{
    s_sc_game = (Game *)g_bk_ptr_a;

    if (!s_sc_game) return;
    s_sc_shop_def = sc_shop_active(s_sc_game);
    s_sc_gold = (uint16_t)s_sc_game->state.currency.amount[0];

    sc_draw_text(0, 3, "  GOLD: ", 8);
    sc_format_uint(s_sc_gold, s_sc_str);
    sc_draw_text(8, 3, s_sc_str, 10);

    s_sc_dst = (volatile uint8_t *)(0x9800 + ((uint16_t)3 << 5) + 0);
    VBK_REG = 0;
    sc_vram_sync_write(s_sc_dst, UI_TILE_COIN);
    sc_color_span(0, 3, 1, UI_COLOR_GOLD);

    if (!s_sc_shop_def) {
        sc_draw_text(0, 5, "(nothing)", 9);
        sc_draw_text(0, 7, "[B] Leave", 9);
        return;
    }

    s_sc_shop_scroll = s_sc_game->item_menu_scroll;
    for (s_sc_shop_pos = s_sc_shop_scroll;
         s_sc_shop_pos < s_sc_shop_def->count &&
         s_sc_shop_pos < (uint8_t)(s_sc_shop_scroll + SHOP_VISIBLE);
         s_sc_shop_pos++) {
        s_sc_card_def = sc_card_get_def(s_sc_shop_def->items[s_sc_shop_pos]);
        s_sc_y = (uint8_t)(5 + (uint8_t)(s_sc_shop_pos - s_sc_shop_scroll));
        sc_put_char(0, s_sc_y, (s_sc_game->item_menu_index == s_sc_shop_pos) ? '>' : ' ');
        if (s_sc_card_def) {
            /* Icon tiles mirror the battle hand (screens/cards_skin.json):
             * weapon icon only.  Element riders are OAM sprites on the
             * battle screen: the element cell stays blank and the element
             * reads from the color span below (FIRE/POISON/ICE slots). */
            s_sc_tile_elem = 0;

            if (s_sc_card_def->battle_type == BATTLE_CARD_TYPE_HEAL || s_sc_card_def->effect == CARD_EFFECT_HEAL_HP)
                s_sc_tile_wpn = UI_TILE_CARD_RING;
            else if (s_sc_card_def->battle_type == BATTLE_CARD_TYPE_SHIELD)
                s_sc_tile_wpn = UI_TILE_CARD_SHIELD;
            else if (s_sc_card_def->battle_type == BATTLE_CARD_TYPE_BOW)
                s_sc_tile_wpn = UI_TILE_CARD_BOW;
            else if (s_sc_card_def->battle_type == BATTLE_CARD_TYPE_DAGGER)
                s_sc_tile_wpn = UI_TILE_CARD_DAGGER;
            else
                s_sc_tile_wpn = UI_TILE_CARD_SWORD;

            sc_card_code_str(s_sc_card_def->battle_type, s_sc_card_def->power, s_sc_code);
            sc_draw_text(3, s_sc_y, s_sc_code, 4);

            s_sc_dst = (volatile uint8_t *)(0x9800 + ((uint16_t)s_sc_y << 5) + 1);
            VBK_REG = 0;
            sc_vram_sync_write(s_sc_dst, s_sc_tile_elem);
            sc_vram_sync_write(s_sc_dst + 1, s_sc_tile_wpn);

            /* Icon palettes mirror the battle-hand display slots
             * (generated card_display_slots.json: weapons all WOOD/5,
             * fire 1, ice 2, poison 4): the element cell takes its
             * element slot, the weapon cell WOOD.  Code/price text
             * stays on the default paper slot.  Bank-2 bodies cannot
             * read the bank-4 skin const, so the skin mapping is
             * mirrored here -- keep in sync with cards_skin.json. */
            if (s_sc_card_def->status_id == STATUS_BURN)
                sc_color_span(1, s_sc_y, 1, UI_COLOR_FIRE);
            else if (s_sc_card_def->status_id == STATUS_POISON)
                sc_color_span(1, s_sc_y, 1, UI_COLOR_POISON);
            else if (s_sc_card_def->status_id == STATUS_FREEZE)
                sc_color_span(1, s_sc_y, 1, UI_COLOR_ICE);
            sc_color_span(2, s_sc_y, 1, UI_COLOR_WOOD);
        } else {
            sc_draw_text(1, s_sc_y, "???", 3);
        }
        sc_format_uint(s_sc_card_def ? (uint16_t)s_sc_card_def->price : 0, s_sc_str);
        sc_draw_text(12, s_sc_y, s_sc_str, 4);
        sc_put_char(16, s_sc_y, 'G');
        s_sc_dst = (volatile uint8_t *)(0x9800 + ((uint16_t)s_sc_y << 5) + 16);
        VBK_REG = 0;
        sc_vram_sync_write(s_sc_dst, UI_TILE_COIN);
        sc_color_span(16, s_sc_y, 1, UI_COLOR_GOLD);
    }

    sc_draw_text(0, 16, "[A] Buy  [B] Leave", 18);
    if (s_sc_game->shop_message != SC_SHOP_MSG_NONE) {
        sc_draw_text(0, 17,
                     (s_sc_game->shop_message == SC_SHOP_MSG_BOUGHT) ? "Bought!" :
                     (s_sc_game->shop_message == SC_SHOP_MSG_MAX_COPIES) ? "Too many!" : "Not enough!",
                     12);
    }
}

void save_load_content_render(void)
{
    s_sc_game = (Game *)g_bk_ptr_a;

    if (!s_sc_game) return;
    s_sc_present = g_bk_byte_a;

    for (s_sc_save_pos = 0; s_sc_save_pos < SAVE_SLOT_COUNT; s_sc_save_pos++) {
        s_sc_y = (uint8_t)(4 + (s_sc_save_pos << 1));
        sc_put_char(0, s_sc_y, (s_sc_game->save_slot_index == s_sc_save_pos) ? '>' : ' ');
        sc_draw_text(1, s_sc_y, (s_sc_save_pos == 0) ? "SLOT 1:" : ((s_sc_save_pos == 1) ? "SLOT 2:" : "SLOT 3:"), 7);
        sc_draw_text(9, s_sc_y, (s_sc_present & (1 << s_sc_save_pos)) ? "SAVED" : "(EMPTY)", 7);
    }
    sc_draw_text(0, 11, (s_sc_game->save_slot_mode == 1) ? "[A] SAVE  " : "[A] LOAD  ", 10);
    sc_draw_text(10, 11, "[B] BACK", 8);
    if (s_sc_game->save_slot_message != 0) {
        sc_draw_text(2, 13, (s_sc_game->save_slot_message == 1) ? "Game Saved!" : "Slot is empty!", 14);
    }
}
