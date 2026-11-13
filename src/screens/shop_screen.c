#include "game.h"
#include "screen.h"
#include "telemetry.h"
#include "rpg/cards.h"
#include "rpg/currency.h"
#include "rpg/deck.h"
#include "rpg/loot.h"
#include "menu.h"
#include "shops.h"
#include "economy.h"
#include "game_ids.h"
#include "ui.h"
#include "banked.h"

#define SHOP_MSG_NONE 0
#define SHOP_MSG_BOUGHT 1
#define SHOP_MSG_NO_GOLD 2
#define SHOP_MSG_MAX_COPIES 3

/* BUY/SELL tabs (docs/loot.md §34.6): the BUY list shows the stock --
 * one-copy rares hide once owned, price-0 basics (free starter rebuys)
 * always list, capped by max_copies at buy time.  The SELL list shows
 * the player's sellable cards (rarity-cut value for curated stock,
 * full formula value for loot; quest items and free basics never
 * list).  The active tab rides g->item_menu_tab (0 = BUY, 1 = SELL;
 * the item screen owns the other values and resets on open).
 * The cursor addresses VISIBLE positions (dense from row 5).  The
 * visible map is rebuilt by the bank-2 render on every full redraw
 * (entry, purchase) into WRAM (g_shop_vis_map, owned by
 * screen_content.c); this update only reads it, so the fixed bank pays
 * no scan (AGENTS.md §52.11.1).  A purchase hides its own row: the
 * redraw rebuilds + clamps before painting, so no stale row is ever
 * addressable. */
extern uint8_t g_shop_vis_map[];
extern uint8_t g_shop_vis_count;
/* Banked row quote (screen_content.c, bank 2: needs the catalog +
 * loot synth + material tables, and reads the WRAM deck directly):
 * g_bk_ptr_a = Game*, g_bk_byte_a = visible row; stages price in
 * g_bk_byte_b (0 = refuse), card id in g_bk_byte_c, owned-after in
 * g_bk_byte_d (SELL only). */
void shop_row_quote_banked(void);

/* Shared quote dispatch (both tabs resolve through the active tab's
 * map bank-side, so the fixed bank pays no catalog/collection scan). */
static void shop_row_quote(Game *g)
{
    g_bk_call_bank = 2;
    g_bk_call_target = (uint16_t)&shop_row_quote_banked;
    g_bk_ptr_a = (void *)g;
    g_bk_byte_a = g->item_menu_index;
    banked_call_run();
}

/* Full shop content draw lives in the bank-2 body (screen_content.c) so the
 * fixed _CODE/_HOME area stays under 0x8000 (AGENTS.md 52.11.1). */
void shop_content_render(void);
/* Shared caret repaint (bank 11: bank 2 is full, and the cursor is pure
 * WRAM math needing no ROM tables). */
void item_menu_step_banked(void);

/* Direction nav (AGENTS.md 36): stage old index + direction + visible
 * count; the bank-11 body clamps, sets g->item_menu_index and repaints
 * row 5+index.  Bounds live banked so the fixed bank pays no branch.
 * No fixed-side cell math, no render-cache touch. */
static void shop_menu_step(Game *g, uint8_t down)
{
    g_bk_call_bank = 11;
    g_bk_call_target = (uint16_t)&item_menu_step_banked;
    g_bk_ptr_a = (void *)g;
    g_bk_byte_a = g->item_menu_index;
    g_bk_byte_b = down;
    g_bk_byte_c = g_shop_vis_count;
    banked_call_run();
}

/* SELL-tab apply: A sells one copy of the cursor row at its quoted
 * value (0 refuses with a BACK blip -- fully decked rows and
 * unlistable rows never reach here through the map, so this is a
 * guard, not a path).  The banked quote verified ownership, so the
 * remove always lands.  The last copy of a one-copy card re-opens
 * its BUY row. */
static void shop_sell(Game *g)
{
    uint8_t sold_id;
    uint8_t sold_price;
    uint8_t sold_left;
    shop_row_quote(g);
    if (g_bk_byte_b == 0) {
        audio_play_sfx(SFX_BACK);
        return;
    }
    /* Snapshot the quote: currency_add dispatches through the bank-11
     * body via the g_bk_* staging globals, so it clobbers the staged
     * id/price/left before the emit below reads them. */
    sold_id = g_bk_byte_c;
    sold_price = g_bk_byte_b;
    sold_left = g_bk_byte_d;
    deck_collection_remove(&g->state.cards, sold_id, 1);
    currency_add(&g->state, CURRENCY_ID_GOLD, (int16_t)sold_price);
    telemetry_emit(EVENT_CARD_SOLD, sold_id, sold_price,
                   sold_left, 0);
    g->render_cache.valid = false;
}

/* A-handler split out of shop_screen_update (fixed-bank cascade:
 * one 723 B function allocates far worse than three small ones,
 * AGENTS.md §52.18).  Buy verdict/apply + messages; the quote (id +
 * price) arrives banked, so the fixed bank pays no catalog scan.
 * Price 0 = free basic: the gold check always passes, max_copies
 * still caps the rebuy. */
static void shop_confirm(Game *g)
{
    shop_row_quote(g);
    if (currency_get(&g->state, CURRENCY_ID_GOLD) < g_bk_byte_b) {
        g->shop_message = SHOP_MSG_NO_GOLD;
        telemetry_emit(EVENT_CARD_PURCHASE_FAILED, g_bk_byte_c, 1, 0, 0);
    } else {
        if (!deck_collection_add(&g->state.cards, g_bk_byte_c, 1)) {
            g->shop_message = SHOP_MSG_MAX_COPIES;
            telemetry_emit(EVENT_CARD_PURCHASE_FAILED, g_bk_byte_c, 2, 0, 0);
        } else {
            uint8_t paid_id;
            uint8_t paid_price;
            /* Snapshot the quote (same staging-clobber reason as
             * shop_sell): currency_add reuses g_bk_byte_* for its
             * own bank-11 dispatch. */
            paid_id = g_bk_byte_c;
            paid_price = g_bk_byte_b;
            currency_add(&g->state, CURRENCY_ID_GOLD, -(int16_t)paid_price);
            /* Shop spend notes gold_out (docs/economy.md §6.1). */
            economy_note_spent(&g->state, paid_price);
            telemetry_emit(EVENT_CARD_PURCHASED, paid_id, paid_price, 0, 0);
            g->shop_message = SHOP_MSG_BOUGHT;
        }
    }
    g->render_cache.valid = false;
}

/* B/START exit split out of shop_screen_update (same cascade reason).
 * Leaving after a purchase stages the merchant's equip tip for the
 * post-tick dialogue slot (docs/loot.md 34.6): the tip opens as a
 * normal dialogue box instead of the old in-shop hint lines, and
 * fires once per buying visit.  Staged, not opened here: the fixed
 * bank sits hard against 0x8000, and a dialogue_start_def +
 * screen_change pair here overflows it into bank 2 (AGENTS.md
 * §52.18).  Target DIALOGUE directly (not OVERWORLD): the tilemap
 * still holds the shop screen when the post-tick opens the dialogue
 * the same frame, and the dialogue renderer only does its full
 * LCD-off world redraw when prev_screen != OVERWORLD -- routing
 * through OVERWORLD first leaves the stale SHOP title in VRAM row 0
 * under the box and on the overworld after (walk-a shop-closed
 * regression).  screen_change is idempotent, so the post-tick's own
 * screen_change(DIALOGUE) is a no-op.  Dialogue close returns to
 * the overworld through the standard exit path (no stashed
 * encounter here, unlike Carl's lesson). */
static void shop_leave(Game *g)
{
    if (g->shop_message == SHOP_MSG_BOUGHT)
        g_bk_dlg_id = (uint8_t)DIALOGUE_ID_MERCHANT_EQUIP_TIP;
    /* Shop-boundary market update (docs/economy.md §6): runs on every
     * exit; the guards skip zero-counter / zero-battle visits, so
     * browsing never moves the index. */
    g_bk_call_bank = 11;
    g_bk_call_target = (uint16_t)&economy_update_banked;
    banked_call_run();
    g->shop_message = SHOP_MSG_NONE;
    g->item_menu_index = 0;
    audio_play_sfx(SFX_BACK);
    /* Single call site: the ternary keeps the fixed bank small
     * (two call sites cost ~8 B the 0x8000-ceilinged bank cannot
     * spare -- debug headroom is 10 B with this shape).  Reading
     * back g_bk_dlg_id is safe: no other shop-frame producer
     * stages the post-tick dialogue slot, so nonzero here means
     * this exit staged the tip. */
    screen_change(g, (g_bk_dlg_id != 0) ? SCREEN_DIALOGUE : SCREEN_OVERWORLD);
}

void shop_screen_update(Game *g)
{
    if (!g) return;

    /* LEFT/RIGHT flips the BUY/SELL tab (cursor reset, full redraw). */
    if (input_pressed(INPUT_LEFT) || input_pressed(INPUT_RIGHT)) {
        g->item_menu_tab = (uint8_t)(g->item_menu_tab == 0 ? 1 : 0);
        g->item_menu_index = 0;
        g->item_menu_scroll = 0;
        g->render_cache.valid = false;
        return;
    }

    if (g_shop_vis_count > 0) {
        if (input_pressed(INPUT_UP)) {
            shop_menu_step(g, 0);
        } else if (input_pressed(INPUT_DOWN)) {
            shop_menu_step(g, 1);
        } else if (input_pressed(INPUT_A) && g->item_menu_index < g_shop_vis_count) {
            if (g->item_menu_tab == 1)
                shop_sell(g);
            else
                shop_confirm(g);
        }
    }
    if (input_pressed(INPUT_B) || input_pressed(INPUT_START)) {
        shop_leave(g);
    }
}

void shop_screen_render(Game *g)
{
    RenderCache *rc;

    if (!g) return;
    rc = &g->render_cache;

    if (!rc->valid || rc->prev_screen != SCREEN_SHOP) {
        menu_draw_frame("SHOP");
        /* UI-mode CRAM (base set): the card icons stamped below are
         * encoded with the battle display ramps (card_display_slots.json).
         * Without this they would inherit the world tileset's CRAM and
         * show village/forest colors.  The overworld/battle full redraws
         * restore their own CRAM on exit, so no restore is needed here. */
        ui_set_cram_palette(0);
        g_bk_call_bank = 2;
        g_bk_call_target = (uint16_t)&shop_content_render;
        g_bk_ptr_a = (void *)g;
        banked_call_run();
        telemetry_emit(EVENT_RENDER_SCREEN, (uint8_t)SCREEN_SHOP, 0, 0, 0);
        rc->valid = true;
        rc->prev_screen = SCREEN_SHOP;
    }
}
