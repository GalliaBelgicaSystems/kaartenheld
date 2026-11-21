#pragma bank 10
#pragma disable_warning 110

#include <stdint.h>
#include <gb/gb.h>
#include "banked.h"
#include "game.h"
#include "game_ids.h"
#include "actor.h"
#include "rpg/state.h"

/* ── Carl's "leaving without the lesson" offer (ROM bank 10) ─────────
 * The field is where Carl teaches.  Walking out of it -- a gate/edge
 * step that would cross the map boundary -- before winning the spar and
 * before taking the AP skip stops the step (the position has not
 * committed yet) and stages Carl's offer through the generic post-tick
 * dialogue request ("HEY! WANNA LEARN HOW FIGHT?" with a STAY / LEAVE
 * option row); the bank-10 choice body resolves the answer: LEAVE grants
 * the spar's +1 AP once and sets STORY_FLAG_ID_TUTORIAL_SKIPPED (so the
 * offer never repeats and the tutorial victory grants no second point),
 * STAY just closes so the hero can walk back to Carl -- and the next
 * attempt to leave asks again (the offer deliberately does NOT disarm
 * the departure tile: STAY must keep blocking the exit, not open it).
 *
 * Lives here (not bank 8 with the rest of the per-frame tick) because
 * bank 8 sits hard against 16 KB (AGENTS.md 52.18/55.5): the tick
 * dispatches it through the WRAM trampoline like every other banked
 * body.  Self-contained (AGENTS.md 52.11.1): WRAM game state + same-bank
 * code only, no fixed-bank calls (the flag reads are inlined below
 * instead of calling bank-8 helpers across the window).
 *
 * No departure-tile memory: the cancelled step already sets move_state
 * back to IDLE, and the screen guard below suppresses the offer while
 * the dialogue is open, so holding the exit direction re-asks on the
 * next fresh EDGE attempt instead of looping inside one frame. */
void carl_skip_offer_banked(void)
{
    World *w = &g_game.world;
    uint8_t f;

    if (g_game.world.map_id != MAP_FIELD
#ifdef TEST_LEVELS
        && g_game.world.map_id != MAP_TEST_FIELD
#endif
        ) {
        return;
    }
    if (g_game.screen != SCREEN_OVERWORLD) return;
    if (w->move_state != MOVE_STATE_MOVING) return;
    if (w->move_outcome != MOVE_OUTCOME_EDGE &&
        w->move_outcome != MOVE_OUTCOME_EXIT) return;
    /* Spar already won (one-shot latch): no offer.  The latch is the
     * STORY_FLAG_ID_TUTORIAL_SPAR_WON story flag (set by the victory
     * event), NOT the zone defeat bit: zone changes wipe the bit, but
     * the lesson stays taught.  Same bit math as rpg/state.c. */
    f = (uint8_t)(STORY_FLAG_ID_TUTORIAL_SPAR_WON - 1);
    if ((g_game.state.flags.bytes[(uint8_t)(f >> 3)] &
         (uint8_t)(1u << (f & 7))) != 0) {
        return;
    }
    /* Skip already taken (one-shot): no offer.  Same bit math as
     * rpg/state.c (flag N lives in bit (N-1)%8 of flags.bytes[(N-1)/8]). */
    f = (uint8_t)(STORY_FLAG_ID_TUTORIAL_SKIPPED - 1);
    if ((g_game.state.flags.bytes[(uint8_t)(f >> 3)] &
         (uint8_t)(1u << (f & 7))) != 0) {
        return;
    }

    /* Cancel the departure: the hero stays on the field to answer. */
    w->move_state = MOVE_STATE_IDLE;
    w->move_progress = 0;
    w->move_outcome = MOVE_OUTCOME_NONE;
    g_bk_dlg_id = (uint8_t)DIALOGUE_ID_TUTORIAL_CARL_SKIP_ASK;
}
