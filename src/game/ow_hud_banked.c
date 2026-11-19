#pragma bank 8

#include <stdint.h>
#include <gb/gb.h>
#include "world.h"
#include "banked.h"
#include "game.h"
#include "ui.h"
#include "game_ids.h"

/* ── Overworld status-bar HUD (ROM bank 8) ──────────────────────────
 * Bottom screen row (row 17) is a white status bar rendered on the PPU
 * Window layer (0x9C00 row 0, WX=7, WY=136), updated on overworld
 * transitions (boot, map change, battle/shop/menu/dialogue returns)
 * through the thin fixed wrapper ui_draw_ow_hud().  Keeping the bar in
 * the Window layer prevents camera scrolling from duplicating or
 * erasing BG map tiles, and skipping steady overworld frames keeps
 * walking at full 60 fps.  Bank 8: the body does not fit the thin banks
 * (debug bank 5 has ~540 B free); bank 8 has room in both builds.
 *
 *   col: 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9
 *   row17:" _ H H H _ _ A n n _ > n n _ d d d G _ _ "
 *          margin hearts  AP iconic arrows gold digits  spin
 *                        value    value  +G suffix   cells (white)
 *
 * - 3 hearts, 1 heart = 5 HP.  Remainder 3-4 rounds UP once a heart
 *   is complete (8-10 -> 2 full, 4 -> 1 full); remainders 1-2 show a
 *   half (12 -> 2 full + half, 5-7 -> 1 full + half); exact multiples
 *   below max keep their half (0 and 5 show it, 10 and 15 do not);
 *   anything under one full heart shows a half, never empty (0-3 ->
 *   half). Caps at 3 full hearts.
 * - AP/arrows icons + zero-padded values (pool/quiver, clamp 99);
 *   gold 3-wide zero-padded (clamps 999) + 'G' suffix.
 * - Icons stamp the streamed status tiles (VRAM 208-211, bank-11
 *   stream from ui_world_tail, time-shared with the menu chrome);
 *   everything else is a font tile on the fight3 paper ramp (slot 4):
 *   white bar, red hearts, bronze icons/digits/suffix on every
 *   tileset. Cols 18-19 stay blank white under the spinner OAM.
 * - The buffer keeps plain letters, so harness screen_row asserts
 *   are stable across art changes.
 *
 * Self-contained (AGENTS.md §52.11.1): WRAM game state + same-bank
 * code only, no fixed-bank calls, no div/mod (repeated subtraction:
 * the SDCC div library would land in fixed _CODE, AGENTS.md 52.18).
 * No di/ei anywhere (AGENTS.md §52.11.3): BANKED_VRAM_WRITE only. */

#define OW_HUD_ROW 17u
#define OW_HUD_COLS 20u

/* Zero-padded 2-wide decimal, no div/mod. */
static void hud_num2(uint8_t v, char *dst)
{
    uint8_t tens = 0;
    while (v >= 10) {
        v = (uint8_t)(v - 10);
        tens++;
    }
    dst[0] = (char)('0' + tens);
    dst[1] = (char)('0' + v);
}

/* Clamp a display value into [0, hi]. */
static int16_t hud_clamp(int16_t v, int16_t hi)
{
    if (v < 0) return 0;
    if (v > hi) return hi;
    return v;
}

/* HUD letter -> VRAM tile: streamed status icons for the four icon
 * cells, font tiles otherwise. */
static uint8_t hud_tile(char ch)
{
    if (ch == 'H') return UI_TILE_STATUS_HEART_FULL;
    if (ch == 'h') return UI_TILE_STATUS_HEART_HALF;
    if (ch == 'A') return UI_TILE_STATUS_AP;
    if (ch == '>') return UI_TILE_STATUS_ARROWS;
    return (uint8_t)(ui_font_tile_base + (uint8_t)(ch - ' '));
}

void ui_ow_hud_banked(void)
{
    uint8_t hp;
    uint8_t hp0;
    uint8_t full;
    uint8_t half;
    uint8_t hundreds;
    int16_t ap;
    int16_t arrows;
    int16_t gold;
    uint8_t col;
    uint8_t cell;
    volatile uint8_t *dst;
    char line[20];

    hp = g_game.state.party.members[0].hp;
    hp0 = hp;
    ap = hud_clamp(g_game.state.variables.values[VARIABLE_ID_ENERGY_POOL - 1], 99);
    arrows = hud_clamp(g_game.state.currency.amount[CURRENCY_ID_ARROWS - 1], 99);
    gold = hud_clamp(g_game.state.currency.amount[CURRENCY_ID_GOLD - 1], 999);

    /* Hearts: consume 5 HP per full heart, cap at 3 cells. */
    full = 0;
    while (hp >= 5 && full < 3) {
        hp = (uint8_t)(hp - 5);
        full++;
    }
    half = 0;
    if (full < 3) {
        if (hp >= 4 || (hp == 3 && full >= 1)) {
            full++;
            if (full >= 3) {
                full = 3;
                half = 0;
            }
        } else if (hp >= 1) {
            half = 1;
        } else {
            half = (uint8_t)(hp0 < 10);
        }
    }

    /* Bar content (col 0 margin, cols 18-19 spinner margin). */
    line[0] = ' ';
    for (col = 0; col < 3; col++) {
        cell = (uint8_t)(col + 1);
        if (col < full) {
            line[cell] = 'H';
        } else if (col == full && half) {
            line[cell] = 'h';
        } else {
            line[cell] = ' ';
        }
    }
    line[4] = ' ';
    line[5] = ' ';
    line[6] = 'A';
    hud_num2((uint8_t)ap, &line[7]);
    line[9] = ' ';
    line[10] = '>';
    hud_num2((uint8_t)arrows, &line[11]);
    line[13] = ' ';
    hundreds = 0;
    while (gold >= 100) {
        gold = (int16_t)(gold - 100);
        hundreds++;
    }
    line[14] = (char)('0' + hundreds);
    hud_num2((uint8_t)gold, &line[15]);
    line[17] = 'G';
    line[18] = ' ';
    line[19] = ' ';

    /* Paint Window row 0 (0x9C00..0x9C13) in two contiguous passes: tiles
     * first (VBK=0), then paper attrs on CGB (VBK=1), then enable the
     * Window anchored at screen row 17 (WY=136, WX=7). */
    dst = (volatile uint8_t *)0x9C00u;
    VBK_REG = 0;
    for (col = 0; col < OW_HUD_COLS; col++) {
        uint8_t tile = hud_tile(line[col]);
        BANKED_VRAM_WRITE(dst + col, tile);
        g_ui_screen_buf[OW_HUD_ROW][col] = line[col];
    }
    g_ui_screen_buf[OW_HUD_ROW][OW_HUD_COLS] = '\0';
    if (g_is_cgb) {
        VBK_REG = 1;
        for (col = 0; col < OW_HUD_COLS; col++) {
            BANKED_VRAM_WRITE(dst + col, UI_COLOR_PAPER);
        }
        VBK_REG = 0;
    }
    WX_REG = 7;
    WY_REG = 136;
    LCDC_REG |= 0x60u;
}
