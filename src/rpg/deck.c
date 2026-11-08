#include "rpg/deck.h"
#include "rpg/cards.h"
#include "rpg/loot.h"
#include "telemetry.h"
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
 * each row.
 *
 * The merge itself lives in the banked body deck_merge_banked (ROM bank
 * 11 -- bank 2, the catalog's bank, is full): the identity compare needs
 * real definitions, and card_get_def is a fixed-bank trampoline a banked
 * body must never call (AGENTS.md §52.11.1), so the banked body reads a
 * hand-written identity table (parity-gated by card-costs
 * --check-identity) and synthesizes loot identities with pure macros.
 * Capped folds keep a remainder row and capped deck retargeting, so
 * nothing is ever lost and nothing stacks or decks past max_copies;
 * uncapped pairs fold fully.  It runs after every collection add that
 * opens a new row and on save load (an old save can already hold the
 * duplicate) and is idempotent. */

/* Banked merge body (src/rpg/deck_merge_banked.c, ROM bank 11):
 * g_bk_ptr_a = CardState*.  Dispatched by the wrapper below. */
void deck_merge_banked(void);

void deck_collection_merge_duplicates(CardState *cs)
{
    if (!cs) return;
    g_bk_call_bank = 11;
    g_bk_call_target = (uint16_t)&deck_merge_banked;
    g_bk_ptr_a = (void *)cs;
    banked_call_run();
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
        /* Stacking onto an existing row can complete a capped pair
         * (e.g. capped row below cap + remainder row): always re-merge,
         * a same-id stack never creates a new row so this cannot fail. */
        deck_collection_merge_duplicates(cs);
        return true;
    }

    /* Full shelf: the merge may still absorb the candidate into an
     * existing same-identity row (all-or-nothing, no new row needed),
     * so a folding drop never vanishes silently on a full collection. */
    if (cs->collection.count >= MAX_CARD_COLLECTION) {
        deck_collection_merge_duplicates(cs);
        if (cs->collection.count >= MAX_CARD_COLLECTION) {
            g_bk_call_bank = 11;
            g_bk_call_target = (uint16_t)&deck_absorb_banked;
            g_bk_ptr_a = (void *)cs;
            g_bk_byte_a = id;
            g_bk_byte_b = quantity;
            banked_call_run();
            return g_bk_byte_c ? true : false;
        }
    }
    if (def && def->max_copies > 0 && quantity > def->max_copies) return false;
    e = &cs->collection.entries[cs->collection.count];
    e->id = id;
    e->count = quantity;
    cs->collection.count++;
    /* A curated row and its loot twin are one card to the player.  The
     * merge folds duplicates, keeping a capped remainder row when the
     * pair would otherwise stack past max_copies -- so the DECK menu
     * can list the same card twice (curated row + loot twin row), each
     * row honestly showing its own owned/decked counts, while caps and
     * the identity-wide deck gate keep every copy legal. */
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

/* Identity signature: the 8 bytes that make a curated row and its loot
 * twin the same physical card (id, max_copies, price and name are NOT
 * identity).  Resolved through the banked gate below (bank 10, with the
 * curated table): card_get_def reuses its scratch and the fixed bank has
 * no room for a per-card scan (AGENTS.md §52.18). */
void deck_ident_gate_banked(void);

bool deck_add_card(CardState *cs, CardId id)
{
    const CardDefinition *def;
    uint8_t in_deck;
    uint8_t owned;
    if (!cs || id == CARD_NONE) return false;
    if (cs->deck.count >= MAX_DECK_CARDS) return false;
    owned = deck_collection_count(cs, id);
    if (owned == 0) return false;

    def = card_get_def(id);
    if (!def) return false;
    if (def->type == CARD_TYPE_SPECIAL) return false;
    in_deck = deck_count_in_deck(&cs->deck, id);
    if (def->max_copies > 0 && in_deck >= def->max_copies) return false;
    if (in_deck >= owned) return false;

    /* Cross-row cap (docs/loot.md §34): a capped curated row and its
     * loot twin are one card in two collection rows after a capped
     * fold, so the per-id checks above cannot see the pair's total.
     * The banked gate counts decked copies of the same identity and
     * enforces the tighter cap -- otherwise the twin decks past it. */
    g_bk_call_bank = 11;
    g_bk_call_target = (uint16_t)&deck_ident_gate_banked;
    g_bk_ptr_a = (void *)cs;
    g_bk_byte_a = id;
    banked_call_run();
    if (!g_bk_byte_c) return false;

    cs->deck.cards[cs->deck.count] = id;
    cs->deck.count++;
    return true;
}

uint8_t deck_equip_card(CardState *cs, CardId id, uint8_t pool)
{
    if (!cs || id == CARD_NONE) return EQUIP_REJECTED;
    /* The whole verdict runs banked (bank 11 hosts the identity
     * tables; the fixed bank sits against 0x8000 and cannot host the
     * scans).  g_bk_byte_c answers 1 proceed / 2 pool-locked (cost in
     * b) / 3 deck full / anything else per-card refusal.  A proceed
     * verdict still applies through deck_add_card, so there is one
     * append site; the verdict and the apply re-check the same pure
     * state, so they cannot disagree. */
    g_bk_call_bank = 11;
    g_bk_call_target = (uint16_t)&deck_equip_verdict_banked;
    g_bk_ptr_a = (void *)cs;
    g_bk_byte_a = id;
    g_bk_byte_d = pool;
    banked_call_run();
    if (g_bk_byte_c == 2) {
        /* Locked (docs/enemies_ranking.md §4.8): the rejection is
         * assertable; no CARD_ADDED_TO_DECK fires with it.  In release
         * telemetry_emit compiles out (telemetry.h). */
        telemetry_emit(EVENT_CARD_EQUIP_LOCKED, id, g_bk_byte_b, 0, 0);
        return EQUIP_LOCKED;
    }
    if (g_bk_byte_c == 3) return EQUIP_FULL;
    if (g_bk_byte_c == 6) return EQUIP_MIN;
    if (g_bk_byte_c == 5) return EQUIP_CLEAR;
    if (g_bk_byte_c == 1 && deck_add_card(cs, id)) return EQUIP_OK;
    return EQUIP_REJECTED;
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


