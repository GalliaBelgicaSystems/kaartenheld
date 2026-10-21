#pragma bank 8

#include <gb/gb.h>
#include "game.h"
#include "game_ids.h"
#include "ui.h"
#include "banked.h"

/* ── Heal-mage YES/NO choice navigation + resolve, bank-8 body ─────
 * Dispatched from dialogue_screen_update() when a choice dialogue sits
 * on its last page unconfirmed (fixed-bank cost: one predicate +
 * staging + call -- the fixed bank sits hard against 0x8000, AGENTS.md
 * §52.18).  Follows the quest-tab nav precedent (item_quest_banked.c):
 * pad edges read directly from WRAM (always mapped), compile-time
 * bitmasks, no helper calls.
 *
 * The engine owns the choice UI contract (last line "> YES   NO",
 * cursor cells 0/7); this game-owned body owns what an answer means.
 * UP/DOWN/LEFT/RIGHT toggles the answer and repaints the cursor cells inline
 * (scroll-aware through the ring, exactly like ui_draw_dialogue_line,
 * with the buffer-checked skip).  A/START confirms:
 *   - ASK + NO  -> the mage declines (the fixed wrapper opens the
 *                  follow-up; nothing is set, so the mage asks again).
 *   - ASK + YES -> full-party restore loop (kept bank-local: a banked
 *                  body must never call across banks).  The fixed wrapper
 *                  marks HEAL_MAGE_USED through the real flag API (which
 *                  owns both the bit and the STORY_FLAG_SET telemetry);
 *                  the flag id is staged for it below.  The ASK row only
 *                  matches while USED is clear, so reaching here with it
 *                  set is unreachable.
 *   - other choice dialogue + either answer -> plain confirm (no game
 *                  effect; future choice dialogues reuse the UI free).
 *
 * Self-contained: WRAM + regs only, no fixed-bank calls.  Reads g_game
 * directly (like the heal body), so nothing is staged on entry.  The
 * engine dispatches unconditionally while a dialogue is open; non-choice
 * dialogues (or choice dialogues off the last page / already confirmed)
 * report 0 immediately.
 *
 * Out: g_bk_byte_a = 0 (not a choice situation and no advance input),
 *      1 (toggled, repainted), 2 (confirmed, no game effect),
 *      3 (NO: open the follow-up staged in g_bk_byte_c),
 *      6 (YES: healed; g_bk_byte_b = restored HP, g_bk_byte_c = flag id
 *      for the STORY_FLAG_SET telemetry, g_bk_byte_d = follow-up id:
 *      the fixed wrapper owes the jingle plus the staged telemetry,
 *      then opens the thanks instead of advancing),
 *      5 (plain advance: A/START edge outside any live choice).
 * The engine dispatches unconditionally while a dialogue is open; non-choice
 * dialogues (or choice dialogues off the last page / already confirmed)
 * report 0/5 through the same edge evaluation, so the fixed side pays no
 * input reads of its own. */

static void hm_put_char_ring(uint8_t x, uint8_t y, char ch, uint8_t ox, uint8_t oy)
{
    if (y >= 18 || x >= 20) return;
    if (g_ui_screen_buf[y][x] == ch) return;
    VBK_REG = 0;
    BANKED_VRAM_WRITE(
        &((volatile uint8_t *)0x9800)[((y + oy) & 31) * 32 + ((x + ox) & 31)],
        (uint8_t)(ui_font_tile_base + (uint8_t)(ch - ' ')));
    g_ui_screen_buf[y][x] = ch;
}

void dialogue_choice_nav_banked(void)
{
    uint8_t edges;
    uint8_t line;
    uint8_t row_y;
    uint8_t mi;
    uint8_t restored;
    uint8_t on_last_page;

    /* Choice UI is only live on the last page of an unconfirmed YES/NO
     * dialogue; otherwise an A/START edge is a plain advance (reported
     * as 5 so the fixed side reads no input itself). */
    on_last_page = ((uint8_t)(g_game.dialogue.current_line + 3) >= g_game.dialogue.line_count);
    if (g_game.dialogue.choice != DIALOGUE_CHOICE_YES_NO ||
        !on_last_page || g_game.dialogue.choice_made) {
        edges = (uint8_t)(g_inp_pad & (uint8_t)~g_inp_prev);
        if (edges & (uint8_t)((1u << INPUT_A) | (1u << INPUT_START))) {
            g_bk_byte_a = 5;
            return;
        }
        g_bk_byte_a = 0;
        return;
    }
    /* Pressed edges in one pass (item_quest_nav_banked idiom). */
    edges = (uint8_t)(g_inp_pad & (uint8_t)~g_inp_prev);
    /* The option row reads "> YES   NO" left-to-right: all four D-pad
     * directions toggle (one compile-time immediate, no extra ROM). */
    if (edges & (uint8_t)((1u << INPUT_UP) | (1u << INPUT_DOWN) | (1u << INPUT_LEFT) | (1u << INPUT_RIGHT))) {
        if (!g_game.dialogue.line_count) {
            g_bk_byte_a = 0;
            return;
        }
        line = (uint8_t)(g_game.dialogue.line_count - 1);
        row_y = (uint8_t)(15 + (uint8_t)(line - g_game.dialogue.current_line));
        g_game.dialogue.choice_index = (uint8_t)(g_game.dialogue.choice_index ^ 1);
        if (g_game.dialogue.choice_index == DIALOGUE_CHOICE_YES) {
            hm_put_char_ring(0, row_y, '>', g_game.world.scroll_x, g_game.world.scroll_y);
            hm_put_char_ring(7, row_y, ' ', g_game.world.scroll_x, g_game.world.scroll_y);
        } else {
            hm_put_char_ring(0, row_y, ' ', g_game.world.scroll_x, g_game.world.scroll_y);
            hm_put_char_ring(7, row_y, '>', g_game.world.scroll_x, g_game.world.scroll_y);
        }
        g_bk_byte_a = 1;
        return;
    }
    if (edges & (uint8_t)((1u << INPUT_A) | (1u << INPUT_START))) {
        g_game.dialogue.choice_made = 1;
        if (g_game.dialogue.id == DIALOGUE_ID_KOBOLD_INVITE) {
            /* Chuck-a-luck invite: YES opens the table (fixed handoff
             * via code 7; the dialogue is closed here since the fixed
             * side switches screens without running dialogue_end).
             * NO confirms plainly (code 2): the dialogue advances,
             * closes, and returns to the overworld -- a walk-away. */
            if (g_game.dialogue.choice_index == DIALOGUE_CHOICE_NO) {
                g_bk_byte_a = 2;
            } else {
                g_game.dialogue.active = 0;
                g_bk_byte_a = 7;
            }
            return;
        }
        if (g_game.dialogue.id != DIALOGUE_ID_HEAL_MAGE_ASK) {
            g_bk_byte_a = 2;
            return;
        }
        if (g_game.dialogue.choice_index == DIALOGUE_CHOICE_NO) {
            g_bk_byte_a = 3;
            g_bk_byte_c = (uint8_t)DIALOGUE_ID_HEAL_MAGE_DECLINED;
            return;
        }
        /* YES: full restore loop (bank-local: a banked body must never
         * call across banks). USED is set by the fixed wrapper through
         * game_flag_set (which owns the STORY_FLAG_SET telemetry);
         * setting the bit here as well would make that call a silent
         * no-op and lose the event (AGENTS.md §8). */
        restored = 0;
        for (mi = 0; mi < g_game.state.party.count; mi++) {
            if (g_game.state.party.members[mi].hp < g_game.state.party.members[mi].max_hp) {
                uint8_t missing;
                missing = (uint8_t)(g_game.state.party.members[mi].max_hp - g_game.state.party.members[mi].hp);
                if (missing > restored) restored = missing;
                g_game.state.party.members[mi].hp = g_game.state.party.members[mi].max_hp;
            }
        }
        g_bk_byte_a = 6;
        g_bk_byte_b = restored;
        g_bk_byte_c = STORY_FLAG_ID_HEAL_MAGE_USED;
        g_bk_byte_d = (uint8_t)DIALOGUE_ID_HEAL_MAGE_THANKS;
        return;
    }
    g_bk_byte_a = 0;
}
