#include "game.h"
#include "screen.h"
#include "telemetry.h"
#include "ui.h"
#include "audio.h"
#include "banked.h"

/* ASCII intro sequence shown when starting a NEW GAME from the title
 * screen.  Three scripted slides (a hero of a new kind, cards as your
 * strength, the slime blight); A/START advances, the final slide drops
 * into a fresh overworld playthrough.  Rendering lives in the bank-4
 * intro_content body (title_content.c) to keep the fixed bank small. */

#define INTRO_SLIDE_COUNT 3

/* Shared slide-show renderer used by the intro, tutorial and splash
 * screens: a full clear, a banked content-body call staged on the address
 * of the renderer + slide byte, telemetry, and a render-cache commit.
 * `bank` selects the content body's ROM bank (4 = ASCII slides, 2 = the
 * studio splash bitmap body).  Kept in the fixed bank to hold the fixed
 * _CODE/_HOME area within budget (make memmap). */

void slide_screen_render(Game *g, ScreenId scr, uint16_t content_target, uint8_t slide, uint8_t bank)
{
    RenderCache *rc;
    if (!g) return;
    rc = &g->render_cache;
    if (!rc->valid || rc->prev_screen != scr) {
        ui_clear_screen();
        g_bk_call_bank = bank;
        g_bk_call_target = content_target;
        g_bk_byte_a = slide;
        banked_call_run();
        telemetry_emit(EVENT_RENDER_SCREEN, (uint8_t)scr, 0, 0, 0);
        rc->valid = true;
        rc->prev_screen = scr;
    }
}

void intro_screen_render(Game *g)
{
    slide_screen_render(g, SCREEN_INTRO, (uint16_t)&intro_content_render, g->intro_slide, 4);
}

void intro_screen_update(Game *g)
{
    if (!g) return;
    if (input_pressed(INPUT_A) || input_pressed(INPUT_START)) {
        audio_play_sfx(SFX_CONFIRM);
        if (g->intro_slide + 1 >= INTRO_SLIDE_COUNT) {
            g->intro_slide = 0;
            game_restart(g);
        } else {
            g->intro_slide++;
            /* Force a redraw of the new slide. */
            g->render_cache.valid = false;
        }
    }
}
