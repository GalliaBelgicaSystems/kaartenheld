#pragma bank 3

#include <gb/gb.h>
#include <gb/cgb.h>
#include "ui.h"
#include "banked.h"

/* ── CGB attribute clear, bank-3 body ───────────────────────────────
 * The VRAM bank-1 attribute clear lives here so the fixed bank stays
 * under 0x8000 (AGENTS.md §55.5).  Runs LCD-off from ui_lcd_off(), so the
 * 1024-byte wipe is plain writes (no PPU waits needed).
 */

/* DEBUG-only font test body.  Lives in bank 3 so the debug fixed bank keeps
 * a real margin under 0x8000 (AGENTS.md §52.18);
 * ui_draw_font_test() in ui.c is the thin fixed-bank wrapper that stages the
 * bank+target and dispatches here.  Self-contained: it cannot call fixed-bank
 * helpers, so the 18x20 VRAM clear is inlined. */
#ifdef DEBUG_BUILD
void ui_draw_font_test_banked(void)
{
    uint8_t ch, row, col;
    uint8_t x, y;
    volatile uint8_t *v;

    VBK_REG = 0;
    for (y = 0; y < 18; y++) {
        v = (volatile uint8_t *)(0x9800 + ((uint16_t)y << 5));
        for (x = 0; x < 20; x++) {
            v[x] = ui_font_tile_base;
            g_ui_screen_buf[y][x] = ' ';
        }
        g_ui_screen_buf[y][20] = '\0';
    }

    row = 0;
    col = 0;
    for (ch = ' '; ch <= '~'; ch++) {
        VBK_REG = 0;
        ((volatile uint8_t *)0x9800)[row * 32 + col] = (uint8_t)(ui_font_tile_base + (uint8_t)(ch - ' '));
        g_ui_screen_buf[row][col] = (char)ch;
        col++;
        if (col == 20) {
            col = 0;
            row += 2;
        }
    }
}
#endif


/* Reset every BG tilemap attribute byte (0x9800-0x9BFF) to palette 0
 * (grayscale).  Span writers only ever SET attributes, so without this wipe
 * stale palettes leak across full redraws.  Called from ui_lcd_off() while
 * the LCD is off, so the wipe is plain writes. */
void ui_clear_atts_banked(void)
{
    uint16_t i;
    volatile uint8_t *dst;

    if (!g_is_cgb) return;
    dst = (volatile uint8_t *)0x9800;
    VBK_REG = 1;
    for (i = 0; i < 1024; i++) {
        dst[i] = 0;
    }
    VBK_REG = 0;
}
