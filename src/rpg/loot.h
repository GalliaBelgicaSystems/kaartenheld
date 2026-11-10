#ifndef RPG_LOOT_H
#define RPG_LOOT_H

#include <stdint.h>
#include <stdbool.h>
#include "rpg/cards.h"

/* ── Procedural loot: material x effect x weapon (docs/loot.md §34) ──
 * Every loot combination maps to a derived CardId; card_get_def()
 * synthesizes its definition from the tables in loot_banked.c.  The
 * existing count-based collection/deck hold derived ids like any other
 * card. */

/* 0x60..0xFF: 5 materials x 4 effects x 8 weapons = 160 ids.  The base
 * is 32-aligned and its bits 3-4 are clear, so effect/weapon masks work
 * directly on the id while material subtracts the base. */
#define LOOT_ID_BASE 0x60

/* Axis counts — id-decode uses shifts/masks because banked code
 * cannot call the SDCC div/mod library (AGENTS.md 52.11.1).
 * Bit layout (8 bits, 160 IDs fitting 0x60..0xFF):
 *   bits 5-7: material (5 values, 32 ids each)
 *   bits 3-4: effect   (4 values)
 *   bits 0-2: weapon   (8 values)  */
#define LOOT_NMATERIALS 5          /* WOOD BRONZE IRON STEEL MYTHRIL */
#define LOOT_NEFFECTS   4          /* PLAIN POISON FIRE ICE (+ reserved) */
#define LOOT_NWEAPONS   8          /* SWORD SHIELD BOW DAGGER RING + rsv */

enum {
    MAT_WOOD = 0, MAT_BRONZE, MAT_IRON, MAT_STEEL, MAT_MYTHRIL
};

/* AP (VARIABLE_ID_ENERGY_POOL) gates which material tiers can drop.
 * Steel and above only appear at AP 5+; the weighted roll keeps them
 * rare even then.  docs/loot.md §34.5.
 * Mythril riders cost 6 (docs/card-ap-cost-formula.md §6.1): the
 * generator withholds them until the pool reaches 6. */
#define LOOT_AP_BRONZE 2
#define LOOT_AP_IRON   4
#define LOOT_AP_STEEL  5
#define LOOT_AP_MYTHRIL_RIDER 6

enum {
    EFF_PLAIN = 0, EFF_POISON, EFF_FIRE, EFF_ICE
};

enum {
    WPN_SWORD = 0, WPN_SHIELD, WPN_BOW, WPN_DAGGER, WPN_RING
};

/* Material power bands (docs/loot.md §34.1).  One shared definition so
 * the synth, battle-init and display paths cannot drift: sword/bow take
 * the high end of a band, shield/dagger/ring the low end.
 * Wood 1, bronze 2-3, iron 4-5, steel 6-7, mythril 8-9.
 * Closed form (no compare chains: `mat` is a runtime value, so nested
 * ternaries compile to branch ladders in every banked copy): base is
 * `mat ? mat<<1 : 1`, sword/bow take one more on non-wood. */
#define LOOT_POWER(mat, wpn) ((uint8_t)(((mat) == 0) ? 1 : \
    (uint8_t)(((uint8_t)(mat) << 1) + ((((wpn) == WPN_SWORD) || ((wpn) == WPN_BOW)) ? 1 : 0))))

/* Ring uses per battle (docs/loot.md §34.6): wood 1, bronze 2, iron 3,
 * steel 4, mythril 5 -- material tier + 1.  A wood ring stays
 * single-use per battle (a free repeatable heal would trivialize
 * attrition); higher tiers hold more charges instead of healing more
 * per charge.  The curated shop ring follows the same band as loot
 * iron (3 uses). */
#define LOOT_RING_USES(mat) ((uint8_t)((mat) + 1))

/* ── Card AP cost (docs/card-ap-cost-formula.md) ──
 * One AP step = 8 value points: power 1, material tier 4, mythril
 * premium 12 (plain mythril always costs 5), rider 8.  Never clamped at
 * runtime: the largest legal input is 9 + 4·4 + 12 + 8 = 45 -> 6.
 * CARD_AP_MAX is the specification ceiling -- make card-costs asserts
 * every table-built combo against it -- not a clamp; it has no C
 * call sites by design.  Pure shifts/compare, no div/mod (AGENTS.md
 * §52.18); safe in every banked copy. */
#define CARD_AP_MATERIAL_PTS 4
#define CARD_AP_RIDER_PTS    8
#define CARD_AP_MYTHRIL_PTS  12
#define CARD_AP_MAX          6

#define CARD_AP_VALUE(power, material, rider)                    \
    ((uint8_t)((power) + ((material) << 2) + ((rider) << 3) +    \
               (((material) == MAT_MYTHRIL) ? CARD_AP_MYTHRIL_PTS : 0)))

#define CARD_AP(power, material, rider)                          \
    ((uint8_t)((CARD_AP_VALUE(power, material, rider) + 7) >> 3))

/* Bow uses per battle (docs/loot.md §34.6): AP-derived, inverse to power
 * (wood 6 .. mythril 2, floored at 2).  The _FROM_AP form reads the
 * already-computed AP cost byte instead of re-expanding CARD_AP --
 * bank-2 call sites stage cost first and the bank-2 budget is tight
 * (overflow corrupts bank 3, AGENTS.md §52.18).  Both forms agree by
 * construction; card_costs.py asserts the ladder either way. */
#define LOOT_BOW_USES_FROM_AP(ap) \
    ((uint8_t)(((ap) >= 6) ? 2 : (uint8_t)(7 - (ap))))
#define LOOT_BOW_USES(power, material, rider) \
    (LOOT_BOW_USES_FROM_AP(CARD_AP(power, material, rider)))

/* Derived-id helpers: pure arithmetic, safe as macros (no code emitted). */
#define loot_is_loot_id(id)          ((id) >= LOOT_ID_BASE)
#define loot_id_material(id)         ((uint8_t)(((id) - LOOT_ID_BASE) >> 5))
#define loot_id_effect(id)           (((id) >> 3) & 3)
#define loot_id_weapon(id)           ((id) & 7)

/* Shared WRAM results (defined in src/rpg/loot.c):
 * g_loot_id       -- derived-id result byte written by the banked
 *                    drop/synth bodies, read by fixed-bank wrappers.
 * g_loot_rng_state -- isolated xorshift stream (seeded from the main
 *                    seed by rng_set_seed); banked bodies step it
 *                    inline -- a banked body must never call a
 *                    fixed-bank function (AGENTS.md 52.11.1). */
extern uint8_t g_loot_id;
extern uint16_t g_loot_rng_state;

/* Sell value: synthesized loot defs carry it in CardDefinition.price
 * (set by loot_synth_banked -- the centralized formula,
 * docs/loot.md §16/§34.6); read via card_get_def() like any price.
 * Catalog (non-loot) prices come from the game's card table. */

/* Banked bodies:
 * game_loot_drop_banked   -- always-drop + profile pick + roll
 *                            (src/game/loot_drop_banked.c, bank 9),
 *                            dispatched by game_loot_drop() in
 *                            src/game/content.c.
 * game_loot_first_victory_banked -- first-real-victory bronze shield
 *                            override (src/game/loot_first_banked.c,
 *                            bank 11: bank 9 is full), second leg of
 *                            game_loot_drop().
 * loot_synth_banked       -- fills g_card_scratch for a derived id
 *                            (src/rpg/loot_banked.c, bank 3), dispatched
 *                            by card_get_def(). */
void game_loot_drop_banked(void);
void loot_synth_banked(void);

#endif /* RPG_LOOT_H */
