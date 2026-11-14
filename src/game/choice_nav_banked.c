#pragma bank 10

#include <gb/gb.h>
#include "game.h"
#include "game_ids.h"
#include "ui.h"
#include "banked.h"
#include "game/gamble.h"
#include "carl_tutorial.h"

/* ── YES/NO choice navigation + resolve, bank-10 body ─────────
 * Dispatched from dialogue_screen_update() when a choice dialogue sits
 * on its last page unconfirmed (fixed-bank cost: one predicate +
 * staging + call -- the fixed bank sits hard against 0x8000, AGENTS.md
 * §52.18).  Follows the quest-tab nav precedent (item_quest_banked.c):
 * pad edges read directly from WRAM (always mapped), compile-time
 * bitmasks, no helper calls.  Bank 10 (not 8): the fixed bank-8 budget
 * fits no new bodies, while bank 10 has room in both builds (AGENTS.md
 * 55.5: banks 8-15 need no asm change to reach).
 *
 * The engine owns the choice UI contract (option row on the last line,
 * cursor cells 0/7 -- "> YES   NO" for most prompts, "> STAY  LEAVE"
 * for Carl's leave-the-field offer with the second option still at
 * col 8); this game-owned body owns what an answer means.
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
 *   - CARL INTRO + NO  -> clear the stashed spar (no battle) and open
 *                  the decline line (staged follow-up).
 *   - CARL INTRO + YES -> assign LESSON progress (full re-init, so
 *                  re-accepting replays the lesson) and close into the
 *                  stashed spar (plain confirm: the dialogue close starts
 *                  the battle through the stashed slot).
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
 *      5 (plain advance: A/START edge outside any live choice),
 *      9 (YES: grant the staged item + set the staged one-shot flag +
 *      open the staged follow-up; a failed grant closes plainly with
 *      nothing set, so the offer can be retried),
 *      10 (YES: swap the staged remove-item for the staged add-item +
 *      set the staged quest variable to COMPLETE + open the staged
 *      follow-up; a failed add closes plainly with nothing removed
 *      or set, so the trade can be retried).
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
    /* The option row reads "> YES   NO" (or "> STAY  LEAVE" for the
     * skip offer, same cursor cells) left-to-right: all four D-pad
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
        if (g_game.dialogue.id == DIALOGUE_ID_TUTORIAL_CARL_SKIP_ASK) {
            /* Leaving the field without the lesson.  STAY (first option)
             * closes plainly (the hero stays put and can walk back to
             * Carl; the next exit attempt asks again -- the offer keeps
             * no departure-tile memory, so STAY never opens the exit);
             * LEAVE (second option) hands over the +1 AP the spar would
             * have granted: code 8 has the fixed wrapper apply the staged
             * variable add + one-shot skip flag through the real APIs
             * (VARIABLE_SET / STORY_FLAG_SET telemetry) and open the
             * "TAKE THIS AP!" line. */
            if (g_game.dialogue.choice_index == DIALOGUE_CHOICE_NO) {
                g_bk_byte_a = 8;
                g_bk_byte_b = VARIABLE_ID_ENERGY_POOL;
                g_bk_byte_c = STORY_FLAG_ID_TUTORIAL_SKIPPED;
                g_bk_byte_d = (uint8_t)DIALOGUE_ID_TUTORIAL_CARL_SKIP_GRANTED;
                /* AP milestone: the fixed side applies the staged grant
                 * below in this same update; the flush saves it. */
                g_autosave_pending = 1;
                return;
            }
            g_bk_byte_a = 2;
            return;
        }
        if (g_game.dialogue.id == DIALOGUE_ID_TUTORIAL_CARL_INTRO) {
            /* Carl's spar offer: NO clears the stashed spar so closing
             * the follow-up returns to the overworld with no fight; YES
             * assigns LESSON progress (full re-init, so re-accepting
             * replays the lesson) and closes into the stashed spar. */
            if (g_game.dialogue.choice_index == DIALOGUE_CHOICE_NO) {
                g_game.world.encounter_actor_index = NO_ACTOR_INDEX;
                g_bk_byte_a = 3;
                g_bk_byte_c = (uint8_t)DIALOGUE_ID_TUTORIAL_CARL_DECLINE;
                return;
            }
            /* Full re-init, so re-accepting replays the lesson.  The
             * AP line of the attack box is truthful only when THIS
             * spar's start will grant the AP (one-shot via
             * TUTORIAL_SKIPPED, owned by the bank-5 grant body): a
             * clear latch now means the grant fires, so arm the claim
             * bit; a set latch means a re-spar or post-skip spar, so
             * the no-grant coaching box opens instead. */
            g_carl_tut_flags = CARL_TUT_FLAG_LESSON;
            {
                uint8_t sf = (uint8_t)(STORY_FLAG_ID_TUTORIAL_SKIPPED - 1);
                if ((g_game.state.flags.bytes[(uint8_t)(sf >> 3)] &
                     (uint8_t)(1u << (sf & 7))) == 0) {
                    g_carl_tut_flags |= CARL_TUT_FLAG_AP_GRANTED;
                }
            }
            g_carl_tut_hurt = 0;
            g_bk_byte_a = 2;
            return;
        }
        if (g_game.dialogue.id == DIALOGUE_ID_KOBOLD_INVITE) {
            /* Chuck-a-luck invite: YES reports code 7 and leaves the
             * dialogue ACTIVE (the fixed side ends it through
             * dialogue_end()) while raising g_gamble_enter for the tick
             * body.  NO confirms plainly (code 2): the dialogue
             * advances, closes, and returns to the overworld -- a
             * walk-away. */
            if (g_game.dialogue.choice_index == DIALOGUE_CHOICE_NO) {
                g_bk_byte_a = 2;
            } else {
                /* YES path: raising the enter request and reporting
                 * code 7 keeps DIALOGUE_ENDED paired with the choice
                 * event (a raw active = 0 here would silently skip it).
                 * The bank-8
                 * tick then opens SCREEN_GAMBLE. */
                g_gamble_enter = 1;
                g_bk_byte_a = 7;
            }
            return;
        }
        /* Save & quit offer (QUEST-tab A): YES reports the quit code
         * (the fixed side flushes the active slot and lands on the
         * title); NO closes plainly through the ordinary confirm. */
        if (g_game.dialogue.id == DIALOGUE_ID_QUIT_CONFIRM) {
            if (g_game.dialogue.choice_index == DIALOGUE_CHOICE_NO) {
                g_bk_byte_a = 2;
                return;
            }
            g_bk_byte_a = 11;
            return;
        }
        /* Tunnel skeleton's bone offer: YES grants the bone through
         * the real collection API (fixed wrapper, code 9: staged item
         * + one-shot flag + taken follow-up); NO closes plainly and
         * the skeleton asks again. */
        if (g_game.dialogue.id == DIALOGUE_ID_SKELETON_ASK) {
            if (g_game.dialogue.choice_index == DIALOGUE_CHOICE_NO) {
                g_bk_byte_a = 2;
                return;
            }
            g_bk_byte_a = 9;
            g_bk_byte_b = (uint8_t)CARD_BONE;
            g_bk_byte_c = STORY_FLAG_ID_BONE_TAKEN;
            g_bk_byte_d = (uint8_t)DIALOGUE_ID_SKELETON_TAKEN;
            return;
        }
        /* Village dog's key trade: YES swaps the bone for the Old Key
         * through the real collection API (fixed wrapper, code 10:
         * staged remove + staged add + staged quest-COMPLETE +
         * thanks follow-up).  The one-shot DOG_KEY flag rides the
         * thanks dialogue's completion_flag.  NO closes plainly. */
        if (g_game.dialogue.id == DIALOGUE_ID_DOG_ASK) {
            if (g_game.dialogue.choice_index == DIALOGUE_CHOICE_NO) {
                g_bk_byte_a = 2;
                return;
            }
            g_bk_byte_a = 10;
            g_bk_byte_b = (uint8_t)CARD_BONE;
            g_bk_byte_c = (uint8_t)CARD_OLD_KEY;
            g_bk_byte_d = (uint8_t)DIALOGUE_ID_DOG_THANKS;
            g_bk_byte_e = (uint8_t)VARIABLE_ID_DOG_QUEST;
            /* Quest solve: the fixed side applies the staged trade below
             * in this same update; the game_update flush saves it. */
            g_autosave_pending = 1;
            return;
        }
        /* Rings lesson ask ("Want to learn how rings work?"): YES opens
         * the tactics lesson below; completing it sets RING_LESSON
         * through its completion_flag, retiring the ask row so later
         * talks fall through to the heal flow.  NO falls through to the
         * normal heal flow while the free heal is still available
         * (USED unset -- the heal ask owns the one-shot there, so no
         * second heal is possible); with USED already set there is
         * nothing to fall through to, so NO closes plainly and the ask
         * repeats next talk.  Code 3 opens the staged follow-up
         * regardless of which answer selected it. */
        if (g_game.dialogue.id == DIALOGUE_ID_HEAL_MAGE_RINGS_ASK) {
            if (g_game.dialogue.choice_index == DIALOGUE_CHOICE_NO) {
                uint8_t sf = (uint8_t)(STORY_FLAG_ID_HEAL_MAGE_USED - 1);
                /* Same bit layout as flag_byte_bit() in rpg/state.c
                 * (byte (id-1)/8, mask 1 << ((id-1)%8)); a banked body
                 * cannot call that fixed-bank helper. */
                if ((g_game.state.flags.bytes[(uint8_t)(sf >> 3)] &
                     (uint8_t)(1u << (sf & 7))) == 0) {
                    g_bk_byte_a = 3;
                    g_bk_byte_c = (uint8_t)DIALOGUE_ID_HEAL_MAGE_ASK;
                    return;
                }
                g_bk_byte_a = 2;
                return;
            }
            g_bk_byte_a = 3;
            g_bk_byte_c = (uint8_t)DIALOGUE_ID_HEAL_MAGE_RING;
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
