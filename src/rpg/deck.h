#ifndef RPG_DECK_H
#define RPG_DECK_H

#include <stdint.h>
#include <stdbool.h>
#include "rpg/cards.h"

/* Deck and collection sizes.
 * MAX_DECK_CARDS = 20 matches the target deck size from deck.md.
 * GameState is ~202 bytes with these limits, well within the 252-byte
 * save slot limit. */
/* 50-row shelf (was 12): loot ids span 160 combinations and every
 * distinct drop opens a row, so 12 silently merged or refused drops
 * (docs/loot.md §34).  Costs +76 B WRAM in GameState plus the wider
 * snapshot/descriptor wire sections (telemetry.h); SRAM slots still
 * fit (save.h). */
#define MAX_CARD_COLLECTION 50
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
 * decked copies onto the survivor.  Capped folds keep a remainder row so
 * nothing is lost or stacked past max_copies.  Idempotent;
 * deck_collection_add runs it after opening a new row and save load runs
 * it on the restored state. */
/* Banked duplicate-fold (src/rpg/deck_merge_banked.c, ROM bank 11):
 * g_bk_ptr_a = CardState*.  Dispatched by deck_collection_merge_duplicates'
 * thin wrapper (this file) and never called directly. */
void deck_merge_banked(void);

/* Banked capped-twin gate (same file): g_bk_ptr_a = CardState*,
 * g_bk_byte_a = candidate id, g_bk_byte_d = live pool (0 = ungated
 * grant path); g_bk_byte_c = 1 proceed, 0 capped, 2 pool-locked (cost in
 * g_bk_byte_b).  Dispatched by deck_add_card's thin wrapper below. */
void deck_ident_gate_banked(void);

/* Banked equip verdict (same file): g_bk_ptr_a = CardState*,
 * g_bk_byte_a = candidate id, g_bk_byte_d = live pool;
 * g_bk_byte_c = 1 proceed / 2 pool-locked (cost in g_bk_byte_b) /
 * 3 deck full / 4 per-card refusal / 5 fully-decked row clears /
 * 6 fully-decked row refused by the MIN floor.  5/6 answer before the
 * gate: clearing only shrinks the deck.  Dispatched by deck_equip_card. */
void deck_equip_verdict_banked(void);

/* Banked full-collection absorb (same file): g_bk_ptr_a = CardState*,
 * g_bk_byte_a = candidate id, g_bk_byte_b = quantity; g_bk_byte_c = 1
 * when the candidate folded into an existing same-identity row
 * (all-or-nothing), 0 otherwise.  Dispatched by deck_collection_add's
 * full-collection path. */
void deck_absorb_banked(void);

/* Banked identity total (same file): g_bk_ptr_a = CardState*,
 * g_bk_byte_a = id; g_bk_byte_b = owned copies across every
 * same-identity row.  Dispatched by the loot-grant path for truthful
 * LOOT_CARD_ADDED counts after folds. */
void deck_ident_total_banked(void);

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
 * plus one -- no card (curated or loot) whose cost exceeds the live
 * energy pool can be decked.  Pre-decked grants are unaffected (the
 * starter deck, shop/quest rewards arrive via the ungated
 * deck_add_card); only player-chosen equipping (quick-screen A,
 * harness deck add) is gated. */ typedef enum {
     EQUIP_OK = 0,       /* decked */
     EQUIP_FULL = 1,     /* deck already holds MAX_DECK_CARDS */
     EQUIP_MIN = 2,      /* fully-decked row refused by the MIN floor */
     EQUIP_LOCKED = 3,   /* card costs more AP than the pool */
     EQUIP_REJECTED = 4, /* any other deck_add_card refusal */
     EQUIP_CLEAR = 5     /* fully-decked row: caller clears it (MIN holds) */
 } DeckEquipResult;

uint8_t deck_equip_card(CardState *cs, CardId id, uint8_t pool);

/* Remove a card from the deck.  Returns false if the card is not
 * in the deck or the deck is at DECK_MIN_CARDS. */
bool deck_remove_card(CardState *cs, CardId id);

/* How many copies of a specific card are in the deck. */
uint8_t deck_count_in_deck(const DeckState *d, CardId id);


#endif /* RPG_DECK_H */
