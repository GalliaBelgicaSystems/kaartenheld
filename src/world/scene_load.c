#ifdef TEST_LEVELS
#pragma bank 4
#define SCENE_HOME_BANK 4
#else
#pragma bank 5
#define SCENE_HOME_BANK 5
#endif

#include "scene.h"
#include "banked.h"

/* Banked body of scene_load_tiles() (see scene.c).  Lives in the home
 * content bank (5 release, 4 TEST fixtures) and runs through the WRAM
 * banked-call trampoline so the terrain builder does not consume the
 * fixed-bank _CODE budget.  Self-contained: the scene tables
 * (g_scenes / exits / terrain blocks) are read directly while this bank
 * is mapped (no banked_copy, no fixed-bank calls).  Overflow scenes keep
 * their terrain_blocks in a roomier bank (SceneDefinition.terrain_bank);
 * the fixed wrapper dispatches a per-bank stamp body for those (see
 * scene.c).  It reads the World pointer from g_bk_ptr_a and the map id
 * from g_bk_byte_a. */

extern const SceneDefinition g_scenes[];

void scene_load_tiles_banked(void)
{
    World *w = (World *)g_bk_ptr_a;
    MapId map_id = (MapId)g_bk_byte_a;
    const SceneDefinition *def;
    const SceneTerrainBlock *tbl;
    uint8_t x, y;

    if (!w) return;
#ifdef TEST_LEVELS
    /* Frozen harness fixtures (bank 4): TEST MapIds (fixed block at
     * MAP_TEST_FIELD) index the fixture table from 0.  The engine must
     * not touch real scenes here (the test build contains no real
     * content). */
    if (map_id < MAP_TEST_FIELD || map_id >= (MapId)(MAP_TEST_FIELD + MAP_TEST_COUNT)) return;
    def = &g_scenes[map_id - MAP_TEST_FIELD];
#else
    if (map_id >= MAP_REAL_COUNT) return;
    def = &g_scenes[map_id];
#endif

    for (y = 0; y < w->height; y++) {
        uint8_t *row = w->map[y];
        for (x = 0; x < w->width; x++) {
            row[x] = (x == 0 || x == (uint8_t)(w->width - 1) || y == 0 || y == (uint8_t)(w->height - 1)) ? TILE_WALL : def->default_tile;
        }
    }

    /* Overflow terrain lives in another bank: skip it here. The fixed
     * wrapper (scene_load_tiles) dispatches a per-bank stamp body for
     * those scenes, which reads its own-bank arrays directly. Switching
     * banks here is impossible: this body executes from switchable ROM,
     * so selecting another bank would unmap its own instruction stream
     * mid-execution (the WRAM copy trampoline exists for exactly this
     * reason). The TEST build never skips (fixture terrain shares
     * bank 4). */
    tbl = def->terrain_blocks;
    if (tbl && def->terrain_bank == SCENE_HOME_BANK) {
        /* Pointer iteration, not an 8-bit index: a map may exceed 255
         * terrain blocks (s_forest_terrain is 284), and `uint8_t i`
         * would wrap 255 -> 0 before reaching the sentinel, hanging the
         * ROM on map load.  Matches the generated overflow stampers. */
        const SceneTerrainBlock *b = tbl;
        for ( ; ; b++) {
            uint8_t ex, ey;
            if (b->w == 0) break;
            ey = (uint8_t)(b->y + b->h);
            ex = (uint8_t)(b->x + b->w);
            if (ey > w->height) ey = w->height;
            if (ex > w->width) ex = w->width;
            for (y = b->y; y < ey; y++) {
                uint8_t *row = w->map[y];
                for (x = b->x; x < ex; x++) {
                    row[x] = b->tile;
                }
            }
        }
    }
}

/* Banked body of scene_exit_check() (see scene.h).  Same bank-5/4
 * residency as the scene tables it reads, so the exit rows are plain
 * same-bank ROM reads (never banked_copy: switching banks mid-body
 * would unmap the running body).  In: g_bk_ptr_a = World* (WRAM, always
 * mapped), g_bk_ptr_b = SceneDefinition* (the WRAM scratch from
 * scene_definition_for_map, likewise mapped), g_bk_byte_a/b = gate
 * tile; out: g_bk_byte_c = hit flag.  On a hit the EXIT outcome is
 * staged straight through the World pointer (the stepped gate tile
 * stays the move target; the far destination goes to move_exit_*).
 * Self-contained: no fixed-bank calls. */
void scene_exit_check_banked(void)
{
    World *w = (World *)g_bk_ptr_a;
    const SceneDefinition *def = (const SceneDefinition *)g_bk_ptr_b;
    uint8_t x = g_bk_byte_a;
    uint8_t y = g_bk_byte_b;
    uint8_t i;

    g_bk_byte_c = 0;
    if (!w || !def) return;
    for (i = 0; i < def->exit_count; i++) {
        const SceneExit *e = &def->exits[i];
        if (e->gate_x == x && e->gate_y == y) {
            w->move_outcome = MOVE_OUTCOME_EXIT;
            w->move_param = e->target_scene;
            w->move_target_x = x;
            w->move_target_y = y;
            w->move_exit_x = e->spawn_x;
            w->move_exit_y = e->spawn_y;
            g_bk_byte_c = 1;
            return;
        }
    }
}

/* Banked body of scene_spawn() (see scene.c).  Same bank-5 residency as
 * scene_load_tiles_banked: reads the spawn compiled from the level JSON's
 * player.spawn directly out of g_scenes, so no fixed-bank code ever touches
 * the scene table for this (the fixed-bank _CODE budget sits hard against
 * 0x8000; see AGENTS.md 52.18/55.5).  Map id in g_bk_byte_a; spawn
 * (x, y, facing) out via g_bk_byte_b/c/d.  Out-of-range maps yield the
 * historic (4,4)/DOWN fallback.  Self-contained: no fixed-bank calls. */
void scene_spawn_banked(void)
{
    MapId map_id = (MapId)g_bk_byte_a;
    const SceneDefinition *def;

#ifdef TEST_LEVELS
    if (map_id < MAP_TEST_FIELD || map_id >= (MapId)(MAP_TEST_FIELD + MAP_TEST_COUNT)) {
        g_bk_byte_b = 4;
        g_bk_byte_c = 4;
        g_bk_byte_d = (uint8_t)DIRECTION_DOWN;
        return;
    }
    def = &g_scenes[map_id - MAP_TEST_FIELD];
#else
    if (map_id >= MAP_REAL_COUNT) {
        g_bk_byte_b = 4;
        g_bk_byte_c = 4;
        g_bk_byte_d = (uint8_t)DIRECTION_DOWN;
        return;
    }
    def = &g_scenes[map_id];
#endif
    g_bk_byte_b = def->spawn_x;
    g_bk_byte_c = def->spawn_y;
    g_bk_byte_d = def->spawn_facing;
}

/* Banked body of scene_entry() (see scene.h).  Same bank-5/4 residency
 * as scene_spawn_banked: reads the edge entries compiled from the level
 * JSON's player.entry directly out of g_scenes, so no fixed-bank code
 * ever touches the scene table for this (the fixed-bank _CODE budget
 * sits hard against 0x8000; see AGENTS.md 52.18/55.5).  Map id in
 * g_bk_byte_a, crossed edge (NbrEdge: the side left through) in
 * g_bk_byte_b; entry (x, y, facing) out via g_bk_byte_b/c/d.  The
 * entered side is opposite the crossed edge: leaving north arrives from
 * the south, leaving east arrives from the west, etc.  Absent sides
 * (entry facing == SCENE_ENTRY_ABSENT) and out-of-range maps yield the
 * main spawn (or the historic (4,4)/DOWN fallback).  Self-contained: no
 * fixed-bank calls. */
void scene_entry_banked(void)
{
    MapId map_id = (MapId)g_bk_byte_a;
    uint8_t crossed = g_bk_byte_b;
    const SceneDefinition *def;
    uint8_t ex;
    uint8_t ey;
    uint8_t ef;

#ifdef TEST_LEVELS
    if (map_id < MAP_TEST_FIELD || map_id >= (MapId)(MAP_TEST_FIELD + MAP_TEST_COUNT)) {
        g_bk_byte_b = 4;
        g_bk_byte_c = 4;
        g_bk_byte_d = (uint8_t)DIRECTION_DOWN;
        return;
    }
    def = &g_scenes[map_id - MAP_TEST_FIELD];
#else
    if (map_id >= MAP_REAL_COUNT) {
        g_bk_byte_b = 4;
        g_bk_byte_c = 4;
        g_bk_byte_d = (uint8_t)DIRECTION_DOWN;
        return;
    }
    def = &g_scenes[map_id];
#endif
    if (crossed == (uint8_t)NBR_N) {
        ex = def->entry_s_x;
        ey = def->entry_s_y;
        ef = def->entry_s_facing;
    } else if (crossed == (uint8_t)NBR_S) {
        ex = def->entry_n_x;
        ey = def->entry_n_y;
        ef = def->entry_n_facing;
    } else if (crossed == (uint8_t)NBR_E) {
        ex = def->entry_w_x;
        ey = def->entry_w_y;
        ef = def->entry_w_facing;
    } else {
        ex = def->entry_e_x;
        ey = def->entry_e_y;
        ef = def->entry_e_facing;
    }
    if (ef == SCENE_ENTRY_ABSENT) {
        ex = def->spawn_x;
        ey = def->spawn_y;
        ef = def->spawn_facing;
    }
    g_bk_byte_b = ex;
    g_bk_byte_c = ey;
    g_bk_byte_d = ef;
}
