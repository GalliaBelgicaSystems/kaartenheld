#pragma bank 5

#include "banked.h"
#include "game.h"
#include "game_ids.h"
#include "telemetry.h"
#include "telemetry_ring.h"

/* ── Carl's spar-start AP grant (ROM bank 5) ─────────────────────────
 * The lesson's +1 AP is handed over when the spar STARTS, not on the
 * win: start_battle_from_world() dispatches this body for BATTLE_CARL
 * encounters, and the opening coaching box announces it ("TAKE THIS AP
 * TO DO POKER HANDS!" -- first spar only; re-spars open the no-grant
 * variant, see CARL_TUT_FLAG_AP_GRANTED in carl_tutorial.h).
 * One-shot via STORY_FLAG_ID_TUTORIAL_SKIPPED
 * (shared with the leave-the-field skip, which hands over the same
 * point): re-spars grant nothing and the victory event keeps only its
 * graduation speech.
 *
 * Lives here (not inline in the overworld screen) because the fixed
 * bank sits hard against 0x8000 (AGENTS.md §52.18): the encounter path
 * pays only staging + one dispatch.  Bank 5 (not 10 with the other
 * Carl bodies): bank 10 hosts the event table and the tight world/shop
 * banks leave no room there either.  Self-contained (AGENTS.md
 * §52.11.1): WRAM game state + same-bank code only, never calls
 * fixed-bank functions -- the flag bit math and the variable add are
 * inlined below (same replicas as carl_offer_banked.c), and telemetry
 * appends run inline on the ring buffer (debug builds only), replicated
 * field-for-field from battle_telegraph_banked.c instead of dispatching
 * another banked body (banked.h non-reentrancy).  No staging: reads
 * g_game directly like the choice body.
 *
 * No di/ei anywhere (AGENTS.md §52.11.3): the body runs with interrupts
 * enabled under the music-clock trampoline. */

/* Ring telemetry runs through telemetry_ring.h (one shared inline
 * writer); the release macro there compiles the calls out. */

void carl_spar_grant_banked(void)
{
    int16_t pool;

    /* One-shot: already granted (spar start or leave-the-field skip). */
    if ((g_game.state.flags.bytes[(STORY_FLAG_ID_TUTORIAL_SKIPPED - 1) >> 3] &
         (uint8_t)(1u << ((STORY_FLAG_ID_TUTORIAL_SKIPPED - 1) & 7))) != 0) return;

    /* Uniform ADD 1 (replica of game_variable_add: uncapped, always
     * emits VARIABLE_SET with the new value LE). */
    pool = g_game.state.variables.values[(uint8_t)(VARIABLE_ID_ENERGY_POOL - 1)];
    pool = (int16_t)(pool + 1);
    g_game.state.variables.values[(uint8_t)(VARIABLE_ID_ENERGY_POOL - 1)] = pool;
    telemetry_ring_emit((uint8_t)EVENT_VARIABLE_SET,
                    (uint8_t)VARIABLE_ID_ENERGY_POOL,
                    (uint8_t)(pool & 0xFF),
                    (uint8_t)((pool >> 8) & 0xFF), 0);
    /* AP milestone: the game_update flush saves the active slot. */
    g_autosave_pending = 1;

    /* Latch the one-shot (replica of game_flag_set: emits only on change;
     * the early return above guarantees the change).  Whether THIS spar
     * granted the AP is recorded by the INTRO YES handler, not here:
     * bank 5 sits at 0 B headroom in release (AGENTS.md §52.18), so the
     * bit lives or dies with the one-shot test the choice body already
     * pays (carl_tutorial.h). */
    g_game.state.flags.bytes[(STORY_FLAG_ID_TUTORIAL_SKIPPED - 1) >> 3] |=
        (uint8_t)(1u << ((STORY_FLAG_ID_TUTORIAL_SKIPPED - 1) & 7));
    telemetry_ring_emit((uint8_t)EVENT_STORY_FLAG_SET,
                    (uint8_t)STORY_FLAG_ID_TUTORIAL_SKIPPED, 0, 0, 0);

    /* Refresh the live fight: this body runs after battle_start staged
     * the pool from the pre-grant variable, so without this the variable
     * would read 2 while the current fight still ran at 1 AP.  Cap
     * mirrors the entry stager (uncapped variable, 255-capped pool). */
    if (pool > 255) pool = 255;
    g_battle_pool_max = (uint8_t)pool;
    g_game.battle.energy = (uint8_t)pool;
}
