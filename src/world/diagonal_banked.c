#pragma bank 11

#include <gb/gb.h>
#include "world.h"
#include "actor.h"
#include "banked.h"
#include "input.h"
#include "tile_walk.h"

/* Combined diagonal gate check (bank 11: bank 2 is full, bank 5 is
 * full in release, fixed is over budget -- AGENTS.md 52.18). Replaces
 * the bank-2 gate check for diagonal moves only; cardinals keep the
 * bank-2 body so their cost is unchanged. In/Out mirrors
 * world_gate_check_banked (ptr_a = World*, ptr_b = 4 neighbor bytes,
 * byte_a/b = target x/y; writes outcome/param/dir/target), plus the
 * corner rule: both orthogonal stepping stones must be walkable AND
 * actor-free (hostile incl. in-flight patrol target, or static),
 * else NONE. Pure WRAM/banked-table reads, no fixed calls. */

/* Mirrors edge_walkable in src/world/edge_banked.c (bank 2), which
 * inlines the same generated table. A tile class added to one and not
 * the other makes cardinal and diagonal movement silently disagree --
 * keep them in sync. */
static uint8_t d_edge_walkable(const World *w, uint8_t x, uint8_t y)
{
    uint8_t tile;
    if (!w || x >= w->width || y >= w->height) return 0;
    tile = w->map[y][x];
    if (tile == TILE_FLOOR) return 1;
    if (tile >= TILE_DESOLATE_FLOOR_00 && tile <= TILE_DESOLATE_FLOOR_03) return 1;
    if (tile == TILE_DESOLATE_FLOOR_PLAIN) return 1;
    if (tile_landscape_walkable(tile)) return 1;
    return 0;
}

void diagonal_gate_check_banked(void)
{
    World *w = (World *)g_bk_ptr_a;
    const uint8_t *nbr = (const uint8_t *)g_bk_ptr_b;
    uint8_t tx = g_bk_byte_a;
    uint8_t ty = g_bk_byte_b;
    uint8_t px;
    uint8_t py;
    uint8_t k;
    uint8_t i;
    uint8_t hmax;
    uint8_t wmax;
    uint8_t want;
    uint8_t prev_held;
    if (!w) return;
    w->move_outcome = MOVE_OUTCOME_NONE;
    px = w->player.position.x;
    py = w->player.position.y;
    if (tx == px || ty == py) return;
    /* Thumb-roll guard (real hardware): rolling UP->LEFT brushes a frame
     * with both D-pad bits set. A diagonal starts only if both axes were
     * already held last frame (sustained hold) or neither was (same-frame
     * press, including the harness press_buttons mask). A fresh second
     * axis alone blocks this start; the next held frame qualifies, so a
     * deliberate diagonal costs at most one frame while a roll keeps its
     * cardinal direction. J_ bits equal the input_held bits by the
     * input.c layout check. */
    want = 0;
    if (tx > px) want |= (uint8_t)J_RIGHT;
    else want |= (uint8_t)J_LEFT;
    if (ty > py) want |= (uint8_t)J_DOWN;
    else want |= (uint8_t)J_UP;
    prev_held = (uint8_t)(g_inp_prev & want);
    if (prev_held != want && prev_held != 0) return;
    if (!nbr || !d_edge_walkable(w, tx, ty)) return;
    /* Corner rule: orthogonals must be walkable and actor-free. */
    if (!d_edge_walkable(w, tx, py)) return;
    if (!d_edge_walkable(w, px, ty)) return;
    for (k = 0; k < MAX_WORLD_ACTORS; k++) {
        const WorldActorRuntime *a = &w->actors[k];
        if (!a->active) continue;
        if ((a->x == tx && a->y == py) || (a->x == px && a->y == ty)) return;
        if (a->move_state) {
            if ((a->move_target_x == tx && a->move_target_y == py) ||
                (a->move_target_x == px && a->move_target_y == ty)) return;
        }
    }
    for (k = 0; k < g_static_actor_count && k < MAX_STATIC_ACTORS; k++) {
        if ((g_static_actors[k].x == tx && g_static_actors[k].y == py) ||
            (g_static_actors[k].x == px && g_static_actors[k].y == ty)) return;
    }
    w->move_outcome = MOVE_OUTCOME_NORMAL;
    w->move_target_x = tx;
    w->move_target_y = ty;
    /* NbrEdge order matches the neighbor-byte order (n,s,e,w) with
     * first-match break, mirroring the bank-2 body: a corner tile linked
     * on two sides resolves N first, dropping the other link. */
    hmax = (uint8_t)(w->height - 1);
    wmax = (uint8_t)(w->width - 1);
    for (i = 0; i < 4; i++) {
        uint8_t on;
        if (i == 0) on = (ty == 0);
        else if (i == 1) on = (ty == hmax);
        else if (i == 2) on = (tx == wmax);
        else on = (tx == 0);
        if (on && nbr[i] != MAP_NONE) {
            w->move_outcome = MOVE_OUTCOME_EDGE;
            w->move_param = nbr[i];
            w->move_dir = i;
            break;
        }
    }
}
