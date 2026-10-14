#pragma bank 3

#include "world.h"
#include "banked.h"

/* Bank-3 body of the world pixel-interpolation helpers (see world.c).
 * Pure arithmetic over WRAM structs: reads only the staged pointer and
 * writes the shared g_px_result byte (world.c).  Self-contained. */

extern uint8_t g_px_result;

static uint8_t px_calc_interp(uint8_t base, uint8_t target, uint8_t progress)
{
    uint8_t px = (uint8_t)(base << 3);
    if (target > base) return (uint8_t)(px + progress);
    if (target < base) return (uint8_t)(px - progress);
    return px;
}

void world_px_banked(void)
{
    const World *w = (const World *)g_bk_ptr_a;
    uint8_t variant = g_bk_byte_a;

    if (!w) { g_px_result = 0; return; }
    if (variant == 0) {
        /* player X */
        g_px_result = (w->move_state == MOVE_STATE_MOVING) ?
            px_calc_interp(w->player.position.x, w->move_target_x,
                           w->move_progress) :
            (uint8_t)(w->player.position.x << 3);
    } else {
        /* player Y */
        g_px_result = (w->move_state == MOVE_STATE_MOVING) ?
            px_calc_interp(w->player.position.y, w->move_target_y,
                           w->move_progress) :
            (uint8_t)(w->player.position.y << 3);
    }
}
