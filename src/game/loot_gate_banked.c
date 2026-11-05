#pragma bank 9

#include "rpg/loot.h"
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

/* Loot equip-gate check (docs/enemies_ranking.md §4.8).
 * g_bk_byte_a = card id, g_bk_byte_b = live pool; result in g_bk_byte_c:
 * 0 = proceed (curated ids are exempt; reserved weapons cost 0), else
 * the required AP (the card's synthesized cost, which exceeds the pool).
 * The cost math mirrors loot_synth_banked exactly (same macros, same
 * rider legality, same reserved-weapon rule) so the gate and the def
 * can never disagree. */
void deck_equip_gate_banked(void)
{
    uint8_t id = g_bk_byte_a;
    uint8_t pool = g_bk_byte_b;
    uint8_t material;
    uint8_t effect;
    uint8_t weapon;
    uint8_t wi;
    uint8_t power;
    uint8_t has_rider;
    uint8_t cost;

    if (!loot_is_loot_id(id)) {
        g_bk_byte_c = 0;
        return;
    }
    material = loot_id_material(id);
    effect = loot_id_effect(id);
    weapon = loot_id_weapon(id);
    if (weapon > WPN_RING) {
        g_bk_byte_c = 0;
        return;
    }
    wi = weapon;
    power = LOOT_POWER(material, wi);
    has_rider = (material >= MAT_BRONZE && effect != EFF_PLAIN &&
                 ((effect == EFF_POISON) ? (weapon == WPN_DAGGER) :
                  ((effect == EFF_FIRE || effect == EFF_ICE) ?
                      (weapon == WPN_SWORD) : 0)));
    cost = CARD_AP(power, material, has_rider);
    g_bk_byte_c = (cost > pool) ? cost : 0;
}
