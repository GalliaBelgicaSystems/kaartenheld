#include "telemetry.h"
#include "audio.h"
#include "screen.h"
#include "actor.h"
#include "banked.h"
uint8_t g_snap_buf[SNAPSHOT_TOTAL_SIZE];
uint8_t g_state_snap_buf[STATE_SNAP_TOTAL_SIZE];
GameEvent g_telemetry_buffer[MAX_TELEMETRY_EVENTS];
uint8_t g_telemetry_count;  /* BSS zero (AGENTS.md 52.27) */
uint8_t g_telemetry_head;  /* BSS zero (AGENTS.md 52.27) */
/* Event-arg staging + sequence state for the banked append body
 * (telemetry_emit_banked.c, ROM bank 2): WRAM, always mapped, BSS zero
 * (AGENTS.md 52.27).  Names are part of NO host contract (the host reads
 * only the ring + count + head); the GameEvent layout is unchanged. */
uint8_t g_telemetry_emit_args[5];
uint32_t g_telemetry_seq;
const uint32_t *g_telemetry_frame_ptr;

void telemetry_init(void)
{
    g_telemetry_count = 0;
    g_telemetry_head = 0;
    g_telemetry_seq = 0;
    g_telemetry_frame_ptr = NULL;
}

void telemetry_emit(uint8_t type, uint8_t d0, uint8_t d1, uint8_t d2, uint8_t d3)
{
    /* Thin fixed-bank wrapper: stage the 5 event bytes and dispatch the
     * bank-2 append body through the WRAM trampoline (fixed _CODE
     * budget, AGENTS.md 52.18).  Synchronous: the body appends before
     * this returns, so back-to-back emits keep ring order. */
    g_telemetry_emit_args[0] = type;
    g_telemetry_emit_args[1] = d0;
    g_telemetry_emit_args[2] = d1;
    g_telemetry_emit_args[3] = d2;
    g_telemetry_emit_args[4] = d3;
    g_bk_call_bank = 2;
    g_bk_call_target = (uint16_t)&telemetry_emit_banked;
    banked_call_run();
}



void telemetry_set_frame_ptr(const uint32_t *frame_ptr)
{
    g_telemetry_frame_ptr = frame_ptr;
}

#ifdef DEBUG_BUILD
void debug_snapshot(void)
{
    /* Body runs banked (src/debug/snapshot_banked.c, ROM bank 2) through
     * the WRAM trampoline; no staging args are needed. */
    g_bk_call_bank = 2;
    g_bk_call_target = (uint16_t)&debug_snapshot_banked;
    banked_call_run();
}

void debug_state_snapshot(void)
{
    /* The ~460-byte builder runs banked (src/debug/telemetry_snap.c, ROM
     * bank 2) through the WRAM banked-call trampoline so it does not consume
     * the fixed-bank _CODE budget.  It reads g_game / g_state_snap_buf
     * directly, so no staging args are needed. */
    g_bk_call_bank = 2;
    g_bk_call_target = (uint16_t)&debug_state_snapshot_banked;
    banked_call_run();
}
#endif
