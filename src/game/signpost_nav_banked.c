#pragma bank 11

#include <gb/gb.h>
#include "game.h"
#include "game_ids.h"
#include "ui.h"
#include "banked.h"
#include "travel_routes_generated.h"

/* ── Signpost picker body (travel-demo ROM only, bank 11) ─────────
 * The signpost dialogues (field/village/desolate_signpost, choice
 * kind SIGNPOST) pick the journey on the dialogue's last row: phase 0
 * chooses the destination, phase 1 toggles CARRIAGE/WALK, A confirms
 * into code 13 with the staged route + method.  B backs out in either
 * phase (plain close, no journey).  Arrows mark live options only and
 * are dynamic: the lone demo road shows its bare name, the method row
 * shows "CARRIAGE 50G >" / "< WALK" (each side points where you can
 * still go).  Dispatched from
 * dialogue_screen_update() instead of the bank-12 YES/NO body (bank
 * 12 sits ~200 B under its ceiling; this bank has kilobytes to spare,
 * AGENTS.md 55.5).
 *
 * Demo scope: one road per signpost, uniform 50G carriage fare.  The
 * fare and the method labels below duplicate the journey route JSON
 * files (a banked body may only read its own bank plus WRAM -- banked_copy
 * restores the home bank mid-body, so the bank-14 route tables are
 * unreadable here; AGENTS.md 52.11.1).  A multi-destination signpost
 * wants the road list staged to WRAM (and per-route fares with it)
 * instead of these constants; verify_travel_demo's premium asserts
 * the 50G charge, so a fare change fails loudly there first.
 *
 * Out (same contract as the YES/NO body): g_bk_byte_a = 0 (nothing),
 * 1 (repainted/toggled: fixed side blips), 2 (plain close, B backout),
 * 5 (plain advance on non-last pages), 13 (confirmed: route staged in
 * g_bk_byte_b, method in g_bk_byte_c; fixed side ends the dialogue
 * and opens the journey with travel_start_method -- no full-screen
 * method view).
 *
 * Self-contained: WRAM (g_game state/dialogue, screen buffer) plus
 * same-bank consts only.  No fixed-bank calls, no di/ei (52.11.3).
 * Repaints go through the buffer-checked ring cell writer (scroll
 * aware, LCD-safe), so the harness text asserts stay exact.
 */

#define SIGNPOST_CARRIAGE_COST 50

static uint8_t s_sp_id;
static uint8_t s_sp_phase;
static uint8_t s_sp_method;

static const char s_sp_carriage[] = "CARRIAGE 50G >";
static const char s_sp_walk[] = "< WALK";
static const char s_sp_poor[] = "NEED 50G!";

static void sp_put_char_ring(uint8_t x, uint8_t y, char ch, uint8_t ox, uint8_t oy)
{
    if (y >= 18 || x >= 20) return;
    if (g_ui_screen_buf[y][x] == ch) return;
    VBK_REG = 0;
    BANKED_VRAM_WRITE(
        &((volatile uint8_t *)0x9800)[((y + oy) & 31) * 32 + ((x + ox) & 31)],
        (uint8_t)(ui_font_tile_base + (uint8_t)(ch - ' ')));
    g_ui_screen_buf[y][x] = ch;
}

/* Repaint the picker's last row (space-padded full width, so a short
 * "< WALK" clears a longer "CARRIAGE 50G >"). */
static void sp_paint_row(uint8_t row_y, const char *s)
{
    uint8_t x;
    uint8_t len;
    len = 0;
    while (s[len] != '\0') len++;
    for (x = 0; x < 20; x++) {
        sp_put_char_ring(x, row_y, (x < len) ? s[x] : ' ',
                         g_game.world.scroll_x, g_game.world.scroll_y);
    }
}

/* Paint the destination row without its JSON "< ... >" brackets: the
 * bracket is only the static placeholder shape, the live arrows mark
 * live options -- a lone road shows its bare name (neither side has
 * anywhere to go). Repainted idempotently every phase-0 frame; the
 * buffer skip makes repeats free. */
static void sp_paint_dest(uint8_t row_y, const char *s)
{
    uint8_t x;
    uint8_t i;
    uint8_t len;
    uint8_t start;
    uint8_t end;
    char ch;
    len = 0;
    while (s[len] != '\0') len++;
    start = 0;
    end = len;
    if (len >= 4 && s[0] == '<' && s[1] == ' ' &&
        s[end - 1] == '>' && s[end - 2] == ' ') {
        start = 2;
        end = (uint8_t)(end - 2);
    }
    for (x = 0; x < 20; x++) {
        i = (uint8_t)(start + x);
        if (i < end) {
            ch = s[i];
        } else {
            ch = ' ';
        }
        sp_put_char_ring(x, row_y, ch,
                         g_game.world.scroll_x, g_game.world.scroll_y);
    }
}

void signpost_nav_banked(void)
{
    uint8_t edges;
    uint8_t line;
    uint8_t row_y;
    uint8_t id;
    uint8_t route;
    int16_t gold;
    uint8_t on_last_page;

    /* The picker is only live on the last page; earlier pages advance
     * plainly like any other dialogue (reported as 5 so the fixed
     * side reads no input itself). */
    on_last_page = ((uint8_t)(g_game.dialogue.current_line + 3) >= g_game.dialogue.line_count);
    if (!on_last_page || g_game.dialogue.choice_made) {
        edges = (uint8_t)(g_inp_pad & (uint8_t)~g_inp_prev);
        if (edges & (uint8_t)((1u << INPUT_A) | (1u << INPUT_START))) {
            g_bk_byte_a = 5;
            return;
        }
        g_bk_byte_a = 0;
        return;
    }
    /* Fresh dialogue (always opens at line 0): destination first,
     * defaulting to the free method. */
    if (s_sp_id != g_game.dialogue.id || g_game.dialogue.current_line == 0) {
        s_sp_id = g_game.dialogue.id;
        s_sp_phase = 0;
        s_sp_method = 1;
    }
    line = (uint8_t)(g_game.dialogue.line_count - 1);
    row_y = (uint8_t)(15 + (uint8_t)(line - g_game.dialogue.current_line));
    edges = (uint8_t)(g_inp_pad & (uint8_t)~g_inp_prev);
    /* Phase 0 shows the bare destination name: the JSON "< ... >"
     * brackets are only the static placeholder shape, and a lone
     * road has no live option on either side. Repainted every frame
     * (idempotent via the buffer skip) so the placeholder never
     * lingers past the page turn. */
    if (s_sp_phase == 0) {
        sp_paint_dest(row_y,
            g_game.dialogue.lines[(uint8_t)(g_game.dialogue.line_count - 1)]);
    }
    /* B backs out in either phase (plain close, no journey). */
    if (edges & (uint8_t)(1u << INPUT_B)) {
        g_game.dialogue.choice_made = 1;
        g_bk_byte_a = 2;
        return;
    }
    /* D-pad toggles the method in phase 1 (phase 0 holds the single
     * demo destination: silent no-op). */
    if (edges & (uint8_t)((1u << INPUT_UP) | (1u << INPUT_DOWN) |
                          (1u << INPUT_LEFT) | (1u << INPUT_RIGHT))) {
        if (s_sp_phase == 1) {
            s_sp_method = (uint8_t)(s_sp_method ^ 1);
            sp_paint_row(row_y, (s_sp_method == 0) ? s_sp_carriage : s_sp_walk);
            g_bk_byte_a = 1;
            return;
        }
        g_bk_byte_a = 0;
        return;
    }
    if (edges & (uint8_t)((1u << INPUT_A) | (1u << INPUT_START))) {
        /* Destination confirm: show the method row. */
        if (s_sp_phase == 0) {
            s_sp_phase = 1;
            sp_paint_row(row_y, (s_sp_method == 0) ? s_sp_carriage : s_sp_walk);
            g_bk_byte_a = 1;
            return;
        }
        /* Unaffordable carriage stays put with a refusal row. */
        if (s_sp_method == 0) {
            gold = g_game.state.currency.amount[CURRENCY_ID_GOLD - 1];
            if (gold < SIGNPOST_CARRIAGE_COST) {
                sp_paint_row(row_y, s_sp_poor);
                g_bk_byte_a = 1;
                return;
            }
        }
        /* Map the signpost dialogue to its road (chapter picks the
         * field road's act). */
        id = g_game.dialogue.id;
        if (id == DIALOGUE_ID_FIELD_SIGNPOST_TRAVEL) {
            if (g_game.state.variables.values[VARIABLE_ID_CHAPTER - 1] == 1) {
                route = TRAVEL_ROUTE_FIELD_TO_VILLAGE_ACT1;
            } else {
                route = TRAVEL_ROUTE_FIELD_TO_VILLAGE;
            }
        } else if (id == DIALOGUE_ID_VILLAGE_SIGNPOST_TRAVEL) {
            route = TRAVEL_ROUTE_TOWN_TO_DESOLATE;
        } else {
            route = TRAVEL_ROUTE_DESOLATE_TO_FIELD;
        }
        g_bk_byte_b = route;
        g_bk_byte_c = s_sp_method;
        g_game.dialogue.choice_made = 1;
        g_bk_byte_a = 13;
        return;
    }
    g_bk_byte_a = 0;
}
