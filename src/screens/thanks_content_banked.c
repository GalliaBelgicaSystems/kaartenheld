#pragma bank 9
#pragma disable_warning 110

#include <gb/gb.h>
#include "banked.h"

/* Battle-demo outro body.  Lives in bank 9 (not bank 4 with the other
 * simple screens) because bank 4 is 13 bytes short of fitting the four
 * thanks lines (release link overflowed 0x13fee -> 0x1400d); bank 9 is
 * the project's overflow bank with kilobytes to spare.  Self-contained
 * like every banked body (AGENTS.md 52.11.1): own-bank strings only,
 * staged WRAM globals, no fixed-bank calls.  The fixed wrapper in
 * simple_screens.c ui_clear_screen()s first (VRAM + g_ui_screen_buf
 * mirror), so unconditional writes below stay consistent. */

extern char g_ui_screen_buf[18][21];
extern uint8_t ui_font_tile_base;

static void thanks_draw_line(uint8_t x, uint8_t y, const char *text, uint8_t len)
{
    uint8_t i;
    volatile uint8_t *dst;

    if (y >= 18) return;
    VBK_REG = 0;
    dst = (volatile uint8_t *)(0x9800 + ((uint16_t)y << 5) + x);
    for (i = 0; i < len; i++) {
        uint8_t tile = (uint8_t)(ui_font_tile_base + (uint8_t)(text[i] - ' '));
        BANKED_VRAM_WRITE(dst, tile);
        g_ui_screen_buf[y][(uint8_t)(x + i)] = text[i];
        dst++;
    }
}

void thanks_content_render(void)
{
    thanks_draw_line(1, 3, "Congratulations for", 19);
    thanks_draw_line(1, 5, "beating the battle", 18);
    thanks_draw_line(1, 7, "demo!", 5);
    thanks_draw_line(1, 9, "Full game out", 13);
    thanks_draw_line(1, 11, "sometime in the", 15);
    thanks_draw_line(1, 13, "future!", 7);
}
