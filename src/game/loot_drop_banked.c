#pragma bank 3

#include "rpg/loot.h"
#include "banked.h"
#include "actor.h"

/* ── Victory loot drop, bank-3 body (docs/loot.md §8/§17/§34.5) ─────
 * SINGLE banked entry for the whole drop decision: archetype weapon
 * pick from the enemy family's profile, material + legal-effect roll,
 * derived-id encode.  Every victory drops exactly one card -- quality
 * (material/effect) is what varies.
 *
 * Runs from ROM bank 3 via the WRAM trampoline.  Self-contained: reads
 * the battle id from g_bk_byte_a, steps g_loot_rng_state inline (never
 * a fixed-bank call), and reports through g_loot_id.
 *
 * The profile table is game content and lives here with the body; the
 * fixed-bank cost of drops is one tiny wrapper in src/game/content.c.
 */

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

    /* Roll material + legal effect for the weapon (weighted wood-heavy;
     * legal non-plain effects per §34.2: daggers poison, swords burn or
     * chill, everything else stays plain).  Demo scope: wild rings come
     * in wood only (the merchant's ring is the metal one) -- the stream
     * still steps above so later draws keep their historical positions. */
    g_loot_rng_state ^= g_loot_rng_state << 7;
    g_loot_rng_state ^= g_loot_rng_state >> 9;
    g_loot_rng_state ^= g_loot_rng_state << 8;
    {
        uint8_t mat_roll = (uint8_t)g_loot_rng_state;
        uint8_t eff_roll = (uint8_t)(g_loot_rng_state >> 8);
        uint8_t material = (weapon == WPN_RING) ? MAT_WOOD :
                           (mat_roll < 128) ? MAT_WOOD :
                           (mat_roll < 192) ? MAT_BRONZE :
                           (mat_roll < 224) ? MAT_IRON : MAT_MYTHRIL;
        uint8_t effect = EFF_PLAIN;

        /* Plain-heavy (§34.5): only 25% of rolls carry a rider. */
        if (eff_roll < 64) {
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
