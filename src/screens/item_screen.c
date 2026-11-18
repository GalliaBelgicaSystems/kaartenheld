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

/* DECK cursor bodies (menu_cursor_banked.c, bank 15): group sums,
 * per-side packed draw maps and cursor geometry; the fixed bank only
 * declares the two entry points it dispatches. */
/* Grouped DECK view build (bank 15): g_bk_ptr_a = Game*, fills
 * s_view_indices/s_view_count (one row per MATERIAL+WEAPON) and the
 * per-side packed draw maps. */
void item_view_build_banked(void);
/* Text-only filter/sort picker (bank 15, no catalog reads): dispatched
 * from fixed when the DECK tab is in picker mode.  Same staging as the
 * render path. */
void item_picker_render_banked(void);
/* Whole-DECK update (bank 15): the entire DECK branch of
 * item_screen_update moved out of the fixed bank (AGENTS.md 52.18).
 * Same-bank helpers are plain calls inside it; WRAM pad edges replace
 * input_pressed; SFX stages through the g_bk_sfx_req mailbox.
 * Staging: g_bk_ptr_a = Game* (no-arg body). */
void item_deck_update_banked(void);
/* In-place row painter (bank 2): dispatched from fixed when the moved
 * banked nav stages g_bk_paint_req.  Same staging as the render path. */
void item_card_refresh_banked(void);

/* DECK nav mutations (filter/sort/tab) run banked (menu_cursor_banked.c,
 * item_menu_cycle_banked, now bank 15 with the rest of the DECK menu):
 * pure WRAM state machines.  Mode 0 = filter cycle, 1 = sort cycle,
 * 2 = tab switch; dir is the tab direction (0 back, 1 forward), read
 * here -- a banked body must never call input_pressed (AGENTS.md
 * §52.11.1).  Modes 0/1 ignore dir. */
void item_menu_cycle_banked(void);
static void menu_cycle(Game *g, uint8_t mode, uint8_t dir)
{
    g_bk_call_bank = 15;
    g_bk_call_target = (uint16_t)&item_menu_cycle_banked;
    g_bk_ptr_a = (void *)g;
    g_bk_byte_a = mode;
    g_bk_byte_b = dir;
    banked_call_run();
}

/* Bank-13 GEM-tab bodies (item_gem_banked.c): the GEM nav/render route
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
 * BUILT BY THE BANKED VIEW BODY (item_view_build_banked, bank 15) right
 * before every DECK paint, and consumed read-only here: the fixed
 * bank sits at 0x8000, so the view code lives with the card catalog it
 * needs (AGENTS.md 52.18).  Every mutation path below invalidates the
 * render cache, so the rebuild lands before the next consumer. */
uint8_t s_view_indices[MAX_CARD_COLLECTION];
uint8_t s_view_count;


void item_screen_reset(Game *g)
{
    g->item_menu_index = ROW_TOP;
    g->item_menu_tab = TAB_DECK;
    g->item_menu_scroll = 0;
    g->item_menu_scroll_deck = 0;
    g->item_menu_side = 0;
    g->item_menu_filter = FILTER_ALL;
    g->item_menu_sort = SORT_NONE;
    g->item_menu_mode = MODE_LIST;
    g->item_menu_message = MSG_NONE;
    g->item_menu_msg_ttl = 0;
}

static void close_menu(Game *g)
{
    item_screen_reset(g);
    screen_change(g, g->prev_screen);
}

void item_screen_update(Game *g)
{
    /* Transient rejection messages expire on their own (~0.75s).  Every
     * surface takes the full redraw: the next paint blanks and redraws
     * without wiping, so expiry never blinks.  (The GEM tab shares this
     * TTL for MAX 3 GEMS, its LIST mode is also 0; picker/detail own
     * row 16 themselves.) */
    if (g->item_menu_msg_ttl > 0) {
        g->item_menu_msg_ttl--;
        if (g->item_menu_msg_ttl == 0) {
            g->item_menu_message = MSG_NONE;
            if (g->item_menu_tab == TAB_DECK && g->item_menu_mode == MODE_LIST)
                g_bk_paint_req = 1;
            else
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
                    g->item_menu_scroll_deck = 0;
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
     * detail input for QUEST lives in bank 13 with the quest registry
     * and GEM input routes through the bank-13 gem body (which forwards
     * QUEST back to the quest nav in-bank, so this dispatch needs no
     * tab branch of its own -- fixed-bank headroom: AGENTS.md 52.18). */
    if (g->item_menu_tab != TAB_DECK) {
        if (g->item_menu_mode == MODE_QUEST_DETAIL) return;
        if (input_pressed(INPUT_LEFT) || input_pressed(INPUT_RIGHT)) {
            menu_cycle(g, 2, input_pressed(INPUT_RIGHT) ? 1 : 0);
            return;
        }
        if (g->item_menu_tab == TAB_HERO) return;
        g_bk_call_bank = 13;
        g_bk_call_target = (uint16_t)&item_gem_nav_banked;
        g_bk_ptr_a = (void *)g;
        banked_call_run();
        /* QUEST-tab save & quit: A opens the quit confirm dialogue
         * (overworld only; A is otherwise a no-op on this tab).  YES
         * flushes the active slot and lands on the title PRESS START. */
        if (input_pressed(INPUT_A) && g->item_menu_tab == TAB_QUEST &&
            g->prev_screen == SCREEN_OVERWORLD) {
            dialogue_start_def(&g->dialogue, (DialogueId)DIALOGUE_ID_QUIT_CONFIRM);
            screen_change(g, SCREEN_DIALOGUE);
        }
        return;
    }

    /* DECK tab: the whole branch lives banked (menu_cursor_banked.c
     * item_deck_update_banked, bank 15) so the fixed bank stays under
     * 0x8000 (AGENTS.md 52.18).  Same-bank helpers are plain calls
     * inside it; WRAM pad edges replace input_pressed; SFX stages
     * through the g_bk_sfx_req mailbox; in-place nav repaints stage
     * g_bk_paint_req for the bank-2 row painter below. */
    g_bk_call_bank = 15;
    g_bk_call_target = (uint16_t)&item_deck_update_banked;
    g_bk_ptr_a = (void *)g;
    banked_call_run();
    if (g_bk_paint_req) {
        g_bk_paint_req = 0;
        g_bk_call_bank = 2;
        g_bk_call_target = (uint16_t)&item_card_refresh_banked;
        g_bk_ptr_a = (void *)g;
        banked_call_run();
    }
}

void item_screen_render_banked(void);

void item_screen_render(Game *g)
{
    RenderCache *rc;
    if (!g) return;
    rc = &g->render_cache;
    if (!rc->valid || rc->prev_screen != SCREEN_ITEM) {
        /* No screen wipe and no extra branch: ui_clear_screen() is gone.
         * Each banked body paints its rows directly in one pass (no
         * wholesale blank: the 320-cell blank spanned LCD-on frames
         * and flashed).  Every row covers its full width, so tab
         * switches morph cell-by-cell with no blank intermediate.
         * CRAM still resets unconditionally: the first
         * entry from another screen needs the base set, and reprogramming
         * identical values mid-stay is invisible.  (Fixed-bank _HOME is
         * nearly full, AGENTS.md 52.18: the bodies carry the cost in
         * banked room.) */
        /* UI-mode CRAM (base set): card icons are encoded with the battle
         * display ramps (card_display_slots.json); the world tileset's
         * CRAM would show wrong colors.  Overworld/battle full redraws
         * restore their own CRAM on exit.  Only programmed on first entry
         * from another screen (rc->prev_screen != SCREEN_ITEM): same-screen
         * tab/mode switches keep the live menu CRAM (including slot 7's
         * heal-rider green) without 64 redundant STAT-waited writes. */
        if (rc->prev_screen != SCREEN_ITEM) {
            ui_set_cram_palette(0);
        }

        /* Rebuild the grouped view first, then dispatch the render (two
         * sequential banked calls, same frame): the open frame can run
         * before any update (same-frame screen_change), so the view can
         * never wait for one.  Only the DECK tab has this view. */
        if (g->item_menu_tab == TAB_DECK) {
            g_bk_call_bank = 15;
            g_bk_call_target = (uint16_t)&item_view_build_banked;
            g_bk_ptr_a = (void *)g;
            banked_call_run();
        }

        /* DECK content renders in bank 2, picker in bank 15 (text-only,
         * no catalog: keeps bank 2 under 0x8000); QUEST, GEM and HERO
         * render in bank 13 (AGENTS.md 52.11.1: code shares its tables'
         * bank; bank 2 sits hard against 16 KB while banks 13/15 are
         * free).  GEM routes through item_gem_render_banked, which
         * forwards QUEST and HERO to their renderers in-bank, so this
         * dispatch needs no extra branch. */
        if (g->item_menu_tab == TAB_DECK) {
            if (g->item_menu_mode == MODE_PICKER) {
                g_bk_call_bank = 15;
                g_bk_call_target = (uint16_t)&item_picker_render_banked;
            } else {
                g_bk_call_bank = 2;
                g_bk_call_target = (uint16_t)&item_screen_render_banked;
            }
        } else {
            g_bk_call_bank = 13;
            g_bk_call_target = (uint16_t)&item_gem_render_banked;
        }
        g_bk_ptr_a = (void *)g;
        banked_call_run();

        telemetry_emit(EVENT_RENDER_SCREEN, (uint8_t)SCREEN_ITEM, 0, 0, 0);
        rc->valid = true;
        rc->prev_screen = SCREEN_ITEM;
    }
}
