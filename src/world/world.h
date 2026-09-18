#ifndef WORLD_H
#define WORLD_H

#include "entity.h"
#include "rpg/state.h"
#include <stdbool.h>

/* Tileset art families for scene rendering.  Defined here (rather than
 * scene.h) so World can carry its loaded kind without a circular include;
 * scene.h picks it up through this header. */
typedef enum {
    WORLD_TILESET_FOREST   = 2,
    WORLD_TILESET_VILLAGE  = 12,
    WORLD_TILESET_DESOLATE = 14,
    WORLD_TILESET_CASTLE   = 15
} WorldTilesetKind;

/* Hard caps for the tile buffer: a scene may be any size up to these.
 * The overworld camera windows a WORLD_VIEW_W x WORLD_VIEW_H view out of
 * the scene and scrolls it with the scroll offset; the 40-col cap
 * exercises the tilemap ring-buffer wrap (the 32x32 BG tilemap is 32
 * wide). */
#define WORLD_WIDTH  40
#define WORLD_HEIGHT 24

/* Overworld camera view window in tiles.  The camera (World.scroll_x/y)
 * keeps the player inside this window, scrolling as the player crosses its
 * edge and clamping at the scene bounds (world width/height). */
#define WORLD_VIEW_W 20
#define WORLD_VIEW_H 18

/* Maximum concurrent hostile actors in a scene (compile-time constant). */
#define MAX_WORLD_ACTORS 4
#define NO_ACTOR_INDEX   0xFF

/* Frames to walk a single tile (1 px/frame of an 8px tile).  Movement is
 * animated: a tile commit happens MOVE_FRAMES frames after the move starts,
 * at which point PLAYER_MOVED / exits / encounters are resolved. */
#define MOVE_FRAMES 8

typedef enum {
    MOVE_STATE_IDLE   = 0,
    MOVE_STATE_MOVING = 1
} MoveState;

/* What the commit of a started move must resolve.  Set by world_try_begin_move
 * from the target tile's contents, consumed by world_update_move. */
typedef enum {
    MOVE_OUTCOME_NONE      = 0,
    MOVE_OUTCOME_NORMAL    = 1,
    MOVE_OUTCOME_EXIT      = 2,
    MOVE_OUTCOME_ENCOUNTER = 3,
    MOVE_OUTCOME_EDGE      = 4
} MoveOutcome;

typedef enum {
    MOVE_RESULT_NONE        = 0,
    MOVE_RESULT_BLOCKED     = 1,
    MOVE_RESULT_MOVED       = 2,
    MOVE_RESULT_MAP_CHANGED = 3,
    MOVE_RESULT_ENCOUNTER   = 4
} WorldMoveResult;

/* MapId is a plain uint8_t; its VALUES come from the generated
 * src/world/scene_ids_generated.h (single source of truth:
 * levels/registry.json, emitted by tools/level_compiler/compile.py).
 * Humans add levels in the editor — never hand-edit ids here. */
typedef uint8_t MapId;
#include "scene_ids_generated.h"

/* A single generic exit tile type; the scene definition owns the
 * destination/spawn/visual of each exit. */
typedef enum {
    TILE_FLOOR              = 0,
    TILE_WALL               = 1,
    TILE_EXIT               = 2,
    TILE_BUILDING           = 3,
    TILE_STUMP_TL           = 4,
    TILE_STUMP_TR           = 5,
    TILE_STUMP_BL           = 6,
    TILE_STUMP_BR           = 7,
    /* Forest Landscape tiles: 48 tiles in sheet scan order (0..47),
     * mapping 1:1 to the 48-tile forest VRAM block (RPG_TILE_BASE_WORLD). */
    TILE_FOREST_00 = 8,
    TILE_FOREST_01 = 9,
    TILE_FOREST_02 = 10,
    TILE_FOREST_03 = 11,
    TILE_FOREST_04 = 12,
    TILE_FOREST_05 = 13,
    TILE_FOREST_06 = 14,
    TILE_FOREST_07 = 15,
    TILE_FOREST_08 = 16,
    TILE_FOREST_09 = 17,
    TILE_FOREST_10 = 18,
    TILE_FOREST_11 = 19,
    TILE_FOREST_12 = 20,
    TILE_FOREST_13 = 21,
    TILE_FOREST_14 = 22,
    TILE_FOREST_15 = 23,
    TILE_FOREST_16 = 24,
    TILE_FOREST_17 = 25,
    TILE_FOREST_18 = 26,
    TILE_FOREST_19 = 27,
    TILE_FOREST_20 = 28,
    TILE_FOREST_21 = 29,
    TILE_FOREST_22 = 30,
    TILE_FOREST_23 = 31,
    TILE_FOREST_24 = 32,
    TILE_FOREST_25 = 33,
    TILE_FOREST_26 = 34,
    TILE_FOREST_27 = 35,
    TILE_FOREST_28 = 36,
    TILE_FOREST_29 = 37,
    TILE_FOREST_30 = 38,
    TILE_FOREST_31 = 39,
    TILE_FOREST_32 = 40,
    TILE_FOREST_33 = 41,
    TILE_FOREST_34 = 42,
    TILE_FOREST_35 = 43,
    TILE_FOREST_36 = 44,
    TILE_FOREST_37 = 45,
    TILE_FOREST_38 = 46,
    TILE_FOREST_39 = 47,
    TILE_FOREST_40 = 48,
    TILE_FOREST_41 = 49,
    TILE_FOREST_42 = 50,
    TILE_FOREST_43 = 51,
    TILE_FOREST_44 = 52,
    TILE_FOREST_45 = 53,
    TILE_FOREST_46 = 54,
    TILE_FOREST_47 = 55,

    /* Castle Landscape tiles: 27 tiles in sheet scan order (0..26),
     * mapping 1:1 to the 27-tile castle VRAM block (RPG_TILE_BASE_WORLD). */
    TILE_CASTLE_00 = 56,
    TILE_CASTLE_01 = 57,
    TILE_CASTLE_02 = 58,
    TILE_CASTLE_03 = 59,
    TILE_CASTLE_04 = 60,
    TILE_CASTLE_05 = 61,
    TILE_CASTLE_06 = 62,
    TILE_CASTLE_07 = 63,
    TILE_CASTLE_08 = 64,
    TILE_CASTLE_09 = 65,
    TILE_CASTLE_10 = 66,
    TILE_CASTLE_11 = 67,
    TILE_CASTLE_12 = 68,
    TILE_CASTLE_13 = 69,
    TILE_CASTLE_14 = 70,
    TILE_CASTLE_15 = 71,
    /* Spare sheet cells wired for exact palette coverage (plain
     * ground repeats + the CSV-named exit art at sheet (8,2)). No map
     * uses them; the ui.c castle range intentionally still ends at
     * TILE_CASTLE_15: slots 16+ do not hold these cells' art because the
     * ROM block compiles in sheet-scan order while vram_block is 8-wide,
     * so TILE_CASTLE_08+ may already address shifted art -- reconcile
     * that order before addressing these ids from maps. Values fill the
     * reserved 72-82 gap. */
    TILE_CASTLE_16 = 72,
    TILE_CASTLE_17 = 73,

    /* Village Landscape tiles: 48 tiles in sheet scan order (0..47),
     * mapping 1:1 to the 48-tile village VRAM block (RPG_TILE_BASE_WORLD).
     * TILE_VILLAGE_<i> renders VRAM block i. value = 83 + i. */
    TILE_VILLAGE_00 = 83,
    TILE_VILLAGE_01 = 84,
    TILE_VILLAGE_02 = 85,
    TILE_VILLAGE_03 = 86,
    TILE_VILLAGE_04 = 87,
    TILE_VILLAGE_05 = 88,
    TILE_VILLAGE_06 = 89,
    TILE_VILLAGE_07 = 90,
    TILE_VILLAGE_08 = 91,
    TILE_VILLAGE_09 = 92,
    TILE_VILLAGE_10 = 93,
    TILE_VILLAGE_11 = 94,
    TILE_VILLAGE_12 = 95,
    TILE_VILLAGE_13 = 96,
    TILE_VILLAGE_14 = 97,
    TILE_VILLAGE_15 = 98,
    TILE_VILLAGE_16 = 99,
    TILE_VILLAGE_17 = 100,
    TILE_VILLAGE_18 = 101,
    TILE_VILLAGE_19 = 102,
    TILE_VILLAGE_20 = 103,
    TILE_VILLAGE_21 = 104,
    TILE_VILLAGE_22 = 105,
    TILE_VILLAGE_23 = 106,
    TILE_VILLAGE_24 = 107,
    TILE_VILLAGE_25 = 108,
    TILE_VILLAGE_26 = 109,
    TILE_VILLAGE_27 = 110,
    TILE_VILLAGE_28 = 111,
    TILE_VILLAGE_29 = 112,
    TILE_VILLAGE_30 = 113,
    TILE_VILLAGE_31 = 114,
    TILE_VILLAGE_32 = 115,
    TILE_VILLAGE_33 = 116,
    TILE_VILLAGE_34 = 117,
    TILE_VILLAGE_35 = 118,
    TILE_VILLAGE_36 = 119,
    TILE_VILLAGE_37 = 120,
    TILE_VILLAGE_38 = 121,
    TILE_VILLAGE_39 = 122,
    TILE_VILLAGE_40 = 123,
    TILE_VILLAGE_41 = 124,
    TILE_VILLAGE_42 = 125,
    TILE_VILLAGE_43 = 126,
    TILE_VILLAGE_44 = 127,
    TILE_VILLAGE_45 = 128,
    TILE_VILLAGE_46 = 129,
    TILE_VILLAGE_47 = 130,
    /* Desolate landscape */
    TILE_DESOLATE_WALL_00    = 141,
    TILE_DESOLATE_WALL_01    = 142,
    TILE_DESOLATE_WALL_02    = 143,
    TILE_DESOLATE_WALL_03    = 144,
    TILE_DESOLATE_WALL_04    = 145,
    TILE_DESOLATE_WALL_05    = 146,
    TILE_DESOLATE_WALL_06    = 147,
    TILE_DESOLATE_WALL_07    = 148,
    TILE_DESOLATE_WALL_08    = 149,
    TILE_DESOLATE_WALL_09    = 150,
    TILE_DESOLATE_WALL_10    = 151,
    TILE_DESOLATE_WALL_11    = 152,
    TILE_DESOLATE_TREE_TL    = 153,
    TILE_DESOLATE_TREE_TR    = 154,
    TILE_DESOLATE_ROCK_TL    = 155,
    TILE_DESOLATE_ROCK_TR    = 156,
    TILE_DESOLATE_WALL_12    = 157,
    TILE_DESOLATE_WALL_13    = 158,
    TILE_DESOLATE_WALL_14    = 159,
    TILE_DESOLATE_WALL_15    = 160,
    TILE_DESOLATE_WALL_16    = 161,
    TILE_DESOLATE_WALL_17    = 162,
    TILE_DESOLATE_FLOOR_00   = 163,
    TILE_DESOLATE_FLOOR_01   = 164,
    TILE_DESOLATE_FLOOR_02   = 165,
    TILE_DESOLATE_FLOOR_03   = 166,
    TILE_DESOLATE_WALL_18    = 167,
    TILE_DESOLATE_WALL_19    = 168,
    TILE_DESOLATE_TREE_BL    = 169,
    TILE_DESOLATE_TREE_BR    = 170,
    TILE_DESOLATE_ROCK_BL    = 171,
    TILE_DESOLATE_ROCK_BR    = 172,
    TILE_DESOLATE_FLOOR_PLAIN = 173,
    TILE_DESOLATE_HERO_01    = 174,
    TILE_DESOLATE_HERO_02    = 175,
    TILE_DESOLATE_KOBOLD_01  = 176,
    TILE_DESOLATE_KOBOLD_02  = 177,
    TILE_DESOLATE_FIRE_01    = 178,
    TILE_DESOLATE_FIRE_02    = 179,
    TILE_DESOLATE_MERCHANT   = 180,
    TILE_DESOLATE_STAIRCASE  = 181,
    /* Desolate Landscape (level-editor) tiles: 48 tiles in sheet scan order,
     * mapping 1:1 to the 48-tile desolate VRAM block (RPG_TILE_BASE_DESOLATE).
     * TILE_DESOLATE_LANDSCAPE_<i> renders VRAM block i. value = 182 + i. */
    TILE_DESOLATE_LANDSCAPE_00 = 182,
    TILE_DESOLATE_LANDSCAPE_01 = 183,
    TILE_DESOLATE_LANDSCAPE_02 = 184,
    TILE_DESOLATE_LANDSCAPE_03 = 185,
    TILE_DESOLATE_LANDSCAPE_04 = 186,
    TILE_DESOLATE_LANDSCAPE_05 = 187,
    TILE_DESOLATE_LANDSCAPE_06 = 188,
    TILE_DESOLATE_LANDSCAPE_07 = 189,
    TILE_DESOLATE_LANDSCAPE_08 = 190,
    TILE_DESOLATE_LANDSCAPE_09 = 191,
    TILE_DESOLATE_LANDSCAPE_10 = 192,
    TILE_DESOLATE_LANDSCAPE_11 = 193,
    TILE_DESOLATE_LANDSCAPE_12 = 194,
    TILE_DESOLATE_LANDSCAPE_13 = 195,
    TILE_DESOLATE_LANDSCAPE_14 = 196,
    TILE_DESOLATE_LANDSCAPE_15 = 197,
    TILE_DESOLATE_LANDSCAPE_16 = 198,
    TILE_DESOLATE_LANDSCAPE_17 = 199,
    TILE_DESOLATE_LANDSCAPE_18 = 200,
    TILE_DESOLATE_LANDSCAPE_19 = 201,
    TILE_DESOLATE_LANDSCAPE_20 = 202,
    TILE_DESOLATE_LANDSCAPE_21 = 203,
    TILE_DESOLATE_LANDSCAPE_22 = 204,
    TILE_DESOLATE_LANDSCAPE_23 = 205,
    TILE_DESOLATE_LANDSCAPE_24 = 206,
    TILE_DESOLATE_LANDSCAPE_25 = 207,
    TILE_DESOLATE_LANDSCAPE_26 = 208,
    TILE_DESOLATE_LANDSCAPE_27 = 209,
    TILE_DESOLATE_LANDSCAPE_28 = 210,
    TILE_DESOLATE_LANDSCAPE_29 = 211,
    TILE_DESOLATE_LANDSCAPE_30 = 212,
    TILE_DESOLATE_LANDSCAPE_31 = 213,
    TILE_DESOLATE_LANDSCAPE_32 = 214,
    TILE_DESOLATE_LANDSCAPE_33 = 215,
    TILE_DESOLATE_LANDSCAPE_34 = 216,
    TILE_DESOLATE_LANDSCAPE_35 = 217,
    TILE_DESOLATE_LANDSCAPE_36 = 218,
    TILE_DESOLATE_LANDSCAPE_37 = 219,
    TILE_DESOLATE_LANDSCAPE_38 = 220,
    TILE_DESOLATE_LANDSCAPE_39 = 221,
    TILE_DESOLATE_LANDSCAPE_40 = 222,
    TILE_DESOLATE_LANDSCAPE_41 = 223,
    TILE_DESOLATE_LANDSCAPE_42 = 224,
    TILE_DESOLATE_LANDSCAPE_43 = 225,
    TILE_DESOLATE_LANDSCAPE_44 = 226,
    TILE_DESOLATE_LANDSCAPE_45 = 227,
    TILE_DESOLATE_LANDSCAPE_46 = 228,
    TILE_DESOLATE_LANDSCAPE_47 = 229
} TileType;

/* Frames between autonomous patrol steps for hostile actors (~0.53 seconds). */
#define PATROL_STEP_INTERVAL 32

/* How a world actor is drawn in the overworld.  The choice is a
 * per-actor property selected by the level editor's object sprite
 * (overworld_sprite / animation_frames) and carried end-to-end from the
 * level JSON through compile.py into the ROM.  Defined here so both
 * WorldActorRuntime (this header) and WorldActorDefinition (actor.h, which
 * includes world.h) can use it without a circular include.  Values are
 * append-only: compiled rows store them numerically. */
typedef enum {
    SPRITE_KIND_ASCII = 0,  /* render the ASCII visual via the console font */
    SPRITE_KIND_KOBOLD = 1, /* 2-frame slime-as-kobold OBJ sprite */
    SPRITE_KIND_BAT = 2,    /* 2-frame per-map bat OBJ sprite */
    SPRITE_KIND_BOSS = 3,   /* 2x2 castle boss drawn as background tiles */
    SPRITE_KIND_CHEST = 4,  /* 1-frame pickup chest OBJ sprite (statics) */
    SPRITE_KIND_TILE = 5,   /* background-art actor: cell shows its tileset
                               map tile (NPC art), no OAM sprite */
    SPRITE_KIND_ENEMY = 6   /* per-enemy-type shared OAM sprite (append-only):
                               art comes from the enemy-type row named by
                               ow_type (0xFF = fall back to ASCII glyph) */
} ActorSpriteKind;

/* Mutable runtime state for a spawned World Actor.  Static actor
 * configuration lives in WorldActorDefinition; hostile actors are spawned
 * into World.actors by actor_load_scene().  actor_id is the persistent
 * ActorId copied from the definition (0 for non-persistent). */
typedef struct {
    uint16_t actor_id;
    EntityId id;
    uint8_t active;
    uint8_t x;
    uint8_t y;
    uint8_t facing;
    uint8_t hp;
    uint8_t max_hp;
    uint8_t flags;               /* runtime state flags (future) */
    uint8_t gold_reward;         /* copied from the definition */
    uint8_t reward_currency;     /* copied from the definition */
    const char *display_name;    /* copied from the definition */
    uint8_t visual;              /* ASCII char: 'S', 'B', etc. */
    ActorSpriteKind sprite_kind; /* how this actor is drawn (from def) */
    uint8_t ow_type;             /* enemy-type OAM index for SPRITE_KIND_ENEMY
                                    (into g_enemy_types; 0xFF = ASCII fallback) */
    uint8_t spawn_x;             /* patrol anchor origin */
    uint8_t spawn_y;
    uint8_t ai_type;             /* ActorAiType */
    uint8_t ai_step;             /* step index in patrol cycle */
    uint8_t ai_timer;            /* countdown to next patrol step */
    uint8_t move_state;          /* 0 = idle, 1 = moving between tiles */
    uint8_t move_target_x;
    uint8_t move_target_y;
    uint8_t move_progress;       /* 0..7 sub-tile pixels */
    uint8_t battle_type;         /* BattleId */
    uint8_t solo;                /* copied from the definition: engage alone */
} WorldActorRuntime;

typedef struct {
    uint8_t width;
    uint8_t height;
    MapId map_id;
    uint8_t encounter_actor_index;   /* slot in actors[], or NO_ACTOR_INDEX */
    bool map_changed;
    Entity player;
    WorldActorRuntime actors[MAX_WORLD_ACTORS];
    uint8_t map[WORLD_HEIGHT][WORLD_WIDTH];

    /* Overworld camera in PIXELS (top-left of the view window).  The camera
     * follows the player's pixel position smoothly (world_update_scroll);
     * SCX/SCY are set from these each frame and the tilemap window is drawn
     * around scroll_x/y (= camera_px/8).  Runtime only, never persistent. */
    uint8_t camera_px_x;
    uint8_t camera_px_y;

    /* Overworld camera tile origin (camera_px/8), the top-left tile of the
     * view window into the scene.  Derived by world_update_scroll and
     * exposed to the snapshot as scroll_x/scroll_y. */
    uint8_t scroll_x;
    uint8_t scroll_y;

    /* Movement animation state (runtime only, never persistent).  When
     * move_state == MOVE_STATE_MOVING, the player is animating from
     * player.position toward move_target over MOVE_FRAMES; the tile
     * position only commits at the end of the walk.  The renderer derives
     * the sub-tile pixel position from (position, target, progress). */
    uint8_t move_state;      /* MoveState */
    uint8_t move_target_x;   /* target tile committed at the end of the walk */
    uint8_t move_target_y;
    uint8_t move_progress;   /* 0..MOVE_FRAMES, sub-tile pixel offset */
    uint8_t move_outcome;    /* MoveOutcome resolved at commit */

    /* Scene tileset kind, copied from the scene definition at load time.
     * Appended at the END of World (never mid-struct): the renderer reads
     * it per cell instead of the fragile static cache in scene.c, whose
     * map-0 line could poison once and misrender a whole map as generic
     * fallback art.  Runtime only, never persistent. */
    WorldTilesetKind tileset_kind;

    /* Edge-crossing target (MapId) for MOVE_OUTCOME_EDGE moves, staged by
     * world_try_begin_move and consumed by world_update_move.  Runtime
     * only, never persistent. */
    uint8_t move_param;
    /* Edge-crossing direction (NbrEdge) for MOVE_OUTCOME_EDGE moves: the
     * commit path must mirror the decide path exactly (corners belong to
     * two edges), so the direction is staged, never recomputed. */
    uint8_t move_dir;
    /* Point-exit destination, staged by world_try_begin_move on the
     * MOVE_OUTCOME_EXIT path and consumed at commit.  Kept separate from
     * move_target_x/y (which stays the STEPPED gate tile so the walk
     * animation is a normal one-tile step, not a slide toward the far
     * destination).  Runtime only, never persistent. */
    uint8_t move_exit_x;
    uint8_t move_exit_y;
} World;

/* Neighbor edge indices (n,s,e,w), shared by the move decision and the
 * banked edge-spawn body. */
typedef enum {
    NBR_N = 0,
    NBR_S = 1,
    NBR_E = 2,
    NBR_W = 3
} NbrEdge;

void world_init(World *w, const GameState *state);
void world_load_map(World *w, MapId map_id, const GameState *state);
void world_change_map(World *w, MapId map_id, uint8_t spawn_x, uint8_t spawn_y,
                      const GameState *state);

/* Overworld camera: keep the player centred in the WORLD_VIEW_W x
 * WORLD_VIEW_H view, clamped at the scene bounds (scenes smaller than the
 * view never scroll; near a scene edge the player moves off-centre while
 * the camera stays at the boundary).  The camera glides smoothly in pixels
 * (SCX/SCY); scroll_x/y (= camera_px/8) drive the tile window and the
 * snapshot.  Called once per overworld frame. */
void world_update_scroll(World *w);

/* Animated movement: world_try_begin_move validates the target and starts
 * the MOVE_FRAMES animation (returns BLOCKED if the tile cannot be walked);
 * world_update_move advances it one frame and resolves the commit (tile
 * move, exit, or encounter).  Returns a WorldMoveResult for the caller.
 * The renderer derives the player's pixel position from the move state via
 * world_player_px/player_py. */
WorldMoveResult world_try_begin_move(World *w, int8_t dx, int8_t dy,
                                     const GameState *state);
WorldMoveResult world_update_move(World *w, const GameState *state);

/* Advance autonomous patrol AI for all active hostile actors in the scene.
 * If an actor steps into the player, sets encounter_actor_index and returns
 * MOVE_RESULT_ENCOUNTER. */
WorldMoveResult world_update_actors(World *w);

/* Renderer pixel position (tile*8 plus the sub-tile walk progress) of the
 * player sprite.  Valid whenever the player is not animating a move. */
/* Bank-3 pixel-interpolation body (src/world/px_banked.c); dispatched by
 * the four world_*_px/py wrappers. */
void world_px_banked(void);

uint8_t world_player_px(const World *w);
uint8_t world_player_py(const World *w);

uint8_t world_actor_px(const WorldActorRuntime *a);
uint8_t world_actor_py(const WorldActorRuntime *a);

void world_on_battle_end(Game *g, bool victory);
void world_patrol_slot_banked(void);

/* End a battle by fleeing: the enemy stays on the map at the HP it had when
 * the hero ran (written back into the runtime actor); no reward, defeat or
 * quest progress is applied. */
void world_on_battle_fled(Game *g);
/* Bank-2 body (src/world/fled_banked.c) dispatched by the wrapper. */
void world_on_battle_fled_banked(void);

#endif /* WORLD_H */
