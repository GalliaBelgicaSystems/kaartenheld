#pragma bank 9

#include <stdint.h>
#include "banked.h"

/* ── Rank/equip gate helpers (ROM bank 9) ─────────────────────────
 * Two tiny banked bodies sharing the loot bank (which has room), so the
 * fixed bank pays only staging wrappers (AGENTS.md §52.11.1/§52.18).
 * Both are pure WRAM + same-bank reads; no fixed-bank calls, no di/ei.
 */

/* BattleId -> rank R0-R6 (compiled by battle_compile.py from the
 * enemy_type JSON ranks; same bank, direct read). */
extern const uint8_t g_enemy_rank_by_battle[9];

/* Battle -> rank byte (docs/enemies_ranking.md §4.3).
 * g_bk_byte_a = battle id; result in g_bk_byte_b (rank 0 allowed: lesson
 * fights; unknown battles fall back to the fodder row, never CARD_NONE).
 * Lets fixed-bank victory code report the rank in LOOT_CARD_ADDED d2
 * without a WRAM byte and without mapping bank 9 itself. */
void game_loot_rank_banked(void)
{
    uint8_t battle_type = g_bk_byte_a;
    uint8_t rank = 1;

    if (battle_type < 9) {
        rank = g_enemy_rank_by_battle[battle_type];
        if (rank > 6) rank = 1;
    }
    g_bk_byte_b = rank;
}
