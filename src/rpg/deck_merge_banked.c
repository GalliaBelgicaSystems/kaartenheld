#pragma bank 11
#pragma disable_warning 110

#include <stdint.h>
#include <stdbool.h>
#include "rpg/deck.h"
#include "rpg/cards.h"
#include "rpg/loot.h"
#include "rpg/status.h"
#include "game_ids.h"
#include "banked.h"

/* ── Banked collection merge (ROM bank 11: bank 2, the catalog's bank,
 * is full, bank 10 filled up with the fade pair plus this file's own
 * growth, and the fixed bank cannot host the compare loop) ─────────────────────────────
 * Entry point: deck_merge_banked, g_bk_ptr_a = CardState*.  Moved out
 * of the fixed bank (AGENTS.md §52.11.1/§52.18): the identity compare
 * needs real definitions, and card_get_def is a fixed-bank trampoline
 * a banked body must never call.
 *
 * Curated identities live in the table below (hand-written next to the
 * catalog it mirrors in src/game/cards_content.c, kept honest by
 * tools/card_costs.py --check-identity); loot ids are synthesized with
 * pure macros (LOOT_POWER / CARD_AP / LOOT_RING_USES, same formulas as
 * loot_synth_banked -- slugs and prices stay out, they are not identity).
 *
 * One row per card identity: a curated catalog row and its mechanically
 * identical loot twin are the same physical card.  Capped folds keep a
 * remainder row so nothing is ever lost and nothing stacks or decks
 * past max_copies (shop one-copy rows hide once the identity is owned,
 * rebuys refuse, the deck never retargets past the cap); uncapped
 * pairs fold fully into the first row.  Idempotent; runs after every
 * collection add that opens a new row and on save load. */

/* Curated identities, index = CardId - CARD_FIRST_GAME (dense catalog
 * range, see game_ids.h GAME_CARD_COUNT): type, power, cost, uses,
 * effect, battle type, rider id, rider chance, max_copies. */
static const uint8_t s_ident_catalog[GAME_CARD_COUNT][9] = {
    /* CARD_IRON_SWORD    */ { 0, 5, 2, 0, 1, 0, 0, 0, 4 },
    /* CARD_WOODEN_SHIELD */ { 1, 1, 1, 0, 2, 1, 0, 0, 4 },
    /* CARD_IRON_RING     */ { 2, 4, 2, 3, 3, 3, 0, 0, 1 },
    /* CARD_FIRE_SWORD    */ { 0, 3, 2, 0, 1, 0, 2, 128, 3 },
    /* CARD_POISON_DAGGER */ { 0, 2, 2, 0, 1, 4, 1, 128, 3 },
    /* CARD_AMULET        */ { 5, 0, 0, 0, 0, 0, 0, 0, 1 },
    /* CARD_BOW_9         */ { 0, 9, 5, 2, 1, 2, 0, 0, 1 },
    /* CARD_WOOD_SWORD    */ { 0, 1, 1, 0, 1, 0, 0, 0, 4 },
    /* CARD_STEEL_SHIELD  */ { 1, 6, 3, 0, 2, 1, 0, 0, 2 },
    /* CARD_BONE          */ { 5, 1, 0, 0, 0, 0, 0, 0, 1 },
    /* CARD_OLD_KEY       */ { 5, 2, 0, 0, 0, 0, 0, 0, 1 },
};

/* (type, battle type, effect) per loot weapon, index 0..4
 * (sword/shield/bow/dagger/ring; reserved ids ride the ring row).
 * Flat tables (not [5][3]): a stride-3 index needs the 8-bit multiply
 * library, whose helpers are RAM-resident and unreachable while a bank
 * is mapped (AGENTS.md §52.1/52.11). */
static const uint8_t s_ident_type[5] = { 0, 1, 0, 0, 2 };
static const uint8_t s_ident_battle[5] = { 0, 1, 2, 4, 3 };
static const uint8_t s_ident_fx[5] = { 1, 2, 1, 1, 3 };
static const uint8_t s_ident_chance[4] = { 0, 128, 128, 96 };

/* Rider legality as a macro (not a call): deck_ident_cmp sits on the
 * loot-grant stack chain, and a separate frame there costs worst-case
 * depth (AGENTS.md §52.24).  Effect id doubles as the status id. */
#define DECK_RIDER_OK(fx, wpn) \
    ((uint8_t)(((fx) == EFF_POISON) ? ((wpn) == WPN_DAGGER) : \
               (((fx) == EFF_FIRE || (fx) == EFF_ICE) ? ((wpn) == WPN_SWORD) : 0)))

/* Loot identity synthesis as a macro (not a call): deck_ident_cmp sits
 * on the loot-grant stack chain, and a separate frame there costs
 * worst-case depth (AGENTS.md §52.24).  Same pure macros as
 * loot_synth_banked (LOOT_POWER / CARD_AP / LOOT_RING_USES). */
#define DECK_LOOT_IDENT(lid, out)                                          \
    do {                                                                   \
        uint8_t _m = loot_id_material(lid);                                \
        uint8_t _e = loot_id_effect(lid);                                  \
        uint8_t _w = loot_id_weapon(lid);                                  \
        uint8_t _wi = ((_w) <= WPN_RING) ? (_w) : WPN_SWORD;                \
        uint8_t _wr = ((_w) <= WPN_RING) ? (_w) : WPN_RING;                 \
        uint8_t _hr = ((_m) >= MAT_BRONZE && (_e) != EFF_PLAIN &&           \
                       DECK_RIDER_OK((_e), (_w)));                          \
        (out)[0] = s_ident_type[_wr];                                      \
        (out)[1] = LOOT_POWER((_m), (_wi));                                 \
        (out)[3] = ((_wr) == 4) ? LOOT_RING_USES(_m) : 0;                   \
        (out)[4] = s_ident_fx[_wr];                                        \
        (out)[5] = s_ident_battle[_wr];                                    \
        if (_hr) {                                                         \
            (out)[6] = (_e);                                               \
            (out)[7] = s_ident_chance[_e];                                 \
        } else {                                                           \
            (out)[6] = STATUS_NONE;                                        \
            (out)[7] = 0;                                                  \
        }                                                                  \
        (out)[2] = ((_w) <= WPN_RING) ?                                     \
            CARD_AP((out)[1], (_m), (_hr)) : 0;                             \
    } while (0)

/* Resolve-and-compare identity check.  Returns 1 when a and b are
 * the same physical card (identical type/power/cost/uses/effect/
 * battle type/rider; id, max_copies, price and name are NOT identity),
 * with *cap_out the pair's cap (0 = uncapped).  The cap always rides
 * side A: a curated A row carries its table cap, a loot A resolves its
 * curated twin's cap through the inline catalog scan below (the cap
 * travels with the identity, so loot-vs-loot pairs of a capped card
 * stay capped) -- and any matching B shares the identity, hence the
 * cap, so no second lookup is needed.  Catalog identities must be
 * unique rows for this to hold (tools/card_costs.py --check-identity
 * asserts it).  Synthesis and scan are inline (macros + unrolled
 * loops), never calls: this compare sits on the loot-grant stack chain
 * (AGENTS.md §52.24). */
static uint8_t deck_ident_cmp(CardId a, CardId b, uint8_t *cap_out)
{
    uint8_t ab[9];
    uint8_t bb[8];
    uint8_t f;
    if (loot_is_loot_id(a)) {
        uint8_t k;
        DECK_LOOT_IDENT(a, ab);
        /* The cap travels with the identity, not the row: scan the
         * curated table for the same 8 bytes (a loot twin of a capped
         * card is capped even with no curated row in the collection).
         * Inlined (not a call): this compare sits on the loot-grant
         * stack chain (AGENTS.md §52.24). */
        ab[8] = 0;
        for (k = 0; k < GAME_CARD_COUNT; k++) {
            for (f = 0; f < 8; f++) {
                if (s_ident_catalog[k][f] != ab[f]) break;
            }
            if (f == 8) {
                ab[8] = s_ident_catalog[k][8];
                break;
            }
        }
    } else {
        uint8_t k;
        if (a < CARD_FIRST_GAME) return 0;
        k = (uint8_t)(a - CARD_FIRST_GAME);
        if (k >= GAME_CARD_COUNT) return 0;
        for (f = 0; f < 9; f++) ab[f] = s_ident_catalog[k][f];
    }
    if (loot_is_loot_id(b)) {
        DECK_LOOT_IDENT(b, bb);
    } else {
        uint8_t k;
        if (b < CARD_FIRST_GAME) return 0;
        k = (uint8_t)(b - CARD_FIRST_GAME);
        if (k >= GAME_CARD_COUNT) return 0;
        for (f = 0; f < 8; f++) bb[f] = s_ident_catalog[k][f];
    }
    for (f = 0; f < 8; f++) {
        if (ab[f] != bb[f]) return 0;
    }
    *cap_out = ab[8];
    return 1;
}

static uint8_t deck_in_deck(const DeckState *d, CardId id)
{
    uint8_t i;
    uint8_t n = 0;
    if (!d) return 0;
    for (i = 0; i < d->count; i++) {
        if (d->cards[i] == id) n++;
    }
    return n;
}

/* Decked copies follow their card to the surviving id, capped: at most
 * (cap - already decked) move when cap > 0, so the deck never holds
 * more capped copies than max_copies allows. */
static void deck_retarget_capped(DeckState *d, CardId from, CardId to,
                                 uint8_t cap)
{
    uint8_t i;
    uint8_t have = 0;
    if (!d || from == to) return;
    if (cap > 0) {
        have = deck_in_deck(d, to);
        if (have >= cap) return;
    }
    for (i = 0; i < d->count; i++) {
        if (d->cards[i] == from && (cap == 0 || have < cap)) {
            d->cards[i] = to;
            have++;
        }
    }
}

/* Capped twin gate for deck_add_card (same bank, plain call from the
 * fixed-bank wrapper): g_bk_ptr_a = CardState*, g_bk_byte_a = candidate
 * id; g_bk_byte_c = 1 when decking keeps every identity at-or-under its
 * cap, 0 when the candidate's identity is already saturated (tighter of
 * the pair's max_copies; loot twins resolve their curated cap through
 * the catalog) or the id is unknown.  deck_add_card's fixed-side def
 * check already rejects unknown ids; this just never waves garbage
 * through. */
void deck_ident_gate_banked(void)
{
    CardState *cs = (CardState *)g_bk_ptr_a;
    CardId id = g_bk_byte_a;
    uint8_t cap = 0;
    uint8_t total = 0;
    uint8_t i;
    if (!cs || !deck_ident_cmp(id, id, &cap)) {
        g_bk_byte_c = 0;
        return;
    }
    for (i = 0; i < cs->deck.count; i++) {
        uint8_t c = 0;
        if (!deck_ident_cmp(id, cs->deck.cards[i], &c)) continue;
        total++;
        if (c > 0 && (cap == 0 || c < cap)) cap = c;
    }
    g_bk_byte_c = (uint8_t)(cap == 0 || total < cap);
}

/* Full-collection absorb (same bank): g_bk_ptr_a = CardState*,
 * g_bk_byte_a = candidate id, g_bk_byte_b = quantity; g_bk_byte_c = 1
 * when the candidate folded into an existing same-identity row (atomic:
 * all-or-nothing), 0 when no row matches or a capped fold would need a
 * remainder row that does not fit.  Dispatched by deck_collection_add's
 * full-collection path -- a drop that matches an owned identity must
 * never vanish silently just because the 12-row shelf is full. */
void deck_absorb_banked(void)
{
    CardState *cs = (CardState *)g_bk_ptr_a;
    CardId id = g_bk_byte_a;
    uint8_t qty = g_bk_byte_b;
    uint8_t i;
    g_bk_byte_c = 0;
    if (!cs || qty == 0) return;
    for (i = 0; i < cs->collection.count; i++) {
        uint8_t cap = 0;
        uint8_t total;
        if (!deck_ident_cmp(cs->collection.entries[i].id, id, &cap))
            continue;
        total = (uint8_t)(cs->collection.entries[i].count + qty);
        if (total < cs->collection.entries[i].count) total = 255;
        /* Capped overflow needs a remainder row: with no free row the
         * whole grant is refused (the caller keeps collection unchanged
         * and reports failure) rather than stacking past the cap. */
        if (cap > 0 && total > cap) return;
        cs->collection.entries[i].count = total;
        deck_retarget_capped(&cs->deck, id,
                             cs->collection.entries[i].id, cap);
        g_bk_byte_c = 1;
        return;
    }
}

/* Identity total (same bank): g_bk_ptr_a = CardState*, g_bk_byte_a = id;
 * g_bk_byte_b = owned copies across every same-identity row (0 for
 * unknown ids).  Dispatched by the loot-grant path for truthful
 * LOOT_CARD_ADDED counts after folds move copies onto curated ids. */
void deck_ident_total_banked(void)
{
    CardState *cs = (CardState *)g_bk_ptr_a;
    CardId id = g_bk_byte_a;
    uint8_t total = 0;
    uint8_t i;
    if (!cs) {
        g_bk_byte_b = 0;
        return;
    }
    for (i = 0; i < cs->collection.count; i++) {
        uint8_t cap = 0;
        if (!deck_ident_cmp(cs->collection.entries[i].id, id, &cap))
            continue;
        total = (uint8_t)(total + cs->collection.entries[i].count);
        if (total < cs->collection.entries[i].count) total = 255;
    }
    g_bk_byte_b = total;
}

/* Legacy-deck eviction helper: after a capped fold, the deck may still hold
 * more copies of the folded identity than cap (twins decked before the gate
 * existed, or stacked identically-capped rows).  Unreachable through current
 * gameplay (the gate refuses the over-cap add and the retarget above moves
 * at most up to the cap), so this only fires for pre-fix states -- and then
 * it must not strand the deck below DECK_MIN_CARDS.  Counts via the tiny
 * per-id counter (no identity scan: only the folded pair can be over cap).
 * Silent: the state was never legal, and the repair is idempotent. */
static void deck_evict_overcap(DeckState *d, CardId keep_id, CardId drop_id,
                               uint8_t cap)
{
    uint8_t have;
    uint8_t n;
    if (!d || cap == 0) return;
    have = (uint8_t)(deck_in_deck(d, keep_id) + deck_in_deck(d, drop_id));
    while (have > cap && d->count > DECK_MIN_CARDS) {
        uint8_t victim = 0xFF;
        for (n = 0; n < d->count; n++) {
            if (d->cards[n] == drop_id) {
                victim = n;
                break;
            }
        }
        if (victim == 0xFF) {
            for (n = 0; n < d->count; n++) {
                if (d->cards[n] == keep_id) {
                    victim = n;
                    break;
                }
            }
        }
        if (victim == 0xFF) return;
        d->count--;
        d->cards[victim] = d->cards[d->count];
        have--;
    }
}

void deck_merge_banked(void)
{
    CardState *cs = (CardState *)g_bk_ptr_a;
    uint8_t i;
    uint8_t j;

    if (!cs) return;
    for (i = 0; i < cs->collection.count; i++) {
        CardId keep_id = cs->collection.entries[i].id;
        uint8_t keep_count = cs->collection.entries[i].count;
        j = (uint8_t)(i + 1);
        while (j < cs->collection.count) {
            CardId drop_id = cs->collection.entries[j].id;
            uint8_t drop_count = cs->collection.entries[j].count;
            uint8_t cap = 0;
            uint8_t total;
            if (!deck_ident_cmp(keep_id, drop_id, &cap)) {
                j++;
                continue;
            }
            /* Prefer the capped id as the survivor (loot twins are never
             * capped, so a nonzero cap with a loot keep_id means the drop
             * row is the curated twin). */
            if (cap > 0 && loot_is_loot_id(keep_id) &&
                !loot_is_loot_id(drop_id)) {
                keep_id = drop_id;
                drop_id = cs->collection.entries[i].id;
                cs->collection.entries[i].id = keep_id;
            }
            /* Saturating 8-bit add (no 16-bit library needed in this
             * banked body): counts wrap only past 255 copies of one
             * card, where saturating keeps the cap comparison sound. */
            total = (uint8_t)(keep_count + drop_count);
            if (total < keep_count) total = 255;
            if (cap > 0 && total > cap) {
                /* Capped fold: keep the cap, leave the excess under the
                 * other id -- nothing lost, nothing over cap.  The
                 * remainder row must carry the drop id: in the swap path
                 * entries[j] still names the curated id, and without the
                 * rename both rows would carry it (loot remainder lost,
                 * merge never converges). */
                keep_count = cap;
                cs->collection.entries[i].count = cap;
                drop_count = (uint8_t)(total - cap);
                cs->collection.entries[j].id = drop_id;
                cs->collection.entries[j].count = drop_count;
                deck_retarget_capped(&cs->deck, drop_id, keep_id, cap);
                deck_evict_overcap(&cs->deck, keep_id, drop_id, cap);
                j++;
            } else {
                keep_count = total;
                cs->collection.entries[i].count = total;
                deck_retarget_capped(&cs->deck, drop_id, keep_id, cap);
                cs->collection.count--;
                cs->collection.entries[j].id =
                    cs->collection.entries[cs->collection.count].id;
                cs->collection.entries[j].count =
                    cs->collection.entries[cs->collection.count].count;
            }
        }
    }
}
