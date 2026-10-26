#ifndef COMBO_H
#define COMBO_H

#include "card.h"
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    COMBO_PHASE_ATTACK = 0,
    COMBO_PHASE_DEFEND = 1
} ComboPhase;

/* Banked-dispatch staging pack (combo.c encodes into g_bk_byte_b,
 * combo_content.c decodes): bit 0 = ComboPhase, bits 1..7 =
 * CardEffectType.  Single source of truth — both sides must use these,
 * never hand-rolled shifts/masks.  Valid ranges (phase <= 1, effect <=
 * 127) are compile-time enforced at each use site, so a future phase or
 * effect id that outgrows the byte breaks the build instead of wrapping
 * silently. */
#define COMBO_PACK(phase, fx) \
    ((uint8_t)(((uint8_t)(phase)) | ((uint8_t)((uint8_t)(fx) << 1))))
#define COMBO_UNPACK_PHASE(b) ((uint8_t)(((uint8_t)(b)) & 0x01))
#define COMBO_UNPACK_EFFECT(b) ((uint8_t)(((uint8_t)(b)) >> 1))

/* Poker hand tiers (docs/combo-system.md hand table).  Strict sizes:
 * STRAIGHT / FLUSH / STRAIGHT_FLUSH / FIVE_KIND require all five
 * selected cards; pairs and kinds work at any count >= 2; the SHORT
 * STRAIGHT (docs/short-straight.md) needs a 3+ run at any staged count
 * >= 3 and only classifies while its gem is equipped.  Order here
 * mirrors ranking; the scored multiplier table lives in
 * combo_content.c (bank 3). */
typedef enum {
    HAND_NONE = 0,           /* high card / fewer than 2 effective cards */
    HAND_PAIR = 1,           /* 120% */
    HAND_TWO_PAIR = 2,       /* 150% */
    HAND_THREE_KIND = 3,     /* 180% */
    HAND_STRAIGHT = 4,       /* 210% (all 5, sequential values) */
    HAND_FLUSH = 5,          /* 240% (all 5, same symbol, not sequential) */
    HAND_FULL_HOUSE = 6,     /* 260% (trips + pair) */
    HAND_FOUR_KIND = 7,      /* 280% */
    HAND_STRAIGHT_FLUSH = 8, /* 350% (all 5, sequential + same symbol) */
    HAND_FIVE_KIND = 9,      /* 400% (all 5, same value) */
    HAND_SHORT_STRAIGHT = 10 /* 130% (3+ run, gem-gated; below THREE_KIND) */
} HandTier;

#define HAND_TIER_COUNT 11

typedef struct {
    Card cards[5];
    uint8_t count;
    uint8_t eff_count;   /* cards that entered the hand (attack: every
                           * card; defend: shields + rings -- other
                           * types are inert for block (0 block, 0 combo),
                           * but a daggers-only selection counterattacks
                           * through a separate attack-phase dispatch) */
    uint8_t tier;        /* HandTier */
    uint8_t suited;      /* all selected share one symbol (telemetry) */
    uint16_t multiplier; /* EFFECTIVE percent incl. suited bonus:
                          * tier % + 25 when suited -- the exact scalar
                          * effect resolution applies to base_power */
    uint16_t base_power;
} ComboResult;

/* Evaluate a sequence of up to 5 cards for the specified phase AND resolve
 * the requested effect against the evaluated hand in one banked dispatch.
 *
 * The evaluator produces ONLY the quality of the selection (tier,
 * multiplier, base_power) into out_result -- never an effect magnitude.
 * The resolver (src/rpg/effects_content.c, same bank) then scales that
 * quality into g_effect_last per effect_type; battle reads g_effect_last
 * directly and applies it (docs/combo-system.md §5/§7).
 *
 * effect_type: CardEffectType the played hand performs.  Attack play uses
 * the leading card's Card.effect; defend play requests BLOCK_DAMAGE (the
 * phase defines the action).
 *
 * Note: To conserve Bank 0 ROM budget, the dispatch does not copy cards
 * into out_result->cards[]; the caller pre-populates or aliases
 * out_result->cards[] if needed (as in battle_play_hand).
 *
 * There is deliberately NO fixed-bank combo_resolve() staging wrapper:
 * battle_play_hand stages and dispatches directly so the deepest battle
 * chain carries one fewer nesting level (AGENTS.md 52.24).  Do not
 * reintroduce a wrapper without re-checking make stack-budget. */

/* The banked no-arg body (src/battle/combo_content.c, ROM bank 3), run via
 * the WRAM banked-call trampoline.  Read only the staged _DATA globals;
 * self-contained apart from the bank-local call into effects_content.c. */
void combo_resolve_banked(void);

/* Order-independent hand classifier (bank-local utility): evaluates card
 * values 1..10 through an effective->selection index map, substituting
 * ring_sub for ring cards (0 = real values).  Bank-3 callers invoke this
 * DIRECTLY -- same bank, no trampoline: the resolve dispatch
 * (combo_resolve_banked) and the battle HUD's live COMBO-row preview share
 * this one ladder instead of duplicating it. */
uint8_t combo_classify_loot(const volatile Card *cards, const uint8_t *idx,
                             uint8_t n, uint8_t ring_sub);

/* All effective cards share one symbol (bank-local, shared by the
 * resolver and the HUD preview so the suited rule lives in one place). */
uint8_t combo_same_type(const volatile Card *cards, const uint8_t *idx,
                        uint8_t n);

/* Participating-hand base (same bank, shared by resolve + preview):
 * ONLY poker-hand cards count -- extras deal/block 0.  HIGH CARD scores
 * the single highest eligible card; PAIR/TWO_PAIR/kinds score only the
 * paired cards; 5-card hands score all five.  With count_ring=0 rings
 * count as 0 (damage/block); the attack ring-heal passes count_ring=1
 * so the ring counts at its substituted value.  Shields never count in
 * attack; non-shields never count in defend (daggers counter through a
 * separate attack-phase dispatch instead -- see battle.c).
 *
 * Precondition: call combo_classify_loot on the SAME (cards, idx, n)
 * selection first -- this reads the histogram that call leaves behind
 * in the shared s_hist scratch (same-bank WRAM, never re-entered).  A
 * ringed kind bypasses the histogram entirely (its value comes from
 * ring_sub); HIGH CARD tracks max and all-five tiers sum directly, so
 * the leftover histogram is only consumed for no-ring kinds and the
 * (ringed or plain) short straight -- which is why callers re-run the
 * best ring trial before calling (so s_hist holds the classifying
 * values, not the v=1 leftovers).  The classify call is still required
 * every time, since it also validates values and detects the tier.
 * Callers (combo_resolve_banked and the HUD preview) both classify
 * immediately before, so preview and resolve can never disagree. */
uint8_t combo_participating_base(const volatile Card *cards, const uint8_t *idx,
                                 uint8_t n, uint8_t tier, uint8_t ring_sub,
                                 uint8_t phase, uint8_t count_ring);

/* Tier percent table (combo_content.c, bank 3): index by HandTier.  Shared
 * read-only between the resolver and the damage preview so both scale by
 * the same numbers; never duplicated. */
extern const uint16_t g_combo_tier_mult[HAND_TIER_COUNT];

/* Best-subset resolution for SHORT STRAIGHT (combo_content.c, bank 3):
 * when the ladder classifies the full staged set as SHORT STRAIGHT, a
 * pair subset or a single card can score higher.  Ringless only (caller
 * checks: ringed hands convert to healing, and the joker trial already
 * explores ring-assisted kinds; dropping the ring is an explicit
 * unstage).  tier/base/suited are in/out: on entry the full-set outcome,
 * on return the winning play (untouched on ties).  Leaf (calls
 * nothing). */
void combo_short_best(const volatile Card *cards, const uint8_t *idx,
                      uint8_t n, uint8_t phase,
                      uint8_t *tier_io, uint16_t *base_io, uint8_t *suited_io);

#endif /* COMBO_H */
