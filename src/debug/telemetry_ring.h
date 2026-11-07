#ifndef DEBUG_TELEMETRY_RING_H
#define DEBUG_TELEMETRY_RING_H

#include <stdint.h>
#include "telemetry.h"

/* ── Inline ring writer for banked bodies ────────────────────────────
 * A banked body cannot re-enter the WRAM trampoline to reach the bank-2
 * telemetry_emit_banked body (banked.h non-reentrancy), so bodies that
 * need telemetry write the ring directly.  This single static copy
 * replaces the four field-for-field replicas (economy, currency, Carl
 * grant, telegraph); each including bank gets its own copy, which is
 * what the replicas did anyway.  Keep field-for-field in sync with
 * telemetry_emit_banked.c (seq/frame/type/data discipline).
 *
 * Engine files must never include this (AGENTS.md §55.1 is about game
 * headers; this one is debug-owned and banked-only by convention):
 * fixed-bank code uses telemetry_emit(), never this. */
#ifdef DEBUG_BUILD
static void telemetry_ring_emit(uint8_t type, uint8_t d0, uint8_t d1,
                                uint8_t d2, uint8_t d3)
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
#else
#define telemetry_ring_emit(type, d0, d1, d2, d3) ((void)0)
#endif

/* The (head + 1) & (MAX - 1) step is only a ring when MAX is a power of
 * two; fail the build if anyone changes the capacity carelessly. */
typedef char telemetry_ring_pow2[
    ((MAX_TELEMETRY_EVENTS & (MAX_TELEMETRY_EVENTS - 1)) == 0) ? 1 : -1];

#endif /* DEBUG_TELEMETRY_RING_H */
