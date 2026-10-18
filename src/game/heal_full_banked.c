#pragma bank 2

#include <stdint.h>
#include "banked.h"
#include "game.h"

/* ── Heal Mage full-party restore, bank-2 body ─────────────────────
 * Dispatched from the event executor's EVENT_ACTION_HEAL_FULL branch
 * via the WRAM trampoline (fixed-bank cost: branch + staging + call,
 * like the slime-trio spawn -- the fixed bank sits hard against
 * 0x8000 and cannot host the loop, AGENTS.md §52.18).
 *
 * Self-contained: WRAM only (g_game, g_bk_byte_b), no fixed-bank
 * calls (telemetry_emit lives in the fixed bank, so the fixed wrapper
 * emits EVENT_HEALED itself from the staged result -- cause 3 = NPC
 * heal).  Reports the largest single-member missing HP through
 * g_bk_byte_b for the telemetry arg.  game_update mirrors party HP
 * into the world/overworld HUD copy next frame, so no mirror write
 * is needed here. */

void heal_full_banked(void)
{
    uint8_t mi;
    uint8_t restored;
    restored = 0;
    for (mi = 0; mi < g_game.state.party.count; mi++) {
        if (g_game.state.party.members[mi].hp < g_game.state.party.members[mi].max_hp) {
            uint8_t missing;
            missing = (uint8_t)(g_game.state.party.members[mi].max_hp - g_game.state.party.members[mi].hp);
            if (missing > restored) restored = missing;
            g_game.state.party.members[mi].hp = g_game.state.party.members[mi].max_hp;
        }
    }
    g_bk_byte_b = restored;
}
