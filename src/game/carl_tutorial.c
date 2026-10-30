#include "game.h"
#include "content.h"
#include "game_ids.h"
#include "banked.h"
#include "carl_tutorial.h"

/* ── Carl's sparring tutorial, fixed-bank half (game layer) ─────
 * The decision logic lives in the bank-10 tutorial body
 * (carl_tutorial_banked.c); box openings ride
 * game_update's generic post-tick dialogue request (g_bk_dlg_id +
 * single-line override), so the tight fixed bank pays staging + one
 * yield test per battle frame (AGENTS.md 52.11.1/52.18).  Engine-owned
 * callers (battle_screen, dialogue_screen) reach this only through the
 * content.h hooks, so no engine file names Carl or his dialogues
 * (AGENTS.md 55.1).  YES/NO arbitration lives with the Heal Mage's in
 * the bank-10 choice body (choice_nav_banked.c).
 *
 * The pre-battle lesson (TUTORIAL_CARL_INTRO) is a YES/NO ask: YES
 * assigns LESSON progress and closes into the stashed spar, NO clears
 * the stash and opens the decline line.  The fight itself is a plain
 * solo battle screen; Carl coaches through normal dialogue boxes once
 * each (attack, defend, poker, flee), then one looping jeer per player
 * attack (plus a flee reminder every 10 HP removed).
 *
 * State is three BSS bytes.  Non-Carl battles reset them in the tick
 * body; YES assigns them on accept; the battle-start hook is gone by
 * design -- same-session staleness cannot meet a real fight (harness
 * scenarios boot fresh RAM per process; gameplay Carl battles always
 * pass through YES; the walkthrough fights one Carl battle). */

/* Per-fight progress (defined here, shared with the bank-10 bodies). */
uint8_t g_carl_tut_flags;
uint8_t g_carl_tut_hurt;
uint8_t g_carl_tut_mile;

bool game_carl_tutorial_update(Game *g)
{
    /* No null check (fixed-bank budget): battle_screen_update already
     * returned on null before calling.  The banked body stages any box
     * for game_update's post-tick opener; truthy means yield this
     * frame (the box opens later in the same game_update, so input and
     * battle_update must not run into it). */
    g_bk_call_bank = 10;
    g_bk_call_target = (uint16_t)&carl_tutorial_banked;
    g_bk_ptr_a = (void *)g;
    banked_call_run();
    return g_bk_byte_a != (uint8_t)DIALOGUE_ID_NONE;
}
