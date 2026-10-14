#ifndef CARD_H
#define CARD_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    BATTLE_CARD_TYPE_SWORD = 0,  /* SW: Physical attack (fire/ice = riders) */
    BATTLE_CARD_TYPE_SHIELD = 1, /* SH: Defense / block */
    BATTLE_CARD_TYPE_BOW = 2,    /* BO: Ranged physical attack */
    BATTLE_CARD_TYPE_HEAL = 3,   /* HE: Restore HP (rings) */
    BATTLE_CARD_TYPE_DAGGER = 4  /* DA: 1 dmg, poison rider (POISON DAGGER) */
} BattleCardType;

/* Sentinel for an empty hand slot (played cards stay empty until the turn-
 * start draw refills them).  Must not collide with any BattleCardType; kept
 * as a #define because SDCC enums are signed 8-bit (0x80+ would wrap). */
#define BATTLE_CARD_TYPE_EMPTY 0xFF

typedef struct {
    uint8_t type;           /* BattleCardType */
    uint8_t value;          /* Number 1 - 9 */
    uint8_t uses_remaining; /* 0 = depleted; 0xFF = unlimited */
    uint8_t cost;           /* Energy cost to play (paid from Battle.energy) */
    /* What playing this card DOES (CardEffectType, rpg/cards.h).  Copied
     * from CardDefinition.effect when the battle deck is built from the
     * persistent collection; the definition owns the effect, combat only
     * consumes it (docs/combo-system.md §3-4). */
    uint8_t effect;
    /* On-hit status rider, copied from CardDefinition (Phase C).
     * status_chance in 1/255 units; 0/0 = no rider. */
    uint8_t status_id;
    uint8_t status_chance;
    /* Loot RING marker (docs/loot.md §34.3): joker for both hand
     * phases -- offense skips its value in the damage sum (the ring
     * heals its power instead), defense counts it as shield value;
     * classification substitutes its value freely for the best tier.
     * MAX ONE per selection (battle-side gate). */
    uint8_t ring;
} Card;

/* Packed description table geometry for the bank-2 blob
 * (card_content.c, read directly by item_content_banked.c).
 * Stride = longest text + NUL, padded. */
#define CARD_DESC_STRIDE 18
#define CARD_DESC_TYPES  5

#endif /* CARD_H */
