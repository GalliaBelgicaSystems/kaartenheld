#pragma bank 3

#include "combo.h"
#include "rpg/effects.h"
#include "banked.h"

/* ── Banked combo evaluation + effect dispatch ──────────────────────
 * The fixed-bank wrapper (src/battle/combo.c) stages its arguments into
 * the _DATA globals (banked.c) and runs this no-arg body through the WRAM
 * banked-call trampoline (crt0.s).  This file stays in ROM bank 3 so combo
 * evaluation does not consume the fixed-bank budget.  Apart from the
 * bank-local call into effects_content.c below (same ROM bank, direct
 * call -- allowed), it is self-contained: no fixed-bank calls.
 *
 * Architectural rule (docs/combo-system.md §5): the combo evaluator answers
 * "what hand did these cards form" -- tier, multiplier, base_power.
 * It must never compute an effect magnitude; scaling base_power into
 * damage/heal/block happens in effect_resolve_into() at the end of this
 * dispatch, whose result battle reads via effect_last().
 *
 * Hand model: strict poker.  Pairs/kinds any count >= 2; STRAIGHT, FLUSH,
 * STRAIGHT_FLUSH and FIVE_KIND need all five effective cards.  Values are
 * classified order-independently (histogram + min/max), fixing the old
 * selection-order quirk.  Suited bonus: +25 percent when every effective
 * card shares one symbol and a tier was made.
 *
 * No multiplies/divides here or in effect_resolve_into(): SDCC lowers
 * them to __mulint/__divuint, which link into the FIXED bank -- an
 * illegal call while bank 3 is mapped (AGENTS.md 52.11.1). */

static const uint16_t s_tier_mult[HAND_TIER_COUNT] = {
    100, /* NONE */
    120, /* PAIR */
    150, /* TWO PAIR */
    180, /* THREE KIND */
    210, /* STRAIGHT */
    240, /* FLUSH */
    260, /* FULL HOUSE */
    280, /* FOUR KIND */
    350, /* STRAIGHT FLUSH */
    400  /* FIVE KIND */
};

/* Classify n card values (order-independent).  vals are 1..10,
 * types are BattleCardType. */
uint8_t combo_classify(const uint8_t *vals, const uint8_t *types,
                       uint8_t n)
{
    uint8_t hist[10]; /* values are 1..10 */
    uint8_t i, distinct, min, max, pairs, trips;
    uint8_t quads = 0, fives = 0;
    uint8_t all_same_type = 1;

    for (i = 0; i < 10; i++) hist[i] = 0;

    min = 10;
    max = 0;
    for (i = 0; i < n; i++) {
        uint8_t v = vals[i];
        if (v < 1 || v > 10) return HAND_NONE;
        hist[v - 1]++;
        if (v < min) min = v;
        if (v > max) max = v;
        if (i > 0 && types[i] != types[0]) all_same_type = 0;
    }

    /* FIVE KIND: all five share one value (beats everything). */
    if (n == 5 && hist[min - 1] == 5) return HAND_FIVE_KIND;

    /* Sequential with no duplicates: straight / straight flush (5 only). */
    if ((uint8_t)(max - min) == (uint8_t)(n - 1)) {
        distinct = 0;
        for (i = 0; i < 10; i++) {
            if (hist[i] != 0) distinct++;
        }
        if (distinct == n && n == 5) {
            return all_same_type ? HAND_STRAIGHT_FLUSH : HAND_STRAIGHT;
        }
    }

    /* Flush: all five same symbol, not sequential (checked above). */
    if (n == 5 && all_same_type) return HAND_FLUSH;

    pairs = 0;
    trips = 0;
    for (i = 0; i < 10; i++) {
        if (hist[i] >= 5) fives++;
        else if (hist[i] == 4) quads++;
        else if (hist[i] == 3) trips++;
        else if (hist[i] == 2) pairs++;
    }
    if (fives != 0) return HAND_FIVE_KIND;
    if (quads != 0) return HAND_FOUR_KIND;
    if (trips != 0 && pairs != 0) return HAND_FULL_HOUSE;
    if (trips != 0) return HAND_THREE_KIND;
    if (pairs >= 2) return HAND_TWO_PAIR;
    if (pairs == 1) return HAND_PAIR;
    return HAND_NONE;
}

/* Decode-range guards (mirror of combo.c's pack guards): fail the build
 * if the packed fields ever outgrow the staging byte. */
static const int g_combo_unpack_phase_ok[(COMBO_PHASE_DEFEND <= 1) ? 1 : 0];
static const int g_combo_unpack_effect_ok[(CARD_EFFECT_HEAL_HP <= 127) ? 1 : 0];

void combo_resolve_banked(void)
{
    const Card *cards = (const Card *)g_bk_ptr_a;
    uint8_t count = g_bk_byte_a;
    uint8_t phase = COMBO_UNPACK_PHASE(g_bk_byte_b);
    uint8_t effect = COMBO_UNPACK_EFFECT(g_bk_byte_b);
    ComboResult *out_result = (ComboResult *)g_bk_ptr_b;
    uint8_t i, eff_count = 0;
    uint8_t sum = 0;
    uint16_t mult;

    if (!out_result) return;

    if (!cards || count == 0) {
        out_result->count = 0;
        out_result->eff_count = 0;
        out_result->tier = HAND_NONE;
        out_result->suited = 0;
        out_result->multiplier = 100;
        out_result->base_power = 0;
        g_effect_last.type = effect;
        g_effect_last.amount = 0;
        return;
    }

    if (count > 5) count = 5;
    out_result->count = count;

    /* Phase rules (docs/loot.md §34.3/§34.4): attack sums every
     * non-SHIELD, non-RING value (rings deal 0 attack -- they heal
     * instead); defend sums SHIELD values AND rings (a ring is a
     * wild-card shield worth its power).  All selected cards enter the
     * hand for classification either way. */
    {
        uint8_t vals[5];
        uint8_t types[5];
        uint8_t w = 0;

        for (i = 0; i < count; i++) {
            uint8_t t = cards[i].type;
            if (phase == COMBO_PHASE_ATTACK) {
                if (t != BATTLE_CARD_TYPE_SHIELD && !cards[i].ring)
                    sum += cards[i].value;
            } else {
                if (t != BATTLE_CARD_TYPE_SHIELD && !cards[i].ring) continue;
                sum += cards[i].value;
            }
            vals[w] = cards[i].value;
            types[w] = t;
            w++;
        }
        eff_count = w;

        out_result->base_power = sum;
        out_result->eff_count = eff_count;

        if (eff_count < 2) {
            out_result->tier = HAND_NONE;
            out_result->suited = 0;
            out_result->multiplier = 100;
        } else {
            /* Ring JOKER (§34.3): a ring's value substitutes freely --
             * try every legal value (1..10) and keep the best tier (types are
             * untouched, so the suited bonus is unaffected). */
            uint8_t has_ring = 0;
            for (i = 0; i < eff_count; i++) {
                if (cards[i].ring) has_ring = 1;
            }
            if (has_ring) {
                uint8_t trial[5];
                uint8_t best = combo_classify(vals, types, eff_count);
                uint8_t v, k2, t2;
                /* Card values are 1..10 (BOW 10): the joker tries the
                 * full legal range. */
                for (v = 1; v <= 10; v++) {
                    for (k2 = 0; k2 < eff_count; k2++) {
                        trial[k2] = cards[k2].ring ? v : vals[k2];
                    }
                    t2 = combo_classify(trial, types, eff_count);
                    if (t2 > best) best = t2;
                }
                out_result->tier = best;
            } else {
                out_result->tier = combo_classify(vals, types, eff_count);
            }
            mult = s_tier_mult[out_result->tier];
            if (out_result->tier == HAND_NONE) {
                out_result->suited = 0;
            } else {
                uint8_t same = 1;
                for (i = 1; i < eff_count; i++) {
                    if (types[i] != types[0]) { same = 0; break; }
                }
                out_result->suited = same ? 1 : 0;
                if (same) mult += 25;
            }
            out_result->multiplier = mult;
        }
    }

    /* Effect resolution consumes the evaluated hand (bank-local call). */
    effect_resolve_into(effect, out_result, &g_effect_last);
}
