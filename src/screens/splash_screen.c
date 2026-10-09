#include "game.h"
#include "screen.h"
#include "ui.h"

/* Studio splash shown once at boot, before the title screen.  The logo is
 * a bitmap (assets/gallia_belgica_systems.png) rendered by the bank-2
 * ui_splash_logo_render_banked body; the render wrapper reuses the shared
 * slide renderer (intro_screen.c).  The splash auto-advances after
 * SPLASH_FRAMES, or immediately on START/A. */
#define SPLASH_FRAMES 150

void splash_screen_render(Game *g)
{
    slide_screen_render(g, SCREEN_SPLASH,
                        (uint16_t)&ui_splash_logo_render_banked, 0, 2);
}

void splash_screen_update(Game *g)
{
    if (!g) return;
    if (g->frame >= SPLASH_FRAMES ||
        input_pressed(INPUT_START) || input_pressed(INPUT_A)) {
        screen_change(g, SCREEN_TITLE);
    }
}
