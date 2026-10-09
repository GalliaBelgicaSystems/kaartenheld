#pragma bank 2

#include "rpg/cards.h"
#include "rpg/status.h"
#include "battle/card.h"
#include "game_ids.h"

/* ── Card catalog (game content, banked ROM) ────────────────────── */
const CardDefinition g_cards[] = {
    { CARD_IRON_SWORD,    CARD_TYPE_ATTACK,  3, 1,  0, 4, CARD_EFFECT_DAMAGE_TARGET, BATTLE_CARD_TYPE_SWORD,    10, STATUS_NONE,       0,   "I SW" },
    { CARD_WOODEN_SHIELD, CARD_TYPE_DEFENSE, 2, 1,  0, 3, CARD_EFFECT_BLOCK_DAMAGE,  BATTLE_CARD_TYPE_SHIELD,    0, STATUS_NONE,       0,   "W SH" },
    { CARD_WOOD_RING,      CARD_TYPE_HEAL,    5, 1,  3, 3, CARD_EFFECT_HEAL_HP,       BATTLE_CARD_TYPE_HEAL,     20, STATUS_NONE,       0,   "I RG" },
    /* Fire Sword (SW4 + fire rider): the fire element lives on a sword,
     * never a separate "tome" type (docs/loot.md §34).  Carries the BURN
     * on-hit rider exactly like a loot fire sword. */
    { CARD_FIRE_SWORD,    CARD_TYPE_ATTACK,  4, 1,  0, 3, CARD_EFFECT_DAMAGE_TARGET, BATTLE_CARD_TYPE_SWORD,    0, STATUS_BURN,     128,  "F SW" },
    /* Poison Dagger (DA1): 1 physical damage; base 50% poison chance
     * scaled by the played hand's multiplier (docs/combo-system.md §13). */
    { CARD_POISON_DAGGER, CARD_TYPE_ATTACK,  1, 1,  0, 3, CARD_EFFECT_DAMAGE_TARGET, BATTLE_CARD_TYPE_DAGGER,    0, STATUS_POISON,   128,  "P DA" },
    { CARD_AMULET,        CARD_TYPE_SPECIAL, 0, 0,  0, 1, CARD_EFFECT_NONE,          BATTLE_CARD_TYPE_SWORD,     0, STATUS_NONE,       0,   "AMULET"  },
    /* Mythril bow (no element): heavy hitter sold by the Merchant (shop 2). */
    { CARD_BOW_9,         CARD_TYPE_ATTACK, 9,  2,  2, 1,  CARD_EFFECT_DAMAGE_TARGET, BATTLE_CARD_TYPE_BOW,      30, STATUS_NONE,       0,   "M BO"  }
};

/* Keep the register-time count in game_ids.h in sync with this table. */
typedef char card_table_count_ok[
    sizeof(g_cards) / sizeof(g_cards[0]) == GAME_CARD_COUNT ? 1 : -1
];
