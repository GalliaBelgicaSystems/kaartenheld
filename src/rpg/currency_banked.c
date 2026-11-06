#pragma bank 11

#include "rpg/currency.h"
#include "banked.h"
#include "telemetry.h"

/* ── Banked currency core (ROM bank 11) ──────────────────────────────
 * Hosts currency_add's arithmetic out of the fixed bank (which sits ~100 B
 * under 0x8000, AGENTS.md §52.18).  The fixed wrapper (currency.c) only
 * stages and dispatches; this body validates, adds, saturates, and emits.
 * Self-contained per §52.11.1: WRAM globals only, no fixed-bank calls,
 * no di/ei, no * / %.  Telemetry is written to the ring directly (a banked
 * body cannot re-enter the trampoline to reach the bank-2 emit body);
 * the writer mirrors telemetry_emit_banked exactly. */

#define CURRENCY_MAX_DISPLAY 9999

#ifdef DEBUG_BUILD
/* Direct ring write (debug only: the release ROM has no telemetry ring
 * and currency_add compiles silent there).  Same shape as the economy
 * module's writer; kept local so the engine never includes game headers
 * (AGENTS.md §55.1). */
static void cur_emit(uint8_t type, uint8_t d0, uint8_t d1, uint8_t d2,
                     uint8_t d3)
{
    GameEvent *ev = &g_telemetry_buffer[g_telemetry_head];
    ev->seq = g_telemetry_seq++;
    ev->frame = g_telemetry_frame_ptr ? *g_telemetry_frame_ptr : 0;
    ev->type = type;
    ev->data[0] = d0;
    ev->data[1] = d1;
    ev->data[2] = d2;
    ev->data[3] = d3;
    g_telemetry_head = (uint8_t)((g_telemetry_head + 1) &
                                 (MAX_TELEMETRY_EVENTS - 1));
    if (g_telemetry_count < MAX_TELEMETRY_EVENTS) g_telemetry_count++;
}
#endif

/* In: g_bk_ptr_a = GameState*, g_bk_byte_a = currency,
 * g_bk_byte_b/c = amount lo/hi.  Saturates at 9999 / 0
 * (docs/economy.md §7: fits the 4-digit HUD). */
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
    new_value = (int16_t)(state->currency.amount[currency - 1] + amount);
    if (new_value > CURRENCY_MAX_DISPLAY) new_value = CURRENCY_MAX_DISPLAY;
    if (new_value < 0) new_value = 0;
    state->currency.amount[currency - 1] = new_value;
#ifdef DEBUG_BUILD
    if (amount > 0) {
        cur_emit(EVENT_CURRENCY_ADDED, currency,
                 (uint8_t)(amount & 0xFF),
                 (uint8_t)((amount >> 8) & 0xFF), 0);
    } else {
        cur_emit(EVENT_CURRENCY_SPENT, currency,
                 (uint8_t)((-amount) & 0xFF),
                 (uint8_t)((-amount >> 8) & 0xFF), 0);
    }
#endif
}
