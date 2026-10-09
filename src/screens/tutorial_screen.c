#include "game.h"
#include "screen.h"
#include "telemetry.h"
#include "ui.h"
#include "audio.h"
#include "banked.h"

/* ASCII tutorial screen.  Left/right navigates slides, B exits to title.
 * Rendering lives in bank-2 body (tutorial_content.c) to keep fixed bank small. */

void tutorial_screen_render(Game *g)
{
    slide_screen_render(g, SCREEN_TUTORIAL, (uint16_t)&tutorial_content_render, g->tutorial_slide, 4);
}

void tutorial_screen_update(Game *g)
{
    if (!g) return;

    if (input_pressed(INPUT_LEFT)) {
        if (g->tutorial_slide == 0) g->tutorial_slide = TUTORIAL_SLIDE_COUNT - 1;
        else g->tutorial_slide--;
        audio_play_sfx(SFX_CURSOR);
        g->render_cache.valid = false;
    }
    if (input_pressed(INPUT_RIGHT)) {
        if (g->tutorial_slide + 1 >= TUTORIAL_SLIDE_COUNT) g->tutorial_slide = 0;
        else g->tutorial_slide++;
        audio_play_sfx(SFX_CURSOR);
        g->render_cache.valid = false;
    }
    if (input_pressed(INPUT_B) || input_pressed(INPUT_START) || input_pressed(INPUT_A)) {
        g->tutorial_slide = 0;
        screen_change(g, SCREEN_TITLE);
    }
}