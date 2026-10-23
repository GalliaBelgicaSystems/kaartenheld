#pragma bank 3

#include "combo.h"
#include "rpg/effects.h"
#include "banked.h"
#include "game.h"
#include "game/game_ids.h"

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
 * dispatch, whose result battle reads from g_effect_last directly.
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

const uint16_t g_combo_tier_mult[HAND_TIER_COUNT] = {    100, /* NONE */
    120, /* PAIR */
    150, /* TWO PAIR */
    180, /* THREE KIND */
    210, /* STRAIGHT */
    240, /* FLUSH */
    260, /* FULL HOUSE */
    280, /* FOUR KIND */
    350, /* STRAIGHT FLUSH */
    400, /* FIVE KIND */
    130  /* SHORT STRAIGHT (gem-gated, docs/short-straight.md) */
};

/* Classify n card values (order-independent).  vals are 1..10,
 * types are BattleCardType.  Shared histogram scratch (AGENTS.md 52.14):
 * the battle banked-call chain runs out of stack under the debug ROM (SP
 * collides with _DATA globals -- observed smashing input state and
 * ghost-firing menu confirms), so scratch lives in WRAM, never on the
 * stack.  Fully rewritten on every call; single-threaded, shared with
 * combo_classify_loot below but never nested. */
/* Shared histogram scratch (AGENTS.md 52.14): WRAM, always mapped, so
 * every bank-3 classifier/consumer shares it.  Fully rewritten on every
 * classify call; single-threaded, never re-entered: the HUD preview and
 * the resolve dispatch never nest. */
uint8_t s_hist[10]; /* values are 1..10 */

/* Shared tier ladder: both classifiers fill s_hist, min/max, and the
 * suited flag, then resolve here.  One copy keeps bank-3 ROM small
 * (the release bank sits ~260 B under its ceiling).
 *
 * Short-straight gate (docs/short-straight.md): the tier only classifies
 * while the STRAIGHT gem is equipped.  The check is an inline expression
 * (never a helper call): this ladder sits at the bottom of the deepest
 * battle chain and every extra call edge eats the stack budget
 * (AGENTS.md 52.24).  It reads the canonical equipped bitmask straight
 * out of GameState (WRAM, always mapped): both the resolve dispatch and
 * the HUD preview classify through combo_classify_loot, so the gate
 * applies to both with no duplicated check.  Bank-local read (never a
 * fixed-bank call, AGENTS.md 52.11.1); the ids it names are game-layer
 * data (same pattern as the bank-5 quest bodies reading the quest
 * registry). */
static uint8_t combo_tier_from_hist(uint8_t n, uint8_t all_same_type,
                                    uint8_t min, uint8_t max)
{
    uint8_t i, distinct, pairs, trips;
    uint8_t quads = 0, fives = 0;

    /* FIVE KIND: all five share one value (beats everything). */
    if (n == 5 && s_hist[min - 1] == 5) return HAND_FIVE_KIND;

    /* Sequential with no duplicates: straight / straight flush (5 only). */
    if ((uint8_t)(max - min) == (uint8_t)(n - 1)) {
        distinct = 0;
        for (i = 0; i < 10; i++) {
            if (s_hist[i] != 0) distinct++;
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
        if (s_hist[i] >= 5) fives++;
        else if (s_hist[i] == 4) quads++;
        else if (s_hist[i] == 3) trips++;
        else if (s_hist[i] == 2) pairs++;
    }
    if (fives != 0) return HAND_FIVE_KIND;
    if (quads != 0) return HAND_FOUR_KIND;
    if (trips != 0 && pairs != 0) return HAND_FULL_HOUSE;
    if (trips != 0) return HAND_THREE_KIND;
    /* Short straight (docs/short-straight.md): the longest run of distinct
     * sequential values has length >= 3 at any staged count >= 3.  Slots
     * below THREE_KIND, above TWO_PAIR: single-pair/two-pair/high-card
     * hands with a 3-run upgrade to it (a bare pair with no run falls
     * through to PAIR, unchanged), and the 5-card straight above keeps
     * its own tier (checked first).  Gem-gated: unequipped, the ladder is
     * exactly today's (inline expression, never a call -- stack budget,
     * see above). */
    if (n >= 3 &&
        (g_game.state.variables.values[VARIABLE_ID_GEM_EQUIPPED - 1] &
         (int16_t)(1 << GEM_STRAIGHT)) != 0) {
        uint8_t run = 0;
        uint8_t best_run = 0;
        for (i = 0; i < 10; i++) {
            if (s_hist[i] != 0) {
                run++;
                if (run > best_run) best_run = run;
            } else {
                run = 0;
            }
        }
        if (best_run >= 3) return HAND_SHORT_STRAIGHT;
    }
    if (pairs >= 2) return HAND_TWO_PAIR;
    if (pairs == 1) return HAND_PAIR;
    return HAND_NONE;
}

/* Loot-aware classifier: same tiers as the shared ladder below, but reads
 * values through the effective->selection map and substitutes ring_sub for
 * ring cards (the joker trial).  ring_sub == 0 disables substitution (real
 * values, used for the baseline evaluation).  Shares the static s_hist
 * (single-threaded ROM; never re-entered: the HUD preview and the resolve
 * dispatch never nest).  Cards are const-volatile so both the WRAM staging
 * (combo_resolve_banked) and the live volatile battle hand (HUD preview)
 * call this directly, same bank, no trampoline. */
uint8_t combo_classify_loot(const volatile Card *cards, const uint8_t *idx,
                             uint8_t n, uint8_t ring_sub)
{
    uint8_t i, min, max;
    uint8_t all_same_type = 1;

    for (i = 0; i < 10; i++) s_hist[i] = 0;

    min = 10;
    max = 0;
    for (i = 0; i < n; i++) {
        const volatile Card *c = &cards[idx[i]];
        uint8_t v = (ring_sub != 0 && c->ring) ? ring_sub : c->value;
        uint8_t t = c->type;
        if (v < 1 || v > 10) return HAND_NONE;
        s_hist[v - 1]++;
        if (v < min) min = v;
        if (v > max) max = v;
        if (i > 0 && t != cards[idx[0]].type) all_same_type = 0;
    }

    return combo_tier_from_hist(n, all_same_type, min, max);
}

/* Decode-range guards (mirror of combo.c's pack guards): fail the build
 * if the packed fields ever outgrow the staging byte. */
static const int g_combo_unpack_phase_ok[(COMBO_PHASE_DEFEND <= 1) ? 1 : 0];
static const int g_combo_unpack_effect_ok[(CARD_EFFECT_COUNT <= 128) ? 1 : 0];

/* All effective cards share one symbol (bank-local, shared by resolve
 * and the HUD preview so the suited rule lives in one place). */
uint8_t combo_same_type(const volatile Card *cards, const uint8_t *idx,
                        uint8_t n)
{
    uint8_t i;
    for (i = 1; i < n; i++) {
        if (cards[idx[i]].type != cards[idx[0]].type) return 0;
    }
    return 1;
}

/* Best-subset scratch (WRAM BSS, same discipline as s_hist above):
 * the subset comparison needs more simultaneous state than any
 * battle-chain stack frame has room for (AGENTS.md 52.24 -- the worst
 * chain sits exactly at its budget), so every helper temporary lives
 * here instead of on the stack.  Fully rewritten on every
 * combo_short_best call; single-threaded, never re-entered. */
static uint8_t s_ss_n;
static uint8_t s_ss_pair;
static uint8_t s_ss_single;
static uint8_t s_ss_st;
static uint8_t s_ss_i;
static uint8_t s_ss_t;
static uint8_t s_ss_raw;
static uint16_t s_ss_m;
static uint16_t s_ss_a;
static uint16_t s_ss_best;
static uint16_t s_ss_sub;

/* Best-subset resolution for SHORT STRAIGHT (docs/short-straight.md) ──
 * The ladder classifies the full staged set, but a pair subset or a
 * single card can score higher: SHORT outranks pairs yet can deal less
 * (3,3,4,4,5 stages SHORT for 15 while its two pairs deal 21), and a lone
 * high card can beat a low run.  Ringless only: ringed hands convert to
 * healing (amounts incomparable across natures), the joker trial already
 * explores ring-assisted kinds, and dropping the ring is an explicit
 * unstage.  The caller resolved the full set first (tier == SHORT, base
 * and suited set); on return the IO tier/base/suited name the winning
 * play, or are untouched when the full set already wins (ties keep the
 * full set).  Amounts compare as raw products (division by 100 is
 * monotonic, so it never changes the order -- and there is no divide
 * instruction to spend).  Leaf (calls nothing): keeps the battle stack
 * shallow (AGENTS.md 52.24). */
void combo_short_best(const volatile Card *cards, const uint8_t *idx,
                      uint8_t n, uint8_t phase,
                      uint8_t *tier_io, uint16_t *base_io, uint8_t *suited_io)
{
    if (!tier_io || !base_io || !suited_io) return;
    if (*tier_io != HAND_SHORT_STRAIGHT) return;

    /* Pair values present (ringless: s_hist counts are real; SHORT
     * implies no trips, so count == 2 marks exactly the paired values).
     * TWO_PAIR when two values pair, else PAIR. */
    s_ss_n = 0;
    for (s_ss_i = 0; s_ss_i < 10; s_ss_i++) {
        if (s_hist[s_ss_i] == 2) s_ss_n++;
    }

    /* Full-set amount via shared shift-add below (mult in s_ss_m,
     * base in s_ss_a, result in s_ss_best). */
    s_ss_m = (*suited_io) ? 155 : 130;
    s_ss_a = *base_io;
    s_ss_best = 0;
    while (s_ss_m != 0) {
        if (s_ss_m & 1u) s_ss_best = (uint16_t)(s_ss_best + s_ss_a);
        s_ss_a <<= 1;
        s_ss_m >>= 1;
    }

    /* One pass over the staged cards.  Ringless: every card counts at
     * its face value when phase-eligible (no ring branch: the caller
     * only runs this with no ring staged). */
    s_ss_pair = 0;
    s_ss_single = 0;
    s_ss_st = 0xFF;
    for (s_ss_i = 0; s_ss_i < n; s_ss_i++) {
        s_ss_t = cards[idx[s_ss_i]].type;
        s_ss_raw = cards[idx[s_ss_i]].value;
        if (phase == COMBO_PHASE_ATTACK) {
            if (s_ss_t != BATTLE_CARD_TYPE_SHIELD && s_ss_raw > s_ss_single)
                s_ss_single = s_ss_raw;
        } else {
            if (s_ss_t == BATTLE_CARD_TYPE_SHIELD && s_ss_raw > s_ss_single)
                s_ss_single = s_ss_raw;
        }
        /* Paired cards feed the pair subset (base) and vote on its
         * suitedness (same symbol on every PAIRED card -- extras must not
         * vote; a 0-contrib paired card still votes its symbol). */
        if (s_hist[s_ss_raw - 1] == 2) {
            if ((phase == COMBO_PHASE_ATTACK && s_ss_t != BATTLE_CARD_TYPE_SHIELD) ||
                (phase == COMBO_PHASE_DEFEND && s_ss_t == BATTLE_CARD_TYPE_SHIELD))
                s_ss_pair = (uint8_t)(s_ss_pair + s_ss_raw);
            if (s_ss_st == 0xFF) s_ss_st = s_ss_t;
            else if (s_ss_st != s_ss_t) s_ss_st = 0xFE;
        }
    }

    /* Pair subset vs the full set (only when a pair exists): TWO_PAIR
     * for two paired values (150 unsuited / 175 suited), else PAIR
     * (120 / 145).  Shared shift-add loop: no 8-bit * or /. */
    if (s_ss_n != 0) {
        if (s_ss_n >= 2)
            s_ss_m = (s_ss_st == 0xFE || s_ss_st == 0xFF) ? 150 : 175;
        else
            s_ss_m = (s_ss_st == 0xFE || s_ss_st == 0xFF) ? 120 : 145;
        s_ss_a = s_ss_pair;
        s_ss_sub = 0;
        while (s_ss_m != 0) {
            if (s_ss_m & 1u) s_ss_sub = (uint16_t)(s_ss_sub + s_ss_a);
            s_ss_a <<= 1;
            s_ss_m >>= 1;
        }
        if (s_ss_sub > s_ss_best) {
            s_ss_best = s_ss_sub;
            *tier_io = (s_ss_n >= 2) ? HAND_TWO_PAIR : HAND_PAIR;
            *base_io = s_ss_pair;
            *suited_io = (s_ss_st == 0xFE || s_ss_st == 0xFF) ? 0 : 1;
        }
    }

    /* Lone high card vs the winner so far (mult 100, never suited). */
    s_ss_m = 100;
    s_ss_a = s_ss_single;
    s_ss_sub = 0;
    while (s_ss_m != 0) {
        if (s_ss_m & 1u) s_ss_sub = (uint16_t)(s_ss_sub + s_ss_a);
        s_ss_a <<= 1;
        s_ss_m >>= 1;
    }
    if (s_ss_sub > s_ss_best) {
        *tier_io = HAND_NONE;
        *base_io = s_ss_single;
        *suited_io = 0;
    }
}

/* Participating-hand base: only cards forming the hand count.
 * See combo.h for the contract.  Bank-local (same bank as resolve and
 * the HUD preview): no trampoline, no fixed-bank calls, no mul/div.
 * No-ring kinds locate the hand values with one scan of the
 * classifier's s_hist (left behind by the tier search); a ringed kind
 * uses its substituted value directly (the joker always sits in the
 * hand it completes).  HIGH CARD tracks the max; all-five tiers sum;
 * the short straight counts one best-eligible card per run value (a
 * paired duplicate shares the value but is not part of the run).
 * This stays a leaf (it calls nothing) so the deepest battle chain
 * gains no nesting (AGENTS.md 52.24). */
uint8_t combo_participating_base(const volatile Card *cards, const uint8_t *idx,
                                 uint8_t n, uint8_t tier, uint8_t ring_sub,
                                 uint8_t phase, uint8_t count_ring)
{
    uint8_t i;
    uint8_t acc;
    uint8_t need;
    uint8_t pv1;
    uint8_t pv2;

    /* Callers guarantee 1 <= n <= 5 (resolve caps count; preview stages
     * at most a 5-card hand; w == 0 returns above). */
    if (n == 0) return 0;
    need = (tier == HAND_FOUR_KIND) ? 4
         : (tier == HAND_THREE_KIND) ? 3
         : (tier == HAND_PAIR || tier == HAND_TWO_PAIR) ? 2 : 0;
    pv1 = 0;
    pv2 = 0;
    if (tier == HAND_SHORT_STRAIGHT) {
        /* Longest run wins; ties go to the higher run.  s_hist holds the
         * classifying trial (callers re-run the best ring trial first).
         * Scratch discipline (AGENTS.md 52.24): `need` doubles as the
         * best-length cell, then as the run-value offset below -- it is
         * never read as a kind-count for this tier. */
        uint8_t run = 0;
        need = 0;
        for (i = 0; i < 10; i++) {
            if (s_hist[i] != 0) {
                run++;
                if (run > need) {
                    need = run;
                    if (run >= 3) pv1 = (uint8_t)(i + 1);
                } else if (run == need && run >= 3) {
                    pv1 = (uint8_t)(i + 1);
                }
            } else {
                run = 0;
            }
        }
        /* One card per run value (pv1, pv1-1, pv1-2): the best ELIGIBLE
         * card at each value counts.  A paired duplicate shares its value
         * but is not part of the run -- it scores 0, like any extra. */
        acc = 0;
        for (need = 0; need < 3; need++) {
            run = 0;
            for (i = 0; i < n; i++) {
                const volatile Card *c = &cards[idx[i]];
                uint8_t t = c->type;
                uint8_t rg = c->ring;
                uint8_t raw = c->value;
                uint8_t mv = (rg && ring_sub != 0) ? ring_sub : raw;
                uint8_t contrib = 0;
                if (phase == COMBO_PHASE_ATTACK) {
                    if (rg) {
                        if (count_ring) contrib = mv;
                    } else if (t != BATTLE_CARD_TYPE_SHIELD) {
                        contrib = raw;
                    }
                } else {
                    if (!rg && t == BATTLE_CARD_TYPE_SHIELD) contrib = raw;
                }
                if (mv == (uint8_t)(pv1 - need) && contrib > run) run = contrib;
            }
            acc = (uint8_t)(acc + run);
        }
        return acc;
    } else if (need != 0) {
        if (ring_sub != 0) {
            /* Ringed kinds need no histogram: the joker always sits in
             * the hand it completes, so its substituted value IS the
             * hand value (a ringed TWO_PAIR is always outbid by trips,
             * so pv2 never matters with a ring). */
            pv1 = ring_sub;
        } else {
            for (i = 0; i < 10; i++) {
                if (s_hist[i] == need) {
                    if (pv1 == 0) pv1 = (uint8_t)(i + 1);
                    else pv2 = (uint8_t)(i + 1);
                }
            }
        }
    }
    acc = 0;
    for (i = 0; i < n; i++) {
        const volatile Card *c = &cards[idx[i]];
        uint8_t t = c->type;
        uint8_t rg = c->ring;
        uint8_t raw = c->value;
        uint8_t mv = (rg && ring_sub != 0) ? ring_sub : raw;
        uint8_t contrib = 0;
        if (phase == COMBO_PHASE_ATTACK) {
            if (rg) {
                if (count_ring) contrib = mv;
            } else if (t != BATTLE_CARD_TYPE_SHIELD) {
                contrib = raw;
            }
        } else {
            if (!rg && t == BATTLE_CARD_TYPE_SHIELD) contrib = raw;
        }
        if (tier == HAND_NONE) {
            if (contrib > acc) acc = contrib;
        } else if (need == 0) {
            acc += contrib;
        } else if (mv == pv1 || mv == pv2) {
            acc += contrib;
        }
    }
    return acc;
}

void combo_resolve_banked(void)
{
    uint8_t count = g_bk_byte_a;
    ComboResult *out_result = (ComboResult *)g_bk_ptr_b;
    uint8_t i;
    uint8_t w = 0;
    uint8_t idx[5];
    /* Stack-budget discipline (AGENTS.md 52.24): this body sits near the
     * bottom of the deepest battle chain, so it keeps almost no locals.
     * Inputs are reread from the staging globals at each use -- safe
     * because nothing called from here clobbers staging (the classifiers
     * take plain params, and effect_resolve_into() runs last while its
     * own argument is evaluated first).  Outputs accumulate straight into
     * the staged result struct.  The idx[] map and s_hist stay untouched:
     * they replaced a 20 B scratch buffer after it smashed input state. */

    if (!out_result) return;

    if (!g_bk_ptr_a || count == 0) {
        out_result->count = 0;
        out_result->eff_count = 0;
        out_result->tier = HAND_NONE;
        out_result->suited = 0;
        out_result->multiplier = 100;
        out_result->base_power = 0;
        g_effect_last.type = COMBO_UNPACK_EFFECT(g_bk_byte_b);
        g_effect_last.amount = 0;
        return;
    }

    if (count > 5) count = 5;
    out_result->count = count;

    /* Poker-hands-only scoring: only cards forming the hand count --
     * extras deal/block 0 (HIGH CARD scores the single highest eligible
     * card).  Attack rings deal 0 damage: with a ring present the whole
     * hand converts to healing (scaled participating value, ring at its
     * substituted value); a lone ring heals its face value.  Defend rings
     * add 0 block but enable the tier (wildcard) and the over-block heal. */
    out_result->base_power = 0;
    {
        uint8_t has_ring;
        uint8_t best_v;
        has_ring = 0;
        for (i = 0; i < count; i++) {
            uint8_t t = ((const Card *)g_bk_ptr_a)[i].type;
            if (((const Card *)g_bk_ptr_a)[i].ring) has_ring = 1;
            if (COMBO_UNPACK_PHASE(g_bk_byte_b) == COMBO_PHASE_ATTACK) {
                idx[w] = i;
                w++;
            } else {
                if (t != BATTLE_CARD_TYPE_SHIELD && !((const Card *)g_bk_ptr_a)[i].ring) continue;
                idx[w] = i;
                w++;
            }
        }

        best_v = 0;
        if (w < 2) {
            out_result->tier = HAND_NONE;
        } else {
            /* Ring JOKER: a ring's value substitutes freely -- try every
             * legal value and keep the best tier (types are untouched,
             * so the suited bonus is unaffected).  The scan runs 10..1
             * so ties keep the LARGER value (highest pair, not lowest).
             * The map keeps effective order.  Each trial rebuilds s_hist
             * from scratch, so after the loop it holds the v=1 trial;
             * participating_base only consumes that leftover for no-ring
             * kinds (identical across trials when no ring is present) and
             * takes ringed values straight from ring_sub -- see the
             * precondition in combo.h. */
            uint8_t best;
            uint8_t v, t2;
            if (has_ring) {
                best = HAND_NONE;
                for (v = 10; v != 0; v--) {
                    t2 = combo_classify_loot((const Card *)g_bk_ptr_a, idx, w, v);
                    /* Rank-aware max (docs/short-straight.md): enum order
                     * matches rank EXCEPT SHORT STRAIGHT (10), which slots
                     * between TWO_PAIR and THREE_KIND -- a raw max would let
                     * it outrank straights and better (e.g. a ringed 4-run
                     * plus straight completion must stay STRAIGHT). */
                    if (t2 == HAND_SHORT_STRAIGHT) {
                        if (best < HAND_THREE_KIND) { best = t2; best_v = v; }
                    } else if (t2 > best || (best == HAND_SHORT_STRAIGHT &&
                                             t2 >= HAND_THREE_KIND)) {
                        best = t2;
                        best_v = v;
                    }
                }
                out_result->tier = best;
                if (best_v != 0) {
                    /* Leave s_hist holding the BEST trial, not the v=1
                     * leftovers: participating_base scans s_hist for the
                     * short straight (ringed kinds read ring_sub
                     * directly, so they never cared which trial is left). */
                    combo_classify_loot((const Card *)g_bk_ptr_a, idx, w, best_v);
                }
            } else {
                out_result->tier = combo_classify_loot((const Card *)g_bk_ptr_a, idx, w, 0);
            }
        }
        if (out_result->tier == HAND_NONE) {
            out_result->suited = 0;
        } else {
            out_result->suited = combo_same_type((const Card *)g_bk_ptr_a,
                                                 idx, w);
        }
            /* Attack + ring converts to healing: the ring counts at its
             * substituted value (count_ring=1); otherwise rings add 0. */
            out_result->base_power = combo_participating_base(
                (const Card *)g_bk_ptr_a, idx, w, out_result->tier,
                best_v, COMBO_UNPACK_PHASE(g_bk_byte_b),
                (COMBO_UNPACK_PHASE(g_bk_byte_b) == COMBO_PHASE_ATTACK && has_ring) ? 1 : 0);
        /* Dynamic best-subset (docs/short-straight.md): the ladder
         * proposes SHORT, but a pair subset or a lone card can score
         * higher -- resolve the winner, not just the rank.  Ringless
         * only (healing amounts are incomparable). */
        if (out_result->tier == HAND_SHORT_STRAIGHT && !has_ring) {
            combo_short_best((const Card *)g_bk_ptr_a, idx, w,
                             COMBO_UNPACK_PHASE(g_bk_byte_b),
                             &out_result->tier, &out_result->base_power,
                             &out_result->suited);
        }
        /* Multiplier recomputes from the (possibly updated) tier: the
         * subset above can downgrade SHORT to a pair tier or HIGH CARD. */
        out_result->multiplier =
            g_combo_tier_mult[out_result->tier];
        if (out_result->suited) out_result->multiplier += 25;
        out_result->eff_count = w;

        /* Attack + ring converts the whole hand to healing: force the
         * HEAL effect so EFFECT_RESOLVED reports healing, not damage. */
        effect_resolve_into(
            (COMBO_UNPACK_PHASE(g_bk_byte_b) == COMBO_PHASE_ATTACK && has_ring) ? CARD_EFFECT_HEAL_HP
                                                      : COMBO_UNPACK_EFFECT(g_bk_byte_b),
            (ComboResult *)g_bk_ptr_b, &g_effect_last);
    }
}
