#include "game.h"
#include "screen.h"
#include "telemetry.h"
#include "rpg/save.h"
#include "menu.h"
#include "scene.h"
#include "ui.h"
#include "banked.h"

/* Full save/load content draw lives in the bank-11 body
 * (save_load_banked.c) so the fixed _CODE/_HOME area stays under
 * 0x8000 (AGENTS.md 52.11.1). */
void save_load_content_render(void);

static void save_load_draw(Game *g)
{
    menu_draw_frame((g->save_slot_mode == 2) ? "NEW GAME" : "LOAD GAME");

    /* The bank-11 body probes the slots and paints the rows itself (one
     * dispatch; fixed-bank budget, AGENTS.md 52.18). */
    g_bk_call_bank = 11;
    g_bk_call_target = (uint16_t)&save_load_content_render;
    g_bk_ptr_a = (void *)g;
    banked_call_run();
}

void save_load_screen_update(Game *g)
{
    GameState *st;
    uint8_t slot;
    if (!g) return;
    /* The whole picker decision runs banked (fixed-bank budget,
     * AGENTS.md 52.18): nav edges, overwrite confirm, auto-continue.
     * The applier below owns screens plus the LOAD tail (load +
     * telemetry + scene_load must stay fixed: bank-8 dispatch and
     * fixed calls). */
    g_bk_call_bank = 12;
    g_bk_call_target = (uint16_t)&save_menu_update_banked;
    g_bk_ptr_a = (void *)g;
    banked_call_run();
    if (g_bk_byte_b == 1) {
        screen_change(g, SCREEN_INTRO);
    } else if (g_bk_byte_b == 2) {
        /* LOAD (manual or game-over auto): the slot was validated by
         * the picker probe already, but the load re-validates (same
         * frame SRAM could only change through this path, so it
         * always succeeds in practice; message 2 covers the rest). */
        slot = g_bk_byte_c;
        /* scene is GameState's first member: small offsets from the
         * staged pointer instead of full Game-relative addressing. */
        st = &g->state;
        if (!load_game_slot(slot, &g->state)) {
            g->save_slot_message = 2;
            g->render_cache.valid = false;
        } else {
            telemetry_emit(EVENT_GAME_LOADED, (uint8_t)(slot + 1), 0, 0, 0);
            scene_load(g, st->scene.scene_id, st->scene.player_x, st->scene.player_y);
            g->world.player.facing = (Direction)st->scene.player_facing;
            screen_change(g, SCREEN_OVERWORLD);
        }
    } else if (g_bk_byte_b == 3) {
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
