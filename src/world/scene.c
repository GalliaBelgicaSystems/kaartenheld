#include "scene.h"
#include "actor.h"
#include "banked.h"

/* ── Scene data location ───────────────────────────────────────────
 * Real content (levels/) compiles into ROM bank 5; the frozen harness
 * fixtures (TEST_LEVELS, debug build only) compile into bank 4.  The
 * engine only ever reads scene data through these WRAM scratch copies,
 * so the ROM bank is a build-variant constant, not gameplay state. */
#ifdef TEST_LEVELS
#define SCENE_CONTENT_BANK 4
#else
#define SCENE_CONTENT_BANK 5
#endif

extern const SceneExit g_all_exits[];
extern const SceneDefinition g_scenes[];
#ifndef TEST_LEVELS
/* Overflow terrain stamp bodies (one per bank; see scenes_terrain_b*.c,
 * emitted by compile.py from OVERFLOW_TERRAIN_BANK). Keep the bank list
 * in sync with that map. */
extern void terrain_stamp_b6_banked(void);
extern void terrain_stamp_b7_banked(void);
#endif

static SceneDefinition s_scene_scratch;
static SceneExit s_exit_scratch;

const SceneDefinition *scene_definition_for_map(MapId map_id)
{
#ifdef TEST_LEVELS
    if (map_id < MAP_TEST_FIELD || map_id >= (MapId)(MAP_TEST_FIELD + MAP_TEST_COUNT)) return NULL;
    banked_copy(SCENE_CONTENT_BANK, &s_scene_scratch,
                &g_scenes[map_id - MAP_TEST_FIELD], sizeof(SceneDefinition));
#else
    if (map_id >= MAP_REAL_COUNT) return NULL;
    banked_copy(SCENE_CONTENT_BANK, &s_scene_scratch,
                &g_scenes[map_id], sizeof(SceneDefinition));
#endif
    return &s_scene_scratch;
}

static MapId s_cached_tileset_map = (MapId)0xFF;
static WorldTilesetKind s_cached_tileset_kind = WORLD_TILESET_FOREST;

WorldTilesetKind scene_get_tileset(MapId map_id)
{
    const SceneDefinition *def;
    if (map_id == s_cached_tileset_map) {
        return s_cached_tileset_kind;
    }
    s_cached_tileset_map = map_id;
    def = scene_definition_for_map(map_id);
    s_cached_tileset_kind = def ? def->tileset : WORLD_TILESET_FOREST;
    return s_cached_tileset_kind;
}

const SceneExit *scene_exit_at(const SceneDefinition *def, uint8_t x, uint8_t y)
{
    uint8_t i;
    if (!def) return NULL;
    for (i = 0; i < def->exit_count; i++) {
        banked_copy(SCENE_CONTENT_BANK, &s_exit_scratch, &def->exits[i], sizeof(SceneExit));
        if (s_exit_scratch.gate_x == x && s_exit_scratch.gate_y == y) {
            return &s_exit_scratch;
        }
    }
    return NULL;
}

/* scene_load_tiles() is a fixed-bank wrapper around the banked body in
 * src/world/scene_load.c (ROM bank 5).  The wrapper stages the World pointer
 * and map id into the _DATA globals (banked.c) and runs the banked no-arg
 * function through the WRAM banked-call trampoline (crt0.s). */
void scene_load_tiles(World *w, MapId map_id)
{
    g_bk_call_bank = SCENE_CONTENT_BANK;
    g_bk_call_target = (uint16_t)&scene_load_tiles_banked;
    g_bk_ptr_a = (void *)w;
    g_bk_byte_a = (uint8_t)map_id;
    banked_call_run();
#ifndef TEST_LEVELS
    /* Overflow scenes keep terrain outside the home bank: the body above
     * prefills and skips, then a per-bank stamp body reads its own-bank
     * arrays directly (no bank switching anywhere — switching from
     * switchable ROM would unmap the switcher mid-execution). Fixture
     * terrain always shares bank 4, so the debug build omits this. */
    {
        const SceneDefinition *d = scene_definition_for_map(map_id);
        if (d && d->terrain_blocks && d->terrain_bank != SCENE_CONTENT_BANK) {
            g_bk_ptr_a = (void *)w;
            g_bk_ptr_b = (void *)d->terrain_blocks;
            g_bk_call_bank = d->terrain_bank;
            if (d->terrain_bank == 6) {
                g_bk_call_target = (uint16_t)&terrain_stamp_b6_banked;
                banked_call_run();
            } else if (d->terrain_bank == 7) {
                g_bk_call_target = (uint16_t)&terrain_stamp_b7_banked;
                banked_call_run();
            }
            /* Unknown overflow bank: leave prefilled floor. Never a hang;
             * extending OVERFLOW_TERRAIN_BANK requires extending this. */
        }
    }
#endif
}

/* scene_spawn() is a fixed-bank wrapper around the banked body in
 * src/world/scene_load.c (ROM bank 5).  The wrapper stages the map id into
 * the _DATA globals (banked.c) and runs the banked no-arg function through
 * the WRAM banked-call trampoline (crt0.s); the spawn comes back in
 * g_bk_byte_b/c/d. */
void scene_spawn(MapId map_id)
{
    g_bk_call_bank = SCENE_CONTENT_BANK;
    g_bk_call_target = (uint16_t)&scene_spawn_banked;
    g_bk_byte_a = (uint8_t)map_id;
    banked_call_run();
}
