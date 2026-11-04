/* Bank split (memory budget): this file lives in ROM bank 10 in both
 * builds -- release bank 4 is full and the despair fades need a roomy
 * bank with no per-build split.  The fixed-bank wrapper (ui_draw_dialogue
 * in ui.c) dispatches through UI_DIALOGUE_BOX_BANK (ui.h), so pragma and
 * dispatch can never disagree.  Self-contained: hardware regs + WRAM
 * staging only, no banked_copy (which would restore the home bank
 * mid-body -- see the shops_lookup revert). */
#pragma bank 10

#include <gb/gb.h>
#include <gb/cgb.h>
#include "ui.h"
#include "banked.h"

extern uint8_t g_slime_despair;
extern char g_ui_screen_buf[18][21];

static void despair_box_fade_in(void);
static void despair_box_wait(void);

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
    /* Despair fade-in (demo finale): the defeat faded out to white on the
     * result screen; fade the wake area back in here, over the freshly
     * redrawn world.  Gated on the still-set flag (cleared at close) AND
     * the LCD: the entry repaints with the LCD off (skipped then -- LY
     * stalls at 0 with the LCD off), the repaint right below runs with
     * the LCD on, and page turns go through ui_draw_dialogue_page (a
     * different function), so normal dialogues never see it. */
    if (g_slime_despair && (LCDC_REG & 0x80)) {
        despair_box_fade_in();
    }
}

/* Despair white fade-out (demo finale): whites the current palettes out.
 * Dispatched from game_slime_despair_outro() on the DEFEATED result
 * screen (the fade-in twin lives below, next to its call site).  Same
 * self-containment contract as the paper body above. */
static void despair_box_wait(void)
{
    uint8_t n = 6;
    while (n--) {
        while (LY_REG != 144) {
        }
        while (LY_REG == 144) {
        }
    }
}

void game_despair_fade_out_banked(void)
{
    uint8_t slot;
    uint8_t col;

    if (!g_is_cgb) {
        BGP_REG = 0x50;
        OBP0_REG = 0x50;
        OBP1_REG = 0x50;
        despair_box_wait();
        BGP_REG = 0x00;
        OBP0_REG = 0x00;
        OBP1_REG = 0x00;
        despair_box_wait();
        return;
    }
    {
        uint8_t pass = 5;
        uint8_t lo;
        uint8_t hi;
        uint8_t r;
        uint8_t g;
        uint8_t b;
        while (pass--) {
            for (slot = 0; slot < 16; slot++) {
                if (slot < 8) {
                    BCPS_REG = (uint8_t)(0x80u | (slot << 3));
                } else {
                    OCPS_REG = (uint8_t)(0x80u | ((slot - 8) << 3));
                }
                for (col = 0; col < 4; col++) {
                    if (slot < 8) {
                        lo = BCPD_REG;
                        hi = BCPD_REG;
                    } else {
                        lo = OCPD_REG;
                        hi = OCPD_REG;
                    }
                    r = (uint8_t)(lo & 31);
                    g = (uint8_t)(((lo >> 5) | (hi << 3)) & 31);
                    b = (uint8_t)((hi >> 2) & 31);
                    r = (uint8_t)(r + ((uint8_t)(31 - r) >> 1));
                    g = (uint8_t)(g + ((uint8_t)(31 - g) >> 1));
                    b = (uint8_t)(b + ((uint8_t)(31 - b) >> 1));
                    lo = (uint8_t)((g << 5) | r);
                    hi = (uint8_t)((b << 2) | (g >> 3));
                    if (slot < 8) {
                        BCPS_REG = (uint8_t)(0x80u | (slot << 3) | (col << 1));
                        BANKED_VRAM_WRITE(&BCPD_REG, lo);
                        BANKED_VRAM_WRITE(&BCPD_REG, hi);
                    } else {
                        OCPS_REG = (uint8_t)(0x80u | ((slot - 8) << 3) | (col << 1));
                        BANKED_VRAM_WRITE(&OCPD_REG, lo);
                        BANKED_VRAM_WRITE(&OCPD_REG, hi);
                    }
                }
            }
            despair_box_wait();
        }
        for (slot = 0; slot < 16; slot++) {
            if (slot < 8) {
                BCPS_REG = (uint8_t)(0x80u | (slot << 3));
            } else {
                OCPS_REG = (uint8_t)(0x80u | ((slot - 8) << 3));
            }
            for (col = 0; col < 4; col++) {
                if (slot < 8) {
                    BANKED_VRAM_WRITE(&BCPD_REG, 0xFF);
                    BANKED_VRAM_WRITE(&BCPD_REG, 0x7F);
                } else {
                    BANKED_VRAM_WRITE(&OCPD_REG, 0xFF);
                    BANKED_VRAM_WRITE(&OCPD_REG, 0x7F);
                }
            }
        }
        despair_box_wait();
    }
}

/* White-to-current fade for the despair entry (see above).  Per color:
 * snapshot the target, then step white -> target in quarters (shifts
 * only).  All colors share the step frame so the area materializes
 * evenly.  DMG steps 50 -> E4 (BGP/OBP, the only values the ROM ever
 * programs besides the fade's own).  Snapshot + step math mirrors
 * game_despair_fade_out_banked: no WRAM growth, the 128 scratch bytes
 * are text rows 0-6, rebuilt by the next full redraw. */
static void despair_box_fade_in(void)
{
    uint8_t slot;
    uint8_t n;
    uint8_t s;
    char *snap;

    if (!g_is_cgb) {
        BGP_REG = 0x50;
        OBP0_REG = 0x50;
        OBP1_REG = 0x50;
        despair_box_wait();
        BGP_REG = 0xE4;
        OBP0_REG = 0xE4;
        OBP1_REG = 0xE4;
        despair_box_wait();
        return;
    }
    snap = &g_ui_screen_buf[0][0];
    for (slot = 0; slot < 16; slot++) {
        if (slot < 8) {
            BCPS_REG = (uint8_t)(0x80u | (slot << 3));
        } else {
            OCPS_REG = (uint8_t)(0x80u | ((slot - 8) << 3));
        }
        for (n = 0; n < 4; n++) {
            if (slot < 8) {
                *snap++ = (char)BCPD_REG;
                *snap++ = (char)BCPD_REG;
            } else {
                *snap++ = (char)OCPD_REG;
                *snap++ = (char)OCPD_REG;
            }
        }
    }
    s = 1;
    while (s <= 4) {
        snap = &g_ui_screen_buf[0][0];
        for (n = 0; n < 64; n++) {
            uint8_t lo = (uint8_t)*snap++;
            uint8_t hi = (uint8_t)*snap++;
            uint8_t r = (uint8_t)(lo & 31);
            uint8_t g = (uint8_t)(((lo >> 5) | (hi << 3)) & 31);
            uint8_t b = (uint8_t)((hi >> 2) & 31);
            uint8_t dr = (uint8_t)(31 - r);
            uint8_t dg = (uint8_t)(31 - g);
            uint8_t db = (uint8_t)(31 - b);
            uint8_t col;
            if (s == 1) {
                r = (uint8_t)(31 - (dr >> 2));
                g = (uint8_t)(31 - (dg >> 2));
                b = (uint8_t)(31 - (db >> 2));
            } else if (s == 2) {
                r = (uint8_t)(31 - (dr >> 1));
                g = (uint8_t)(31 - (dg >> 1));
                b = (uint8_t)(31 - (db >> 1));
            } else if (s == 3) {
                r = (uint8_t)(31 - (uint8_t)((dr >> 1) + (dr >> 2)));
                g = (uint8_t)(31 - (uint8_t)((dg >> 1) + (dg >> 2)));
                b = (uint8_t)(31 - (uint8_t)((db >> 1) + (db >> 2)));
            }
            lo = (uint8_t)((g << 5) | r);
            hi = (uint8_t)((b << 2) | (g >> 3));
            slot = (uint8_t)(n >> 2);
            col = (uint8_t)(n & 3);
            if (slot < 8) {
                BCPS_REG = (uint8_t)(0x80u | (slot << 3) | (col << 1));
                BANKED_VRAM_WRITE(&BCPD_REG, lo);
                BANKED_VRAM_WRITE(&BCPD_REG, hi);
            } else {
                OCPS_REG = (uint8_t)(0x80u | ((slot - 8) << 3) | (col << 1));
                BANKED_VRAM_WRITE(&OCPD_REG, lo);
                BANKED_VRAM_WRITE(&OCPD_REG, hi);
            }
        }
        s++;
        despair_box_wait();
    }
}
