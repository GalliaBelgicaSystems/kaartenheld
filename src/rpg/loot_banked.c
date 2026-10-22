#pragma bank 3

#include "rpg/loot.h"
#include "rpg/status.h"
#include "banked.h"
#include "battle/card.h"
#include "rpg/cards.h"

/* ── Loot identity tables + banked bodies (docs/loot.md §34) ────────
 * Runs from ROM bank 3 via the trampoline.  Entry point:
 *   loot_synth_banked       id(byte_a) -> CardDefinition in g_card_scratch
 * Consumes/writes only WRAM and its own bank-local tables.
 * Extensible: append rows to grow materials/effects/weapons. */

#define STATUS_NONE    0
#define STATUS_POISON  1

static const struct {
    char code[4];          /* 1-letter material initial (§34.1) */
} s_materials[LOOT_NMATERIALS] = {
    { "W" }, { "B" }, { "I" }, { "S" }, { "M" }
};

/* Effect rows: on-hit status rider applied by battle (docs/combo-system.md
 * §13 pipeline; legality per weapon in docs/loot.md §34.2).  chance is in
 * 1/255 units, tuned like the catalog's DA1 (128 ~= 50%). */
static const struct {
    uint8_t status_id;
    uint8_t chance;        /* on-hit threshold, 1/255 units */
    char code[4];          /* 1-letter effect initial (§34.1) */
} s_effects[LOOT_NEFFECTS] = {
    { STATUS_NONE,     0,   ""  },   /* plain   */
    { STATUS_POISON,   128, "P" },   /* daggers */
    { STATUS_BURN,     128, "F" },   /* swords  */
    { STATUS_FREEZE,   96,  "I" }    /* swords  */
};

static const struct {
    uint8_t cost;
    char symbol[3];        /* 2-letter weapon code (§34.1) */
} s_weapons[LOOT_NWEAPONS] = {
    { 1, "SW" },   /* sword */
    { 1, "SH" },   /* shield */
    { 1, "BO" },   /* bow */
    { 1, "DA" },   /* dagger */
    { 1, "RG" },   /* ring (heal amount = power) */
    { 0, "??" },   /* reserved */
    { 0, "??" },   /* reserved */
    { 0, "??" }    /* reserved */
};

/* Build the compact identity name "[material][ effect] weapon" into the
 * scratch def (§34.1): e.g. "W SW", "M P DA", "W F SW".  Max 7 chars +
 * NUL -- fits CardDefinition.name[12].  `has_rider` false suppresses
 * the effect infix (illegal pairs carry no rider to name). */
static void synth_name(uint8_t material, uint8_t effect, uint8_t weapon,
                       uint8_t has_rider)
{
    char *n = g_card_scratch.name;
    const char *m = s_materials[material].code;
    const char *w = s_weapons[weapon].symbol;

    while (*m) *n++ = *m++;
    *n++ = ' ';
    if (has_rider) {
        const char *e = s_effects[effect].code;
        while (*e) *n++ = *e++;
        *n++ = ' ';
    }
    while (*w) *n++ = *w++;
    *n = '\0';
}

/* §34.2 effect legality per weapon: poison is dagger-only, fire/ice
 * are sword-only.  The generator only rolls legal pairs, but synth is
 * the last line of defense -- direct encoders (debug tooling) can
 * construct illegal ids, and an illegal pair must carry NO rider. */
static uint8_t rider_legal(uint8_t effect, uint8_t weapon)
{
    if (effect == EFF_POISON) return weapon == WPN_DAGGER;
    if (effect == EFF_FIRE || effect == EFF_ICE) return weapon == WPN_SWORD;
    return 0;
}
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

    while (n--) *dst++ = 0;   /* zero-fill, then set fields */

    g_card_scratch.id = id;
    g_card_scratch.type = CARD_TYPE_ATTACK;
    g_card_scratch.power = LOOT_POWER(material, wi);
    g_card_scratch.cost = s_weapons[weapon].cost;
    /* Centralized sell value (docs/loot.md §16/§34.6): synthesized
     * power + material tier*2 (+2 poison rider).  Stored in the def's
     * price field so every consumer (shop SELL mode) reads it like any
     * other card price -- no separate formula anywhere else in the game. */
    g_card_scratch.price = (uint8_t)(g_card_scratch.power +
                                     ((uint8_t)material << 1) +
                                     ((effect == EFF_POISON &&
                                       weapon == WPN_DAGGER) ? 2 : 0));
    g_card_scratch.uses_per_battle = 0;          /* unlimited */
    g_card_scratch.max_copies = 0;               /* unlimited duplicates */

    switch (weapon) {
        case WPN_SWORD:
            g_card_scratch.battle_type = BATTLE_CARD_TYPE_SWORD;
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
            /* Demo scope: wild rings are single-use per battle (the
             * merchant's iron ring carries 4). */
            g_card_scratch.uses_per_battle = 1;
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
                 s_effects[effect].status_id != STATUS_NONE &&
                 rider_legal(effect, weapon));
    if (has_rider) {
        g_card_scratch.status_id = s_effects[effect].status_id;
        g_card_scratch.status_chance = s_effects[effect].chance;
    }

    synth_name(material, effect, weapon, has_rider);
}
