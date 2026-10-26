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
    uint8_t i, x, y;

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
        for (i = 0; ; i++) {
            const SceneTerrainBlock *b = &tbl[i];
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
