#pragma bank 3

#include <gb/gb.h>
#include <gb/cgb.h>
#include "ui.h"
#include "banked.h"

/* ── CGB effect-color support, bank-3 body (docs/loot.md §34) ─────────
 * The palette ramps and the VRAM bank-1 attribute writer live here so the
 * fixed bank stays under 0x8000 (AGENTS.md §55.5).  ui_init() banked_copies
 * the palette table into WRAM to program BCPS; the fixed-bank ui_color_span()
 * wrapper stages x/y/len/palette through g_bk_ptr_a and dispatches here.
 */



/* Self-contained VRAM-sync write (banked code cannot call ui_vram_sync_write).
 * di/ei guards the PPU wait; unconditionally re-enabling IME via ei is safe
 * because interrupts are always active during normal play and stubbed under the harness. */
static void color_vram_sync_write(volatile uint8_t *dst, uint8_t v)
{
    if (LCDC_REG & 0x80) {
        while (STAT_REG & 0x02);
        *dst = v;
    } else {
        *dst = v;
    }
}

/* Reads the staged x/y/len/palette from g_bk_ptr_a (see ui_color_span()). */
void ui_color_span_banked(void)
{
    uint8_t *a = (uint8_t *)g_bk_ptr_a;
    uint8_t x = a[0];
    uint8_t y = a[1];
    uint8_t len = a[2];
    uint8_t palette = a[3];
    uint8_t i;
    volatile uint8_t *dst;

    if (!g_is_cgb) return;
    if (y >= 18 || x >= 32) return;
    if ((uint8_t)(x + len) > 32) len = (uint8_t)(32 - x);

    dst = (volatile uint8_t *)(0x9800 + ((uint16_t)y << 5) + x);
    VBK_REG = 1;
    for (i = 0; i < len; i++) {
        color_vram_sync_write(&dst[i], (uint8_t)(palette & 0x07));
    }
    VBK_REG = 0;
}

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

/* Dialogue box paper + camera sync, bank-3 body (see ui.h).  Reads the
 * camera low byte from g_bk_byte_c.  The paper palette (CRAM slot 4,
 * re-programmed here to a pure white/black document ramp: pure white box,
 * pure black font ink in every tileset) must land on the same SCROLLED
 * tilemap cells as the box text -- a fixed 0..19 span paints the wrong
 * cells whenever scroll_x != 0, leaving the box on world background.
 * Palette 0 cannot be used: it anchors the world tiles' background tone
 * (tan in the village, slate in the desolate lands).  Slot 4 is free in
 * every world tileset and the quick screen (its only other consumer)
 * cannot be open during a dialogue; every screen transition re-programs
 * CRAM, restoring the set's own slot-4 ramp.  SCX/SCY are re-synced here
 * because the post-battle victory dialogue arrives with SCX/SCY = 0
 * (screen_change zeroes them for battle) while its text is drawn at the
 * scrolled cells -- without this the box shifts right and clips. */
static const uint8_t s_dialogue_paper[8] = {
    0xFF, 0x7F, /* RGB8(255,255,255) */
    0x39, 0x67, /* RGB8(200,200,200) */
    0x4A, 0x29, /* RGB8(85,85,85) */
    0x00, 0x00  /* RGB8(0,0,0) */
};

void ui_dialogue_box_banked(void)
{
    uint8_t sy = g_bk_byte_b;
    uint8_t i;
    volatile uint8_t *dst;
    /* First box row in tilemap space.  Rows are painted full-width (see
     * ui.h): sy shifts the base down on vertically scrolled maps.  The
     * validator caps height at 24 (sy <= 6), so (sy << 5) fits in a byte
     * and rows never wrap the 32-row ring. */
    dst = (volatile uint8_t *)((uint16_t)(0x9980u + ((uint8_t)(sy << 5))));
    if (g_is_cgb) {
        BCPS_REG = (uint8_t)(0x80u | (UI_COLOR_PAPER << 3));
        for (i = 0; i < 8; i++) {
            BCPD_REG = s_dialogue_paper[i];
        }
    }
    /* Direct VRAM writes: the sole caller (ui_draw_dialogue, first dialogue
     * frame) runs with the LCD off, so no PPU wait is needed. */
    VBK_REG = 1;
    i = 192;
    do {
        *dst = (uint8_t)(UI_COLOR_PAPER & 0x07);
        dst++;
    } while (--i != 0);
    VBK_REG = 0;
    /* SCX re-sync (SCY is 0 on every map: all are 18 tall): the post-battle
     * victory dialogue arrives with SCX = 0 (screen_change zeroes it for
     * battle) while its text is drawn at the scrolled cells -- without
     * this the box shifts right and clips. */
    SCX_REG = g_bk_byte_c;
}

/* Reset every BG tilemap attribute byte (0x9800-0x9BFF) to palette 0
 * (grayscale).  The span writers above only ever SET attributes, so without
 * this wipe stale palettes leak across full redraws: overworld tiles at the
 * coordinates of a previous menu/battle span keep their tint, and a menu row
 * that previously held a colored name renders its description text in the
 * old palette.  Called from ui_lcd_off() while the LCD is off, so the
 * per-byte PPU wait inside color_vram_sync_write is skipped. */
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
