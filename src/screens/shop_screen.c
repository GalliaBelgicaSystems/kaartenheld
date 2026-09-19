#include "game.h"
#include "screen.h"
#include "telemetry.h"
#include "rpg/cards.h"
#include "rpg/currency.h"
#include "rpg/deck.h"
#include "menu.h"
#include "shops.h"
#include "game_ids.h"
#include "ui.h"
#include "banked.h"

#define SHOP_MSG_NONE 0
#define SHOP_MSG_BOUGHT 1
#define SHOP_MSG_NO_GOLD 2
#define SHOP_MSG_MAX_COPIES 3

/* The active shop is chosen by the actor that was engaged (g->shop_id).
 * Cursor state lives in g->item_menu_index. */
static const ShopDefinition *shop_active(const Game *g)
{
    return game_shop_for_id(g ? g->shop_id : 1);
}

/* Full shop content draw lives in the bank-2 body (screen_content.c) so the
 * fixed _CODE/_HOME area stays under 0x8000 (AGENTS.md 52.11.1). */
void shop_content_render(void);
/* Shared caret repaint (bank 2, with the ITEM screen). */
void item_menu_cursor_banked(void);

/* Cursor-only move (AGENTS.md 36): stage the old index (banked_run -> body)
 * and let the bank-2 body set g->item_menu_index and repaint row 5+index.
 * No fixed-side cell math, no render-cache touch. */
static void shop_menu_step(Game *g, uint8_t new_index)
{
    g_bk_call_bank = 2;
    g_bk_call_target = (uint16_t)&item_menu_cursor_banked;
    g_bk_ptr_a = (void *)g;
    g_bk_byte_a = g->item_menu_index;   /* old index; body owns mutation */
    g_bk_byte_b = new_index;
    banked_call_run();
}

void shop_screen_update(Game *g)
{
    const ShopDefinition *def;
    uint8_t count;

    if (!g) return;
    def = shop_active(g);
    count = def ? def->count : 0;

    if (count > 0) {
        if (input_pressed(INPUT_UP) && g->item_menu_index > 0) {
            shop_menu_step(g, (uint8_t)(g->item_menu_index - 1));
        } else if (input_pressed(INPUT_DOWN) && (uint8_t)(g->item_menu_index + 1) < count) {
            shop_menu_step(g, (uint8_t)(g->item_menu_index + 1));
        } else if (input_pressed(INPUT_A) && g->item_menu_index < count) {
            CardId card_id = def->items[g->item_menu_index];
            const CardDefinition *card = card_get_def(card_id);
            uint8_t price = card ? card->price : 0;
            if (currency_get(&g->state, CURRENCY_ID_GOLD) < price) {
                g->shop_message = SHOP_MSG_NO_GOLD;
                telemetry_emit(EVENT_CARD_PURCHASE_FAILED, (uint8_t)card_id, 1, 0, 0);
            } else {
                if (!deck_collection_add(&g->state.cards, card_id, 1)) {
                    g->shop_message = SHOP_MSG_MAX_COPIES;
                    telemetry_emit(EVENT_CARD_PURCHASE_FAILED, (uint8_t)card_id, 2, 0, 0);
                } else {
                    currency_add(&g->state, CURRENCY_ID_GOLD, -(int16_t)price);
                    telemetry_emit(EVENT_CARD_PURCHASED, (uint8_t)card_id, (uint8_t)price, 0, 0);
                    g->shop_message = SHOP_MSG_BOUGHT;
                }
            }
            g->render_cache.valid = false;
        }
    }
    if (input_pressed(INPUT_B) || input_pressed(INPUT_START)) {
        g->shop_message = SHOP_MSG_NONE;
        g->item_menu_index = 0;
        audio_play_sfx(SFX_BACK);
        screen_change(g, SCREEN_OVERWORLD);
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
