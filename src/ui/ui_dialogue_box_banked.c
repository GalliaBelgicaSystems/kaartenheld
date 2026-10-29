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
     * bottom border).  Height is capped at 32 (sy <= 14), so 14+sy..17+sy
     * stay <= 31 and never wrap the 32-row ring; sy must be promoted to
     * 16 bits because sy << 5 >= 256 once sy >= 8 (an 8-bit shift
     * overflowed and stamped the paper attrs on the wrong rows). */
    dst = (volatile uint8_t *)(0x99C0u + ((uint16_t)sy << 5));
    if (g_is_cgb) {
        BCPS_REG = (uint8_t)(0x80u | (UI_COLOR_PAPER << 3));
        for (i = 0; i < 8; i++) {
            /* CRAM data store under BANKED_VRAM_WRITE (bounded Mode wait
             * + per-store di): a write dropped into Mode 3 still advances
             * BCPS auto-increment, splicing a stale byte into the paper
             * ramp (wrong box background color). */
            BANKED_VRAM_WRITE(&BCPD_REG, s_dialogue_paper[i]);
        }
    }
    /* VRAM attr writes: PPU-safe so the box can be painted while the LCD
     * stays on (overworld -> dialogue overlay path).  BANKED_VRAM_WRITE
     * skips the wait when the LCD is off (STAT idle) and holds a
     * microsecond di per store when on, so the timer ISR (which now runs
     * mid-body -- the trampoline keeps the music clock live) cannot
     * eclipse the wait->store window into Mode 3. */
    VBK_REG = 1;
    i = 128;
    do {
        BANKED_VRAM_WRITE(dst, (uint8_t)(UI_COLOR_PAPER & 0x07));
        dst++;
    } while (--i != 0);
    VBK_REG = 0;
    /* SCX/SCY re-sync after the post-battle/post-gamble backdrop redraw:
     * direct dialogue entries draw over the live map (regs already right),
     * but battle/gamble zeroed them, and on maps taller than the viewport
     * SCY must come back or the box is drawn off-screen.  g_bk_byte_a is
     * the camera's PIXEL y (the camera glides in pixels, so deriving SCY
     * from the tile-aligned sy would shift the viewport by up to 7 px). */
    SCX_REG = g_bk_byte_c;
    SCY_REG = g_bk_byte_a;
}
