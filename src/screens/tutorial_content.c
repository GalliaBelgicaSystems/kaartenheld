#pragma bank 4
#pragma disable_warning 110

#include "ui.h"
#include "banked.h"
#include "screen.h"
#include <gb/gb.h>

/* Bank-2 self-contained tutorial render body (AGENTS.md 52.11.1):
 * the fixed _CODE/_HOME area is at its size limit (see make memmap), so
 * the entire tutorial text draw lives here, writing VRAM directly
 * with bank-local helpers (a banked body must never call fixed-bank
 * functions -- see title_content.c for the same pattern).
 *
 * Staged inputs (set by the fixed wrapper in tutorial_screen.c):
 *   g_bk_byte_a = tutorial_slide (0..TUTORIAL_SLIDE_COUNT-1) */

extern char g_ui_screen_buf[18][21];
extern uint8_t ui_font_tile_base;

/* Inline button-icon map for the {A} {B} {SELECT} {LR} {UD} control
 * bytes (0x80..0x86) the compiler emits: same streamed
 * UI_TILE_BUTTON_* tiles the footer stamps, so no VRAM/streaming
 * change.  {SELECT} is three cells wide (SELECT_1/2/3, like START). */
static const uint8_t s_tut_buttons[7] = {
    UI_TILE_BUTTON_A,
    UI_TILE_BUTTON_B,
    UI_TILE_BUTTON_SELECT_1,
    UI_TILE_BUTTON_SELECT_2,
    UI_TILE_BUTTON_SELECT_3,
    UI_TILE_BUTTON_DPAD_LR,
    UI_TILE_BUTTON_DPAD_UD
};

static void tutorial_vram_sync_write(volatile uint8_t *dst, uint8_t tile)
{
    BANKED_VRAM_WRITE(dst, tile);
}

static void tutorial_stamp_button(uint8_t x, uint8_t y, uint8_t tile)
{
    volatile uint8_t *dst;
    if (y >= 18 || x >= 20) return;
    VBK_REG = 0;
    dst = (volatile uint8_t *)(0x9800 + ((uint16_t)y << 5) + x);
    tutorial_vram_sync_write(dst, tile);
    g_ui_screen_buf[y][x] = ' ';
}

static void tutorial_draw_text(uint8_t x, uint8_t y, const char *text, uint8_t max_chars)
{
    uint8_t i;
    uint8_t raw;
    uint8_t is_icon;
    char ch;
    uint8_t tile;
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
        raw = (uint8_t)text[i];
        if (raw == 0) ended = 1;
        is_icon = (uint8_t)(!ended && raw >= 0x80u);
        if (ended) {
            ch = ' ';
            tile = (uint8_t)(ui_font_tile_base + (uint8_t)(' ' - ' '));
        } else if (is_icon) {
            /* Icon cell: stamp the button tile.  The buffer keeps ' '
             * (harness text asserts stay readable), but the write is
             * unconditional -- a cleared ' ' buffer would otherwise
             * skip it and leave blank VRAM (same reason the footer
             * stamps buttons unconditionally). */
            ch = ' ';
            tile = s_tut_buttons[(uint8_t)(raw - 0x80u)];
        } else {
            ch = (char)raw;
            tile = (uint8_t)(ui_font_tile_base + (uint8_t)(raw - (uint8_t)' '));
        }
        if (*buf != ch || is_icon) {
            BANKED_VRAM_WRITE(dst, tile);
            *buf = ch;
        }
        dst++;
        buf++;
    }
}

/* Slide text arrays + TUTORIAL_SLIDE_COUNT live in the generated
 * tutorial_text_generated.h (single source: screens/tutorial.json,
 * emitted by tools/screen_compiler/tutorial_compile.py).  Humans
 * edit slides in the editor — never hand-write arrays here. */
#include "tutorial_text_generated.h"

void tutorial_content_render(void)
{
    uint8_t slide = g_bk_byte_a;
    const char (*lines)[20];
    uint8_t count = 4;
    uint8_t i;

    if (slide >= TUTORIAL_SLIDE_COUNT) slide = 0;
    lines = s_tut_slides[slide];

    for (i = 0; i < count; i++) {
        tutorial_draw_text(1, (uint8_t)(7 + i), lines[i], 18);
    }
    tutorial_stamp_button(2, 16, UI_TILE_BUTTON_DPAD_LR);
    tutorial_draw_text(3, 16, "NAV  ", 5);
    tutorial_stamp_button(8, 16, UI_TILE_BUTTON_B);
    tutorial_draw_text(9, 16, "EXIT", 11);
}