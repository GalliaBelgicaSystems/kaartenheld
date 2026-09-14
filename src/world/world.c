#include "world.h"
#include "game.h"
#include "telemetry.h"
#include "actor.h"
#include "scene.h"
#include "event.h"
#include "rpg/currency.h"
#include "rpg/cards.h"
#include "rpg/loot.h"
#include "rpg/deck.h"
#include "rng.h"
#include "content.h"
#include "ui.h"
#include "audio.h"
#include "banked.h"

/* Bank-3 movement bodies (src/world/edge_banked.c). */
void world_gate_check_banked(void);
void world_edge_spawn_banked(void);

void world_load_map(World *w, MapId map_id, const GameState *state)
{
    const SceneDefinition *def;

    if (!w) return;

    /* Scene size comes from the scene definition; it may be smaller than
     * the WORLD_WIDTH/HEIGHT buffer caps.  The overworld camera clamps its
     * view window to width/height. */
    def = scene_definition_for_map(map_id);
    w->width = def ? def->width : WORLD_WIDTH;
    w->height = def ? def->height : WORLD_HEIGHT;
    w->map_id = map_id;
    /* Cache the tileset kind on the world itself (see world.h): the render
     * path reads it per cell without touching the static cache. */
    w->tileset_kind = def ? def->tileset : WORLD_TILESET_FOREST;
    w->encounter_actor_index = NO_ACTOR_INDEX;
    w->map_changed = false;
    w->move_state = MOVE_STATE_IDLE;
    w->move_progress = 0;
    w->move_outcome = MOVE_OUTCOME_NONE;
    w->move_param = MAP_NONE;
    w->move_dir = NBR_N;
    /* The scene owns its music (SceneDefinition.music): switch to it on
     * every map load so gate crossings boot the right area track. */
    audio_play_music(def ? def->music : MUSIC_OVERWORLD);
    /* New scene: start the camera at the scene origin; world_update_scroll
     * brings the player into view (and clamps) on the next overworld frame. */
    w->scroll_x = 0;
    w->scroll_y = 0;
    w->camera_px_x = 0;
    w->camera_px_y = 0;

    /* Scene data determines the terrain and the exits. */
    scene_load_tiles(w, map_id);

    /* Scene data determines which hostile actors are spawned.  Actors
     * whose ActorId is DEFEATED in state are not re-spawned. */
    actor_load_scene(w, map_id, state);
}

void world_update_scroll(World *w)
{
    uint8_t px, py, max_x, max_y;
    if (!w) return;
    px = world_player_px(w);
    py = world_player_py(w);
    px = (px < 80) ? 0 : (uint8_t)(px - 80);
    py = (py < 72) ? 0 : (uint8_t)(py - 72);
    max_x = (w->width > WORLD_VIEW_W) ? (uint8_t)((w->width - WORLD_VIEW_W) << 3) : 0;
    max_y = (w->height > WORLD_VIEW_H) ? (uint8_t)((w->height - WORLD_VIEW_H) << 3) : 0;
    if (px > max_x) px = max_x;
    if (py > max_y) py = max_y;
    w->camera_px_x = px;
    w->camera_px_y = py;
    w->scroll_x = (uint8_t)(px >> 3);
    w->scroll_y = (uint8_t)(py >> 3);
}

void world_init(World *w, const GameState *state)
{
    if (!w) return;
    /* Runtime player position seeds from the canonical state (which
     * game_new_game fills from levels/field.json player.spawn, the single
     * source of truth).  Never hardcode coordinates here: scene_sync_from_world
     * copies the runtime position back into GameState every frame, so a
     * hardcoded value would silently overwrite the authored spawn. */
    entity_init(&w->player, ENTITY_ID_PLAYER,
                state->scene.player_x, state->scene.player_y, 10, 10);
    w->player.facing = (Direction)state->scene.player_facing;
    /* Load the canonical scene's map (set by game_new_game before this
     * call).  Never hardcode MAP_FIELD here: harness builds boot test
     * scenes, and a hardcoded map would silently overwrite the staged
     * world (and scene_sync_from_world would then clobber the staged
     * scene id every frame). */
    world_load_map(w, scene_id_to_map(state->scene.scene_id), state);
}

void world_change_map(World *w, MapId map_id, uint8_t spawn_x, uint8_t spawn_y,
                      const GameState *state)
{
    MapId old_map;
    if (!w) return;
    old_map = w->map_id;
    world_load_map(w, map_id, state);
    w->player.position.x = spawn_x;
    w->player.position.y = spawn_y;
    w->map_changed = true;
    telemetry_emit(EVENT_MAP_CHANGED, (uint8_t)old_map, (uint8_t)map_id, spawn_x, spawn_y);
}

WorldMoveResult world_try_begin_move(World *w, int8_t dx, int8_t dy,
                                     const GameState *state)
{
    uint8_t target_x, target_y;
    uint8_t hostile_slot;
    const StaticActorDefinition *actor;
    const SceneDefinition *def;
    const SceneExit *ex;

    if (!w || w->move_state == MOVE_STATE_MOVING) return MOVE_RESULT_NONE;
    (void)state;

    if (dy < 0) w->player.facing = DIRECTION_UP;
    else if (dy > 0) w->player.facing = DIRECTION_DOWN;
    else if (dx < 0) w->player.facing = DIRECTION_LEFT;
    else if (dx > 0) w->player.facing = DIRECTION_RIGHT;

    target_x = (uint8_t)(w->player.position.x + dx);
    target_y = (uint8_t)(w->player.position.y + dy);

    def = scene_definition_for_map(w->map_id);

    hostile_slot = actor_find_hostile_slot(w, target_x, target_y);
    if (hostile_slot != NO_ACTOR_INDEX) {
        w->move_outcome = MOVE_OUTCOME_ENCOUNTER;
        w->encounter_actor_index = hostile_slot;
        w->move_target_x = target_x;
        w->move_target_y = target_y;
    } else {
        actor = actor_find_at(w, target_x, target_y);
        if (actor) {
            telemetry_emit(EVENT_ACTOR_COLLISION, target_x, target_y,
                           (uint8_t)actor->id, 0);
            return MOVE_RESULT_BLOCKED;
        }
        /* Invisible point-exit triggers fire regardless of the art
         * painted under them.  The STEPPED gate tile stays the move
         * target so the walk animation is one normal step; the far
         * destination is staged separately (a spawn 17 tiles away used
         * to make the sprite slide toward it).  Whole-edge predicates run
         * banked (bank 2 body writes outcome/param/dir/target straight
         * through the staged World pointer). A missing def stages NULL,
         * which the body treats as no links. */
        ex = scene_exit_at(def, target_x, target_y);
        if (ex) {
            w->move_outcome = MOVE_OUTCOME_EXIT;
            w->move_param = (uint8_t)ex->target_scene;
            w->move_target_x = target_x;
            w->move_target_y = target_y;
            w->move_exit_x = ex->spawn_x;
            w->move_exit_y = ex->spawn_y;
        } else {
            g_bk_call_bank = 2;
            g_bk_call_target = (uint16_t)&world_gate_check_banked;
            g_bk_ptr_a = (void *)w;
            g_bk_ptr_b = def ? (void *)&def->neighbor_n : (void *)0;
            g_bk_byte_a = target_x;
            g_bk_byte_b = target_y;
            banked_call_run();
            if (w->move_outcome == MOVE_OUTCOME_NONE) {
                return MOVE_RESULT_BLOCKED;
            }
        }
    }

    w->move_progress = 0;
    w->move_state = MOVE_STATE_MOVING;
    return MOVE_RESULT_MOVED;
}

WorldMoveResult world_update_move(World *w, const GameState *state)
{
    uint8_t target_x, target_y;

    if (!w) return MOVE_RESULT_NONE;
    if (w->move_state != MOVE_STATE_MOVING) return MOVE_RESULT_NONE;

    w->move_progress++;
    if (w->move_progress >= MOVE_FRAMES) {
        /* Commit: resolve the move's outcome against the target tile. */
        target_x = w->move_target_x;
        target_y = w->move_target_y;
        w->move_progress = 0;
        w->move_state = MOVE_STATE_IDLE;

        if (w->move_outcome == MOVE_OUTCOME_EXIT || w->move_outcome == MOVE_OUTCOME_EDGE) {
            /* Scene change: point-exit spawns were staged by the decide
             * path; whole-edge entry spawns run banked (bank 2 body,
             * fixed-bank budget) from the staged cross-coordinate, edge
             * dimension and direction. The player never commits
             * PLAYER_MOVED onto the gate; the map changes. */
            SceneId target_scene = (SceneId)w->move_param;
            uint8_t sx = w->move_target_x;
            uint8_t sy = w->move_target_y;
            if (w->move_outcome == MOVE_OUTCOME_EDGE) {
                /* Mirrored entry spawn runs banked (bank 2 body,
                 * fixed-bank budget): cross-coordinate from the staged
                 * target, both neighbor dims from a table fetch (the
                 * predicate body cannot read tables). */
                const SceneDefinition *nd = scene_definition_for_map((MapId)w->move_param);
                if (!nd) {
                    return MOVE_RESULT_BLOCKED;
                }
                g_bk_call_bank = 2;
                g_bk_call_target = (uint16_t)&world_edge_spawn_banked;
                g_bk_byte_a = (w->move_dir < 2) ? w->move_target_x : w->move_target_y;
                g_bk_byte_b = nd->width;
                g_bk_byte_c = nd->height;
                g_bk_byte_d = w->move_dir;
                banked_call_run();
                sx = g_bk_byte_a;
                sy = g_bk_byte_b;
            } else {
                /* Point exit: the explicit destination staged at decide
                 * time (move_target is the stepped gate tile). */
                sx = w->move_exit_x;
                sy = w->move_exit_y;
            }
            world_change_map(w, scene_id_to_map(target_scene), sx, sy, state);
            return MOVE_RESULT_MAP_CHANGED;
        } else if (w->move_outcome == MOVE_OUTCOME_ENCOUNTER) {
            /* The player does not occupy the enemy tile; battle starts from
             * the pre-move position (matches the legacy instant behavior).
             * The hostile slot was persisted by world_try_begin_move. */
            if (w->encounter_actor_index < MAX_WORLD_ACTORS) {
                telemetry_emit(EVENT_ACTOR_COLLISION, target_x, target_y,
                               (uint8_t)w->actors[w->encounter_actor_index].id, 0);
                telemetry_emit(EVENT_ENCOUNTER_STARTED,
                               (uint8_t)w->actors[w->encounter_actor_index].id, 0, 0, 0);
                return MOVE_RESULT_ENCOUNTER;
            }
            return MOVE_RESULT_BLOCKED;
        } else {
            telemetry_emit(EVENT_PLAYER_MOVED, w->player.position.x,
                           w->player.position.y, target_x, target_y);
            w->player.position.x = target_x;
            w->player.position.y = target_y;
            return MOVE_RESULT_MOVED;
        }
    }
    return MOVE_RESULT_MOVED;
}

/* Pixel helpers run banked (src/world/px_banked.c) -- pure arithmetic,
 * staged pointers, results through the shared byte below. */
uint8_t g_px_result;

static uint8_t px_dispatch(uint8_t variant, const void *p)
{
    g_bk_call_bank = 3;
    g_bk_call_target = (uint16_t)&world_px_banked;
    g_bk_byte_a = variant;
    g_bk_ptr_a = (void *)p;
    banked_call_run();
    return g_px_result;
}

uint8_t world_player_px(const World *w)
{
    return w ? px_dispatch(0, w) : 0;
}

uint8_t world_player_py(const World *w)
{
    return w ? px_dispatch(1, w) : 0;
}

uint8_t world_actor_px(const WorldActorRuntime *a)
{
    return a ? px_dispatch(2, a) : 0;
}

uint8_t world_actor_py(const WorldActorRuntime *a)
{
    return a ? px_dispatch(3, a) : 0;
}

void world_on_battle_end(Game *g, bool victory)
{
    World *w;
    uint8_t idx;
    uint16_t actor_id;
    if (!g) return;
    w = &g->world;

    idx = w->encounter_actor_index;
    w->encounter_actor_index = NO_ACTOR_INDEX;
    if (idx == NO_ACTOR_INDEX) return;

    if (victory) {
        WorldActorRuntime *act = &w->actors[idx];
        actor_id = act->actor_id;
        act->active = 0;
        act->hp = 0;
        act->flags = ACTOR_STATE_NONE;
        telemetry_emit(EVENT_ENTITY_DEFEATED, (uint8_t)act->id, 0, 0, 0);
        if (act->reward_currency != 0 && act->gold_reward != 0) {
            currency_add(&g->state, (CurrencyId)act->reward_currency, act->gold_reward);
        }
        if (actor_id != 0) {
            game_world_set_actor_state(&g->state, actor_id, ACTOR_STATE_DEFEATED);
        }
        event_resolve_actor_defeated(g, actor_id, act->id);

        /* Loot grant (docs/loot.md §8/§17/§34.5): the drop was ROLLED at
         * battle start (game_loot_drop, isolated RNG) and its derived id
         * waits in g_loot_id -- victory grants it to the collection.
         * CARD_NONE = collection previously full (no room to grant). */
        if (g_loot_id != CARD_NONE) {
            if (deck_collection_add(&g->state.cards, g_loot_id, 1)) {
                telemetry_emit(EVENT_LOOT_CARD_ADDED, g_loot_id,
                               deck_collection_count(&g->state.cards,
                                                     g_loot_id), 0, 0);
            } else {
                g_loot_id = CARD_NONE;
            }
        }
    }
}

void world_on_battle_fled(Game *g)
{
    /* Body runs banked (src/world/fled_banked.c) through the WRAM
     * trampoline -- pure WRAM reads/writes, no staging args needed. */
    if (!g) return;
    g_bk_call_bank = 3;
    g_bk_call_target = (uint16_t)&world_on_battle_fled_banked;
    g_bk_ptr_a = (void *)g;
    banked_call_run();
}

uint8_t g_patrol_outcome = 0;
uint8_t g_patrol_evt[4];
World *g_patrol_world;
uint8_t g_patrol_slot;

WorldMoveResult world_update_actors(World *w)
{
    uint8_t slot;

    if (!w) return MOVE_RESULT_NONE;

    for (slot = 0; slot < MAX_WORLD_ACTORS; slot++) {
        g_patrol_outcome = 0;
        g_patrol_evt[0] = g_patrol_evt[1] = g_patrol_evt[2] = g_patrol_evt[3] = 0;
        g_patrol_world = w;
        g_patrol_slot = slot;
#ifdef DEBUG_BUILD
        g_bk_call_bank = 5;
#else
        g_bk_call_bank = 3;
#endif
        g_bk_call_target = (uint16_t)&world_patrol_slot_banked;
        banked_call_run();

        if (g_patrol_outcome == 1) {
            telemetry_emit(EVENT_ACTOR_STATE_CHANGE, g_patrol_evt[0],
                           g_patrol_evt[1], g_patrol_evt[2], g_patrol_evt[3]);
        } else if (g_patrol_outcome == 2) {
            telemetry_emit(EVENT_ACTOR_COLLISION, g_patrol_evt[0],
                           g_patrol_evt[1], g_patrol_evt[2], 0);
            telemetry_emit(EVENT_ENCOUNTER_STARTED, g_patrol_evt[2],
                           0, 0, 0);
            return MOVE_RESULT_ENCOUNTER;
        }
    }

    return MOVE_RESULT_NONE;
}
