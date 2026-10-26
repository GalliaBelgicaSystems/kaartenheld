#ifndef RPG_EFFECTS_H
#define RPG_EFFECTS_H

#include <stdint.h>
#include "rpg/cards.h"
#include "combo.h"

/* ── Effect resolution layer (docs/combo-system.md Phase B) ───────────
 * Combos describe the QUALITY of a card selection (ComboResult); effects
 * describe the CONSEQUENCE (EffectResult).  This module is the only place
 * that turns one into the other -- battle flow applies the result but
 * never computes magnitudes, and the combo evaluator never sees effects.
 *
 * The effect vocabulary is the existing engine-generic CardEffectType
 * (rpg/cards.h): DAMAGE_TARGET / BLOCK_DAMAGE / HEAL_HP correspond to the
 * plan's EFFECT_DAMAGE / EFFECT_BLOCK / EFFECT_HEAL.  New effect kinds
 * (status, draw, ...) extend that enum without touching the combo side.
 *
 * Dispatch note: the scaling body lives in ROM bank 3 next to the combo
 * evaluator, and the banked combo dispatch runs BOTH in one banked
 * dispatch -- the bank-3 combo body calls effect_resolve_into()
 * bank-locally (same bank, no trampoline).  This keeps the fixed-bank
 * cost of the seam to one inline staging sequence (battle_play_hand
 * dispatches directly; there is deliberately no wrapper -- see
 * combo.h).  A standalone fixed-entry effect_resolve() can be added
 * when a second caller appears.
 *
 * Statuses are deliberately NOT implemented yet (plan §2); the seam they
 * will plug into is the per-effect switch in the banked body. */

/* Outcome of resolving one played hand.  Magnitudes only -- never combo
 * internals, never persistent state. */
typedef struct {
    uint8_t type;    /* CardEffectType actually resolved */
    uint8_t amount;  /* Scaled magnitude (damage / block / heal points) */
} EffectResult;

/* Result of the most recent banked combo dispatch.  Single slot by
 * design: combat resolution is sequential.  Read g_effect_last directly
 * immediately after the call. */
extern EffectResult g_effect_last;

/* Bank-3 scaling body, called bank-locally by combo_resolve_banked().
 * Self-contained: pure function of (effect, combo).  Not for fixed-bank
 * callers (bank 3 is not mapped there). */
void effect_resolve_into(uint8_t effect_type, const ComboResult *combo,
                         EffectResult *out);

#endif /* RPG_EFFECTS_H */
