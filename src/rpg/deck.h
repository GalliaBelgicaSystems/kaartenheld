#ifndef RPG_DECK_H
#define RPG_DECK_H

#include <stdint.h>
#include <stdbool.h>
#include "rpg/cards.h"

/* Deck and collection sizes.
 * MAX_DECK_CARDS = 20 matches the target deck size from deck.md.
 * GameState is ~202 bytes with these limits, well within the 252-byte
 * save slot limit. */
#define MAX_CARD_COLLECTION 12
#define MAX_DECK_CARDS      20
/* Minimum playable deck size: must cover a full opening hand
 * (BATTLE_HAND_SIZE in battle/battle.h).  deck_remove_card refuses to
 * go below this so battles never rely on phantom fallback cards. */
#define DECK_MIN_CARDS      5

/* A single owned card in the collection. */
typedef struct {
    CardId id;
    uint8_t count;      /* copies owned */
} CardCollectionEntry;

/* The player's permanent card collection. */
typedef struct {
    CardCollectionEntry entries[MAX_CARD_COLLECTION];
    uint8_t count;
} CardCollectionState;

/* The player's current battle deck (subset of the collection). */
typedef struct {
    CardId cards[MAX_DECK_CARDS];
    uint8_t count;
} DeckState;

/* Combined card state, replacing InventoryState + EquipmentState. */
typedef struct {
    CardCollectionState collection;
    DeckState deck;
} CardState;

/* ── Collection API ─────────────────────────────────────────────── */

/* Add cards to the collection.  Stacks with existing entries.
 * Returns false if the collection is full. */
bool deck_collection_add(CardState *cs, CardId id, uint8_t quantity);

/* Remove cards from the collection.  Returns false if the player
 * does not own enough copies. */
bool deck_collection_remove(CardState *cs, CardId id, uint8_t quantity);

/* How many copies of a card the player owns. */
uint8_t deck_collection_count(const CardState *cs, CardId id);

/* Fold collection rows that describe the same card into one (a curated
 * catalog row and its mechanically identical loot twin), retargeting
 * decked copies onto the survivor.  Idempotent; deck_collection_add runs
 * it after opening a new row and save load runs it on the restored
 * state. */
void deck_collection_merge_duplicates(CardState *cs);

/* ── Deck API ───────────────────────────────────────────────────── */

/* Add a card to the deck.  The card must be owned in the collection.
 * Returns false if the deck is full or the card is not owned. */
bool deck_add_card(CardState *cs, CardId id);

/* Banked equip-gate check (src/game/loot_gate_banked.c, ROM bank 9):
 * g_bk_byte_a = card id, g_bk_byte_b = pool; g_bk_byte_c = 0 proceed,
 * else required AP.  Dispatched by deck_equip_card's thin wrapper. */
void deck_equip_gate_banked(void);

/* Gated equip (docs/enemies_ranking.md §4.8): the deck_add_card rules
 * plus one -- a loot-derived card (loot_is_loot_id) whose synthesized
 * cost exceeds the live energy pool cannot be decked.  Curated cards
 * are exempt (the starter deck carries cost-2 fire swords while the pool
 * starts at 1; shop/quest cards are authored against their pool), and
 * deck_add_card stays ungated for those authored grant paths.  Only
 * player-chosen equipping (quick-screen A, harness deck add) is gated. */
typedef enum {
    EQUIP_OK = 0,       /* decked */
    EQUIP_FULL = 1,     /* deck already holds MAX_DECK_CARDS */
    EQUIP_MIN = 2,      /* reserved: remove-path floor (never from equip) */
    EQUIP_LOCKED = 3,   /* loot card costs more AP than the pool */
    EQUIP_REJECTED = 4  /* any other deck_add_card refusal */
} DeckEquipResult;

uint8_t deck_equip_card(CardState *cs, CardId id, uint8_t pool);

/* Remove a card from the deck.  Returns false if the card is not
 * in the deck or the deck is at DECK_MIN_CARDS. */
bool deck_remove_card(CardState *cs, CardId id);

/* How many copies of a specific card are in the deck. */
uint8_t deck_count_in_deck(const DeckState *d, CardId id);


#endif /* RPG_DECK_H */
