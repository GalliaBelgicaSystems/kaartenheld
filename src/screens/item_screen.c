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
#include "card.h"
#include "shops.h"
#include "banked.h"
#include "game_ids.h"

/* Bank-11 helper (menu_cursor_banked.c); keeps the fixed bank small
 * (AGENTS.md 52.18).  Bank 11, not bank 2 with the renders: bank 2's
 * switchable area is full and the cursor needs no ROM tables. */
void item_menu_cursor_banked(void);

/* DECK group member/sums query (menu_cursor_banked.c, bank 11):
 * g_bk_byte_a = primary collection index, g_bk_byte_d = mode
 * (0 = NTH member by ordinal in g_bk_byte_b -> g_bk_byte_c collection
 * index or 0xFF; 1 = SUMS -> g_bk_byte_c owned, g_bk_byte_b decked). */
void item_group_query_banked(void);
void item_group_remove_one_banked(void);
/* Grouped DECK view build (same bank): g_bk_ptr_a = Game*, fills
 * s_view_indices/s_view_count (one row per MATERIAL+WEAPON). */
void item_view_build_banked(void);

/* Grouped A-action bodies (menu_cursor_banked.c, bank 11): scan finds
 * the first addable member (or the priority refusal reason),
 * remove-one sends the first decked member back to the pile.
 * Staging per body comment. */
void item_group_scan_banked(void);
void item_group_remove_one_banked(void);

/* DECK nav mutations (filter/sort/tab) run banked (menu_cursor_banked.c,
 * item_menu_cycle_banked): pure WRAM state machines that outgrew the
 * fixed bank when the card-group + boss-dialogue trees merged (Bank 1
 * overflow, AGENTS.md 52.18).  Mode 0 = filter cycle, 1 = sort cycle,
 * 2 = tab switch; dir is the tab direction (0 back, 1 forward), read
 * here -- a banked body must never call input_pressed (AGENTS.md
 * §52.11.1).  Modes 0/1 ignore dir. */
void item_menu_cycle_banked(void);
static void menu_cycle(Game *g, uint8_t mode, uint8_t dir)
{
    g_bk_call_bank = 11;
    g_bk_call_target = (uint16_t)&item_menu_cycle_banked;
    g_bk_ptr_a = (void *)g;
    g_bk_byte_a = mode;
    g_bk_byte_b = dir;
    banked_call_run();
}

/* Bank-8 GEM-tab bodies (item_gem_banked.c): the GEM nav/render route
 * through here; non-GEM tabs forward to the quest bodies in the same
 * bank (direct call, no trampoline), so the fixed dispatch below needs
 * no extra branches for the fourth tab (fixed _HOME headroom: AGENTS.md
 * 52.18). */
void item_gem_nav_banked(void);
void item_gem_render_banked(void);

#define TAB_DECK  0
#define TAB_QUEST 1
#define TAB_HERO  2
#define TAB_GEM   3
#define NUM_TABS  4

/* Cursor index 0 on the DECK tab is the FILTER/SORT row; card entries
 * start at 1. */
#define ROW_TOP     0
#define FIRST_CARD  1

#define MODE_LIST         0
#define MODE_CARD_DETAIL  1
#define MODE_QUEST_DETAIL 2
#define MODE_PICKER       3

#define MSG_NONE       0
#define MSG_DECK_FULL  1
#define MSG_DECK_MIN   2
#define MSG_QUEST_ITEM 3
#define MSG_NEED_AP    4
#define MSG_DECK_CAPPED 5

#define FILTER_ALL      0xFF
#define SORT_NONE       0
#define SORT_TYPE       1
#define SORT_POWER      2
#define SORT_COST       3
#define SORT_POWER_DESC 4
#define SORT_COST_DESC  5

/* Filtered/sorted view over the deckable collection entries.  s_view_indices
 * holds collection indices; s_view_count is the number of visible cards.
 * BUILT BY THE BANK-2 RENDER BODY (item_content_banked.c ic_build_view) at
 * the start of every full redraw, and consumed read-only here: the fixed
 * bank sits at 0x8000, so the view code lives with the card catalog it
 * needs (AGENTS.md 52.18).  Every mutation path below invalidates the
 * render cache, so the rebuild lands before the next consumer. */
uint8_t s_view_indices[MAX_CARD_COLLECTION];
uint8_t s_view_count;

/* Scratch for the grouped A handler (see the note at its use site):
 * WRAM, so a banked telemetry emit cannot corrupt it. */
static uint8_t s_grp_rm;
static uint8_t s_grp_id;

/* Targeted cursor move (AGENTS.md 36).  The bank-11 body
 * (menu_cursor_banked.c) computes the caret rows, repaints the two cells
 * (mirrored into g_ui_screen_buf) and, when the visible window must
 * shift, moves the scroll and flags g->render_cache.valid=false so render
 * takes the rare full-redraw path.  A pure caret move never touches the
 * render cache, so the big item_screen_render stays at its baseline shape
 * (AGENTS.md 52.19).  No staging bytes beyond the old/new indices; the
 * body writes game state directly through the staged Game* (WRAM is
 * always mapped). */
static void item_menu_step(Game *g, uint8_t new_index)
{
    g_bk_call_bank = 11;
    g_bk_call_target = (uint16_t)&item_menu_cursor_banked;
    g_bk_ptr_a = (void *)g;
    g_bk_byte_a = g->item_menu_index;
    g_bk_byte_b = new_index;
    banked_call_run();
}


void item_screen_reset(Game *g)
{
    g->item_menu_index = ROW_TOP;
    g->item_menu_tab = TAB_DECK;
    g->item_menu_scroll = 0;
    g->item_menu_side = 0;
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
                }
                g->item_menu_mode = MODE_LIST;
                break;
            default:
                if (g->item_menu_tab == TAB_DECK &&
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

    /* QUEST, HERO and GEM tabs share the pre-DECK branch (LR switches
     * tabs from LIST mode; HERO is read-only otherwise).  Cursor/
     * detail input for QUEST lives in bank 8 with the quest registry
     * and GEM input routes through the bank-8 gem body (which forwards
     * QUEST back to the quest nav in-bank, so this dispatch needs no
     * tab branch of its own -- fixed-bank headroom: AGENTS.md 52.18). */
    if (g->item_menu_tab != TAB_DECK) {
        if (g->item_menu_mode == MODE_QUEST_DETAIL) return;
        if (input_pressed(INPUT_LEFT) || input_pressed(INPUT_RIGHT)) {
            menu_cycle(g, 2, input_pressed(INPUT_RIGHT) ? 1 : 0);
            return;
        }
        if (g->item_menu_tab == TAB_HERO) return;
        g_bk_call_bank = 8;
        g_bk_call_target = (uint16_t)&item_gem_nav_banked;
        g_bk_ptr_a = (void *)g;
        banked_call_run();
        return;
    }

    /* ── DECK tab ─────────────────────────────────────────────── */
    switch (g->item_menu_mode) {
        /* Selling lives in the shop's SELL tab now (docs/loot.md
         * §34.6), not here: A backs out to the list like SELECT. */
        case MODE_CARD_DETAIL:
            if (input_pressed(INPUT_SELECT) || input_pressed(INPUT_A)) {
                g->item_menu_mode = MODE_LIST;
                g->render_cache.valid = false;
            }
            return;
        case MODE_PICKER:
            if (input_pressed(INPUT_UP) || input_pressed(INPUT_DOWN)) {
                g->item_menu_pick_row =
                    (uint8_t)(g->item_menu_pick_row == 0 ? 1 : 0);
                g->render_cache.valid = false;
            } else if (input_pressed(INPUT_LEFT) || input_pressed(INPUT_RIGHT)) {
                if (g->item_menu_pick_row == 0) menu_cycle(g, 0, 0);
                else menu_cycle(g, 1, 0);
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
        if (g->item_menu_index > ROW_TOP) {
            /* Card rows: flip the owned/deck side (same row is valid
             * on both sides: rows list the union identity). */
            g->item_menu_side ^= 1;
            g->render_cache.valid = false;
        } else {
            menu_cycle(g, 2, input_pressed(INPUT_RIGHT) ? 1 : 0);
        }
        return;
    }

    /* No per-frame view build here: every action that can change the view
     * invalidates the render cache, and the render path rebuilds it
     * (item_view_build_banked) right before drawing. */
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
            /* Grouped count-up-then-clear: one row covers every same-name
             * (MATERIAL+WEAPON) member.  A adds one copy (first member with
             * room, collection order); once every owned copy is decked, A
             * clears the group's decked copies (MIN-guarded, all-or-nothing).
             * Members go through deck_equip_card/deck_remove_card so gates,
             * caps and identity rules are unchanged; a saturated identity
             * reports AT CAP instead of clearing what is already decked. */
            CardState *cs = &g->state.cards;
            uint8_t primary;
            uint8_t owned;
            uint8_t decked;
            uint8_t pool;
            uint8_t msg = MSG_NONE;
            int16_t pv;
            if ((uint8_t)(g->item_menu_index - FIRST_CARD) >= s_view_count)
                return;
            primary = s_view_indices[
                (uint8_t)(g->item_menu_index - FIRST_CARD)];
            g_bk_call_bank = 11;
            g_bk_call_target = (uint16_t)&item_group_query_banked;
            g_bk_ptr_a = (void *)g;
            g_bk_byte_a = primary;
            g_bk_byte_d = 1;
            banked_call_run();
            owned = g_bk_byte_c;
            decked = g_bk_byte_b;
            /* Pool clamped through int16_t (ic_pool normalization): unset
             * or negative means the default pool, never AP 0. */
            pv = g->state.variables.values[VARIABLE_ID_ENERGY_POOL - 1];
            if (pv < 1) pv = 1;
            if (pv > 255) pv = 255;
            pool = (uint8_t)pv;
            /* Two-column move: LEFT adds one available copy to the
             * deck, RIGHT sends one decked copy back to the pool.  No
             * toggle, no clear-all.  Loop state lives in WRAM
             * (AGENTS.md 52.19: SDCC caches registers across the
             * trampoline); every dispatch stages bank/target/ptr FRESH
             * and the remove return is read IMMEDIATELY. */
            if (g->item_menu_side == 0) {
                if (decked >= owned) {
                    /* Nothing available: silent no-op. */
                } else {
                    g_bk_call_bank = 11;
                    g_bk_call_target = (uint16_t)&item_group_scan_banked;
                    g_bk_ptr_a = (void *)g;
                    g_bk_byte_a = primary;
                    g_bk_byte_d = pool;
                    banked_call_run();
                    s_grp_id = g_bk_byte_b;
                    if (s_grp_id != 0xFF) {
                        /* Single gated apply for the scan's pick: OK
                         * adds (with telemetry+SFX); FULL/LOCKED report
                         * with priority (LOCKED fires CARD_EQUIP_LOCKED
                         * inside the apply); any other refusal is a
                         * silent no-op (the row stays for the other
                         * side's removal path). */
                        s_grp_id = cs->collection.entries[s_grp_id].id;
                        s_grp_rm = deck_equip_card(cs, (CardId)s_grp_id,
                                                   pool);
                        if (s_grp_rm == EQUIP_OK) {
                            telemetry_emit(EVENT_CARD_ADDED_TO_DECK,
                                           (CardId)s_grp_id, 0, 0, 0);
                            audio_play_sfx(SFX_SELECT);
                        } else if (s_grp_rm == EQUIP_FULL) {
                            msg = MSG_DECK_FULL;
                        } else if (s_grp_rm == EQUIP_LOCKED) {
                            msg = MSG_NEED_AP;
                        }
                        /* Any other refusal is a silent no-op. */
                    }
                }
            } else if (decked == 0) {
                /* Nothing decked: silent no-op. */
            } else if ((uint8_t)(cs->deck.count - 1) < DECK_MIN_CARDS) {
                msg = MSG_DECK_MIN;
            } else {
                /* Send the first decked member (collection order)
                 * back to the pool. */
                g_bk_call_bank = 11;
                g_bk_call_target =
                    (uint16_t)&item_group_remove_one_banked;
                g_bk_ptr_a = (void *)g;
                g_bk_byte_a = primary;
                banked_call_run();
                if (g_bk_byte_c == 0xFF) {
                    /* Race: nothing decked under us; silent no-op. */
                } else {
                    s_grp_id = g_bk_byte_b;
                    telemetry_emit(EVENT_CARD_REMOVED_FROM_DECK,
                                   (CardId)s_grp_id, 0, 0, 0);
                    audio_play_sfx(SFX_SELECT);
                }
            }
            g->item_menu_message = msg;
            if (msg != MSG_NONE) g->item_menu_msg_ttl = 45;
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
        /* The tab header (row 0 tabs + row 1 caret) is drawn by the
         * banked bodies below (fixed-bank _HOME is nearly full,
         * AGENTS.md 52.18): ui_clear_screen + CRAM here, then the
         * dispatched body paints header + content. */

        ui_clear_screen();
        /* UI-mode CRAM (base set): card icons are encoded with the battle
         * display ramps (card_display_slots.json); the world tileset's
         * CRAM would show wrong colors.  Overworld/battle full redraws
         * restore their own CRAM on exit.  No-op when opened from battle
         * (already base). */
        ui_set_cram_palette(0);

        /* Rebuild the grouped view first, then dispatch the render (two
         * sequential banked calls, same frame): the open frame can run
         * before any update (same-frame screen_change), so the view can
         * never wait for one.  Only the DECK tab has this view. */
        if (g->item_menu_tab == TAB_DECK) {
            g_bk_call_bank = 11;
            g_bk_call_target = (uint16_t)&item_view_build_banked;
            g_bk_ptr_a = (void *)g;
            banked_call_run();
        }

        /* DECK + HERO content renders in bank 2; the QUEST and GEM tabs
         * both render in bank 8 with their registries (AGENTS.md
         * 52.11.1: code shares its tables' bank; bank 8 is the overflow
         * bank per AGENTS.md 55.5).  GEM routes through
         * item_gem_render_banked, which forwards QUEST to the quest
         * renderer in-bank, so this dispatch needs no extra branch. */
        if (g->item_menu_tab == TAB_QUEST || g->item_menu_tab == TAB_GEM) {
            g_bk_call_bank = 8;
            g_bk_call_target = (uint16_t)&item_gem_render_banked;
        } else {
            g_bk_call_bank = 2;
            g_bk_call_target = (uint16_t)&item_screen_render_banked;
        }
        g_bk_ptr_a = (void *)g;
        banked_call_run();

        telemetry_emit(EVENT_RENDER_SCREEN, (uint8_t)SCREEN_ITEM, 0, 0, 0);
        rc->valid = true;
        rc->prev_screen = SCREEN_ITEM;
    }
}
