#include "rpg/deck.h"
#include "rpg/cards.h"
#include "rpg/loot.h"
#include "banked.h"
#include <stddef.h>

/* ── Collection internals ───────────────────────────────────────── */

static CardCollectionEntry *collection_find(CardCollectionState *col,
                                            CardId id)
{
    uint8_t i;
    for (i = 0; i < col->count; i++) {
        if (col->entries[i].id == id) {
            return &col->entries[i];
        }
    }
    return NULL;
}

/* ── One collection row per card ──────────────────────────────────────
 * A curated catalog row and a loot-derived id (docs/loot.md §34) can be
 * the same physical card: identical type/power/cost/uses/effect/battle
 * type/rider, differing only in name, price and caps.  Two rows made one
 * card look like two in the DECK menu and let the deck take a copy from
 * each row.  The collection therefore holds ONE row per identity.
 *
 * The merge folds duplicates, preferring the unlimited id (loot twins
 * carry max_copies 0, so the merged stack is not capped by the curated
 * row) and retargeting decked copies onto the survivor.  It runs after
 * every collection add that opens a new row and on save load (an old
 * save can already hold the duplicate) and is idempotent.
 *
 * Never hold a CardDefinition pointer across these calls: card_get_def
 * reuses g_card_scratch. */

/* The identity bytes inside CardDefinition: type..uses (1..4), effect +
 * battle type (6..7) and the rider (9..10).  id, max_copies, price and
 * name are NOT identity -- a curated row and its loot twin differ
 * exactly there.  Layout is asserted below, so a field reorder is a
 * compile error rather than a silent mis-merge. */
static const int g_card_identity_layout_ok[
    (offsetof(CardDefinition, type) == 1 &&
     offsetof(CardDefinition, uses_per_battle) == 4 &&
     offsetof(CardDefinition, effect) == 6 &&
     offsetof(CardDefinition, battle_type) == 7 &&
     offsetof(CardDefinition, status_id) == 9 &&
     offsetof(CardDefinition, status_chance) == 10) ? 1 : -1];

static const uint8_t s_card_identity_bytes[] = {
    1, 2, 3, 4,      /* type, power, cost, uses_per_battle   */
    6, 7,            /* effect, battle type                  */
    9, 10            /* status id, status chance (the rider) */
};

static bool cards_same_identity(CardId a, CardId b)
{
    CardDefinition x;
    const CardDefinition *d;
    uint8_t i;

    if (a == b) return true;
    d = card_get_def(a);
    if (!d) return false;
    x = *d;                 /* second lookup overwrites the scratch */
    d = card_get_def(b);
    if (!d) return false;
    for (i = 0; i < sizeof(s_card_identity_bytes); i++) {
        if (((const uint8_t *)&x)[s_card_identity_bytes[i]] !=
            ((const uint8_t *)d)[s_card_identity_bytes[i]])
            return false;
    }
    return true;
}

/* Decked copies follow their card to the surviving id. */
static void deck_retarget(DeckState *d, CardId from, CardId to)
{
    uint8_t i;

    if (from == to) return;
    for (i = 0; i < d->count; i++) {
        if (d->cards[i] == from) d->cards[i] = to;
    }
}

void deck_collection_merge_duplicates(CardState *cs)
{
    CardCollectionEntry *keep;
    CardCollectionEntry *drop;
    CardId from;
    CardId to;
    uint8_t i;
    uint8_t j;

    if (!cs) return;
    for (i = 0; i < cs->collection.count; i++) {
        keep = &cs->collection.entries[i];
        j = (uint8_t)(i + 1);
        while (j < cs->collection.count) {
            drop = &cs->collection.entries[j];
            if (!cards_same_identity(keep->id, drop->id)) {
                j++;
                continue;
            }
            /* Same card: fold `drop` into `keep`, keeping the unlimited
             * id (a loot twin) when the pair has one. */
            from = drop->id;
            to = keep->id;
            if (loot_is_loot_id(from) && !loot_is_loot_id(to)) {
                from = to;
                to = drop->id;
                keep->id = to;
            }
            deck_retarget(&cs->deck, from, to);
            keep->count = (uint8_t)(keep->count + drop->count);
            /* Swap the last row in; re-examine this slot (j unchanged). */
            *drop = cs->collection.entries[--cs->collection.count];
        }
    }
}

/* ── Collection API ─────────────────────────────────────────────── */

bool deck_collection_add(CardState *cs, CardId id, uint8_t quantity)
{
    const CardDefinition *def;
    CardCollectionEntry *e;
    if (!cs || id == CARD_NONE || quantity == 0) return false;

    def = card_get_def(id);

    e = collection_find(&cs->collection, id);
    if (e) {
        if (def && def->max_copies > 0 &&
            (uint8_t)(e->count + quantity) > def->max_copies)
            return false;
        e->count = (uint8_t)(e->count + quantity);
        return true;
    }

    if (cs->collection.count >= MAX_CARD_COLLECTION) return false;
    if (def && def->max_copies > 0 && quantity > def->max_copies) return false;
    e = &cs->collection.entries[cs->collection.count];
    e->id = id;
    e->count = quantity;
    cs->collection.count++;
    /* A curated row and its loot twin are one card to the player: fold
     * them so the DECK menu never lists the same card twice. */
    deck_collection_merge_duplicates(cs);
    return true;
}

bool deck_collection_remove(CardState *cs, CardId id, uint8_t quantity)
{
    CardCollectionEntry *e;
    if (!cs || id == CARD_NONE || quantity == 0) return false;

    e = collection_find(&cs->collection, id);
    if (!e || e->count < quantity) return false;

    if (e->count == quantity) {
        *e = cs->collection.entries[--cs->collection.count];
    } else {
        e->count = (uint8_t)(e->count - quantity);
    }
    return true;
}

uint8_t deck_collection_count(const CardState *cs, CardId id)
{
    CardCollectionEntry *e;
    if (!cs) return 0;
    e = collection_find((CardCollectionState *)&cs->collection, id);
    return e ? e->count : 0;
}

/* ── Deck internals ─────────────────────────────────────────────── */

uint8_t deck_count_in_deck(const DeckState *d, CardId id)
{
    uint8_t i;
    uint8_t n = 0;
    if (!d) return 0;
    for (i = 0; i < d->count; i++) {
        if (d->cards[i] == id) n++;
    }
    return n;
}

/* ── Deck API ───────────────────────────────────────────────────── */

bool deck_add_card(CardState *cs, CardId id)
{
    const CardDefinition *def;
    uint8_t in_deck;
    if (!cs || id == CARD_NONE) return false;
    if (cs->deck.count >= MAX_DECK_CARDS) return false;
    if (deck_collection_count(cs, id) == 0) return false;

    def = card_get_def(id);
    if (!def) return false;
    if (def->type == CARD_TYPE_SPECIAL) return false;
    in_deck = deck_count_in_deck(&cs->deck, id);
    if (def->max_copies > 0 && in_deck >= def->max_copies) return false;
    if (in_deck >= deck_collection_count(cs, id)) return false;

    cs->deck.cards[cs->deck.count] = id;
    cs->deck.count++;
    return true;
}

bool deck_remove_card(CardState *cs, CardId id)
{
    uint8_t i;
    if (!cs || id == CARD_NONE) return false;
    if (cs->deck.count <= DECK_MIN_CARDS) return false;

    for (i = 0; i < cs->deck.count; i++) {
        if (cs->deck.cards[i] == id) {
            cs->deck.cards[i] = cs->deck.cards[--cs->deck.count];
            return true;
        }
    }
    return false;
}


