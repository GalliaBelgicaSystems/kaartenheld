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
    /* Forest Landscape tiles: 64 tiles in sheet scan order (0..63),
     * mapping 1:1 to the first 64 forest VRAM slots (RPG_TILE_BASE_WORLD). */
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
    TILE_FOREST_48 = 56,
    TILE_FOREST_49 = 57,
    TILE_FOREST_50 = 58,
    TILE_FOREST_51 = 59,
    TILE_FOREST_52 = 60,
    TILE_FOREST_53 = 61,
    TILE_FOREST_54 = 62,
    TILE_FOREST_55 = 63,
    TILE_FOREST_56 = 64,
    TILE_FOREST_57 = 65,
    TILE_FOREST_58 = 66,
    TILE_FOREST_59 = 67,
    TILE_FOREST_60 = 68,
    TILE_FOREST_61 = 69,
    TILE_FOREST_62 = 70,
    TILE_FOREST_63 = 71,
    /* Forest row-4 extension (hut + mailbox, sheet slots 64..68): TileType
     * 0xFF is the empty-cell sentinel and the enum must stay <= 250
     * (g_tiletype_fits_u8), so these live in the free 136..145 gap, NOT
     * contiguously after 71 (72+ belongs to castle/village).  The renderer
     * maps them with (value - 72), not (value - TILE_FOREST_00): see
     * ui_draw_world_cell.  value = 72 + VRAM slot. */
    TILE_FOREST_64 = 136,
    TILE_FOREST_65 = 137,
    TILE_FOREST_66 = 138,
    TILE_FOREST_67 = 139,
    TILE_FOREST_68 = 140,

    /* Castle Landscape tiles: 16 tiles in sheet scan order (0..15),
     * mapping 1:1 to the 16-tile castle VRAM block (RPG_TILE_BASE_WORLD). */
    TILE_CASTLE_00 = 72,
    TILE_CASTLE_01 = 73,
    TILE_CASTLE_02 = 74,
    TILE_CASTLE_03 = 75,
    TILE_CASTLE_04 = 76,
    TILE_CASTLE_05 = 77,
    TILE_CASTLE_06 = 78,
    TILE_CASTLE_07 = 79,
    TILE_CASTLE_08 = 80,
    TILE_CASTLE_09 = 81,
    TILE_CASTLE_10 = 82,
    TILE_CASTLE_11 = 83,
    TILE_CASTLE_12 = 84,
    TILE_CASTLE_13 = 85,
    TILE_CASTLE_14 = 86,
    TILE_CASTLE_15 = 87,

    /* Village Landscape tiles: 48 tiles in sheet scan order (0..47),
     * mapping 1:1 to the 48-tile village VRAM block (RPG_TILE_BASE_WORLD).
     * TILE_VILLAGE_<i> renders VRAM block i. value = 88 + i. */
    TILE_VILLAGE_00 = 88,
    TILE_VILLAGE_01 = 89,
    TILE_VILLAGE_02 = 90,
    TILE_VILLAGE_03 = 91,
    TILE_VILLAGE_04 = 92,
    TILE_VILLAGE_05 = 93,
    TILE_VILLAGE_06 = 94,
    TILE_VILLAGE_07 = 95,
    TILE_VILLAGE_08 = 96,
    TILE_VILLAGE_09 = 97,
    TILE_VILLAGE_10 = 98,
    TILE_VILLAGE_11 = 99,
    TILE_VILLAGE_12 = 100,
    TILE_VILLAGE_13 = 101,
    TILE_VILLAGE_14 = 102,
    TILE_VILLAGE_15 = 103,
    TILE_VILLAGE_16 = 104,
    TILE_VILLAGE_17 = 105,
    TILE_VILLAGE_18 = 106,
    TILE_VILLAGE_19 = 107,
    TILE_VILLAGE_20 = 108,
    TILE_VILLAGE_21 = 109,
    TILE_VILLAGE_22 = 110,
    TILE_VILLAGE_23 = 111,
    TILE_VILLAGE_24 = 112,
    TILE_VILLAGE_25 = 113,
    TILE_VILLAGE_26 = 114,
    TILE_VILLAGE_27 = 115,
    TILE_VILLAGE_28 = 116,
    TILE_VILLAGE_29 = 117,
    TILE_VILLAGE_30 = 118,
    TILE_VILLAGE_31 = 119,
    TILE_VILLAGE_32 = 120,
    TILE_VILLAGE_33 = 121,
    TILE_VILLAGE_34 = 122,
    TILE_VILLAGE_35 = 123,
    TILE_VILLAGE_36 = 124,
    TILE_VILLAGE_37 = 125,
    TILE_VILLAGE_38 = 126,
    TILE_VILLAGE_39 = 127,
    TILE_VILLAGE_40 = 128,
    TILE_VILLAGE_41 = 129,
    TILE_VILLAGE_42 = 130,
    TILE_VILLAGE_43 = 131,
    TILE_VILLAGE_44 = 132,
    TILE_VILLAGE_45 = 133,
    TILE_VILLAGE_46 = 134,
    TILE_VILLAGE_47 = 135,
    /* Desolate landscape */
    TILE_DESOLATE_WALL_00    = 146,
    TILE_DESOLATE_WALL_01    = 147,
    TILE_DESOLATE_WALL_02    = 148,
    TILE_DESOLATE_WALL_03    = 149,
    TILE_DESOLATE_WALL_04    = 150,
    TILE_DESOLATE_WALL_05    = 151,
    TILE_DESOLATE_WALL_06    = 152,
    TILE_DESOLATE_WALL_07    = 153,
    TILE_DESOLATE_WALL_08    = 154,
    TILE_DESOLATE_WALL_09    = 155,
    TILE_DESOLATE_WALL_10    = 156,
    TILE_DESOLATE_WALL_11    = 157,
    TILE_DESOLATE_TREE_TL    = 158,
    TILE_DESOLATE_TREE_TR    = 159,
    TILE_DESOLATE_ROCK_TL    = 160,
    TILE_DESOLATE_ROCK_TR    = 161,
    TILE_DESOLATE_WALL_12    = 162,
    TILE_DESOLATE_WALL_13    = 163,
    TILE_DESOLATE_WALL_14    = 164,
    TILE_DESOLATE_WALL_15    = 165,
    TILE_DESOLATE_WALL_16    = 166,
    TILE_DESOLATE_WALL_17    = 167,
    TILE_DESOLATE_FLOOR_00   = 168,
    TILE_DESOLATE_FLOOR_01   = 169,
    TILE_DESOLATE_FLOOR_02   = 170,
    TILE_DESOLATE_FLOOR_03   = 171,
    TILE_DESOLATE_WALL_18    = 172,
    TILE_DESOLATE_WALL_19    = 173,
    TILE_DESOLATE_TREE_BL    = 174,
    TILE_DESOLATE_TREE_BR    = 175,
    TILE_DESOLATE_ROCK_BL    = 176,
    TILE_DESOLATE_ROCK_BR    = 177,
    TILE_DESOLATE_FLOOR_PLAIN = 178,
    TILE_DESOLATE_HERO_01    = 179,
    TILE_DESOLATE_HERO_02    = 180,
    TILE_DESOLATE_KOBOLD_01  = 181,
    TILE_DESOLATE_KOBOLD_02  = 182,
    TILE_DESOLATE_FIRE_01    = 183,
    TILE_DESOLATE_FIRE_02    = 184,
    TILE_DESOLATE_MERCHANT   = 185,
    TILE_DESOLATE_STAIRCASE  = 186,
    /* Desolate Landscape (level-editor) tiles: 64 tiles in sheet scan order,
     * mapping 1:1 to the 64-tile desolate VRAM block (RPG_TILE_BASE_DESOLATE).
     * TILE_DESOLATE_LANDSCAPE_<i> renders VRAM block i. value = 187 + i. */
    TILE_DESOLATE_LANDSCAPE_00 = 187,
    TILE_DESOLATE_LANDSCAPE_01 = 188,
    TILE_DESOLATE_LANDSCAPE_02 = 189,
    TILE_DESOLATE_LANDSCAPE_03 = 190,
    TILE_DESOLATE_LANDSCAPE_04 = 191,
    TILE_DESOLATE_LANDSCAPE_05 = 192,
    TILE_DESOLATE_LANDSCAPE_06 = 193,
    TILE_DESOLATE_LANDSCAPE_07 = 194,
    TILE_DESOLATE_LANDSCAPE_08 = 195,
    TILE_DESOLATE_LANDSCAPE_09 = 196,
    TILE_DESOLATE_LANDSCAPE_10 = 197,
    TILE_DESOLATE_LANDSCAPE_11 = 198,
    TILE_DESOLATE_LANDSCAPE_12 = 199,
    TILE_DESOLATE_LANDSCAPE_13 = 200,
    TILE_DESOLATE_LANDSCAPE_14 = 201,
    TILE_DESOLATE_LANDSCAPE_15 = 202,
    TILE_DESOLATE_LANDSCAPE_16 = 203,
    TILE_DESOLATE_LANDSCAPE_17 = 204,
    TILE_DESOLATE_LANDSCAPE_18 = 205,
    TILE_DESOLATE_LANDSCAPE_19 = 206,
    TILE_DESOLATE_LANDSCAPE_20 = 207,
    TILE_DESOLATE_LANDSCAPE_21 = 208,
    TILE_DESOLATE_LANDSCAPE_22 = 209,
    TILE_DESOLATE_LANDSCAPE_23 = 210,
    TILE_DESOLATE_LANDSCAPE_24 = 211,
    TILE_DESOLATE_LANDSCAPE_25 = 212,
    TILE_DESOLATE_LANDSCAPE_26 = 213,
    TILE_DESOLATE_LANDSCAPE_27 = 214,
    TILE_DESOLATE_LANDSCAPE_28 = 215,
    TILE_DESOLATE_LANDSCAPE_29 = 216,
    TILE_DESOLATE_LANDSCAPE_30 = 217,
    TILE_DESOLATE_LANDSCAPE_31 = 218,
    TILE_DESOLATE_LANDSCAPE_32 = 219,
    TILE_DESOLATE_LANDSCAPE_33 = 220,
    TILE_DESOLATE_LANDSCAPE_34 = 221,
    TILE_DESOLATE_LANDSCAPE_35 = 222,
    TILE_DESOLATE_LANDSCAPE_36 = 223,
    TILE_DESOLATE_LANDSCAPE_37 = 224,
    TILE_DESOLATE_LANDSCAPE_38 = 225,
    TILE_DESOLATE_LANDSCAPE_39 = 226,
    TILE_DESOLATE_LANDSCAPE_40 = 227,
    TILE_DESOLATE_LANDSCAPE_41 = 228,
    TILE_DESOLATE_LANDSCAPE_42 = 229,
    TILE_DESOLATE_LANDSCAPE_43 = 230,
    TILE_DESOLATE_LANDSCAPE_44 = 231,
    TILE_DESOLATE_LANDSCAPE_45 = 232,
    TILE_DESOLATE_LANDSCAPE_46 = 233,
    TILE_DESOLATE_LANDSCAPE_47 = 234,
    TILE_DESOLATE_LANDSCAPE_48 = 235,
    TILE_DESOLATE_LANDSCAPE_49 = 236,
    TILE_DESOLATE_LANDSCAPE_50 = 237,
    TILE_DESOLATE_LANDSCAPE_51 = 238,
    TILE_DESOLATE_LANDSCAPE_52 = 239,
    TILE_DESOLATE_LANDSCAPE_53 = 240,
    TILE_DESOLATE_LANDSCAPE_54 = 241,
    TILE_DESOLATE_LANDSCAPE_55 = 242,
    TILE_DESOLATE_LANDSCAPE_56 = 243,
    TILE_DESOLATE_LANDSCAPE_57 = 244,
    TILE_DESOLATE_LANDSCAPE_58 = 245,
    TILE_DESOLATE_LANDSCAPE_59 = 246,
    TILE_DESOLATE_LANDSCAPE_60 = 247,
    TILE_DESOLATE_LANDSCAPE_61 = 248,
    TILE_DESOLATE_LANDSCAPE_62 = 249,
    TILE_DESOLATE_LANDSCAPE_63 = 250,
} TileType;

/* Tile ids live in uint8_t map arrays with 0xFF reserved as a sentinel
 * (empty/unset cell), so the enum must never reach it -- another 8-tile
 * block past 250 would overflow.  Enforced by g_tiletype_fits_u8 in
 * world.c (kept out of this header so the 2 bytes are emitted once). */

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

void world_on_battle_end(Game *g, bool victory);
void world_patrol_slot_banked(void);

/* End a battle by fleeing: the enemy stays on the map at the HP it had when
 * the hero ran (written back into the runtime actor); no reward, defeat or
 * quest progress is applied. */
void world_on_battle_fled(Game *g);
/* Bank-2 body (src/world/fled_banked.c) dispatched by the wrapper. */
void world_on_battle_fled_banked(void);

#endif /* WORLD_H */
