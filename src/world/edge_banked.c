#pragma bank 2

#include "world.h"
#include "banked.h"
#include "tile_walk.h"

/* Bank-2 movement body (see world_try_begin_move).
 * Bank 2 has headroom in both builds; bank 3 (release) does not.
 * Pure WRAM/staged arithmetic: no scene-table access (neighbor bytes
 * arrive staged), no fixed-bank calls. */

static uint8_t edge_walkable(const World *w, uint8_t x, uint8_t y)
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
     * fixed side reads them only for EXIT (staged there) and EDGE.
     * Reset the outcome BEFORE the null-def early return: a stale
     * NORMAL/EXIT/ENCOUNTER would otherwise let the fixed side commit a
     * phantom move. */
    if (!w) return;
    w->move_outcome = MOVE_OUTCOME_NONE;
    if (!nbr || !edge_walkable(w, tx, ty)) return;
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

/* Edge crossings land on the neighbor's spawn (player.spawn, per-level
 * Spawn-layer property): no row mirroring, so linked levels need no edge
 * alignment. The entry spawn lives in SceneDefinition (scene_spawn_banked);
 * this file owns only the crossing predicate above. */
