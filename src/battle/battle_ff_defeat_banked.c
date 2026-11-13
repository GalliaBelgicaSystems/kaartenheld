#pragma bank 12

#include "battle.h"
#include "banked.h"
#include "telemetry.h"
#include "telemetry_ring.h"
#include "game.h"

/* ── Friendly-fire defeat telemetry (ROM bank 12, DEBUG builds only) ──
 * Moved out of battle_execute_combo (fixed bank is full, AGENTS.md
 * 52.18): the redirect branch there needs a four-way guard plus an emit,
 * and the fixed bank had no room.  This body re-derives everything from
 * WRAM: the victim slot staged by the defend resolve plus the live enemy
 * HP.  Ring writer is header-inline (no trampolined telemetry_emit call
 * from a banked body, AGENTS.md 52.11.1), so this stays self-contained.
 *
 * The normal and counter paths emit ENTITY_DEFEATED only for the targeted
 * attacker, so a redirected kill on a NON-target victim needs its own
 * report -- including on a battle-ending blow (which the old else-if
 * swallowed under the victory).  The despair floor holds the boss at 1
 * HP, so a floored victim never trips this. */
void battle_ff_defeat_banked(void)
{
    Battle *b = &g_game.battle;
    uint8_t vic = g_battle_confuse_victim;
    if (vic == 0xFF) return;
    if (vic >= b->enemy_count) return;
    if (vic == g_despair_slot) return;
    if (b->enemies[vic].hp != 0) return;
    telemetry_ring_emit(EVENT_ENTITY_DEFEATED, (uint8_t)(vic + 1), 0, 0, 0);
}
