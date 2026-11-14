#pragma bank 12

#include "game.h"
#include "banked.h"

/* ── Town/castle entry autosave probe ─────────────────────────────
 * Dispatched from the overworld gate-crossing branch (fixed cost: one
 * dispatch -- the fixed bank sits hard against 0x8000, AGENTS.md
 * 52.18).  Stages nothing: the live map comes from g_game (WRAM,
 * always mapped).  Sets the autosave request when the gate lands in
 * town or the castle -- the only entries that checkpoint; every other
 * gate is just a walk.  TEST_* aliases included so the fixture suite
 * can pin the trigger (release builds never stage them). */

void game_map_autosave_banked(void)
{
    MapId map = g_game.world.map_id;
    if (map == MAP_TOWN || map == MAP_CASTLE ||
        map == MAP_TEST_TOWN || map == MAP_TEST_CASTLE) {
        g_autosave_pending = 1;
    }
}
