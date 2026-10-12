#pragma bank 3

#include "effects.h"
#include "banked.h"

/* ── Banked effect resolution (docs/combo-system.md Phase B/C) ───────
 * Runs inside the banked combo dispatch (ROM bank 3): the combo body
 * calls effect_resolve_into() bank-locally after evaluating hand shape.
 * Self-contained: pure function of its arguments; writes the shared
 * g_effect_last slot (WRAM) and/or the caller's out pointer; never calls
 * fixed-bank code.
 *
 * Scaling: amount = base_power x multiplier / 100, where multiplier is
 * the tier percent (+25 suited) from the evaluator.  The multiply and
 * divide are hand-rolled shift-add / subtract-loops because SDCC lowers
 * the operators to __mulint/__divuint -- library code that links into
 * the FIXED bank and cannot be called while bank 3 is mapped
 * (AGENTS.md 52.11.1).
 *
 * Every resolved effect currently scales the same way; when per-effect
 * scaling arrives (plan §10 EffectScaling), each case here grows its own
 * response to combo quality. */

/* u16 = u16 x u16 via shift-add (no lib call).  Inputs are small
 * (base_power <= 45, multiplier <= 425), so intermediates fit u16. */
static uint16_t effect_mul_u16(uint16_t a, uint16_t b)
{
    uint16_t r = 0;
    while (b != 0) {
        if (b & 1) r += a;
        a <<= 1;
        b >>= 1;
    }
    return r;
}

/* q = x / 100 via subtraction loop; x <= 45*425 = 19125 so at most ~191
 * iterations of a one-shot battle-resolution path. */
static uint8_t effect_div100(uint16_t x)
{
    uint8_t q = 0;
    while (x >= 100) {
        x -= 100;
        q++;
    }
    return q;
}

/* Scale a hand's base_power by its effective multiplier. */
static uint8_t effect_scale_power(const ComboResult *combo)
{
    return effect_div100(effect_mul_u16(combo->base_power,
                                        combo->multiplier));
}

void effect_resolve_into(uint8_t effect_type, const ComboResult *combo,
                         EffectResult *out)
{
    EffectResult *dst = out ? out : &g_effect_last;

    dst->type = effect_type;

    switch (effect_type) {
        case CARD_EFFECT_DAMAGE_TARGET:
        case CARD_EFFECT_BLOCK_DAMAGE:
        case CARD_EFFECT_HEAL_HP:
            dst->amount = combo ? effect_scale_power(combo) : 0;
            break;
        default:
            dst->amount = 0;
            break;
    }
}
