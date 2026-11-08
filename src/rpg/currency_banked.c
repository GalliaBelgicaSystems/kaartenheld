#pragma bank 11

#include "rpg/currency.h"
#include "banked.h"
#include "telemetry.h"
#include "telemetry_ring.h"

/* ── Banked currency core (ROM bank 11) ──────────────────────────────
 * Hosts currency_add's arithmetic out of the fixed bank (which sits ~100 B
 * under 0x8000, AGENTS.md §52.18).  The fixed wrapper (currency.c) only
 * stages and dispatches; this body validates, adds, saturates, and emits.
 * Self-contained per §52.11.1: WRAM globals only, no fixed-bank calls,
 * no di/ei, no * / %.  Telemetry is written to the ring directly (a banked
 * body cannot re-enter the trampoline to reach the bank-2 emit body);
 * the writer mirrors telemetry_emit_banked exactly. */

#define CURRENCY_MAX_DISPLAY 9999

/* Ring telemetry runs through telemetry_ring.h: one shared inline writer.
 * Kept out of game headers so the engine never includes game content
 * (AGENTS.md §55.1). */

/* In: g_bk_ptr_a = GameState*, g_bk_byte_a = currency,
 * g_bk_byte_b/c = amount lo/hi.  Saturates at 9999 / 0
 * (docs/economy.md §7: fits the 4-digit HUD).  The amount is
 * pre-clamped to ±9999 BEFORE the add: the stored value never exceeds
 * 9999, so the sum stays inside int16 (9999+9999 < 32767) and a large
 * grant can never wrap negative and clamp a rich player to 0.  No
 * 32-bit math (SDCC int32 would work but costs code on every bank-11
 * caller path for a case saturation already handles). */
void currency_add_banked(void)
{
    GameState *state = (GameState *)g_bk_ptr_a;
    uint8_t currency = g_bk_byte_a;
    int16_t amount = (int16_t)((uint16_t)g_bk_byte_b |
                              ((uint16_t)g_bk_byte_c << 8));
    int16_t new_value;
    if (!state || currency < 1 || currency > MAX_CURRENCIES ||
        amount == 0)
        return;
    if (amount > CURRENCY_MAX_DISPLAY) amount = CURRENCY_MAX_DISPLAY;
    if (amount < (int16_t)(0 - CURRENCY_MAX_DISPLAY))
        amount = (int16_t)(0 - CURRENCY_MAX_DISPLAY);
    new_value = (int16_t)(state->currency.amount[currency - 1] + amount);
    if (new_value > CURRENCY_MAX_DISPLAY) new_value = CURRENCY_MAX_DISPLAY;
    if (new_value < 0) new_value = 0;
    state->currency.amount[currency - 1] = new_value;
#ifdef DEBUG_BUILD
    if (amount > 0) {
        telemetry_ring_emit(EVENT_CURRENCY_ADDED, currency,
                            (uint8_t)(amount & 0xFF),
                            (uint8_t)((amount >> 8) & 0xFF), 0);
    } else {
        telemetry_ring_emit(EVENT_CURRENCY_SPENT, currency,
                            (uint8_t)((-amount) & 0xFF),
                            (uint8_t)((-amount >> 8) & 0xFF), 0);
    }
#endif
}
