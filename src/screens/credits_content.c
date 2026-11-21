#pragma bank 10
#pragma disable_warning 110

#include <gb/gb.h>
#include <gb/cgb.h>
#include "game.h"
#include "screen.h"
#include "banked.h"

/* ── Credits: static contributor screen (bank-10 body) ─────────
 * One page (the slide byte is unused): header, four credit blocks, and
 * an exit footer.  Lines are hardcoded like the intro slides in
 * title_content.c (banked data, no compiler step).  All lines fit 20
 * columns; the font is unaccented ASCII, so "Berault" carries no accent.
 *
 * Lives in bank 10, not bank 4 with the other title bodies: the credits
 * text + draw loop overflowed bank 4's 16 KB budget (AGENTS.md 52.18),
 * and release bank 5 (scene rows) has no room either.  The text/color
 * helpers mirror title_content.c's bank-local pair (same buffer-checked
 * VRAM protocol, same g_ui_screen_buf mirror): a banked body cannot
 * call across banks, so sharing one copy is impossible.  KEEP IN SYNC
 * if that protocol changes. */

extern char g_ui_screen_buf[18][21];
extern uint8_t ui_font_tile_base;

static void credits_vram_sync_write(volatile uint8_t *dst, uint8_t tile)
{
    BANKED_VRAM_WRITE(dst, tile);
}

static void credits_draw_text(uint8_t x, uint8_t y, const char *text, uint8_t max_chars)
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
            credits_vram_sync_write(dst, tile);
            *buf = ch;
        }
        dst++;
        buf++;
    }
}

void credits_content_render(void)
{
    credits_draw_text(6, 0, "CREDITS", 7);
    credits_draw_text(0, 2, "Game design by", 14);
    credits_draw_text(0, 3, "Bruno Rodrigues", 15);
    credits_draw_text(0, 4, "and Florent Berault", 19);
    credits_draw_text(0, 6, "Programming by", 14);
    credits_draw_text(0, 7, "Bruno Rodrigues", 15);
    credits_draw_text(0, 9, "Art by Florent", 14);
    credits_draw_text(0, 10, "Berault", 7);
    credits_draw_text(0, 12, "Music by Jonas Bryan", 20);
    credits_draw_text(5, 16, "[A] BACK", 8);
}
