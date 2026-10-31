#pragma bank 2
#pragma disable_warning 110

#include "telemetry.h"

/* ── Banked telemetry-append body (ROM bank 2) ─────────────────────
 * Hosts telemetry_emit()'s ring-buffer append out of the fixed bank
 * (which sits hard against 0x8000, AGENTS.md 52.18).  The fixed wrapper
 * in telemetry.c stages the 5 event bytes into g_telemetry_emit_args and
 * dispatches here through the WRAM trampoline; this body appends the
 * stamped entry (seq++, frame capture, type + data) and advances the
 * bounded ring exactly like the old fixed body did.
 *
 * Self-contained (AGENTS.md 52.11.1): WRAM globals only (ring, head,
 * count, seq, frame pointer, arg staging), no fixed-bank calls, no
 * di/ei (the trampoline runs bodies with interrupts enabled, and the
 * 256 Hz timer ISR never touches telemetry state).  The harness reads
 * the ring while paused, so no locking is needed (same as before).
 * GameEvent layout is unchanged, so host parsing is untouched. */
void telemetry_emit_banked(void)
{
    GameEvent *ev = &g_telemetry_buffer[g_telemetry_head];
    ev->seq = g_telemetry_seq++;
    ev->frame = g_telemetry_frame_ptr ? *g_telemetry_frame_ptr : 0;
    ev->type = g_telemetry_emit_args[0];
    ev->data[0] = g_telemetry_emit_args[1];
    ev->data[1] = g_telemetry_emit_args[2];
    ev->data[2] = g_telemetry_emit_args[3];
    ev->data[3] = g_telemetry_emit_args[4];

    g_telemetry_head = (uint8_t)((g_telemetry_head + 1) & (MAX_TELEMETRY_EVENTS - 1));
    if (g_telemetry_count < MAX_TELEMETRY_EVENTS) {
        g_telemetry_count++;
    }
}
