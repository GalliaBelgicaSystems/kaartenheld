#include "game.h"
#include "screen.h"
#include "scene.h"
#include "interaction.h"
#include "event.h"
#include "telemetry.h"
#include "audio.h"
#include "content.h"
#include "game_ids.h"

void start_battle_from_world(Game *g)
{
    WorldActorRuntime *act;
    uint8_t idx = g->world.encounter_actor_index;
    if (idx == NO_ACTOR_INDEX) return;
    act = &g->world.actors[idx];

    /* Hero HP is authoritative in the party state; the world entity is the
     * runtime engine copy.  Bosses (no enemy deck) and solo-flagged
     * minibosses stand alone on the single-enemy centered boss screen. */
    battle_start(&g->battle,
                 act->display_name ? act->display_name : "ENEMY",
                 g->state.party.members[0].hp,
                 g->state.party.members[0].max_hp,
                 act->hp, act->max_hp,
                 &g->state.cards.deck,
                 act->battle_type,
                 (uint8_t)((act->battle_type == BATTLE_NONE) || act->solo));

    /* Every hostile encounter engages as a trio: the struck actor plus
     * two clones of its stats -- EXCEPT actors with no enemy deck
     * (BATTLE_NONE) and actors flagged solo (e.g. the Lord of Slimes as
     * a proper final boss, the castle mimic as an optional boss): those
     * stand alone.  Enemy decks wrap their draw index, so per-type decks
     * serve trios unchanged. */
    if (act->battle_type != BATTLE_NONE && !act->solo) {
        battle_add_enemy(&g->battle,
                         act->display_name ? act->display_name : "ENEMY",
                         act->hp, act->max_hp);
        battle_add_enemy(&g->battle,
                         act->display_name ? act->display_name : "ENEMY",
                         act->hp, act->max_hp);
    }

    /* Boss actors carry no enemy deck (BATTLE_NONE) and stand alone;
     * give them the dedicated boss theme.  Mimics get their own theme
     * (bank-7 tracker song); everyone else the battle theme. */
    if (act->battle_type == BATTLE_MIMIC) {
        audio_play_music(MUSIC_MIMIC);
    } else if (act->battle_type == BATTLE_NONE) {
        audio_play_music(MUSIC_BOSS);
    } else {
        audio_play_music(MUSIC_BATTLE);
    }
    telemetry_emit(EVENT_ACTOR_COMBAT_START, (uint8_t)act->id, 0, 0, 0);
    screen_change(g, SCREEN_BATTLE);
    /* encounter_actor_index stays set until world_on_battle_end() */
}

void overworld_screen_update(Game *g)
{
    int8_t dx = 0;
    int8_t dy = 0;
    WorldMoveResult move_res = MOVE_RESULT_NONE;
    ActorEngageResult engage = ENGAGE_NONE;

    /* A committed move may resolve a map change or an encounter; resolve
     * those before reading fresh input. */
    move_res = world_update_move(&g->world, &g->state);

    /* Keep the camera on the player: scroll the view window when the player
     * crosses its edge, clamped to the scene bounds. */
    world_update_scroll(&g->world);

    /* A committed move's result is one-shot and must resolve before fresh
     * input is read: pressing START on the commit frame would otherwise
     * swallow the map change or encounter (dropping TOWN_ARRIVAL or the
     * battle start) because the *_RESULT_* blocks below would never run. */
    if (move_res == MOVE_RESULT_MAP_CHANGED) {
        g->world.map_changed = false;
        scene_update_from_map(g);
        event_resolve_map_enter(g, g->world.map_id);
        return;
    }
    if (move_res == MOVE_RESULT_ENCOUNTER) {
        start_battle_from_world(g);
        return;
    }

    /* Advance autonomous patrol AI for active scene actors. */
    move_res = world_update_actors(&g->world);
    if (move_res == MOVE_RESULT_ENCOUNTER) {
        start_battle_from_world(g);
        return;
    }

    if (input_pressed(INPUT_START)) {
        item_screen_reset(g);
        screen_change(g, SCREEN_ITEM);
        return;
    }
    if (input_pressed(INPUT_SELECT)) {
        g->save_slot_mode = 0;
        g->save_slot_index = 0;
        g->save_slot_message = 0;
        screen_change(g, SCREEN_SAVE_LOAD);
        return;
    }

    /* Hold-to-move: input_held starts a move whenever the previous one has
     * finished animating; a held button stays active across frames (a fresh
     * press is just the first held frame). */
    if (g->world.move_state == MOVE_STATE_MOVING) {
        return;
    }

    if (input_held(INPUT_UP)) dy = -1;
    else if (input_held(INPUT_DOWN)) dy = 1;
    else if (input_held(INPUT_LEFT)) dx = -1;
    else if (input_held(INPUT_RIGHT)) dx = 1;

    if (dx != 0 || dy != 0) {
        move_res = world_try_begin_move(&g->world, dx, dy, &g->state);
    }

    if (input_pressed(INPUT_A)) {
        engage = interaction_try_facing(g);
    } else if (move_res == MOVE_RESULT_BLOCKED) {
        engage = interaction_try_bump(g, dx, dy);
    }

    if (engage != ENGAGE_NONE) {
        if (engage == ENGAGE_DIALOGUE) {
            screen_change(g, SCREEN_DIALOGUE);
        } else if (engage == ENGAGE_BATTLE) {
            start_battle_from_world(g);
        } else if (engage == ENGAGE_SHOP) {
            g->item_menu_index = 0;
            g->item_menu_scroll = 0;
            screen_change(g, SCREEN_SHOP);
        } else if (engage == ENGAGE_SAVE) {
            g->save_slot_mode = 1;
            g->save_slot_index = 0;
            g->save_slot_message = 0;
            screen_change(g, SCREEN_SAVE_LOAD);
        }
    }
}

void overworld_screen_render(Game *g)
{
    RenderCache *rc;
    World *w;
    uint8_t px, py;

    if (!g) return;
    rc = &g->render_cache;
    w = &g->world;

    px = (uint8_t)(world_player_px(w) - w->camera_px_x);
    py = (uint8_t)(world_player_py(w) - w->camera_px_y);
    ui_update_camera(w);

    if (!rc->valid || rc->prev_screen != SCREEN_OVERWORLD ||
        w->map_id != rc->prev_map_id) {
        ui_draw_world_full(w);
        telemetry_emit(EVENT_RENDER_SCREEN, (uint8_t)SCREEN_OVERWORLD, 0,
                       (uint8_t)w->map_id, 0);
        rc->valid = true;
        rc->prev_screen = SCREEN_OVERWORLD;
        rc->prev_map_id = w->map_id;
        rc->prev_dialogue_active = false;
        rc->prev_dialogue_line = 255;
        rc->prev_dialogue_id = DIALOGUE_ID_NONE;
    }

    ui_sprite_move(px, py);
    ui_draw_actors_sprites(w);
    rc->prev_player_x = px;
    rc->prev_player_y = py;
}
