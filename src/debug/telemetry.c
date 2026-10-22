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
static uint32_t event_seq = 0;
static const uint32_t *telemetry_frame_ptr;  /* BSS (52.27) */

void telemetry_init(void)
{
    g_telemetry_count = 0;
    g_telemetry_head = 0;
    event_seq = 0;
    telemetry_frame_ptr = NULL;
}

void telemetry_emit(uint8_t type, uint8_t d0, uint8_t d1, uint8_t d2, uint8_t d3)
{
    GameEvent *ev = &g_telemetry_buffer[g_telemetry_head];
    ev->seq = event_seq++;
    ev->frame = telemetry_frame_ptr ? *telemetry_frame_ptr : 0;
    ev->type = (uint8_t)type;
    ev->data[0] = d0;
    ev->data[1] = d1;
    ev->data[2] = d2;
    ev->data[3] = d3;

    g_telemetry_head = (uint8_t)((g_telemetry_head + 1) & (MAX_TELEMETRY_EVENTS - 1));
    if (g_telemetry_count < MAX_TELEMETRY_EVENTS) {
        g_telemetry_count++;
    }
}



void telemetry_set_frame_ptr(const uint32_t *frame_ptr)
{
    telemetry_frame_ptr = frame_ptr;
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
