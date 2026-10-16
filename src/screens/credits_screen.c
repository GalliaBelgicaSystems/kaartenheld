#include "game.h"
#include "screen.h"
#include "telemetry.h"
#include "ui.h"
#include "audio.h"
#include "banked.h"

/* Static credit screen (title menu entry).  Single page: A/START exits
 * back to the title (intro convention).  Rendering lives in the bank-5
 * credits_content body (credits_content.c: bank 4 is full) to keep the
 * fixed bank small.  Input reuses tutorial_screen_update: it exits to
 * TITLE on A/B/START and its LEFT/RIGHT only touches tutorial_slide,
 * which the title entry and the tutorial exit both reset to 0 -- so the
 * reuse is unobservable (and the fixed bank has no room for a dedicated
 * 30 B updater; AGENTS.md 52.18). */

void credits_screen_render(Game *g)
{
    slide_screen_render(g, SCREEN_CREDITS, (uint16_t)&credits_content_render, 0, 5);
}
