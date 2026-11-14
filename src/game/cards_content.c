#pragma bank 2

#include "rpg/cards.h"
#include "rpg/status.h"
#include "rpg/loot.h"
#include "battle/card.h"
#include "game_ids.h"

/* ── Card catalog (game content, banked ROM) ──────────────────────
 * Material bands (docs/loot.md §34.1): wood 1, bronze 2-3, iron 4-5,
 * steel 6-7, mythril 8-9.  Sword/bow take the high end of a band,
 * shield/dagger/ring the low end.  Curated cards follow the same bands
 * so a material name always implies the same damage/block/heal range.
 * AP costs are derived, never typed (docs/card-ap-cost-formula.md):
 * CARD_AP(power, MAT_x, rider) folds to a constant at compile time. */
const CardDefinition g_cards[] = {
    { CARD_IRON_SWORD,    CARD_TYPE_ATTACK,  5, CARD_AP(5, MAT_IRON, 0),  0, 4, CARD_EFFECT_DAMAGE_TARGET, BATTLE_CARD_TYPE_SWORD,    10, 0, STATUS_NONE,       0,   "I SW" },
    { CARD_WOODEN_SHIELD, CARD_TYPE_DEFENSE, 1, CARD_AP(1, MAT_WOOD, 0),  0, 4, CARD_EFFECT_BLOCK_DAMAGE,  BATTLE_CARD_TYPE_SHIELD,    0, 0, STATUS_NONE,       0,   "W SH" },
    { CARD_IRON_RING,      CARD_TYPE_HEAL,    4, CARD_AP(4, MAT_IRON, 0),  3, 1, CARD_EFFECT_HEAL_HP,       BATTLE_CARD_TYPE_HEAL,     20, 0, STATUS_NONE,       0,   "I RG" },
    /* Bronze sword (plain, no rider -- swords never ride): the honest
     * mid-tier blade filling the wood->iron gap.  Plain bronze is 1 AP,
     * so the two starter copies are live at pool 1 (the old pool-2 fire
     * beat is gone with the riders). */
    { CARD_BRONZE_SWORD,   CARD_TYPE_ATTACK,  3, CARD_AP(3, MAT_BRONZE, 0),  0, 3, CARD_EFFECT_DAMAGE_TARGET, BATTLE_CARD_TYPE_SWORD,    0, 0, STATUS_NONE,       0,   "B SW" },
    /* Poison Dagger (bronze DA2): base 208/255 poison chance scaled
     * by the played hand's multiplier (docs/combo-system.md §13). */
    { CARD_POISON_DAGGER, CARD_TYPE_ATTACK,  2, CARD_AP(2, MAT_BRONZE, 1),  0, 3, CARD_EFFECT_DAMAGE_TARGET, BATTLE_CARD_TYPE_DAGGER,    8, 0, STATUS_POISON,   208,  "P DA" },
    { CARD_SHOPKEYS,        CARD_TYPE_SPECIAL, 0, 0,  0, 1, CARD_EFFECT_NONE,          BATTLE_CARD_TYPE_SWORD,     0, 0, STATUS_NONE,       0,   "SHOP KEYS"  },
    /* Mythril bow (no element): same dagger-ladder band as every loot
     * bow (mythril 7, on par with the mythril dagger, well under the
     * mythril sword's 9), sold by the Merchant (shop 2).  Arrows
     * AP-derived like every loot bow (LOOT_BOW_USES: 7 - 5 AP = 2,
     * written as the macro so a future AP retune moves the shop bow
     * too).  Kept the CARD_BOW_9 id: the name is stable API
     * (scenarios/host maps name it). */
    { CARD_BOW_9,         CARD_TYPE_ATTACK, 7,  CARD_AP(7, MAT_MYTHRIL, 0),  LOOT_BOW_USES(7, MAT_MYTHRIL, 0), 1,  CARD_EFFECT_DAMAGE_TARGET, BATTLE_CARD_TYPE_BOW,      30, 0, STATUS_NONE,       0,   "M BO"  },
    /* Wood sword: the tier-1 starter blade (hero.json).  Plain; wood can
     * never carry a rider. */
    { CARD_WOOD_SWORD,    CARD_TYPE_ATTACK,  1, CARD_AP(1, MAT_WOOD, 0),  0, 4, CARD_EFFECT_DAMAGE_TARGET, BATTLE_CARD_TYPE_SWORD,     0, 0, STATUS_NONE,       0,   "W SW" },
    /* Steel shield (power 6, AP 3, 12 g): the merchant-stocked answer to
     * the Lord of Slimes' flat-6 swing (docs/enemies_ranking.md §5.2/§8.4
     * decided) -- blocks it alone, where the old two-SH2 answer leaves 2
     * through.  Curated steel following the same band as loot steel. */
    { CARD_STEEL_SHIELD,  CARD_TYPE_DEFENSE, 6, CARD_AP(6, MAT_STEEL, 0), 0, 2, CARD_EFFECT_BLOCK_DAMAGE,  BATTLE_CARD_TYPE_SHIELD,   12, 0, STATUS_NONE,       0,   "S SH" },
    /* Quest bone (tunnel skeleton -> village dog trade): SPECIAL like
     * the shop keys -- listed in the DECK tab, never deckable, never
     * sold (sell is loot-only).  Sword battle type reuses the
     * shop-keys' icon path (no new art).  Power 1/2 (vs the keys' 0)
     * keeps the three SPECIAL identities distinct: the collection
     * merge folds identical rows, so equal bytes would collapse the
     * bone, the key, and the shop keys into one row.  Power is inert
     * here (SPECIAL cards never enter a deck or a battle). */
    { CARD_BONE,          CARD_TYPE_SPECIAL, 1, 0,  0, 1, CARD_EFFECT_NONE,          BATTLE_CARD_TYPE_SWORD,     0, 0, STATUS_NONE,       0,   "BONE"  },
    /* Quest key (the dog's field-cache key): same SPECIAL treatment.
     * Held as a card so the trade, the locked chest, and the snapshot
     * all ride the existing item-count machinery. */
    { CARD_OLD_KEY,       CARD_TYPE_SPECIAL, 2, 0,  0, 1, CARD_EFFECT_NONE,          BATTLE_CARD_TYPE_SWORD,     0, 0, STATUS_NONE,       0,   "OLD KEY"  }
};

/* Keep the register-time count in game_ids.h in sync with this table. */
typedef char card_table_count_ok[
    sizeof(g_cards) / sizeof(g_cards[0]) == GAME_CARD_COUNT ? 1 : -1
];
