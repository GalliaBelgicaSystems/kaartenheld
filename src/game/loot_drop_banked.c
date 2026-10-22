#pragma bank 9

#include "rpg/loot.h"
#include "banked.h"
#include "actor.h"
#include "core/game.h"
#include "game_ids.h"

/* ── Victory loot drop, bank-9 body (docs/loot.md §8/§17/§34.5) ─────
 * SINGLE banked entry for the whole drop decision: archetype weapon
 * pick from the enemy family's profile, material + legal-effect roll,
 * derived-id encode.  Every victory drops exactly one card -- quality
 * (material/effect) is what varies.
 *
 * Runs from ROM bank 9 via the WRAM trampoline.  Self-contained: reads
 * the battle id from g_bk_byte_a, steps g_loot_rng_state inline (never
 * a fixed-bank call), and reports through g_loot_id.
 *
 * The profile table is game content and lives here with the body; the
 * fixed-bank cost of drops is one tiny wrapper in src/game/content.c.
 */

/* AP-gated material roll (docs/loot.md §34.5).  s_ap_cap is the highest
 * material index that AP can roll (AP 1 = wood only; AP 2-3 = bronze;
 * AP 4 = iron; AP 5+ = steel/mythril).  s_ap_cut is the cumulative
 * 1/255 threshold for each material: the roll walks up while it exceeds
 * the current cut.  High tiers stay rare even at AP 5 (steel ~8%,
 * mythril ~2%).  AP 5 and above share row 4; see LOOT_AP_* in loot.h. */
static const uint8_t s_ap_cap[5] = {
    MAT_WOOD, MAT_BRONZE, MAT_BRONZE, MAT_IRON, MAT_MYTHRIL
};
static const uint8_t s_ap_cut[5][5] = {
    { 255,   0,   0,   0,   0 },   /* AP 1: wood only */
    { 204, 255,   0,   0,   0 },   /* AP 2: ~80/20 wood/bronze */
    { 166, 255,   0,   0,   0 },   /* AP 3: ~65/35 wood/bronze */
    { 128, 204, 255,   0,   0 },   /* AP 4: ~50/30/20 */
    {  89, 166, 230, 250, 255 }    /* AP 5+: ~35/30/25/8/2 */
};

void game_loot_drop_banked(void)
{
    uint8_t battle_type = g_bk_byte_a;
    uint8_t weapon;

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

    /* Roll material + legal effect for the weapon (AP-gated weights;
     * legal non-plain effects per §34.2: daggers poison, swords burn or
     * chill, everything else stays plain).  Rings follow the same
     * material roll -- healing uses the same material bands as attack
     * and defense. */
    g_loot_rng_state ^= g_loot_rng_state << 7;
    g_loot_rng_state ^= g_loot_rng_state >> 9;
    g_loot_rng_state ^= g_loot_rng_state << 8;
    {
        uint8_t mat_roll = (uint8_t)g_loot_rng_state;
        uint8_t eff_roll = (uint8_t)(g_loot_rng_state >> 8);
        uint8_t ap = (uint8_t)g_game.state.variables.values[
            VARIABLE_ID_ENERGY_POOL - 1];
        uint8_t row;
        uint8_t cap;
        uint8_t material = MAT_WOOD;
        uint8_t effect = EFF_PLAIN;

        /* Map AP to the weighted-roll row.  The LOOT_AP_* thresholds are
         * the unlock contract (docs/loot.md §34.5): AP 1 wood; AP 2-3
         * bronze; AP 4 iron; AP 5+ steel/mythril. */
        if (ap < LOOT_AP_BRONZE) {
            row = 0;
        } else if (ap < LOOT_AP_IRON) {
            row = (uint8_t)(1 + (ap - LOOT_AP_BRONZE));
        } else if (ap < LOOT_AP_STEEL) {
            row = 3;
        } else {
            row = 4;
        }
        cap = s_ap_cap[row];
        while (material < cap &&
               mat_roll > s_ap_cut[row][material]) {
            material++;
        }

        /* Plain-heavy (§34.5): only 25% of rolls carry a rider, and
         * wood can never carry one (docs/loot.md §34.1). */
        if (material >= MAT_BRONZE && eff_roll < 64) {
            if (weapon == WPN_DAGGER) {
                effect = EFF_POISON;
            } else if (weapon == WPN_SWORD) {
                effect = (eff_roll < 32) ? EFF_FIRE : EFF_ICE;
            }
        }

        /* Derived id: BASE + mat*32 + eff*8 + wpn (shifts only). */
        g_loot_id = (uint8_t)(LOOT_ID_BASE +
                              (((material << 2) + effect) << 3) + weapon);
    }
}
