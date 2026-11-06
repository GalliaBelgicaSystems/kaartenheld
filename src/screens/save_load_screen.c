#include "game.h"
#include "screen.h"
#include "telemetry.h"
#include "rpg/save.h"
#include "menu.h"
#include "scene.h"
#include "ui.h"
#include "banked.h"

/* Full save/load content draw lives in the bank-2 body (screen_content.c)
 * so the fixed _CODE/_HOME area stays under 0x8000 (AGENTS.md 52.11.1). */
void save_load_content_render(void);

static void save_load_draw(Game *g)
{
    /* Slot present/empty is decided with the real save_present_slot (full
     * checksum); the banked body only draws the resulting text. */
    uint8_t present = 0;
    uint8_t i;

    menu_draw_frame((g->save_slot_mode == 1) ? "SAVE GAME" : "LOAD GAME");

    for (i = 0; i < SAVE_SLOT_COUNT; i++) {
        if (save_present_slot(i)) present |= (uint8_t)(1 << i);
    }
    g_bk_call_bank = 11;
    g_bk_call_target = (uint16_t)&save_load_content_render;
    g_bk_ptr_a = (void *)g;
    g_bk_byte_a = present;
    banked_call_run();
}

void save_load_screen_update(Game *g)
{
    uint8_t idx;
    if (!g) return;
    idx = g->save_slot_index;
    if (input_pressed(INPUT_UP) && idx > 0) {
        g->save_slot_index--;
        g->save_slot_message = 0;
        g->render_cache.valid = false;
    } else if (input_pressed(INPUT_DOWN) && (uint8_t)(idx + 1) < SAVE_SLOT_COUNT) {
        g->save_slot_index++;
        g->save_slot_message = 0;
        g->render_cache.valid = false;
    } else if (input_pressed(INPUT_A)) {
        if (g->save_slot_mode == 1) {
            scene_sync_from_world(g);
            save_game_slot(idx, &g->state);
            g->save_slot_message = 1;
            telemetry_emit(EVENT_GAME_SAVED, (uint8_t)(idx + 1), 0, 0, 0);
        } else if (save_present_slot(idx)) {
            load_game_slot(idx, &g->state);
            telemetry_emit(EVENT_GAME_LOADED, (uint8_t)(idx + 1), 0, 0, 0);
            scene_load(g, g->state.scene.scene_id, g->state.scene.player_x, g->state.scene.player_y);
            g->world.player.facing = (Direction)g->state.scene.player_facing;
            screen_change(g, SCREEN_OVERWORLD);
            return;
        } else {
            g->save_slot_message = 2;
        }
        g->render_cache.valid = false;
    } else if (input_pressed(INPUT_B) || input_pressed(INPUT_START)) {
        /* Return to the screen the player came from: overworld SELECT /
         * wizard SAVE go back to the overworld; title CONTINUE and the
         * game-over continue prompt go back to their own screens.  A
         * hardcoded SCREEN_OVERWORLD here boots a fresh game when the
         * save/load screen was opened from the title menu. */
        screen_change(g, g->prev_screen);
    }
}

void save_load_screen_render(Game *g)
{
    RenderCache *rc;
    if (!g) return;
    rc = &g->render_cache;
    if (!rc->valid || rc->prev_screen != SCREEN_SAVE_LOAD) {
        save_load_draw(g);
        telemetry_emit(EVENT_RENDER_SCREEN, (uint8_t)SCREEN_SAVE_LOAD, 0, 0, 0);
        rc->valid = true;
        rc->prev_screen = SCREEN_SAVE_LOAD;
    }
}
