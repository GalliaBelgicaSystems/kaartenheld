#pragma bank 8

#include "rpg/loot.h"
#include "rpg/status.h"
#include "banked.h"
#include "battle/card.h"
#include "rpg/cards.h"

/* ── Loot identity tables + banked bodies (docs/loot.md §34) ────────
 * Runs from ROM bank 8 via the trampoline (moved out of bank 3 to fund
 * the combo short-straight work; the overflow bank has room).  Entry point:
 *   loot_synth_banked       id(byte_a) -> CardDefinition in g_card_scratch
 * Consumes/writes only WRAM and its own bank-local tables.
 * Extensible: append rows to grow materials/effects/weapons. */

static const char s_materials[LOOT_NMATERIALS] = {
    'W', 'B', 'I', 'S', 'M'
};

/* Effect rows: on-hit status rider applied by battle (docs/combo-system.md
 * §13 pipeline; legality per weapon in docs/loot.md §34.2).  Effect index
 * (0..3) equals StatusId (STATUS_NONE, STATUS_POISON, STATUS_BURN,
 * STATUS_FREEZE); chance is in 1/255 units. */
static const uint8_t s_effect_chance[LOOT_NEFFECTS] = {
    0, 128, 128, 96
};

static const char s_effect_code[LOOT_NEFFECTS] = {
    0, 'P', 'F', 'I'
};

static const char s_weapons[LOOT_NWEAPONS][2] = {
    { 'S', 'W' },   /* sword */
    { 'S', 'H' },   /* shield */
    { 'B', 'O' },   /* bow */
    { 'D', 'A' },   /* dagger */
    { 'R', 'G' },   /* ring (heal amount = power) */
    { '?', '?' },   /* reserved */
    { '?', '?' },   /* reserved */
    { '?', '?' }    /* reserved */
};

/* Build the compact identity name "[material][ effect] weapon" into the
 * scratch def (§34.1): e.g. "W SW", "M P DA", "W F SW".  Max 6 chars +
 * NUL -- fits CardDefinition.name[12].  `has_rider` false suppresses
 * the effect infix (illegal pairs carry no rider to name). */
static void synth_name(uint8_t material, uint8_t effect, uint8_t weapon,
                       uint8_t has_rider)
{
    char *n = g_card_scratch.name;
    const char *w = s_weapons[weapon];

    *n++ = s_materials[material];
    *n++ = ' ';
    if (has_rider) {
        *n++ = s_effect_code[effect];
        *n++ = ' ';
    }
    *n++ = w[0];
    *n++ = w[1];
    *n = '\0';
}

/* §34.2 effect legality per weapon: poison is dagger-only, fire/ice
 * are bow-only (swords are the plain all-rounder, never riders).  The
 * generator only rolls legal pairs, but synth is the last line of
 * defense -- direct encoders (debug tooling) can construct illegal
 * ids, and an illegal pair must carry NO rider. */
static uint8_t rider_legal(uint8_t effect, uint8_t weapon)
{
    if (effect == EFF_POISON) return weapon == WPN_DAGGER;
    if (effect == EFF_FIRE || effect == EFF_ICE) return weapon == WPN_BOW;
    return 0;
}

/* Compile-time assertions for the zero-fill defaults (CARD_TYPE_ATTACK == 0,
 * BATTLE_CARD_TYPE_SWORD == 0) and the 1:1 EFF_* -> StatusId identity mapping
 * used by g_card_scratch.status_id = effect. */
typedef char loot_assert_card_type_attack[(CARD_TYPE_ATTACK == 0) ? 1 : -1];
typedef char loot_assert_battle_type_sword[(BATTLE_CARD_TYPE_SWORD == 0) ? 1 : -1];
typedef char loot_assert_eff_plain[(EFF_PLAIN == STATUS_NONE) ? 1 : -1];
typedef char loot_assert_eff_poison[(EFF_POISON == STATUS_POISON) ? 1 : -1];
typedef char loot_assert_eff_fire[(EFF_FIRE == STATUS_BURN) ? 1 : -1];
typedef char loot_assert_eff_ice[(EFF_ICE == STATUS_FREEZE) ? 1 : -1];

void loot_synth_banked(void)
{
    CardId id = g_bk_byte_a;
    uint8_t material = loot_id_material(id);
    uint8_t effect = loot_id_effect(id);
    uint8_t weapon = loot_id_weapon(id);
    uint8_t *dst = (uint8_t *)&g_card_scratch;
    uint8_t n = sizeof(CardDefinition);
    uint8_t wi = (weapon <= WPN_RING) ? weapon : WPN_SWORD;
    uint8_t has_rider;

    while (n--) *dst++ = 0;   /* zero-fill, then set non-zero fields */

    g_card_scratch.id = id;
    g_card_scratch.power = LOOT_POWER(material, wi);
    /* Centralized sell value (docs/loot.md §16/§34.6): synthesized
     * power + material tier*2 (+2 poison rider).  Stored in the def's
     * price field so every consumer (shop SELL tab) reads it like any
     * other card price -- no separate formula anywhere else in the game. */
    g_card_scratch.price = (uint8_t)(g_card_scratch.power +
                                     ((uint8_t)material << 1) +
                                     ((effect == EFF_POISON &&
                                       weapon == WPN_DAGGER) ? 2 : 0));

    switch (weapon) {
        case WPN_SWORD:
            g_card_scratch.effect = CARD_EFFECT_DAMAGE_TARGET;
            break;
        case WPN_SHIELD:
            g_card_scratch.type = CARD_TYPE_DEFENSE;
            g_card_scratch.battle_type = BATTLE_CARD_TYPE_SHIELD;
            g_card_scratch.effect = CARD_EFFECT_BLOCK_DAMAGE;
            break;
        case WPN_BOW:
            g_card_scratch.battle_type = BATTLE_CARD_TYPE_BOW;
            g_card_scratch.effect = CARD_EFFECT_DAMAGE_TARGET;
            break;
        case WPN_DAGGER:
            g_card_scratch.battle_type = BATTLE_CARD_TYPE_DAGGER;
            g_card_scratch.effect = CARD_EFFECT_DAMAGE_TARGET;
            break;
        default:  /* ring: heals instead of dealing damage */
            g_card_scratch.type = CARD_TYPE_HEAL;
            g_card_scratch.battle_type = BATTLE_CARD_TYPE_HEAL;
            g_card_scratch.effect = CARD_EFFECT_HEAL_HP;
            /* Rings hold material-tier uses per battle (LOOT_RING_USES):
             * wood single-use, mythril five. */
            g_card_scratch.uses_per_battle = LOOT_RING_USES(material);
            break;
    }

    /* Effect rider: attached only for LEGAL (effect, weapon) pairs per
     * §34.2.  The generator rolls legal pairs only, but synth is the
     * last line of defense -- direct encoders (debug tooling) can
     * construct e.g. SWORD+POISON, which must carry no rider and no
     * effect infix.  Written into the def so the existing battle
     * on-hit path consumes it unchanged. */
    /* Rider gate: wood carries no riders (docs/loot.md §34.1); the
     * generator never rolls one there, and synth strips hand-encoded
     * wood+rider ids too. */
    has_rider = (material >= MAT_BRONZE &&
                 effect != EFF_PLAIN &&
                 rider_legal(effect, weapon));
    if (has_rider) {
        g_card_scratch.status_id = effect;
        g_card_scratch.status_chance = s_effect_chance[effect];
    }
    /* Derived AP cost (docs/card-ap-cost-formula.md): the mythril premium
     * rides along inside the macro via `material`, and reserved weapons
     * keep cost 0 (not playable, §5.3). */
    g_card_scratch.cost = (weapon <= WPN_RING)
        ? CARD_AP(g_card_scratch.power, material, has_rider) : 0;
    /* Bow arrows per battle (docs/loot.md §34.6): AP-derived, inverse
     * to power (wood 6 .. mythril 2, floored at 2).  Staged after the
     * rider above so the uses track CARD_AP with no second ladder. */
    if (weapon == WPN_BOW)
        g_card_scratch.uses_per_battle =
            LOOT_BOW_USES_FROM_AP(g_card_scratch.cost);

    synth_name(material, effect, weapon, has_rider);
}
