#pragma bank 9

#include "rpg/loot.h"
#include "banked.h"
#include "actor.h"

/* ── Victory loot drop, bank-9 body (docs/loot.md §8/§17/§34.5,
 * docs/enemies_ranking.md §4.3) ─────────────────────────────
 * SINGLE banked entry for the whole drop decision: archetype weapon
 * pick from the enemy family's profile, rank-centered material +
 * legal-effect roll, derived-id encode.  Every victory drops exactly
 * one card -- quality (material/effect) is what varies.
 *
 * Runs from ROM bank 9 via the WRAM trampoline.  Self-contained: reads
 * the battle id from g_bk_byte_a, the rank from the compiled
 * g_enemy_rank_by_battle row (same bank), steps g_loot_rng_state inline
 * (never a fixed-bank call), and reports through g_loot_id.
 *
 * Rank-only (no AP read): the material walk reads the rank row, so the
 * same seed yields the same card at every pool -- a slime knight is a
 * slime knight.  The equip gate (deck_equip_card) keeps unpayable cards
 * out of the deck instead of clamping the roll.  Rank 0 (lesson fights)
 * writes CARD_NONE: no card.  The first-victory bronze shield is applied
 * by the second game_loot_drop() leg (bank 11, loot_first_banked.c),
 * which never overrides CARD_NONE -- so a spar can neither grant nor
 * consume it.  It lives there and not here because bank 9 is full
 * (loot/terrain bodies + driver + casino).
 *
 * The profile table is game content and lives here with the body; the
 * fixed-bank cost of drops is one tiny wrapper in src/game/content.c.
 */

/* Rank-centered material roll (docs/enemies_ranking.md Appendix A).
 * The roll walks up while it exceeds the current cut.  Rows are the
 * realized 1/256 outcomes, strictly increasing in expected material
 * (R1 0.65 -> R6 3.00): R4 peaks on iron, R5 on steel, R6 on mythril. */
static const uint8_t s_rank_cut[7][5] = {
    {   0,   0,   0,   0,   0 },   /* R0 LESSON   (never rolled) */
    { 139, 216, 247, 253, 255 },   /* R1 FODDER   */
    {  88, 185, 242, 253, 255 },   /* R2 VERMIN   */
    {  50, 139, 216, 247, 255 },   /* R3 GRUNT    */
    {  24,  75, 178, 242, 255 },   /* R4 VETERAN  */
    {   4,  24,  88, 216, 255 },   /* R5 ELITE    */
    {   0,  11,  63, 178, 255 }    /* R6 BOSS     */
};

/* Rank-stepped rider gate (docs/enemies_ranking.md §4.3): R0-1 25%,
 * R2 31.25%, R3 37.5%, R4 43.75%, R5-6 50% of bronze+ sword/dagger
 * rolls carry the legal rider. */
static const uint8_t s_rank_gate[7] = { 64, 64, 80, 96, 112, 128, 128 };

/* BattleId -> rank R0-R6 (compiled by battle_compile.py from the
 * enemy_type JSON ranks; same bank, direct read). */
extern const uint8_t g_enemy_rank_by_battle[9];

void game_loot_drop_banked(void)
{
    uint8_t battle_type = g_bk_byte_a;
    uint8_t weapon;
    uint8_t rank;

    /* Always-drop (docs/loot.md §34.5): the 50% gate is gone -- every
     * victory yields a card, quality varies instead of presence.  The
     * stream is still stepped once here so the weapon/material/effect
     * rolls below keep their historical positions for a given seed.
     * Drops must never consume or shift the shared game RNG (§26/§34.5). */
    g_loot_rng_state ^= g_loot_rng_state << 7;
    g_loot_rng_state ^= g_loot_rng_state >> 9;
    g_loot_rng_state ^= g_loot_rng_state << 8;

    /* Archetype weapon from the enemy family's profile bias (§17).
     * Bats lean ranged; everything else drops the melee set. */
    if (battle_type == BATTLE_BAT) {
        weapon = WPN_BOW;
    } else {
        g_loot_rng_state ^= g_loot_rng_state << 7;
        g_loot_rng_state ^= g_loot_rng_state >> 9;
        g_loot_rng_state ^= g_loot_rng_state << 8;
        /* Profile sizes are powers of two: mask, never % (52.11.1). */
        switch ((uint8_t)g_loot_rng_state & 3) {
            case 0:  weapon = WPN_SWORD;  break;
            case 1:  weapon = WPN_SHIELD; break;
            case 2:  weapon = WPN_DAGGER; break;
            default: weapon = WPN_RING;   break;
        }
    }

    /* Roll material + legal effect for the weapon (rank-centered
     * weights; legal non-plain effects per §34.2: daggers poison, swords
     * burn or chill, everything else stays plain).  Rings follow the same
     * material roll -- healing uses the same material bands as attack
     * and defense. */
    g_loot_rng_state ^= g_loot_rng_state << 7;
    g_loot_rng_state ^= g_loot_rng_state >> 9;
    g_loot_rng_state ^= g_loot_rng_state << 8;
    {
        uint8_t mat_roll = (uint8_t)g_loot_rng_state;
        uint8_t eff_roll = (uint8_t)(g_loot_rng_state >> 8);
        uint8_t material = MAT_WOOD;
        uint8_t effect = EFF_PLAIN;

        /* Rank = shape (docs/enemies_ranking.md §4.3/§8.1).  Unknown
         * battle ids fall back to the fodder row (never CARD_NONE: only
         * rank 0 means no drop). */
        if (battle_type < 9) {
            rank = g_enemy_rank_by_battle[battle_type];
        } else {
            rank = 1;
        }
        if (rank > 6) rank = 1;
        if (rank == 0) {
            g_loot_id = CARD_NONE;
            return;
        }

        while (material < MAT_MYTHRIL &&
               mat_roll > s_rank_cut[rank][material]) {
            material++;
        }

        /* Rider chance steps with rank (s_rank_gate); wood can never
         * carry one (docs/loot.md §34.1).  No mythril hold-back: the
         * equip gate covers unaffordable cards, so the generator hands
         * them out and the collection shows the locked goal.  The
         * fire/ice split scales with the gate (half each,
         * docs/enemies_ranking.md §4.3/§4.5) instead of the old flat 32,
         * which only matched at gate 64 (R0-R1). */
        if (material >= MAT_BRONZE && eff_roll < s_rank_gate[rank]) {
            if (weapon == WPN_DAGGER) {
                effect = EFF_POISON;
            } else if (weapon == WPN_SWORD) {
                effect = (eff_roll < (uint8_t)(s_rank_gate[rank] >> 1)) ?
                    EFF_FIRE : EFF_ICE;
            }
        }

        /* Derived id: BASE + mat*32 + eff*8 + wpn (shifts only). */
        g_loot_id = (uint8_t)(LOOT_ID_BASE +
                              (((material << 2) + effect) << 3) + weapon);

        /* NOTE: the first-victory bronze shield is applied by the
         * second game_loot_drop() leg (bank 11) after this body
         * returns (see content.c).  Nothing is consumed or overridden
         * here, so the isolated stream keeps its positions for a
         * given seed. */
    }
}
