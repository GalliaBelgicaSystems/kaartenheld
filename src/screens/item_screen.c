#include "game.h"
#include "screen.h"
#include "telemetry.h"
#include "rpg/cards.h"
#include "rpg/status.h"
#include "rpg/loot.h"
#include "rpg/deck.h"
#include "rpg/currency.h"
#include "quest.h"
#include "ui.h"
#include "menu.h"
#include "card.h"
#include "shops.h"
#include "banked.h"
#include "game_ids.h"

/* Bank-2 helper (screen_content.c); keeps the fixed bank small
 * (AGENTS.md 52.18). */
void item_menu_cursor_banked(void);

#define TAB_CARDS 0
#define TAB_QUEST 1
#define NUM_TABS  2

/* Cursor index 0 on the CARDS tab is the FILTER/SORT row; card entries
 * start at 1. */
#define ROW_TOP     0
#define FIRST_CARD  1
#define VISIBLE_CARDS 5

#define MODE_LIST         0
#define MODE_CARD_DETAIL  1
#define MODE_QUEST_DETAIL 2
#define MODE_PICKER       3

#define MSG_NONE       0
#define MSG_DECK_FULL  1
#define MSG_DECK_MIN   2
#define MSG_QUEST_ITEM 3

#define FILTER_ALL      0xFF
#define SORT_NONE       0
#define SORT_TYPE       1
#define SORT_POWER      2
#define SORT_COST       3
#define SORT_POWER_DESC 4
#define SORT_COST_DESC  5

/* Filtered/sorted view over the deckable collection entries.  s_view_indices
 * holds collection indices; s_view_count is the number of visible cards. */
uint8_t s_view_indices[MAX_CARD_COLLECTION];
uint8_t s_view_count;

/* SPECIAL cards (quest items like the Lost Amulet) are LISTED but never
 * deckable -- pressing A shows a transient QUEST ITEM message instead. */
static bool card_deckable(const CardDefinition *def)
{
    return def && def->type != CARD_TYPE_SPECIAL;
}

static CardId view_card_id(Game *g, uint8_t pos)
{
    return g->state.cards.collection.entries[s_view_indices[pos]].id;
}

/* ── Sell at the card merchant (docs/loot.md §24/§34.6) ─────────────
 * A loot card's detail page offers SELL while the player has engaged a
 * buying shop (g->shop_id set by the shop actor, cleared on scene
 * change).  Value = the def's price field -- for loot ids that is the
 * centralized sell value synthesized by loot_synth_banked. */
static bool merchant_buys(const Game *g)
{
    const ShopDefinition *shop;
    shop = game_shop_for_id(g->shop_id);
    return shop && shop->buys;
}

static void card_detail_sell(Game *g)
{
    CardState *cs = &g->state.cards;
    CardId id;
    const CardDefinition *def;
    uint8_t owned;

    if ((uint8_t)(g->item_menu_index - FIRST_CARD) >= s_view_count)
        return;
    id = view_card_id(g, (uint8_t)(g->item_menu_index - FIRST_CARD));
    if (!loot_is_loot_id(id) || !merchant_buys(g)) return;
    def = card_get_def(id);

    /* Selling must never strand a decked card without ownership
     * backing: require at least one copy outside the battle deck. */
    owned = deck_collection_count(cs, id);
    if (def == NULL || owned <= deck_count_in_deck(&cs->deck, id)) {
        g->item_menu_message = MSG_DECK_FULL;
        g->item_menu_msg_ttl = 45;
        g->item_menu_mode = MODE_LIST;
        return;
    }
    if (!deck_collection_remove(cs, id, 1)) return;
    currency_add(&g->state, CURRENCY_ID_GOLD, (int16_t)def->price);
    telemetry_emit(EVENT_CARD_SOLD, id, def->price,
                   (uint8_t)(owned - 1), 0);
    /* Back to the list: it rebuilds its view every frame, so a fully
     * sold-out entry simply disappears and the OWN digit drops -- the
     * CARD_SOLD event is the authoritative feedback for tests. */
    g->item_menu_mode = MODE_LIST;
}

static bool filter_matches(Game *g, CardId id)
{
    const CardDefinition *def;
    if (g->item_menu_filter == FILTER_ALL) return true;
    def = card_get_def(id);
    if (!def) return false;
    return def->type == g->item_menu_filter;
}

static uint8_t sort_value(const CardDefinition *def, uint8_t sort)
{
    switch (sort) {
        case SORT_TYPE:  return def->type;
        case SORT_POWER:
        case SORT_POWER_DESC: return def->power;
        case SORT_COST:
        case SORT_COST_DESC:  return def->cost;
        default: return 0;
    }
}

static void sort_view(Game *g)
{
    uint8_t i, j, tmp, va, vb;
    uint8_t desc = (g->item_menu_sort == SORT_POWER_DESC ||
                    g->item_menu_sort == SORT_COST_DESC);

    for (i = 1; i < s_view_count; i++) {
        tmp = s_view_indices[i];
        va = sort_value(card_get_def(g->state.cards.collection.entries[tmp].id), g->item_menu_sort);
        j = i;
        while (j > 0) {
            vb = sort_value(card_get_def(g->state.cards.collection.entries[s_view_indices[j - 1]].id),
                            g->item_menu_sort);
            if (desc ? (vb < va) : (vb > va)) {
                s_view_indices[j] = s_view_indices[j - 1];
                j--;
            } else {
                break;
            }
        }
        s_view_indices[j] = tmp;
    }
}

static void build_view(Game *g)
{
    uint8_t total, i;
    const CardDefinition *def;

    s_view_count = 0;
    total = g->state.cards.collection.count;
    for (i = 0; i < total; i++) {
        def = card_get_def(g->state.cards.collection.entries[i].id);
        if (!card_deckable(def)) continue;
        if (filter_matches(g, g->state.cards.collection.entries[i].id))
            s_view_indices[s_view_count++] = i;
    }
    if (g->item_menu_sort != SORT_NONE && s_view_count > 1)
        sort_view(g);
}

static void cycle_filter(Game *g)
{
    if (g->item_menu_filter == FILTER_ALL)
        g->item_menu_filter = CARD_TYPE_ATTACK;
    else if (g->item_menu_filter == CARD_TYPE_ATTACK)
        g->item_menu_filter = CARD_TYPE_DEFENSE;
    else if (g->item_menu_filter == CARD_TYPE_DEFENSE)
        g->item_menu_filter = CARD_TYPE_HEAL;
    else if (g->item_menu_filter == CARD_TYPE_HEAL)
        g->item_menu_filter = CARD_TYPE_STATUS;
    else if (g->item_menu_filter == CARD_TYPE_STATUS)
        g->item_menu_filter = CARD_TYPE_UTILITY;
    else
        g->item_menu_filter = FILTER_ALL;
}

static void cycle_sort(Game *g)
{
    /* Wrap instead of % -- SM83 has no hardware divide and the SDCC
     * int-mod library would land in the tight fixed bank. */
    g->item_menu_sort = (g->item_menu_sort >= SORT_COST_DESC) ?
        SORT_NONE : (uint8_t)(g->item_menu_sort + 1);
}

/* Shared label tables (fixed bank, same file — safe per AGENTS.md §54.6). */
static const char *const s_card_type_names[CARD_TYPE_COUNT] = {
    "ATK", "DEF", "HEL", "STS", "UTL", "SPL"
};

static const char *card_type_name(uint8_t t)
{
    return (t < CARD_TYPE_COUNT) ? s_card_type_names[t] : "SPL";
}

static const char *filter_name(uint8_t f)
{
    return (f <= CARD_TYPE_UTILITY) ? s_card_type_names[f] : "ALL";
}

static const char *sort_name(uint8_t s)
{
    switch (s) {
        case SORT_TYPE:       return "TYPE";
        case SORT_POWER:      return "PWR+";
        case SORT_COST:       return "CST+";
        case SORT_POWER_DESC: return "PWR-";
        case SORT_COST_DESC:  return "CST-";
        default:              return "OFF";
    }
}

static void switch_tab(Game *g)
{
    g->item_menu_tab = (g->item_menu_tab == TAB_CARDS) ? TAB_QUEST : TAB_CARDS;
    g->item_menu_index = ROW_TOP;
    g->item_menu_scroll = 0;
    g->render_cache.valid = false;
}

/* Targeted cursor move (AGENTS.md 36).  The bank-2 body (screen_content.c)
 * computes the caret rows, repaints the two cells via sc_put_char (mirrored
 * into g_ui_screen_buf) and, when the visible window must shift, moves the
 * scroll and flags g->render_cache.valid=false so render takes the rare
 * full-redraw path.  A pure caret move never touches the render cache, so
 * the big item_screen_render stays at its baseline shape (AGENTS.md 52.19).
 * No staging bytes beyond the old/new indices; the body writes game state
 * directly through the staged Game* (WRAM is always mapped). */
static void item_menu_step(Game *g, uint8_t new_index)
{
    g_bk_call_bank = 2;
    g_bk_call_target = (uint16_t)&item_menu_cursor_banked;
    g_bk_ptr_a = (void *)g;
    g_bk_byte_a = g->item_menu_index;
    g_bk_byte_b = new_index;
    banked_call_run();
}


void item_screen_reset(Game *g)
{
    g->item_menu_index = ROW_TOP;
    g->item_menu_tab = TAB_CARDS;
    g->item_menu_scroll = 0;
    g->item_menu_filter = FILTER_ALL;
    g->item_menu_sort = SORT_NONE;
    g->item_menu_mode = MODE_LIST;
    g->item_menu_message = MSG_NONE;
}

static void close_menu(Game *g)
{
    item_screen_reset(g);
    screen_change(g, g->prev_screen);
}

void item_screen_update(Game *g)
{
    uint8_t total;

    /* Transient rejection messages expire on their own (~0.75s). */
    if (g->item_menu_msg_ttl > 0) {
        g->item_menu_msg_ttl--;
        if (g->item_menu_msg_ttl == 0) {
            g->item_menu_message = MSG_NONE;
            g->render_cache.valid = false;
        }
    }

    if (input_pressed(INPUT_START)) {
        if (g->item_menu_mode == MODE_LIST) {
            audio_play_sfx(SFX_BACK);
            close_menu(g);
        } else {
            g->item_menu_mode = MODE_LIST;
            g->render_cache.valid = false;
        }
        return;
    }
    if (input_pressed(INPUT_B)) {
        switch (g->item_menu_mode) {
            case MODE_CARD_DETAIL:
            case MODE_QUEST_DETAIL:
            case MODE_PICKER:
                if (g->item_menu_mode == MODE_PICKER) {
                    g->item_menu_filter = g->item_menu_prev_filter;
                    g->item_menu_sort = g->item_menu_prev_sort;
                    build_view(g);
                }
                g->item_menu_mode = MODE_LIST;
                break;
            default:
                if (g->item_menu_tab == TAB_CARDS &&
                    g->item_menu_index > ROW_TOP) {
                    /* Two-step close: first B jumps to the top row. */
                    g->item_menu_index = ROW_TOP;
                    g->item_menu_scroll = 0;
                } else {
                    close_menu(g);
                }
                break;
        }
        audio_play_sfx(SFX_BACK);
        g->render_cache.valid = false;
        return;
    }

    if (g->item_menu_tab == TAB_QUEST) {
        if (g->item_menu_mode == MODE_QUEST_DETAIL) return;
        if (input_pressed(INPUT_SELECT)) {
            g->item_menu_mode = MODE_QUEST_DETAIL;
            g->render_cache.valid = false;
            return;
        }
        if (input_pressed(INPUT_LEFT) || input_pressed(INPUT_RIGHT)) {
            switch_tab(g);
            return;
        }
        build_view(g);
        total = quest_count();
        if (total == 0) return;
        if (input_pressed(INPUT_UP)) {
            item_menu_step(g, (g->item_menu_index > 0) ?
                (uint8_t)(g->item_menu_index - 1) : (uint8_t)(total - 1));
        } else if (input_pressed(INPUT_DOWN)) {
            item_menu_step(g, (g->item_menu_index < (uint8_t)(total - 1)) ?
                (uint8_t)(g->item_menu_index + 1) : 0);
        }
        return;
    }

    /* ── CARDS tab ─────────────────────────────────────────────── */
    switch (g->item_menu_mode) {
        case MODE_CARD_DETAIL:
            if (input_pressed(INPUT_SELECT)) {
                g->item_menu_mode = MODE_LIST;
                g->render_cache.valid = false;
            } else if (input_pressed(INPUT_A)) {
                card_detail_sell(g);
                g->render_cache.valid = false;
            }
            return;
        case MODE_PICKER:
            if (input_pressed(INPUT_UP) || input_pressed(INPUT_DOWN)) {
                g->item_menu_pick_row =
                    (uint8_t)(g->item_menu_pick_row == 0 ? 1 : 0);
                g->render_cache.valid = false;
            } else if (input_pressed(INPUT_LEFT) || input_pressed(INPUT_RIGHT)) {
                if (g->item_menu_pick_row == 0) cycle_filter(g);
                else cycle_sort(g);
                build_view(g);
                g->render_cache.valid = false;
            } else if (input_pressed(INPUT_A)) {
                g->item_menu_mode = MODE_LIST;
                g->render_cache.valid = false;
            }
            return;
        default:
            break;
    }

    /* Tab switch (LIST mode only — submenus consume LR first). */
    if (input_pressed(INPUT_LEFT) || input_pressed(INPUT_RIGHT)) {
        switch_tab(g);
        return;
    }

    /* One fresh view per frame for every LIST-mode path below. */
    build_view(g);

    if (input_pressed(INPUT_SELECT)) {
        if (g->item_menu_index >= FIRST_CARD &&
            (uint8_t)(g->item_menu_index - FIRST_CARD) < s_view_count) {
            g->item_menu_mode = MODE_CARD_DETAIL;
            g->render_cache.valid = false;
        }
        return;
    }
    if (input_pressed(INPUT_A)) {
        if (g->item_menu_index == ROW_TOP) {
            g->item_menu_mode = MODE_PICKER;
            g->item_menu_pick_row = 0;
            g->item_menu_prev_filter = g->item_menu_filter;
            g->item_menu_prev_sort = g->item_menu_sort;
        } else {
            CardState *cs = &g->state.cards;
            CardId id;
            if ((uint8_t)(g->item_menu_index - FIRST_CARD) >= s_view_count)
                return;
            id = view_card_id(g, (uint8_t)(g->item_menu_index - FIRST_CARD));
            /* Count-up-then-clear: A adds one copy; once the card is fully
             * decked (add rejected for per-card reasons, not a full deck),
             * A clears every decked copy of it — but only if the remaining
             * deck would still hold DECK_MIN_CARDS cards (all-or-nothing). */
            if (card_get_def(id)->type == CARD_TYPE_SPECIAL) {
                g->item_menu_message = MSG_QUEST_ITEM;
                g->item_menu_msg_ttl = 45;
                /* Without the invalidate the transient message never
                 * appears until the next unrelated redraw. */
                g->render_cache.valid = false;
                return;
            }
            if (deck_add_card(cs, id)) {
                telemetry_emit(EVENT_CARD_ADDED_TO_DECK, id, 0, 0, 0);
                audio_play_sfx(SFX_SELECT);
            } else if (cs->deck.count >= MAX_DECK_CARDS) {
                g->item_menu_message = MSG_DECK_FULL;
                g->item_menu_msg_ttl = 45;
            } else if ((uint8_t)(cs->deck.count -
                                 deck_count_in_deck(&cs->deck, id)) <
                       DECK_MIN_CARDS) {
                g->item_menu_message = MSG_DECK_MIN;
                g->item_menu_msg_ttl = 45;
            } else {
                while (deck_remove_card(cs, id))
                    telemetry_emit(EVENT_CARD_REMOVED_FROM_DECK, id, 0, 0, 0);
            }
        }
        g->render_cache.valid = false;
        return;
    }

    total = (uint8_t)(s_view_count + 1); /* + the FILTER/SORT row */
    if (total <= 1) return;

    if (input_pressed(INPUT_UP)) {
        item_menu_step(g, (g->item_menu_index > 0) ?
            (uint8_t)(g->item_menu_index - 1) : (uint8_t)(total - 1));
    } else if (input_pressed(INPUT_DOWN)) {
        item_menu_step(g, (g->item_menu_index < (uint8_t)(total - 1)) ?
            (uint8_t)(g->item_menu_index + 1) : ROW_TOP);
    }
}

void item_screen_render_banked(void);

void item_screen_render(Game *g)
{
    RenderCache *rc;
    if (!g) return;
    rc = &g->render_cache;
    if (!rc->valid || rc->prev_screen != SCREEN_ITEM) {
        if (g->item_menu_tab == TAB_QUEST)
            menu_draw_frame("QUESTS");
        else
            menu_draw_frame("CARDS");
        /* UI-mode CRAM (base set): card icons are encoded with the battle
         * display ramps (card_display_slots.json); the world tileset's
         * CRAM would show wrong colors.  Overworld/battle full redraws
         * restore their own CRAM on exit.  No-op when opened from battle
         * (already base). */
        ui_set_cram_palette(0);
        ui_draw_text_line(0, 2, "CARDS QUEST", 11);
        ui_draw_text_line((uint8_t)(g->item_menu_tab * 6), 3, "^", 1);

        g_bk_call_bank = 2;
        g_bk_call_target = (uint16_t)&item_screen_render_banked;
        g_bk_ptr_a = (void *)g;
        banked_call_run();

        telemetry_emit(EVENT_RENDER_SCREEN, (uint8_t)SCREEN_ITEM, 0, 0, 0);
        rc->valid = true;
        rc->prev_screen = SCREEN_ITEM;
    }
}
