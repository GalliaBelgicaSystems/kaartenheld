#include "game.h"
#include "screen.h"
#include "scene.h"
#include "telemetry.h"
#include "world.h"
#include "ui.h"
#include "banked.h"

void menus_entry_invalidate_banked(void);

/* Sync Game.scene with the world's current map after a runtime map
 * change (walking through a gate).  Emits SCENE_CHANGED when it differs. */
void scene_update_from_map(Game *g)
{
    SceneId new_scene;
    SceneId old_scene;
    if (!g) return;

    new_scene = map_to_scene_id(g->world.map_id);
    old_scene = g->state.scene.scene_id;
    g->state.scene.scene_id = new_scene;
#ifdef DEBUG_BUILD
    if (old_scene != new_scene) {
        telemetry_emit(EVENT_SCENE_CHANGED, (uint8_t)old_scene, (uint8_t)new_scene, 0, 0);
    }
#endif
}

void scene_load(Game *g, SceneId scene, uint8_t spawn_x, uint8_t spawn_y)
{
    SceneId old_scene;
    if (!g) return;

    old_scene = g->state.scene.scene_id;
    g->state.scene.scene_id = scene;
    g->state.scene.player_x = spawn_x;
    g->state.scene.player_y = spawn_y;
    world_change_map(&g->world, scene_id_to_map(scene), spawn_x, spawn_y, &g->state);
    /* A scene change ends any merchant engagement: the DECK tab's
     * SELL action is only available at a buying shop's doorstep. */
    g->shop_id = 0;
#ifdef DEBUG_BUILD
    if (old_scene != scene) {
        telemetry_emit(EVENT_SCENE_CHANGED, (uint8_t)old_scene, (uint8_t)scene, spawn_x, spawn_y);
    }
#endif
    /* A scene change is not a screen change: force the next frame to
     * redraw fully WITHOUT reseeding render state.  game_render_reset()
     * would copy the stale g->prev_screen (DIALOGUE long after a
     * conversation closed) into rc->prev_screen and seed rc->prev_map_id
     * with the already-new map -- the next frame then misclassifies as
     * a dialogue exit and restores 4 rows instead of repainting.  The
     * untouched prev_screen/prev_map_id keep whatever the last rendered
     * frame left, and their mismatch with the new map routes overworld
     * to the full transition repaint.  Force the tileset reload too:
     * battle art or another set may own VRAM block 1. */
    g->render_cache.valid = false;
    ui_invalidate_tileset();
}

/* Sync the canonical GameState.scene from the runtime World copy.  Called
 * once per frame from game_update(); the World stays the engine copy. */
void scene_sync_from_world(Game *g)
{
    if (!g) return;
    g->state.scene.scene_id = map_to_scene_id(g->world.map_id);
    g->state.scene.player_x = g->world.player.position.x;
    g->state.scene.player_y = g->world.player.position.y;
    g->state.scene.player_facing = (uint8_t)g->world.player.facing;
}

void screen_change(Game *g, ScreenId screen)
{
    ScreenId old_screen;
    if (!g) return;
    if (g->screen == screen) return;

    /* The player sprite only belongs on the overworld (and behind the
     * dialogue box, which redraws the world): hide it in shadow OAM
     * for every other screen.  SCX/SCY reset and VRAM/OAM streaming
     * run later in game_render() inside ui_lcd_off() so the outgoing
     * screen never shows shifted scroll or overwritten tiles for 1-2
     * frames. */
    if (screen != SCREEN_OVERWORLD && screen != SCREEN_DIALOGUE) {
        ui_sprite_hide();
    }

    old_screen = g->screen;
    g->prev_screen = old_screen;
    g->screen = screen;

    /* Screens that own VRAM block-1 art (battle enemy art shares slots
     * 128+ with world tiles) must not leak it into the next overworld
     * redraw; ui_invalidate_tileset() only clears WRAM cache sentinels,
     * while ui_restore_ow_oam() runs inside overworld_screen_render()
     * with the LCD off. */
    if (screen == SCREEN_OVERWORLD) {
        if (old_screen != SCREEN_DIALOGUE) {
            ui_invalidate_tileset();
        }
    } else if (screen != SCREEN_DIALOGUE) {
        /* Clear the time-shared OAM / menu chrome WRAM flags on entry;
         * game_render() streams the required tiles once ui_lcd_off()
         * has blanked the display. */
        g_bk_call_bank = 11;
        g_bk_call_target = (uint16_t)&menus_entry_invalidate_banked;
        banked_call_run();
    }

    telemetry_emit(EVENT_SCREEN_CHANGED, (uint8_t)old_screen, (uint8_t)screen, 0, 0);
    game_render_reset(g);

    /* ENDING entry (legacy final-boss victory route via ENDING_SHOWN):
     * switch to the end-credits theme (ending.uge, loops)
     * exactly at the transition -- and only then.  The despair outro
     * no longer routes here (Act 2 continues in the overworld).
     * Single hook for every ENDING entry, so the fixed bank pays one
     * branch instead of one music call per entry path (AGENTS.md
     * §52.18). */
    if (screen == SCREEN_ENDING) {
        audio_play_music(MUSIC_ENDING);
    }
}

void screen_update(Game *g)
{
    if (!g) return;
    switch (g->screen) {
    case SCREEN_OVERWORLD: overworld_screen_update(g); break;
    case SCREEN_DIALOGUE: dialogue_screen_update(g); break;
    case SCREEN_BATTLE: battle_screen_update(g); break;
    case SCREEN_GAME_OVER: game_over_screen_update(g); break;
    case SCREEN_THANKS: thanks_screen_update(g); break;
    case SCREEN_SHOP: shop_screen_update(g); break;
    case SCREEN_ITEM: item_screen_update(g); break;
    case SCREEN_ENDING: ending_screen_update(g); break;
    case SCREEN_SAVE_LOAD: save_load_screen_update(g); break;
    case SCREEN_TITLE: title_screen_update(g); break;
    case SCREEN_SPLASH: splash_screen_update(g); break;
    case SCREEN_NOTICE: notice_screen_update(g); break;
    case SCREEN_INTRO: intro_screen_update(g); break;
    case SCREEN_TUTORIAL: tutorial_screen_update(g); break;
    case SCREEN_CREDITS: tutorial_screen_update(g); break; /* shared: see credits_screen.c */
#ifdef TRAVEL_DEMO
    case SCREEN_JOURNEY: travel_screen_update(g); break;
#endif
    /* SCREEN_GAMBLE has no update OR render case: the bank-8 per-frame
     * tick body runs the whole table (entry paint + input + audio), so
     * the fixed bank spends nothing here (AGENTS.md 52.18). */
    default: break;
    }
}

void screen_render(Game *g)
{
    if (!g) return;
    switch (g->screen) {
    case SCREEN_OVERWORLD: overworld_screen_render(g); break;
    case SCREEN_DIALOGUE: dialogue_screen_render(g); break;
    case SCREEN_BATTLE: battle_screen_render(g); break;
    case SCREEN_GAME_OVER: game_over_screen_render(g); break;
    case SCREEN_THANKS: thanks_screen_render(g); break;
    case SCREEN_SHOP: shop_screen_render(g); break;
    case SCREEN_ITEM: item_screen_render(g); break;
    case SCREEN_ENDING: ending_screen_render(g); break;
    case SCREEN_SAVE_LOAD: save_load_screen_render(g); break;
    case SCREEN_TITLE: title_screen_render(g); break;
    case SCREEN_SPLASH: splash_screen_render(g); break;
    case SCREEN_NOTICE: notice_screen_render(g); break;
    case SCREEN_INTRO: intro_screen_render(g); break;
    case SCREEN_TUTORIAL: tutorial_screen_render(g); break;
    case SCREEN_CREDITS: credits_screen_render(g); break;
#ifdef TRAVEL_DEMO
    case SCREEN_JOURNEY: travel_screen_render(g); break;
#endif
    default: break;
    }
}
