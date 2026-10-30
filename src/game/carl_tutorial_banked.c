#pragma bank 10

#include <stdint.h>
#include "banked.h"
#include "game.h"
#include "game_ids.h"
#include "world/actor.h"
#include "carl_tutorial.h"

/* ── Carl's sparring tutorial tick, bank-10 body ─────────────────
 * Self-contained (AGENTS.md 52.11.1): WRAM + regs only, never calls
 * fixed-bank functions.  Box openings go through game_update's generic
 * post-tick dialogue request (g_bk_dlg_id + single-line override):
 * the fixed wrapper only yields the frame, so the tight fixed bank
 * pays staging + one test per battle frame (AGENTS.md 52.11.1/52.18).
 *
 * carl_tutorial_banked: g_bk_ptr_a = Game*.  Out: g_bk_byte_a = dialogue
 * id to open (DIALOGUE_ID_NONE = nothing this frame).  Side effects on
 * open: msg_id 9 + g_bk_dlg_id (+ line/count for single-line jeers),
 * plus the fled-lesson grant staging.  Non-Carl battles reset the
 * per-fight progress (the battle-start hook is gone; YES re-inits fully,
 * so same-session staleness cannot meet a real fight -- see
 * carl_tutorial.c).  YES/NO arbitration lives with the Heal Mage's in
 * the bank-10 choice body (choice_nav_banked.c). */

void carl_tutorial_banked(void)
{
    Game *g = (Game *)g_bk_ptr_a;
    uint8_t phase;

    g_bk_byte_a = (uint8_t)DIALOGUE_ID_NONE;
    if (!g) return;
    /* Fled-lesson completion (first Carl flee after the lesson): stage
     * the +1 AP graduation + the good-luck speech for game_update's
     * post-tick applier (real state APIs there keep the VARIABLE_SET /
     * STORY_FLAG_SET telemetry truthful).  FLEE_SHOWN gates on the full
     * lesson (an immediate first-turn flee earns nothing); DONE gates
     * both orders (grind-kill after flee, flee after debug-kill). */
    if (g->battle.battle_over &&
        g->battle.enemy_battle_id == BATTLE_CARL &&
        g->battle.result == BATTLE_RESULT_FLED &&
        (g_carl_tut_flags & CARL_TUT_FLAG_FLEE_SHOWN) != 0) {
        /* Fleeing a finished lesson shows Carl's farewell (the 99 HP
         * spar can no longer be won, so the graduation speech is
         * unreachable in real play; the farewell carries no AP -- the
         * pool stays on the chest/hunt/mimic track).  Gated on the full
         * lesson so an immediate first-turn flee earns nothing but a
         * quiet exit.  Consuming FLEE_SHOWN makes the speech one-shot:
         * the RESULT screen re-arms nothing, so closing it lands on
         * FLED! instead of reopening (re-accepting re-inits everything
         * through YES). */
        g_carl_tut_flags &= (uint8_t)~(CARL_TUT_FLAG_FLEE_SHOWN | CARL_TUT_FLAG_HURT_ARMED);
        g_bk_byte_a = (uint8_t)DIALOGUE_ID_TUTORIAL_CARL_FAREWELL;
        g_bk_dlg_id = (uint8_t)DIALOGUE_ID_TUTORIAL_CARL_FAREWELL;
        g->battle.msg_id = 9;
        return;
    }
    if (g->battle.battle_over) return;
    /* Non-Carl battles reset the per-fight progress (the battle-start
     * hook is gone; YES re-inits fully on accept, so same-session
     * staleness cannot meet a real fight). */
    if (g->battle.enemy_battle_id != BATTLE_CARL) {
        g_carl_tut_flags = 0;
        g_carl_tut_hurt = 0;
        g_carl_tut_mile = CARL_TUT_MILE_START;
        return;
    }
    if (g->battle.result != BATTLE_RESULT_NONE) return;

    phase = g->battle.phase;
    /* No offer accepted: plain fight, no coaching.  Battles that reach
     * Carl through the INTRO YES path carry LESSON (assigned by the
     * choice body); consuming it here keeps the downstream once-each
     * flags sufficient on their own. */
    if ((g_carl_tut_flags & CARL_TUT_FLAG_ATTACK_SHOWN) == 0) {
        if ((g_carl_tut_flags & CARL_TUT_FLAG_LESSON) == 0) return;
        g_carl_tut_flags |= (uint8_t)(CARL_TUT_FLAG_ATTACK_SHOWN);
        g_carl_tut_flags &= (uint8_t)~CARL_TUT_FLAG_LESSON;
        g_bk_byte_a = (uint8_t)DIALOGUE_ID_TUTORIAL_CARL_ATTACK;
        g_bk_dlg_id = (uint8_t)DIALOGUE_ID_TUTORIAL_CARL_ATTACK;
        g->battle.msg_id = 9;
        return;
    }
    if (phase == BATTLE_PHASE_PLAYER_DEFEND &&
        (g_carl_tut_flags & CARL_TUT_FLAG_DEFEND_SHOWN) == 0) {
        g_carl_tut_flags |= (uint8_t)(CARL_TUT_FLAG_DEFEND_SHOWN);
        g_bk_byte_a = (uint8_t)DIALOGUE_ID_TUTORIAL_CARL_DEFEND;
        g_bk_dlg_id = (uint8_t)DIALOGUE_ID_TUTORIAL_CARL_DEFEND;
        g->battle.msg_id = 9;
        return;
    }
    if (phase == BATTLE_PHASE_PLAYER_SELECT &&
        (g_carl_tut_flags & CARL_TUT_FLAG_DEFEND_SHOWN) != 0 &&
        (g_carl_tut_flags & CARL_TUT_FLAG_POKER_SHOWN) == 0) {
        g_carl_tut_flags |= (uint8_t)(CARL_TUT_FLAG_POKER_SHOWN);
        g_bk_byte_a = (uint8_t)DIALOGUE_ID_TUTORIAL_CARL_POKER;
        g_bk_dlg_id = (uint8_t)DIALOGUE_ID_TUTORIAL_CARL_POKER;
        g->battle.msg_id = 9;
        return;
    }
    if (phase == BATTLE_PHASE_PLAYER_SELECT &&
        (g_carl_tut_flags & CARL_TUT_FLAG_POKER_SHOWN) != 0 &&
        (g_carl_tut_flags & CARL_TUT_FLAG_FLEE_SHOWN) == 0) {
        g_carl_tut_flags |= (uint8_t)(CARL_TUT_FLAG_FLEE_SHOWN);
        g_bk_byte_a = (uint8_t)DIALOGUE_ID_TUTORIAL_CARL_FLEE;
        g_bk_dlg_id = (uint8_t)DIALOGUE_ID_TUTORIAL_CARL_FLEE;
        g->battle.msg_id = 9;
        return;
    }
    /* Post-lesson jeers: re-arm on every SELECT phase so each confirmed
     * attack (SELECT -> ANIM) earns exactly one line, looping.  The arm
     * clears when the jeer opens, so the rest of the ANIM window after
     * the dialogue closes cannot re-trigger it. */
    if (phase == BATTLE_PHASE_PLAYER_SELECT &&
        (g_carl_tut_flags & CARL_TUT_FLAG_FLEE_SHOWN) != 0) {
        g_carl_tut_flags |= CARL_TUT_FLAG_HURT_ARMED;
    }
    if (phase == BATTLE_PHASE_PLAYER_ANIM &&
        (g_carl_tut_flags & CARL_TUT_FLAG_HURT_ARMED) != 0) {
        uint8_t dealt;
        g_carl_tut_flags &= (uint8_t)~CARL_TUT_FLAG_HURT_ARMED;
        /* Flee reminder every 10 HP removed: the milestone replaces the
         * jeer for that attack, then advances.  Dealt counts from the
         * spar's max (burn ticks included); debug-staged HP can start
         * mid-ladder, which only shifts which attacks nag. */
        dealt = (uint8_t)(g->battle.enemies[0].max_hp - g->battle.enemies[0].hp);
        if (g_carl_tut_mile <= CARL_TUT_MILE_MAX && dealt >= g_carl_tut_mile) {
            g_carl_tut_mile = (uint8_t)(g_carl_tut_mile + CARL_TUT_MILE_STEP);
            g->battle.msg_id = 9;
            g_bk_byte_a = (uint8_t)DIALOGUE_ID_TUTORIAL_CARL_FLEE_REMINDER;
            g_bk_dlg_id = (uint8_t)DIALOGUE_ID_TUTORIAL_CARL_FLEE_REMINDER;
            return;
        }
        g->battle.msg_id = 9;
        g_bk_byte_a = (uint8_t)DIALOGUE_ID_TUTORIAL_CARL_HURT;
        g_bk_dlg_id = (uint8_t)DIALOGUE_ID_TUTORIAL_CARL_HURT;
        g_bk_dlg_line = g_carl_tut_hurt;
        g_bk_dlg_count = (uint8_t)(g_carl_tut_hurt + 1);
        g_carl_tut_hurt++;
        if (g_carl_tut_hurt >= CARL_TUT_HURT_COUNT) g_carl_tut_hurt = 0;
    }
}

