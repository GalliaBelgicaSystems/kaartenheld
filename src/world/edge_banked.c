#pragma bank 2

#include "world.h"
#include "banked.h"
#include "tile_walk.h"

/* Bank-2 movement bodies (see world_try_begin_move/world_update_move).
 * Bank 2 has headroom in both builds; bank 3 (release) does not.
 * Pure WRAM/staged arithmetic: no scene-table access (neighbor bytes
 * arrive staged, dims arrive staged), no fixed-bank calls. */

static uint8_t edge_walkable(const World *w, uint8_t x, uint8_t y)
{
    uint8_t tile;
    if (!w || x >= w->width || y >= w->height) return 0;
    tile = w->map[y][x];
    if (tile == TILE_FLOOR) return 1;
    if (tile >= TILE_DESOLATE_FLOOR_00 && tile <= TILE_DESOLATE_FLOOR_03) return 1;
    if (tile == TILE_DESOLATE_FLOOR_PLAIN || tile == TILE_DESOLATE_STAIRCASE) return 1;
    if (tile_landscape_walkable(tile)) return 1;
    return 0;
}

/* Whole-edge predicate + walkability. In: g_bk_ptr_a = World* (written
 * directly: outcome/param/dir/target), g_bk_ptr_b = 4 neighbor bytes
 * (n/s/e/w, MAP_NONE = no link), g_bk_byte_a/b = target x/y.
 * MOVE_OUTCOME_NONE = blocked. A linked edge cell must also be
 * walkable (walls stay walls); point-exit triggers are handled
 * fixed-side and take precedence. Writing through the staged World
 * pointer follows the banked-call contract (AGENTS.md 52.11.1). */
void world_gate_check_banked(void)
{
    World *w = (World *)g_bk_ptr_a;
    const uint8_t *nbr = (const uint8_t *)g_bk_ptr_b;
    uint8_t tx = g_bk_byte_a;
    uint8_t ty = g_bk_byte_b;
    uint8_t i;
    uint8_t hmax;
    uint8_t wmax;

    /* move_param/dir keep stale values on NORMAL/BLOCKED paths; the
     * fixed side reads them only for EXIT (staged there) and EDGE. */
    if (!nbr) return;
    w->move_outcome = MOVE_OUTCOME_NONE;
    if (!w || !edge_walkable(w, tx, ty)) return;
    w->move_outcome = MOVE_OUTCOME_NORMAL;
    w->move_target_x = tx;
    w->move_target_y = ty;
    /* NbrEdge order matches the neighbor-byte order (n,s,e,w), so the
     * index doubles as the staged direction. E/W order is irrelevant:
     * tx == 0 and tx == wmax are mutually exclusive (w > 1), as are
     * the ty tests, so corners resolve identically to the old explicit
     * chain (and to the planner's N/S/W/E priority). */
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

static uint8_t edge_clamp(uint8_t v, uint8_t lo, uint8_t hi)
{
    if (hi < lo) return lo;
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

void world_edge_spawn_banked(void)
{
    uint8_t cross = g_bk_byte_a;
    uint8_t nw = g_bk_byte_b;
    uint8_t nh = g_bk_byte_c;
    uint8_t dir = g_bk_byte_d;
    uint8_t xhi;
    uint8_t yhi;
    uint8_t sx;
    uint8_t sy;

    /* dir comes from the staged move (0-3: the predicate body wrote it
     * on the EDGE path that led here); nw/nh are real map dims from the
     * validated neighbor def. No degenerate inputs in practice. */
    xhi = (nw >= 2) ? (uint8_t)(nw - 2) : 0;
    yhi = (nh >= 2) ? (uint8_t)(nh - 2) : 0;
    /* Entry is one cell inside the OPPOSITE edge: leaving north enters
     * through the neighbor's south, leaving east through its west, etc. */
    if (dir == (uint8_t)NBR_N) {
        sx = edge_clamp(cross, 1, xhi);
        sy = yhi;
    } else if (dir == (uint8_t)NBR_S) {
        sx = edge_clamp(cross, 1, xhi);
        sy = 1;
    } else if (dir == (uint8_t)NBR_E) {
        sx = 1;
        sy = edge_clamp(cross, 1, yhi);
    } else {
        sx = xhi;
        sy = edge_clamp(cross, 1, yhi);
    }
    g_bk_byte_a = sx;
    g_bk_byte_b = sy;
}
