#pragma bank 4
#pragma disable_warning 110

#include "ui.h"
#include "screen.h"
#include "banked.h"
#include <gb/gb.h>

/* Bank-2 self-contained render bodies for the simple screens (thanks /
 * ending / game-over).  The fixed _CODE/_HOME area is at its size limit
 * (see make memmap), so the string data + text drawing for these screens
 * lives here, writing VRAM directly with bank-local helpers (AGENTS.md
 * 52.11.1 -- a banked body must never call fixed-bank functions).
 *
 * Staged inputs (set by the fixed wrappers in simple_screens.c):
 *   for the game-over body, g_bk_byte_a = game_over_choice.
 */

extern char g_ui_screen_buf[18][21];
extern uint8_t ui_font_tile_base;

static void simple_vram_sync_write(volatile uint8_t *dst, uint8_t tile)
{
    BANKED_VRAM_WRITE(dst, tile);
}

static void simple_put_char(uint8_t x, uint8_t y, char ch)
{
    if (y < 18 && x < 20) {
        if (g_ui_screen_buf[y][x] != ch) {
            uint8_t tile = (uint8_t)(ui_font_tile_base + (uint8_t)(ch - ' '));
            volatile uint8_t *dst = (volatile uint8_t *)(0x9800 + ((uint16_t)y << 5) + x);
            VBK_REG = 0;
            simple_vram_sync_write(dst, tile);
            g_ui_screen_buf[y][x] = ch;
        }
    }
}

static void simple_draw_text(uint8_t x, uint8_t y, const char *text, uint8_t max_chars)
{
    uint8_t i;
    char ch;
    volatile uint8_t *dst;
    char *buf;
    uint8_t ended;

    if (y >= 18) return;
    if ((uint8_t)(x + max_chars) > 20) max_chars = (uint8_t)(20 - x);

    VBK_REG = 0;
    ended = 0;
    dst = (volatile uint8_t *)(0x9800 + ((uint16_t)y << 5) + x);
    buf = &g_ui_screen_buf[y][x];

    for (i = 0; i < max_chars; i++) {
        ch = text[i];
        if (ch == '\0') ended = 1;
        if (ended) ch = ' ';
        if (*buf != ch) {
            uint8_t tile = (uint8_t)(ui_font_tile_base + (uint8_t)(ch - ' '));
            simple_vram_sync_write(dst, tile);
            *buf = ch;
        }
        dst++;
        buf++;
    }
}

void ending_content_render(void)
{
    uint8_t i;
    static const char * const s_lines[7] = {
        "      THE END",
        "--------------------",
        "The Hero cleared",
        "the land of slimes!",
        "Peace has returned!",
        "Thanks for playing!",
        "[A] RESTART"
    };
    static const uint8_t s_rows[7] = { 1, 2, 4, 5, 7, 9, 14 };

    for (i = 0; i < 7; i++) {
        simple_draw_text(0, s_rows[i], s_lines[i], 20);
    }
}

void game_over_content_render(void)
{
    uint8_t choice = g_bk_byte_a;

    simple_draw_text(4, 3, "GAME OVER", 9);
    simple_draw_text(3, 8, "CONTINUE?", 9);
    simple_draw_text(0, 11, (choice == 0) ? "> YES" : "  YES", 5);
    simple_draw_text(0, 12, (choice != 0) ? "> NO" : "  NO", 4);
    simple_draw_text(1, 16, "[A] CONFIRM", 11);
}
