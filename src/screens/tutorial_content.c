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

static void tutorial_vram_sync_write(volatile uint8_t *dst, uint8_t tile)
{
    if (LCDC_REG & 0x80) {
        while (STAT_REG & 0x02);
        *dst = tile;
    } else {
        *dst = tile;
    }
}

static void tutorial_put_char(uint8_t x, uint8_t y, char ch)
{
    if (y < 18 && x < 20) {
        if (g_ui_screen_buf[y][x] != ch) {
            uint8_t tile = (uint8_t)(ui_font_tile_base + (uint8_t)(ch - ' '));
            volatile uint8_t *dst = (volatile uint8_t *)(0x9800 + ((uint16_t)y << 5) + x);
            VBK_REG = 0;
            tutorial_vram_sync_write(dst, tile);
            g_ui_screen_buf[y][x] = ch;
        }
    }
}

static void tutorial_draw_text(uint8_t x, uint8_t y, const char *text, uint8_t max_chars)
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
            tutorial_vram_sync_write(dst, tile);
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
        tutorial_draw_text(2, (uint8_t)(5 + i), lines[i], 18);
    }
    tutorial_draw_text(2, 16, "[LR] NAV  [B] EXIT", 18);
}