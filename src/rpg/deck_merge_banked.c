#pragma bank 10
#pragma disable_warning 110

#include <stdint.h>
#include <stdbool.h>
#include "rpg/deck.h"
#include "rpg/cards.h"
#include "rpg/loot.h"
#include "rpg/status.h"
#include "game_ids.h"
#include "banked.h"

/* ── Banked collection merge (ROM bank 10: bank 2, the catalog's bank,
 * is full, and the fixed bank cannot host the compare loop) ───────
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

/* Rider legality as a macro (not a call): deck_ident_of sits on the
 * loot-grant stack chain, and a separate frame there costs worst-case
 * depth (AGENTS.md §52.24).  Effect id doubles as the status id. */
#define DECK_RIDER_OK(fx, wpn) \
    ((uint8_t)(((fx) == EFF_POISON) ? ((wpn) == WPN_DAGGER) : \
               (((fx) == EFF_FIRE || (fx) == EFF_ICE) ? ((wpn) == WPN_SWORD) : 0)))

/* Resolve-then-stream identity compare.  Returns 1 when a and b are
 * the same physical card (identical type/power/cost/uses/effect/
 * battle type/rider; id, max_copies, price and name are NOT identity),
 * with *cap_out the tighter of the two caps (0 = uncapped pair).
 * Returns 0 for unknown ids or different cards.
 *
 * Only side A is ever materialized (ab[9]: 8 identity bytes + cap):
 * side B streams field-by-field against it, so the caller keeps no
 * buffers -- the merge loop above stays lean on the loot-grant stack
 * chain (AGENTS.md §52.24).  Loot identities use the same pure macros
 * as loot_synth_banked (LOOT_POWER / CARD_AP / LOOT_RING_USES). */
static uint8_t deck_ident_cmp(CardId a, CardId b, uint8_t *cap_out)
{
    uint8_t ab[9];
    uint8_t f;
    uint8_t cap_b = 0;
    if (loot_is_loot_id(a)) {
        uint8_t material = loot_id_material(a);
        uint8_t effect = loot_id_effect(a);
        uint8_t weapon = loot_id_weapon(a);
        uint8_t wi = (weapon <= WPN_RING) ? weapon : WPN_SWORD;
        uint8_t wr = (weapon <= WPN_RING) ? weapon : WPN_RING;
        uint8_t has_rider = (material >= MAT_BRONZE &&
                             effect != EFF_PLAIN &&
                             DECK_RIDER_OK(effect, weapon));
        ab[0] = s_ident_type[wr];
        ab[1] = LOOT_POWER(material, wi);
        ab[3] = (wr == 4) ? LOOT_RING_USES(material) : 0;
        ab[4] = s_ident_fx[wr];
        ab[5] = s_ident_battle[wr];
        if (has_rider) {
            ab[6] = effect;
            ab[7] = s_ident_chance[effect];
        } else {
            ab[6] = STATUS_NONE;
            ab[7] = 0;
        }
        ab[2] = (weapon <= WPN_RING) ?
            CARD_AP(ab[1], material, has_rider) : 0;
        ab[8] = 0;
    } else {
        uint8_t k;
        if (a < CARD_FIRST_GAME) return 0;
        k = (uint8_t)(a - CARD_FIRST_GAME);
        if (k >= GAME_CARD_COUNT) return 0;
        for (f = 0; f < 9; f++) ab[f] = s_ident_catalog[k][f];
    }
    if (loot_is_loot_id(b)) {
        uint8_t material = loot_id_material(b);
        uint8_t effect = loot_id_effect(b);
        uint8_t weapon = loot_id_weapon(b);
        uint8_t wi = (weapon <= WPN_RING) ? weapon : WPN_SWORD;
        uint8_t wr = (weapon <= WPN_RING) ? weapon : WPN_RING;
        uint8_t has_rider = (material >= MAT_BRONZE &&
                             effect != EFF_PLAIN &&
                             DECK_RIDER_OK(effect, weapon));
        uint8_t cost = (weapon <= WPN_RING) ?
            CARD_AP(LOOT_POWER(material, wi), material, has_rider) : 0;
        if (s_ident_type[wr] != ab[0]) return 0;
        if (LOOT_POWER(material, wi) != ab[1]) return 0;
        if (cost != ab[2]) return 0;
        if (((wr == 4) ? LOOT_RING_USES(material) : 0) != ab[3]) return 0;
        if (s_ident_fx[wr] != ab[4]) return 0;
        if (s_ident_battle[wr] != ab[5]) return 0;
        if ((has_rider ? effect : STATUS_NONE) != ab[6]) return 0;
        if ((has_rider ? s_ident_chance[effect] : 0) != ab[7]) return 0;
    } else {
        uint8_t k;
        if (b < CARD_FIRST_GAME) return 0;
        k = (uint8_t)(b - CARD_FIRST_GAME);
        if (k >= GAME_CARD_COUNT) return 0;
        for (f = 0; f < 8; f++) {
            if (s_ident_catalog[k][f] != ab[f]) return 0;
        }
        cap_b = s_ident_catalog[k][8];
    }
    {
        uint8_t cap = ab[8];
        if (cap_b > 0 && (cap == 0 || cap_b < cap)) cap = cap_b;
        *cap_out = cap;
    }
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
 * the pair's max_copies; loot twins are uncapped) or the id is unknown.
 * deck_add_card's fixed-side def check already rejects unknown ids; this
 * just never waves garbage through. */
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
                 * other id -- nothing lost, nothing over cap. */
                keep_count = cap;
                cs->collection.entries[i].count = cap;
                drop_count = (uint8_t)(total - cap);
                cs->collection.entries[j].count = drop_count;
                deck_retarget_capped(&cs->deck, drop_id, keep_id, cap);
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
