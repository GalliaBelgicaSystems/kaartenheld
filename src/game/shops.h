#ifndef GAME_SHOPS_H
#define GAME_SHOPS_H

#include <stdint.h>
#include "rpg/cards.h"

#define SHOP_MAX_ITEMS 50
/* Rows of a shop's stock visible at once; the list scrolls when a shop
 * stocks more (up to SHOP_MAX_ITEMS).  The cursor is kept in this window
 * by the shared bank-2 cursor body. */
#define SHOP_VISIBLE 10

/* A per-shop stock list (game content).  id matches the WorldActorDefinition
 * shop_id of the actor that runs the shop.  Prices are read from the
 * CardDefinition.price field — single source of truth.  `buys` marks a
 * CARD MERCHANT (docs/loot.md §34.6): the SELL mode lists the player's
 * owned loot cards at their centralized sell value. */
typedef struct {
    uint8_t id;
    uint8_t count;
    uint8_t buys;
    CardId items[SHOP_MAX_ITEMS];
} ShopDefinition;

/* Generated shop table (src/game/shops_content.c): id/count/buys/items. */
extern const ShopDefinition g_shops[];
extern const uint8_t g_shop_count;

/* Look up a shop's stock by its id, or NULL if unknown. */
const ShopDefinition *game_shop_for_id(uint8_t id);

#endif /* GAME_SHOPS_H */
