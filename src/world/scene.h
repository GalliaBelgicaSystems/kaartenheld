#ifndef SCENE_H
#define SCENE_H

#include <stdint.h>
#include "screen.h"
#include "world.h"
#include "audio.h"

/* A generic exit from a scene.  The exit is an invisible trigger: it keeps
 * whatever terrain art is painted at (gate_x, gate_y) and never stamps its
 * own tile.  Stepping onto it moves the player to target_scene at
 * (spawn_x, spawn_y).  tile_char is the rendered glyph ('>' forward, '<'
 * back, etc.) for the semantic buffer.  A trigger on a linked border cell
 * wins over the neighbors edge rule. */
typedef struct {
    uint8_t gate_x;
    uint8_t gate_y;
    uint8_t spawn_x;
    uint8_t spawn_y;
    SceneId target_scene;
    char tile_char;
} SceneExit;

typedef struct {
    uint8_t x;
    uint8_t y;
    uint8_t w;
    uint8_t h;
    uint8_t tile;
} SceneTerrainBlock;

/* WorldTilesetKind lives in world.h (see above); used here for the
 * scene definition's tileset field. */

/* Data-driven scene definition.  Terrain generation is driven by banked
 * terrain_blocks; exit placement is applied automatically from the exits table.
 * width/height set the scene's tile bounds (<= WORLD_WIDTH/HEIGHT); the
 * overworld camera clamps its view window to them.  spawn_x/spawn_y/spawn_facing
 * is the player start compiled from the level JSON's player.spawn (the single
 * source of truth; game_new_game reads the FIELD row). */
typedef struct {
    MapId map_id;
    MusicTrack music;
    uint8_t width;
    uint8_t height;
    const SceneExit *exits;
    uint8_t exit_count;
    WorldTilesetKind tileset;
    const SceneTerrainBlock *terrain_blocks;
    uint8_t spawn_x;
    uint8_t spawn_y;
    uint8_t spawn_facing; /* Direction */
    uint8_t default_tile; /* TileType for unpainted cells (default_walkable) */
    /* Whole-edge map links (the "ocean"): stepping onto a non-wall cell of
     * a linked border crosses to the neighbor scene with a mirrored entry
     * spawn.  MAP_NONE (0xFF) = no link on that edge.  Appended at the END
     * of the struct so existing field offsets never shift. */
    uint8_t neighbor_n; /* MapId or MAP_NONE */
    uint8_t neighbor_s; /* MapId or MAP_NONE */
    uint8_t neighbor_e; /* MapId or MAP_NONE */
    uint8_t neighbor_w; /* MapId or MAP_NONE */
    /* ROM bank holding this scene's terrain_blocks array.  Normally the
     * home content bank (5 release, 4 TEST fixtures); overflow scenes keep
     * their terrain in a roomier bank, stamped by a per-bank body
     * dispatched from scene_load_tiles() (see scene_load.c).  Appended at
     * the END so existing field offsets never shift (rebuild everything
     * after touching this header: AGENTS.md 52.2). */
    uint8_t terrain_bank;
} SceneDefinition;

/* Look up a scene definition by its map id.
 * NOTE: Returns a pointer to an internal static scratch buffer (WRAM),
 * populated on each call via banked_copy() from ROM Bank 2. The returned
 * pointer is valid only until the next call to scene_definition_for_map().
 * Do not retain this pointer across nested or subsequent scene lookups. */
const SceneDefinition *scene_definition_for_map(MapId map_id);

/* Get the tileset kind for a given map id. */
WorldTilesetKind scene_get_tileset(MapId map_id);

/* Find the exit whose gate tile sits at (x, y), or NULL. */
const SceneExit *scene_exit_at(const SceneDefinition *def, uint8_t x, uint8_t y);

/* Populate a World struct's map[][] with boundary walls, terrain features,
 * and exit gates for map_id.  Uses map bounds from the scene definition. */
void scene_load_tiles(World *w, MapId map_id);

/* Banked no-arg body (ROM bank 2) dispatched by scene_load_tiles(). */
void scene_load_tiles_banked(void);

/* Banked no-arg body (ROM bank 5) dispatched by scene_spawn(). */
void scene_spawn_banked(void);

/* Read a scene's compiled player spawn (x, y, facing) into the staging
 * globals g_bk_byte_b/c/d.  Thin fixed-bank wrapper around scene_spawn_banked
 * (branch-free by design: the fallback for bad map ids lives in the banked
 * body, keeping the fixed-bank _CODE budget intact). */
void scene_spawn(MapId map_id);

#define scene_id_to_map(scene) ((MapId)(scene))
#define map_to_scene_id(map) ((SceneId)(map))

#endif /* SCENE_H */
