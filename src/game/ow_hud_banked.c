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
 *   row17:" H H H _ _ A n n _ _ > n n _ _ d d d G X "
 *          hearts  AP iconic  arrows   gold digits  spin
 *                  value      value    + G suffix    cell
 *
 * - 3 hearts, 1 heart = 5 HP (full 'H', 1-4 remainder 'h' half,
 *   empty = nothing, just the white background). Shown HP caps at 3
 *   hearts (15); level-up overflow stays 3 full.
 * - AP icon 'A' (font placeholder) + ENERGY_POOL, 2-wide right-aligned
 *   (pool is uncapped by design; clamps at 99 for display).
 * - Arrows icon '>' (font placeholder) + ARROWS currency, 2-wide
 *   right-aligned (quiver max 30).
 * - Gold 3-wide right-aligned + 'G' suffix (clamps at 999).
 * - Col 19 is NEVER written (tile, attr, mirror, or buffer): the
 *   autosave spinner OAM (entry 39 at 152,136) owns that cell.
 * - White background = space tiles + palette-0 attrs (blank tiles are
 *   white on every tileset; no CRAM changes, slot 4 stays black).
 * - Font glyphs double as the semantic buffer, so harness screen_row
 *   and vram_row asserts read the HUD directly, and ui_refill_semantic
 *   (harness per-frame refill) round-trips it. The menus-tile sheet
 *   already carries the real art (row 0: full heart, half heart, AP,
 *   arrow); swapping the three icon chars for those tiles is a later
 *   art pass that keeps the buffer (and every assert) unchanged.
 *
 * Unconditional repaint (no skip-optimization, no scroll cache): the
 * bar is screen-anchored but lives in the scrolling 32x32 ring, so a
 * camera move changes every cell's ring address even when the content
 * is identical. 38 bounded mode-gated writes/frame is ~1k T-states
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
#define OW_HUD_COLS 19u

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
    char line[19];

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

    ring_row = (uint8_t)((w->scroll_y + OW_HUD_ROW) & 31);
    sx = w->scroll_x;
    for (col = 0; col < OW_HUD_COLS; col++) {
        uint8_t ring_col = (uint8_t)((sx + col) & 31);
        uint16_t idx = (uint16_t)((uint16_t)(ring_row << 5) + ring_col);
        uint8_t tile = (uint8_t)(ui_font_tile_base + (uint8_t)(line[col] - ' '));
        VBK_REG = 0;
        BANKED_VRAM_WRITE((volatile uint8_t *)(0x9800u + idx), tile);
        VBK_REG = 1;
        BANKED_VRAM_WRITE((volatile uint8_t *)(0x9800u + idx), 0);
        VBK_REG = 0;
#ifdef DEBUG_BUILD
        g_tilemap_mirror[idx] = tile;
        g_tilemap_attr_mirror[idx] = 0;
#endif
        g_ui_screen_buf[OW_HUD_ROW][col] = line[col];
    }
}
