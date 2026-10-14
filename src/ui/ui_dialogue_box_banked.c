/* Bank split (memory budget): the dialogue paper/camera body lives here
 * instead of ui_color_banked.c so release bank 3 keeps room for battle
 * HUD work.  Per-build bank (debug 5 / release 4): both have room, and
 * the fixed-bank wrapper (ui_draw_dialogue in ui.c) dispatches through
 * UI_DIALOGUE_BOX_BANK (ui.h), so pragma and dispatch can never
 * disagree.  Self-contained: hardware regs + WRAM staging only, no
 * banked_copy (which would restore the home bank mid-body -- see the
 * shops_lookup revert). */
#ifdef DEBUG_BUILD
#pragma bank 5
#else
#pragma bank 4
#endif

#include <gb/gb.h>
#include <gb/cgb.h>
#include "ui.h"
#include "banked.h"

/* Dialogue box paper + camera sync, banked body (see ui.h).  Reads the
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
     * box is a top bar plus three text rows (14-17): top with the
     * speaker name, then plain full-width text to the screen edge (no
     * bottom border).  The validator caps height at 24 (sy <= 6), so
     * (sy << 5) fits in a byte and rows never wrap the 32-row ring. */
    dst = (volatile uint8_t *)((uint16_t)(0x99C0u + ((uint8_t)(sy << 5))));
    if (g_is_cgb) {
        BCPS_REG = (uint8_t)(0x80u | (UI_COLOR_PAPER << 3));
        for (i = 0; i < 8; i++) {
            BCPD_REG = s_dialogue_paper[i];
        }
    }
    /* VRAM attr writes: PPU-safe so the box can be painted while the LCD
     * stays on (overworld -> dialogue overlay path).  When the LCD is off the
     * wait is skipped (STAT is idle and a wait would spin forever). */
    VBK_REG = 1;
    i = 128;
    do {
        if (LCDC_REG & 0x80) {
            __asm di __endasm;
            while (STAT_REG & 0x02);
            *dst = (uint8_t)(UI_COLOR_PAPER & 0x07);
            __asm ei __endasm;
        } else {
            *dst = (uint8_t)(UI_COLOR_PAPER & 0x07);
        }
        dst++;
    } while (--i != 0);
    VBK_REG = 0;
    /* SCX re-sync (SCY is 0 on every map: all are 18 tall). */
    SCX_REG = g_bk_byte_c;
}
