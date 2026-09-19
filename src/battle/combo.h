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
 * selected cards; pairs and kinds work at any count >= 2.  Order here
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
    HAND_FIVE_KIND = 9       /* 400% (all 5, same value) */
} HandTier;

#define HAND_TIER_COUNT 10

typedef struct {
    Card cards[5];
    uint8_t count;
    uint8_t eff_count;   /* cards that entered the hand (attack: every
                          * card; defend: SHIELD cards only) */
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
 * quality into g_effect_last per effect_type; battle reads it via
 * effect_last() and applies it (docs/combo-system.md §5/§7).
 *
 * effect_type: CardEffectType the played hand performs.  Attack play uses
 * the leading card's Card.effect; defend play requests BLOCK_DAMAGE (the
 * phase defines the action).
 *
 * Note: To conserve Bank 0 ROM budget, the dispatch does not copy cards
 * into out_result->cards[]; the caller pre-populates or aliases
 * out_result->cards[] if needed (as in battle_play_hand). */
void combo_resolve(const Card *cards, uint8_t count, ComboPhase phase,
                   uint8_t effect_type, ComboResult *out_result);

/* The banked no-arg body (src/battle/combo_content.c, ROM bank 3), run via
 * the WRAM banked-call trampoline.  Read only the staged _DATA globals;
 * self-contained apart from the bank-local call into effects_content.c. */
void combo_resolve_banked(void);

/* Order-independent hand classifier (bank-local utility): evaluates vals
 * 1..10 with phase-filtered entries.  Bank-3 callers (e.g. the battle HUD's
 * live COMBO row) invoke this DIRECTLY -- same bank, no trampoline. */
uint8_t combo_classify(const uint8_t *vals, const uint8_t *types, uint8_t n);

#endif /* COMBO_H */
