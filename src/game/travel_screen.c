#include "travel.h"
#include "game.h"
#include "screen.h"
#include "banked.h"
#include "ui.h"

/* Journey screen (travel-demo ROM only).  The paint lives in the bank-13
 * body (travel_paint_banked.c: the fixed bank cannot host it, AGENTS.md
 * 52.18); this TU keeps only the thin dispatch, first-entry CRAM reset,
 * and render-cache validation (same contract as the shop/item screens). */

void travel_paint_banked(void);

void travel_screen_update(Game *g)
{
    travel_update(g);
}

void travel_screen_render(Game *g)
{
    RenderCache *rc;
    uint8_t same_screen;
    if (!g) return;
    rc = &g->render_cache;
    if (rc->valid && rc->prev_screen == SCREEN_JOURNEY) return;
    same_screen = (uint8_t)(rc->prev_screen == SCREEN_JOURNEY);
    if (!same_screen) {
        /* UI-mode CRAM (base set): clear any world/battle tint before
         * painting the journey UI (mirrors shop/item screens). */
        ui_set_cram_palette(0);
    }
    g_bk_call_bank = 13;
    g_bk_call_target = (uint16_t)&travel_paint_banked;
    g_bk_byte_a = same_screen;
    banked_call_run();
    rc->valid = true;
    rc->prev_screen = SCREEN_JOURNEY;
}
