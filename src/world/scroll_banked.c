#pragma bank 12

#include "world.h"
#include "banked.h"

/* Bank-12 overworld scroll (cold-per-frame: once per overworld frame).
 * Moved out of fixed to free _HOME headroom for the diagonal wall-hug
 * slide (AGENTS.md 52.18). Inlines the player-pixel interpolation
 * (tile*8 + walk progress) so it never calls fixed-bank code while
 * bank 12 is mapped (52.11.1). Pure WRAM reads/writes. */
void world_update_scroll_banked(void)
{
    World *w = (World *)g_bk_ptr_a;
    uint8_t px, py, max_x, max_y;
    uint8_t base, target, prog;
    if (!w) return;
    /* Player pixel X. */
    base = w->player.position.x;
    target = w->move_target_x;
    prog = w->move_progress;
    px = (uint8_t)(base << 3);
    if (w->move_state != MOVE_STATE_IDLE) {
        if (target > base) px = (uint8_t)(px + prog);
        else if (target < base) px = (uint8_t)(px - prog);
    }
    /* Player pixel Y. */
    base = w->player.position.y;
    target = w->move_target_y;
    prog = w->move_progress;
    py = (uint8_t)(base << 3);
    if (w->move_state != MOVE_STATE_IDLE) {
        if (target > base) py = (uint8_t)(py + prog);
        else if (target < base) py = (uint8_t)(py - prog);
    }
    px = (px < 80) ? 0 : (uint8_t)(px - 80);
    py = (py < 72) ? 0 : (uint8_t)(py - 72);
    max_x = (w->width > WORLD_VIEW_W) ? (uint8_t)((w->width - WORLD_VIEW_W) << 3) : 0;
    max_y = (w->height > WORLD_VIEW_H) ? (uint8_t)((w->height - WORLD_VIEW_H) << 3) : 0;
    if (px > max_x) px = max_x;
    if (py > max_y) py = max_y;
    w->camera_px_x = px;
    w->camera_px_y = py;
    w->scroll_x = (uint8_t)(px >> 3);
    w->scroll_y = (uint8_t)(py >> 3);
}
