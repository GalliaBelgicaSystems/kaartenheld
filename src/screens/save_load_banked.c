#pragma bank 11

#include <gb/gb.h>
#include <gb/cgb.h>
#include "game.h"
#include "ui.h"
#include "banked.h"
#include "rpg/save.h"

/* ── Banked save/load menu render body (WRAM trampoline, AGENTS.md §52.11.1)
 * Moved out of screen_content.c (ROM bank 2) to relieve the shop
 * full-name/sell-list render pressure (Nov 2026).  The fixed wrapper in
 * save_load_screen.c stages the Game* + present-mask and dispatches
 * here; this body paints the 3 slot rows + action line + transient
 * message with its own file-local text helpers (duplicated from
 * screen_content.c so bank 2 keeps no dependency on this bank).
 *
 * Self-contained: WRAM-only helpers below, staging globals, Game struct
 * (header), SAVE_SLOT_COUNT (header).  No ROM const tables, no
 * fixed-bank calls (ui_vram_write goes through the atomic per-byte
 * form), no di/ei (§52.11.3), no * / % (§52.18).  Tilemap base matches
 * screen_content.c (SC_TILE_BASE 0, ASCII-direct font layout). */

static Game *s_sl_game;
static uint8_t s_sl_present;
static uint8_t s_sl_pos;
static uint8_t s_sl_y;

static void sl_put_char(uint8_t x, uint8_t y, char c)
{
    uint8_t t;

    if (y >= 18 || x >= 20 || c == '\0') return;
    if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    if (g_ui_screen_buf[y][x] == c) return;
    t = (uint8_t)(ui_font_tile_base + (uint8_t)(c - ' '));
    VBK_REG = 0;
    BANKED_VRAM_WRITE((volatile uint8_t *)(0x9800 + ((uint16_t)y << 5) + x), t);
    g_ui_screen_buf[y][x] = c;
}

static void sl_draw_text(uint8_t x, uint8_t y, const char *text, uint8_t max_chars)
{
    uint8_t i;
    if (!text) return;
    for (i = 0; i < max_chars && text[i] != '\0'; i++) {
        sl_put_char((uint8_t)(x + i), y, text[i]);
    }
}

void save_load_content_render(void)
{
    s_sl_game = (Game *)g_bk_ptr_a;

    if (!s_sl_game) return;
    s_sl_present = g_bk_byte_a;

    for (s_sl_pos = 0; s_sl_pos < SAVE_SLOT_COUNT; s_sl_pos++) {
        s_sl_y = (uint8_t)(4 + (s_sl_pos << 1));
        sl_put_char(0, s_sl_y, (s_sl_game->save_slot_index == s_sl_pos) ? '>' : ' ');
        sl_draw_text(1, s_sl_y, (s_sl_pos == 0) ? "SLOT 1:" : ((s_sl_pos == 1) ? "SLOT 2:" : "SLOT 3:"), 7);
        sl_draw_text(9, s_sl_y, (s_sl_present & (1 << s_sl_pos)) ? "SAVED" : "(EMPTY)", 7);
    }
    sl_draw_text(0, 11, (s_sl_game->save_slot_mode == 1) ? "[A] SAVE  " : "[A] LOAD  ", 10);
    sl_draw_text(10, 11, "[B] BACK", 8);
    if (s_sl_game->save_slot_message != 0) {
        sl_draw_text(2, 13, (s_sl_game->save_slot_message == 1) ? "Game Saved!" : "Slot is empty!", 14);
    }
}
