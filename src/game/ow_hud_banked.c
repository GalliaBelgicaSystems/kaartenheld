#pragma bank 8

#include <stdint.h>
#include <gb/gb.h>
#include "world.h"
#include "banked.h"
#include "game.h"
#include "ui.h"
#include "game_ids.h"

/* ── Overworld status-bar HUD (ROM bank 8) ──────────────────────────
 * Bottom screen row (row 17) is a white status bar, repainted every
 * overworld frame by this body (dispatched from overworld_screen_render
 * through the thin fixed wrapper ui_draw_ow_hud). Bank 8: the body is
 * ~870 B of SDCC output (16-bit state reads + ring math), which does
 * not fit the thin banks (debug bank 5 has ~540 B free); bank 8 has
 * room in both builds:
 *
 *   col: 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9
 *   row17:" H H H _ _ A n n _ _ > n n _ _ d d d G _ "
 *          hearts  AP iconic  arrows   gold digits  spin
 *                  value      value    + G suffix   cell (white)
 *
 * - 3 hearts, 1 heart = 5 HP (full icon, 1-4 remainder half icon,
 *   empty = nothing, just the white background). Shown HP caps at 3
 *   hearts (15); level-up overflow stays 3 full.
 * - AP icon (menus-tile status cell) + ENERGY_POOL, 2-wide
 *   right-aligned (pool is uncapped by design; clamps at 99).
 * - Arrows icon (menus-tile status cell) + ARROWS currency, 2-wide
 *   right-aligned (quiver max 30).
 * - Gold 3-wide right-aligned (font digits, black ink) + gold "G"
 *   suffix: the G is an OAM glyph (entry 38, font tile + OBJ slot 1
 *   gold, repaired by the tail stream) over a white BG cell, so the
 *   suffix matches the menu gold while digits stay dark like battle
 *   numbers.
 * - Icons stream to VRAM 208-211 on every overworld full redraw
 *   (bank-11 status body from ui_world_tail, time-shared with the
 *   menu chrome singles); digits/spaces are font tiles.  Every HUD
 *   cell carries the paper attr (slot 4 = paper-white ramp, programmed
 *   by the same stream): opaque white bar on every tileset, DMG-safe.
 * - Col 19 is hard white background too (space tile + paper attr, no
 *   content): the autosave spinner OAM (entry 39 at 152,136) draws over
 *   it, but the bar reads unbroken when the spinner is idle.
 * - The semantic buffer keeps plain letters ('H'/'h'/'A'/digits/
 *   '>'/'G'), so harness screen_row asserts are stable across art
 *   changes; VRAM holds the real tiles (vram_row decodes icons as
 *   '.', same as all art tiles).
 *
 * Unconditional repaint (no skip-optimization, no scroll cache): the
 * bar is screen-anchored but lives in the scrolling 32x32 ring, so a
 * camera move changes every cell's ring address even when the content
 * is identical.  ~40 bounded mode-gated writes/frame is ~1k T-states
 * typical and needs no extra WRAM (stack-gap budget, AGENTS.md 52.24).
 *
 * Self-contained (AGENTS.md §52.11.1): WRAM game state + same-bank code
 * only, no fixed-bank calls, no div/mod (repeated subtraction: the
 * SDCC div library would land in fixed _CODE, AGENTS.md 52.18).
 * No di/ei anywhere (AGENTS.md §52.11.3): BANKED_VRAM_WRITE only. */

#ifdef DEBUG_BUILD
extern uint8_t g_tilemap_mirror[32 * 32];
extern volatile uint8_t g_tilemap_attr_mirror[32 * 32];
#endif

#define OW_HUD_ROW 17u
#define OW_HUD_COLS 20u

void ui_ow_hud_banked(void)
{
    const World *w;
    uint8_t hp;
    uint8_t full;
    uint8_t half;
    int16_t ap;
    int16_t arrows;
    int16_t gold;
    uint8_t ring_row;
    uint8_t sx;
    uint8_t col;
    uint8_t tens;
    uint8_t ones;
    uint8_t hundreds;
    uint8_t tmp;
    char line[20];

    w = (const World *)g_bk_ptr_a;
    if (!w) return;

    hp = g_game.state.party.members[0].hp;

    /* Hearts: consume 5 HP per full heart, cap at 3 cells. */
    full = 0;
    while (hp >= 5 && full < 3) {
        hp = (uint8_t)(hp - 5);
        full++;
    }
    /* Leftover 1-4 HP is one half heart (nothing when exactly spent,
     * or when the bar is already full). */
    half = (uint8_t)(full < 3 && hp > 0);

    ap = g_game.state.variables.values[VARIABLE_ID_ENERGY_POOL - 1];
    if (ap < 0) ap = 0;
    if (ap > 99) ap = 99;
    arrows = g_game.state.currency.amount[CURRENCY_ID_ARROWS - 1];
    if (arrows < 0) arrows = 0;
    if (arrows > 99) arrows = 99;
    gold = g_game.state.currency.amount[CURRENCY_ID_GOLD - 1];
    if (gold < 0) gold = 0;
    if (gold > 999) gold = 999;

    /* Hearts. */
    for (col = 0; col < 3; col++) {
        if (col < full) {
            line[col] = 'H';
        } else if (col == full && half) {
            line[col] = 'h';
        } else {
            line[col] = ' ';
        }
    }
    line[3] = ' ';
    line[4] = ' ';
    /* AP: icon + 2-wide right-aligned value (no div/mod: the SDCC
     * div library would land in fixed _CODE, AGENTS.md 52.18). */
    line[5] = 'A';
    tens = 0;
    tmp = (uint8_t)ap;
    while (tmp >= 10) {
        tmp = (uint8_t)(tmp - 10);
        tens++;
    }
    ones = tmp;
    line[6] = (tens == 0) ? ' ' : (char)('0' + tens);
    line[7] = (char)('0' + ones);
    line[8] = ' ';
    line[9] = ' ';
    /* Arrows: icon + 2-wide right-aligned value. */
    line[10] = '>';
    tens = 0;
    tmp = (uint8_t)arrows;
    while (tmp >= 10) {
        tmp = (uint8_t)(tmp - 10);
        tens++;
    }
    ones = tmp;
    line[11] = (tens == 0) ? ' ' : (char)('0' + tens);
    line[12] = (char)('0' + ones);
    line[13] = ' ';
    line[14] = ' ';
    /* Gold: 3-wide right-aligned + 'G' (int16 range: subtraction
     * loops on the clamped value, still no div/mod). */
    hundreds = 0;
    while (gold >= 100) {
        gold = (int16_t)(gold - 100);
        hundreds++;
    }
    tens = 0;
    while (gold >= 10) {
        gold = (int16_t)(gold - 10);
        tens++;
    }
    ones = (uint8_t)gold;
    line[15] = (hundreds == 0) ? ' ' : (char)('0' + hundreds);
    line[16] = (hundreds == 0 && tens == 0) ? ' ' : (char)('0' + tens);
    line[17] = (char)('0' + ones);
    line[18] = 'G';
    /* Spinner cell: hard white background, no content (the OAM spinner
     * draws over it when an autosave flushes). */
    line[19] = ' ';

    ring_row = (uint8_t)((w->scroll_y + OW_HUD_ROW) & 31);
    sx = w->scroll_x;
    for (col = 0; col < OW_HUD_COLS; col++) {
        uint8_t ring_col = (uint8_t)((sx + col) & 31);
        uint16_t idx = (uint16_t)((uint16_t)(ring_row << 5) + ring_col);
        char ch = line[col];
        uint8_t tile;
        /* Icon cells stamp the streamed status tiles (VRAM 208-211);
         * the gold suffix cell stays white BG (its gold Glyph rides
         * OAM entry 38 below); everything else is a font tile.  The
         * buffer always keeps the letter, so harness asserts never
         * see tile ids. */
        if (ch == 'H') {
            tile = UI_TILE_STATUS_HEART_FULL;
        } else if (ch == 'h') {
            tile = UI_TILE_STATUS_HEART_HALF;
        } else if (ch == 'A') {
            tile = UI_TILE_STATUS_AP;
        } else if (ch == '>') {
            tile = UI_TILE_STATUS_ARROWS;
        } else if (col == 18) {
            tile = ui_font_tile_base;
        } else {
            tile = (uint8_t)(ui_font_tile_base + (uint8_t)(ch - ' '));
        }
        VBK_REG = 0;
        BANKED_VRAM_WRITE((volatile uint8_t *)(0x9800u + idx), tile);
        VBK_REG = 1;
        BANKED_VRAM_WRITE((volatile uint8_t *)(0x9800u + idx), UI_COLOR_PAPER);
        VBK_REG = 0;
#ifdef DEBUG_BUILD
        g_tilemap_mirror[idx] = tile;
        g_tilemap_attr_mirror[idx] = UI_COLOR_PAPER;
#endif
        g_ui_screen_buf[OW_HUD_ROW][col] = ch;
    }
    /* Gold Glyph: OAM gold "G" over the white suffix cell (font tile
     * from the OAM duplicate block + OBJ slot-1 gold).  WRAM shadow
     * writes only (always mapped); screen-anchored, so no scroll math.
     * Hidden by the transition/dialogue hides like every other entry;
     * this same-frame repaint repositions it. */
    {
        volatile uint8_t *e = (volatile uint8_t *)(0xC000u +
            ((uint16_t)HUD_GOLD_GLYPH_OAM_ENTRY << 2));
        e[0] = (uint8_t)(((uint8_t)(OW_HUD_ROW << 3)) + 16);
        e[1] = (uint8_t)((18 << 3) + 8);
        e[2] = (uint8_t)(ui_font_tile_base + (uint8_t)('G' - ' '));
        e[3] = HUD_GOLD_GLYPH_OBJ_PROP;
    }
}
