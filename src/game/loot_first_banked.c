#pragma bank 11

#include "rpg/loot.h"
#include "core/game.h"
#include "game/game_ids.h"

/* ── First-victory bronze shield, bank-11 body ───────────────────────
 * The first REAL victory's drop is always a bronze shield, so the
 * opening loot teaches blocking; every later drop rolls naturally
 * (balancing, docs/loot.md §34.5).  Gated on the global kill counter
 * (0 kills = no victory yet) -- plus the spar exception: a Carl win
 * increments the counter but grants nothing (rank 0 writes CARD_NONE
 * in the bank-9 roll body, never overridden here), so counter == 1
 * with the spar-won latch set still means "no real victory yet" and
 * the shield survives to the next real fight.  Counter == 1 with the
 * latch clear means a real kill already happened (skip path: Carl is
 * one-shot persistent, so kills == 1 with the latch can only be
 * spar-first).  The latch never clears: the counter only grows, so
 * past 1 it is inert.
 *
 * Lives here and not in the bank-9 roll body because bank 9 is full
 * (loot/terrain bodies + driver + casino); pure WRAM reads/writes, so
 * any roomy bank does.  Runs as the second leg of game_loot_drop()
 * (content.c) right after the bank-9 roll.
 */
void game_loot_first_victory_banked(void)
{
    uint8_t f;
    int16_t kills;

    if (g_loot_id == CARD_NONE) return;
    kills = g_game.state.variables.values[
        VARIABLE_ID_MONSTERS_DEFEATED - 1];
    f = (uint8_t)(STORY_FLAG_ID_TUTORIAL_SPAR_WON - 1);
    if (kills == 0 || (kills == 1 &&
            ((g_game.state.flags.bytes[(uint8_t)(f >> 3)] &
              (uint8_t)(1u << (f & 7))) != 0))) {
        g_loot_id = (uint8_t)(LOOT_ID_BASE +
                              (((MAT_BRONZE << 2) + EFF_PLAIN) << 3) +
                              WPN_SHIELD);
    }
}
