#include "travel.h"
#include "game.h"
#include "screen.h"
#include "banked.h"

/* Journey screen (travel-demo ROM only).  The paint lives in the bank-9
 * body (travel_paint_banked.c: the fixed bank cannot host it, AGENTS.md
 * 52.18); this TU keeps only the thin dispatch plus the render-cache
 * validation (same contract as the shop screen: paint once per
 * transition on the LCD-off full-redraw path, skip steady frames). */

void travel_paint_banked(void);

void travel_screen_update(Game *g)
{
    travel_update(g);
}

void travel_screen_render(Game *g)
{
    RenderCache *rc;
    if (!g) return;
    rc = &g->render_cache;
    if (rc->valid && rc->prev_screen == SCREEN_JOURNEY) return;
    g_bk_call_bank = 12;
    g_bk_call_target = (uint16_t)&travel_paint_banked;
    banked_call_run();
    rc->valid = true;
    rc->prev_screen = SCREEN_JOURNEY;
}
