#pragma bank 12

#include <gb/gb.h>
#include <gb/cgb.h>
#include "game.h"
#include "ui.h"
#include "banked.h"

/* ── Boot autosave notice bodies (ROM bank 12) ─────────────────────
 * White screen + the autosave line, with the text ink fading in from
 * white, holding black, and fading back to white (fixed wrapper owns
 * timing/skip/advance).  Self-contained (AGENTS.md §52.11.1): WRAM
 * game state + ROM font reads + CRAM only, never a fixed-bank call,
 * no trampoline nesting, no di/ei (§52.11.3), no * / % (§52.18).
 *
 * Palette model: CGB slot 0 carries white in colors 0-2 with the ink
 * in color 3 (the font's ink index, verified against the font tiles);
 * DMG uses BGP with the same shape (colors 0-2 stay white).  Only the
 * ink moves, so the fade cannot disturb any other ramp. */

extern char g_ui_screen_buf[18][21];
extern uint8_t ui_font_tile_base;
extern uint8_t g_is_cgb;

/* Timeline mirrors notice_screen.c (fixed): fade in 0-15, hold to 107,
 * fade out 108-122, advance at 123. */
#define NOTICE_IN_END   16
#define NOTICE_HOLD_END 108
#define NOTICE_END      123

static void notice_put_char(uint8_t x, uint8_t y, char ch)
{
    volatile uint8_t *dst;
    char *buf;
    uint8_t tile;

    if (y >= 18 || x >= 20 || ch == '\0') return;
    if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);
    buf = &g_ui_screen_buf[y][x];
    if (*buf == ch) return;
    tile = (uint8_t)(ui_font_tile_base + (uint8_t)(ch - ' '));
    VBK_REG = 0;
    dst = (volatile uint8_t *)(0x9800 + ((uint16_t)y << 5) + x);
    BANKED_VRAM_WRITE(dst, tile);
    *buf = ch;
}

static void notice_draw_text(uint8_t x, uint8_t y, const char *text, uint8_t max_chars)
{
    uint8_t i;
    if (!text) return;
    for (i = 0; i < max_chars && text[i] != '\0'; i++) {
        notice_put_char((uint8_t)(x + i), y, text[i]);
    }
}

/* Pack an 8-step grayscale ink (v: 0 = black .. 31 = white) into a
 * CRAM slot-0 color-3 entry.  Same packing as the despair fade
 * (ui_dialogue_box_banked.c). */
static void notice_ink_cgb(uint8_t v)
{
    uint8_t lo = (uint8_t)((v << 5) | v);
    uint8_t hi = (uint8_t)((v << 2) | (v >> 3));
    BCPS_REG = (uint8_t)(0x80 | (3 << 1));
    BANKED_VRAM_WRITE(&BCPD_REG, lo);
    BANKED_VRAM_WRITE(&BCPD_REG, hi);
}

void notice_content_render(void)
{
    uint8_t i;
    /* Slot 0: white everywhere, ink white (the fade darkens it). */
    VBK_REG = 0;
    BCPS_REG = (uint8_t)(0x80 | (0 << 3));
    for (i = 0; i < 8; i++) {
        BANKED_VRAM_WRITE(&BCPD_REG, (uint8_t)(i & 1 ? 0x7F : 0xFF));
    }
    if (!g_is_cgb) {
        BGP_REG = 0x00;
    }
    notice_draw_text(3, 8, "THIS GAME HAS", 13);
    notice_draw_text(1, 9, "AN AUTOSAVE FEATURE", 19);
}

void notice_fade_banked(void)
{
    uint8_t f = g_game.notice_frame;
    uint8_t v;
    if (f < NOTICE_IN_END) {
        /* Fade in: white -> near-black. */
        v = (uint8_t)(31 - ((f >> 1) << 2));
    } else if (f < NOTICE_HOLD_END) {
        v = 0;
    } else {
        /* Fade out: black -> near-white (title repaints after). */
        v = (uint8_t)(((f - NOTICE_HOLD_END) >> 1) << 2);
    }
    if (g_is_cgb) {
        notice_ink_cgb(v);
    } else {
        /* DMG: ink is BGP bits 7-6; bg colors stay white. */
        BGP_REG = (uint8_t)(((31 - v) >> 3) << 6);
    }
}
