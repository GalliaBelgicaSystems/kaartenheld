#pragma bank 4
#pragma disable_warning 110

#include "ui.h"
#include "screen.h"
#include "banked.h"
#include <gb/gb.h>
#include <gb/cgb.h>

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

static void simple_stamp_button(uint8_t x, uint8_t y, uint8_t tile)
{
    volatile uint8_t *dst;
    if (y >= 18 || x >= 20) return;
    VBK_REG = 0;
    dst = (volatile uint8_t *)(0x9800 + ((uint16_t)y << 5) + x);
    simple_vram_sync_write(dst, tile);
    g_ui_screen_buf[y][x] = ' ';
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
    static const char * const s_lines[8] = {
        "     DEMO OVER",
        "--------------------",
        "Congratulations on",
        "beating the battle",
        "demo of Kaartenheld!",
        "Follow",
        "@GBelgicaSystems",
        "on X for updates!"
    };
    static const uint8_t s_rows[8] = { 1, 2, 4, 5, 6, 8, 9, 10 };
    /* Ending page: white background, bronze-tan ink like the dialogue
     * boxes.  Program every BG slot to the paper ramp (both light
     * shades read white; shade 2 stays black, shade 3 is the bronze
     * dialogue ink) so the whole screen is one sheet whatever attribute
     * a cell carries -- no attribute rewrite needed.  The cleared tiles
     * are shade-0 paper and the font ink is shade 3, so on DMG (BGP 0xE4
     * from boot, the only value the ROM ever programs) this is black on
     * white with no CRAM write at all; on CGB the text reads bronze. */
    if (g_is_cgb) {
        static const uint8_t s_paper[8] = {
            0xFF, 0x7F,   /* RGB8(255,255,255) */
            0xFF, 0x7F,   /* light pair reads white: one sheet */
            0x00, 0x00,   /* RGB8(0,0,0) */
            0x6E, 0x19    /* RGB8(117,89,48) bronze ink, like dialogue text */
        };
        uint8_t p;
        for (p = 0; p < 8; p++) {
            BCPS_REG = (uint8_t)(0x80u | (p << 3));
            for (i = 0; i < 8; i++) {
                BANKED_VRAM_WRITE(&BCPD_REG, s_paper[i]);
            }
        }
    }

    for (i = 0; i < 8; i++) {
        simple_draw_text(0, s_rows[i], s_lines[i], 20);
    }
    simple_stamp_button(0, 14, UI_TILE_BUTTON_A);
    simple_draw_text(1, 14, "RESTART", 19);
}

void game_over_content_render(void)
{
    uint8_t choice = g_bk_byte_a;

    simple_draw_text(4, 3, "GAME OVER", 9);
    simple_draw_text(3, 8, "CONTINUE?", 9);
    simple_draw_text(0, 11, (choice == 0) ? "> YES" : "  YES", 5);
    simple_draw_text(0, 12, (choice != 0) ? "> NO" : "  NO", 4);
    simple_stamp_button(1, 16, UI_TILE_BUTTON_A);
    simple_draw_text(2, 16, "CONFIRM", 18);
}
