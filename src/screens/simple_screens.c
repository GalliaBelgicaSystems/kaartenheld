#include "game.h"
#include "screen.h"
#include "telemetry.h"
#include "ui.h"
#include "banked.h"

/* The simple screens' render bodies (strings + text drawing) live in the
 * bank-2 body (simple_content.c) to keep the fixed _CODE/_HOME area small
 * (see make memmap).  These fixed wrappers keep the render-cache gating
 * and input/transition logic, then dispatch to the banked body with the
 * current UI state staged in g_bk_byte_a. */

/* Frames the THANKS screen holds before auto-restarting (~5 s at 60 fps). */
#define THANKS_HOLD_FRAMES 300

/* Frames spent on the current THANKS visit.  Reset on entry from the
 * game-over NO branch below; BSS-zero covers direct boot-into-THANKS. */
static uint16_t s_thanks_frames;

void thanks_screen_update(Game *g)
{
    if (!g) return;
    /* Demo outro: hold the thanks text, then restart the game. */
    if (++s_thanks_frames >= THANKS_HOLD_FRAMES) {
        s_thanks_frames = 0;
        game_restart(g);
    }
}

void thanks_screen_render(Game *g)
{
    RenderCache *rc;
    if (!g) return;
    rc = &g->render_cache;

    if (!rc->valid || rc->prev_screen != SCREEN_THANKS) {
        ui_clear_screen();
        g_bk_call_bank = 9;
        g_bk_call_target = (uint16_t)&thanks_content_render;
        banked_call_run();
        telemetry_emit(EVENT_RENDER_SCREEN, (uint8_t)SCREEN_THANKS, 0, 0, 0);
        rc->valid = true;
        rc->prev_screen = SCREEN_THANKS;
    }
}

void ending_screen_update(Game *g)
{
    if (g && (input_pressed(INPUT_A) || input_pressed(INPUT_START))) {
        game_restart(g);
    }
}

void ending_screen_render(Game *g)
{
    RenderCache *rc;
    if (!g) return;
    rc = &g->render_cache;
    if (!rc->valid || rc->prev_screen != SCREEN_ENDING) {
        ui_clear_screen();
        g_bk_call_bank = 4;
        g_bk_call_target = (uint16_t)&ending_content_render;
        banked_call_run();
        telemetry_emit(EVENT_RENDER_SCREEN, (uint8_t)SCREEN_ENDING, 0, 0, 0);
        rc->valid = true;
        rc->prev_screen = SCREEN_ENDING;
    }
}

void game_over_screen_update(Game *g)
{
    if (!g) return;

    if (input_pressed(INPUT_UP) || input_pressed(INPUT_DOWN)) {
        g->game_over_choice = !g->game_over_choice;
    }
    if (input_pressed(INPUT_A) || input_pressed(INPUT_START)) {
        if (g->game_over_choice == 0) {
            screen_change(g, SCREEN_SAVE_LOAD);
            g->save_slot_mode = 0; /* LOAD */
            g->save_slot_index = 0;
        } else {
            s_thanks_frames = 0;
            screen_change(g, SCREEN_THANKS);
        }
    }
}

void game_over_screen_render(Game *g)
{
    RenderCache *rc;
    if (!g) return;
    rc = &g->render_cache;

    if (!rc->valid || rc->prev_screen != SCREEN_GAME_OVER ||
        g->game_over_choice != rc->prev_game_over_choice) {
        ui_clear_screen();
        g_bk_call_bank = 4;
        g_bk_call_target = (uint16_t)&game_over_content_render;
        g_bk_byte_a = g->game_over_choice;
        banked_call_run();
        telemetry_emit(EVENT_RENDER_SCREEN, (uint8_t)SCREEN_GAME_OVER, 0, 0, 0);
        rc->valid = true;
        rc->prev_screen = SCREEN_GAME_OVER;
        rc->prev_game_over_choice = g->game_over_choice;
    }
}
