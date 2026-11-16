#pragma bank 12

#include <gb/gb.h>
#include "world.h"
#include "actor.h"
#include "banked.h"
#include "input.h"
#include "tile_walk.h"

/* Combined diagonal gate check (bank 12: bank 2, bank 5, and bank 11
 * are tight, fixed is over budget -- AGENTS.md 52.18). Replaces
 * the bank-2 gate check + wall-hug fallback for diagonal moves;
 * cardinals keep the bank-2 body so their cost is unchanged.
 *
 * In: ptr_a = World*, ptr_b = 4 neighbor bytes, byte_a/b = target x/y,
 *     byte_d = nonzero if scene_exit_check matched (tx, ty).
 * Out: writes move outcome/param/dir/target (plus facing on a
 *      horizontal slide); byte_c = 1 on thumb-roll suppress (caller
 *      restores facing and returns MOVE_RESULT_NONE), 2 on wall-hug
 *      slide onto an orthogonal neighbor (caller re-checks point exits
 *      at the slid tile), 0 otherwise.
 *
 * Corner + wall-hug rule: both orthogonal stepping stones must be
 * walkable AND actor-free (hostile incl. in-flight patrol target, or
 * static) for a true diagonal step; otherwise slide along the free
 * actor-free orthogonal (vertical first, then horizontal). Pure
 * WRAM/banked-table reads, no fixed calls. */

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

static uint8_t d_orth_free(const World *w, uint8_t x, uint8_t y)
{
    uint8_t k;
    if (!d_edge_walkable(w, x, y)) return 0;
    for (k = 0; k < MAX_WORLD_ACTORS; k++) {
        const WorldActorRuntime *a = &w->actors[k];
        if (!a->active) continue;
        if (a->x == x && a->y == y) return 0;
        if (a->move_state &&
            a->move_target_x == x && a->move_target_y == y) return 0;
    }
    for (k = 0; k < g_static_actor_count && k < MAX_STATIC_ACTORS; k++) {
        if (g_static_actors[k].x == x &&
            g_static_actors[k].y == y) return 0;
    }
    return 1;
}

void diagonal_gate_check_banked(void)
{
    World *w = (World *)g_bk_ptr_a;
    const uint8_t *nbr = (const uint8_t *)g_bk_ptr_b;
    uint8_t tx = g_bk_byte_a;
    uint8_t ty = g_bk_byte_b;
    uint8_t diag_exit = g_bk_byte_d;
    uint8_t px;
    uint8_t py;
    uint8_t i;
    uint8_t hmax;
    uint8_t wmax;
    uint8_t want;
    uint8_t prev_held;
    uint8_t v_free;
    uint8_t h_free;
    uint8_t rx;
    uint8_t ry;
    g_bk_byte_c = 0;
    if (!w) return;
    w->move_outcome = MOVE_OUTCOME_NONE;
    px = w->player.position.x;
    py = w->player.position.y;
    if (tx == px || ty == py) return;
    /* Thumb-roll guard (real hardware): rolling UP->LEFT brushes a frame
     * with both D-pad bits set. A diagonal starts only if both axes were
     * already held last frame (sustained hold) or neither was (same-frame
     * press, including the harness press_buttons mask). A fresh second
     * axis alone suppresses this move (no diagonal, no wall-hug slide);
     * the next held frame qualifies, so a deliberate diagonal costs at
     * most one frame while a roll keeps its cardinal direction. J_ bits
     * equal the input_held bits by the input.c layout check. Signed
     * deltas handle px==0 / py==0 wraparound safely. */
    want = 0;
    want |= ((int8_t)(tx - px) > 0) ? (uint8_t)J_RIGHT : (uint8_t)J_LEFT;
    want |= ((int8_t)(ty - py) > 0) ? (uint8_t)J_DOWN : (uint8_t)J_UP;
    prev_held = (uint8_t)(g_inp_prev & want);
    if (prev_held != want && prev_held != 0) {
        g_bk_byte_c = 1;
        return;
    }
    if (!nbr) return;
    v_free = d_orth_free(w, px, ty);
    h_free = d_orth_free(w, tx, py);
    if (v_free && h_free && (diag_exit || d_edge_walkable(w, tx, ty))) {
        if (diag_exit) {
            w->move_outcome = MOVE_OUTCOME_EXIT;
            w->move_target_x = tx;
            w->move_target_y = ty;
            return;
        }
        rx = tx;
        ry = ty;
    } else if (v_free) {
        rx = px;
        ry = ty;
        g_bk_byte_c = 2;
    } else if (h_free) {
        rx = tx;
        ry = py;
        w->player.facing = ((int8_t)(tx - px) > 0)
            ? DIRECTION_RIGHT : DIRECTION_LEFT;
        g_bk_byte_c = 2;
    } else {
        return;
    }
    w->move_outcome = MOVE_OUTCOME_NORMAL;
    w->move_target_x = rx;
    w->move_target_y = ry;
    /* NbrEdge order matches the neighbor-byte order (n,s,e,w) with
     * first-match break, mirroring the bank-2 body: a corner tile linked
     * on two sides resolves N first, dropping the other link. */
    hmax = (uint8_t)(w->height - 1);
    wmax = (uint8_t)(w->width - 1);
    for (i = 0; i < 4; i++) {
        uint8_t on;
        if (i == 0) on = (ry == 0);
        else if (i == 1) on = (ry == hmax);
        else if (i == 2) on = (rx == wmax);
        else on = (rx == 0);
        if (on && nbr[i] != MAP_NONE) {
            w->move_outcome = MOVE_OUTCOME_EDGE;
            w->move_param = nbr[i];
            w->move_dir = i;
            break;
        }
    }
}
