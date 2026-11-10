#pragma bank 11
#pragma disable_warning 110

#include <gb/gb.h>
#include <gb/cgb.h>
#include "game.h"
#include "ui.h"
#include "banked.h"
#include "shops.h"
#include "rpg/loot.h"
#include "rpg/cards.h"
#include "rpg/deck.h"
#include "game_ids.h"

/* Shared ITEM/SHOP cursor repaint (AGENTS.md 36): a cursor-only UP/DOWN
 * repaints just the `>` glyph instead of invalidating the render cache.
 *
 * Lives in bank 11 (not bank 2 with the renders that call it): bank 2's
 * switchable area is full, and this body is pure WRAM math -- mode/row
 * computation plus two VRAM cell writes -- needing no bank-2 ROM tables
 * (AGENTS.md §52.11.1).  Staging: g_bk_ptr_a = Game*,
 * g_bk_byte_a = old index, g_bk_byte_b = new index; the body owns the
 * mutation (scroll + caret rows), callers keep their single-call shape
 * so the fixed bank stays under 0x8000 (AGENTS.md §52.18).
 *
 * Moved verbatim from src/screens/screen_content.c; keep the row
 * geometry in sync with the bank-2 renders (bm_cursor_row comment). */

extern char g_ui_screen_buf[18][21];
extern uint8_t ui_font_tile_base;

/* Item menu navigation layout constants (mirror item_screen.c). */
#define CUR_FIRST_CARD 1
#define CUR_VISIBLE_CARD 11

/* Nav-mutation constants (mirror item_screen.c TAB_DECK/TAB_GEM/ROW_TOP,
 * FILTER_ALL, SORT_NONE/SORT_COST_DESC) for item_menu_cycle_banked
 * below -- keep in sync. */
#define CYC_TAB_DECK  0
#define CYC_TAB_GEM   3
#define CYC_ROW_TOP   0
#define CYC_FILTER_ALL 0xFF
#define CYC_SORT_NONE  0
#define CYC_SORT_COST_DESC 5

static Game *s_cu_game;
static uint8_t s_cu_pos;
static uint8_t s_cu_mode;
static uint8_t s_cu_old_index;
static uint8_t s_cu_new_index;
static uint8_t s_cu_old_row;
static uint8_t s_cu_new_row;
static uint8_t s_cu_need_scroll;

static void cu_put_char(uint8_t x, uint8_t y, char ch)
{
    uint8_t tile;

    if (y >= 18 || x >= 20) return;
    if (g_ui_screen_buf[y][x] == ch) return;
    tile = (uint8_t)(ui_font_tile_base + (uint8_t)(ch - ' '));
    VBK_REG = 0;
    BANKED_VRAM_WRITE((volatile uint8_t *)(0x9800 + ((uint16_t)y << 5) + x), tile);
    g_ui_screen_buf[y][x] = ch;
}

static uint8_t cu_mode(const Game *g)
{
    return (g->screen == SCREEN_SHOP) ? 2 : (uint8_t)g->item_menu_tab;
}

static uint8_t cu_cursor_row(uint8_t mode, uint8_t index, uint8_t scroll)
{
    if (mode == 2) return (uint8_t)(5u + (uint8_t)(index - scroll));
    /* DECK tab: row 0 tabs, row 1 caret, row 2 FILTER/SORT, row 3 column
     * header, row 4 separator, one grouped card row per row at rows
     * 5-15.  QUEST/GEM keep rows 3+ (their own render bodies draw no
     * header). */
    if (index == 0) return 2;
    s_cu_pos = (uint8_t)(index - CUR_FIRST_CARD);
    if (mode == 0) return (uint8_t)(5u + (uint8_t)(s_cu_pos - scroll));
    return (uint8_t)(3u + (uint8_t)(s_cu_pos - scroll));
}

/* Shared tail: scroll-window + caret repaint for staged old/new. */
static void cu_apply(void)
{
    s_cu_mode = cu_mode(s_cu_game);
    s_cu_game->item_menu_index = s_cu_new_index;

    if (s_cu_mode == 0) {
        s_cu_need_scroll = s_cu_game->item_menu_scroll;
        if (s_cu_new_index <= CUR_FIRST_CARD)
            s_cu_need_scroll = 0;
        else {
            s_cu_pos = (uint8_t)(s_cu_new_index - CUR_FIRST_CARD);
            if (s_cu_pos < s_cu_need_scroll)
                s_cu_need_scroll = s_cu_pos;
            else if (s_cu_pos > (uint8_t)(s_cu_need_scroll + CUR_VISIBLE_CARD - 1))
                s_cu_need_scroll = (uint8_t)(s_cu_pos - (CUR_VISIBLE_CARD - 1));
        }
        if (s_cu_need_scroll != s_cu_game->item_menu_scroll) {
            s_cu_game->item_menu_scroll = s_cu_need_scroll;
            s_cu_game->render_cache.valid = false;
            return;
        }
    } else if (s_cu_mode == 2) {
        /* Shop list: index is a direct stock position (0-based).  Keep the
         * cursor inside the SHOP_VISIBLE window; a window shift needs a
         * full redraw (the banked body cannot repaint scrolled rows). */
        s_cu_need_scroll = s_cu_game->item_menu_scroll;
        if (s_cu_new_index < s_cu_need_scroll)
            s_cu_need_scroll = s_cu_new_index;
        else if (s_cu_new_index > (uint8_t)(s_cu_need_scroll + SHOP_VISIBLE - 1))
            s_cu_need_scroll = (uint8_t)(s_cu_new_index - (SHOP_VISIBLE - 1));
        if (s_cu_need_scroll != s_cu_game->item_menu_scroll) {
            s_cu_game->item_menu_scroll = s_cu_need_scroll;
            s_cu_game->render_cache.valid = false;
            return;
        }
    }

    s_cu_old_row = cu_cursor_row(s_cu_mode, s_cu_old_index, s_cu_game->item_menu_scroll);
    s_cu_new_row = cu_cursor_row(s_cu_mode, s_cu_new_index, s_cu_game->item_menu_scroll);
    if (s_cu_old_row != s_cu_new_row) {
        cu_put_char(0, s_cu_old_row, ' ');
        cu_put_char(0, s_cu_new_row, '>');
    }
}

void item_menu_cursor_banked(void)
{
    s_cu_game = (Game *)g_bk_ptr_a;

    if (!s_cu_game) return;
    s_cu_old_index = g_bk_byte_a;
    s_cu_new_index = g_bk_byte_b;
    cu_apply();
}

/* ── DECK group member/sums helper ───────────────────────────────
 * One DECK row covers every deckable collection entry sharing the
 * primary's (MATERIAL, WEAPON) name.  The fixed-bank A handler needs
 * the member list + group totals, but the fixed bank cannot resolve
 * loot ids (catalog lives in bank 2) and bank 2 is at its ceiling
 * (AGENTS.md 52.18), so the key compare lives here (bank 11 has room).
 *
 * Staging: g_bk_ptr_a = Game*, g_bk_byte_a = primary collection index,
 * g_bk_byte_b = ordinal (NTH) / ignored (SUMS), g_bk_byte_d = mode
 * (0 = NTH, 1 = SUMS).
 * NTH: g_bk_byte_c = collection index of the ordinal-th same-name
 *   deckable member, or 0xFF past the end.
 * SUMS: g_bk_byte_c = group owned total, g_bk_byte_b = group decked
 *   total (saturating).
 *
 * Key mapping is a copy of bank-2 ic_card_mat_wpn's curated switch
 * (loot axes come from loot.h macros, same as both sides): keep the
 * two in sync when the curated list grows. */
/* Curated rows, index = CardId - CARD_FIRST_GAME (the catalog order in
 * src/game/game_ids.h): material, weapon, deckable, type, power, cost
 * (the last three drive the FILTER and SORT modes for the view build
 * below -- the catalog itself is bank 2 and off-limits from here). */
static const uint8_t s_cu_grp_mat[GAME_CARD_COUNT] =
    { 2, 0, 2, 1, 1, 0, 4, 0, 3, 0, 0 };
static const uint8_t s_cu_grp_wpn[GAME_CARD_COUNT] =
    { 0, 1, 4, 0, 3, 0, 2, 0, 1, 0, 0 };
static const uint8_t s_cu_grp_deckable[GAME_CARD_COUNT] =
    { 1, 1, 1, 1, 1, 0, 1, 1, 1, 0, 0 };
static const uint8_t s_cu_grp_type[GAME_CARD_COUNT] =
    { 0, 1, 2, 0, 0, 5, 0, 0, 1, 5, 5 };
static const uint8_t s_cu_grp_power[GAME_CARD_COUNT] =
    { 5, 1, 4, 3, 2, 0, 9, 1, 6, 1, 2 };
static const uint8_t s_cu_grp_cost[GAME_CARD_COUNT] =
    { 2, 1, 2, 2, 2, 0, 5, 1, 3, 0, 0 };

static uint8_t cu_grp_key(CardId id, uint8_t *mat, uint8_t *wpn)
{
    uint8_t k;
    uint8_t w;
    if (loot_is_loot_id(id)) {
        *mat = loot_id_material(id);
        w = loot_id_weapon(id);
        *wpn = (w <= 4) ? w : 0;
        return 1;
    }
    if (id < CARD_FIRST_GAME) return 0;
    k = (uint8_t)(id - CARD_FIRST_GAME);
    if (k >= GAME_CARD_COUNT) return 0;
    if (!s_cu_grp_deckable[k]) return 0;
    *mat = s_cu_grp_mat[k];
    *wpn = s_cu_grp_wpn[k];
    return 1;
}

/* View-build helpers (same bank as the curated group table; the card
 * catalog lives in bank 2 and card_get_def is a fixed-bank trampoline a
 * banked body must never call -- AGENTS.md 52.11.1).  Loot ids resolve
 * from the id axes with the same pure macros as loot_synth_banked. */
static uint8_t cu_grp_type(CardId id, uint8_t *power, uint8_t *cost)
{
    uint8_t k;
    uint8_t m, w, wi, rider;
    if (loot_is_loot_id(id)) {
        m = loot_id_material(id);
        w = loot_id_weapon(id);
        wi = (w <= WPN_RING) ? w : WPN_SWORD;
        *power = LOOT_POWER(m, wi);
        rider = 0;
        *cost = (w <= WPN_RING) ? CARD_AP(*power, m, rider) : 0;
        if (w == WPN_SHIELD) return 1;                       /* DEF */
        if (w == WPN_RING || w > WPN_RING) return 2;         /* HEAL */
        return 0;                                            /* ATK */
    }
    if (id < CARD_FIRST_GAME) return 0xFF;
    k = (uint8_t)(id - CARD_FIRST_GAME);
    if (k >= GAME_CARD_COUNT) return 0xFF;
    *power = s_cu_grp_power[k];
    *cost = s_cu_grp_cost[k];
    if (!s_cu_grp_deckable[k]) return 0xFF;
    return s_cu_grp_type[k];
}

static uint8_t cu_sort_value(CardId id, uint8_t sort)
{
    uint8_t power = 0;
    uint8_t cost = 0;
    uint8_t type = cu_grp_type(id, &power, &cost);
    if (sort == 9) return type;      /* SORT_TYPE */
    if (sort == 2 || sort == 4) return power;  /* SORT_POWER(+/-) */
    return cost;                     /* SORT_COST(+/-) */
}

/* ── Grouped DECK view build (fixed-bank caller: item_screen.c) ────
 * Fills s_view_indices (collection indices, one per MATERIAL+WEAPON
 * row) + s_view_count from the live collection, honoring the current
 * filter and sort, and the per-row owned/decked sums the render needs
 * (s_row_owned/s_row_decked, indexed by view position).  Staging:
 * g_bk_ptr_a = Game*.  Runs immediately before the bank-2 render in the
 * same frame, so it is always fresh. */
extern uint8_t s_view_indices[MAX_CARD_COLLECTION];
extern uint8_t s_view_count;

/* Per-row sums for the render (bank 2 cannot re-resolve group keys: its
 * switchable area is full and the scan it needs duplicates ~300 B of
 * key logic).  Window-sized: the render draws at most ROW_SUMS rows
 * starting at item_menu_scroll; detail mode uses slot 0 for the
 * selected row. */
#define ROW_SUMS 12
uint8_t s_row_owned[ROW_SUMS];
uint8_t s_row_decked[ROW_SUMS];

/* Owned/decked copies across every deckable collection entry sharing
 * the primary's (MATERIAL, WEAPON) name. */
static void cu_row_sums(Game *g, uint8_t primary_coll, uint8_t *owned,
                        uint8_t *decked)
{
    uint8_t i, j;
    uint8_t mat, wpn, m2, w2;
    uint8_t n;
    CardId id;
    *owned = 0;
    *decked = 0;
    if (primary_coll >= g->state.cards.collection.count) return;
    if (!cu_grp_key(g->state.cards.collection.entries[primary_coll].id,
                    &mat, &wpn)) return;
    for (i = 0; i < g->state.cards.collection.count; i++) {
        id = g->state.cards.collection.entries[i].id;
        if (!cu_grp_key(id, &m2, &w2)) continue;
        if (m2 != mat || w2 != wpn) continue;
        n = g->state.cards.collection.entries[i].count;
        *owned = (uint8_t)(*owned + n);
        if (*owned < n) *owned = 255;
        for (j = 0; j < g->state.cards.deck.count; j++) {
            if (g->state.cards.deck.cards[j] == id) {
                *decked = (uint8_t)(*decked + 1);
                if (*decked == 0) *decked = 255;
            }
        }
    }
}

void item_view_build_banked(void)
{
    Game *g = (Game *)g_bk_ptr_a;
    uint8_t i, j, k;
    uint8_t mat, wpn, m2, w2;
    uint8_t type, power, cost;
    uint8_t desc;
    uint8_t n;
    CardId id;
    if (!g) return;
    s_view_count = 0;
    for (i = 0; i < g->state.cards.collection.count; i++) {
        id = g->state.cards.collection.entries[i].id;
        type = cu_grp_type(id, &power, &cost);
        if (type == 0xFF) continue;
        if (g->item_menu_filter != 0xFF && type != g->item_menu_filter)
            continue;
        /* Fold into the first row with the same (mat, wpn): that row's
         * earliest collection entry stays the primary. */
        if (!cu_grp_key(id, &mat, &wpn)) continue;
        for (k = 0; k < s_view_count; k++) {
            if (!cu_grp_key(g->state.cards.collection.entries[
                    s_view_indices[k]].id, &m2, &w2)) continue;
            if (m2 == mat && w2 == wpn) break;
        }
        if (k < s_view_count) continue;
        s_view_indices[s_view_count] = i;
        s_view_count = (uint8_t)(s_view_count + 1);
    }
    /* Insertion sort (SORT_NONE/TYPE/POWER(+/-)/COST(+/-)). */
    if (g->item_menu_sort != 0 && s_view_count > 1) {
    desc = (uint8_t)(g->item_menu_sort == 4 || g->item_menu_sort == 5);
    for (i = 1; i < s_view_count; i++) {
        j = i;
        while (j > 0) {
            uint8_t va = cu_sort_value(
                g->state.cards.collection.entries[s_view_indices[j]].id,
                g->item_menu_sort);
            uint8_t vb = cu_sort_value(
                g->state.cards.collection.entries[s_view_indices[j - 1]].id,
                g->item_menu_sort);
            if (desc ? (vb < va) : (vb > va)) {
                k = s_view_indices[j];
                s_view_indices[j] = s_view_indices[j - 1];
                s_view_indices[j - 1] = k;
                j--;
            } else {
                break;
            }
        }
    }
    }
    /* Per-row sums for the render: the visible window, plus slot 0 for
     * the selected row in detail mode (no list is drawn then). */
    if (g->item_menu_mode == 1) {
        if (g->item_menu_index >= 1 &&
            (uint8_t)(g->item_menu_index - 1) < s_view_count)
            cu_row_sums(g, s_view_indices[g->item_menu_index - 1],
                        &s_row_owned[0], &s_row_decked[0]);
        return;
    }
    for (i = 0; i < ROW_SUMS; i++) {
        n = (uint8_t)(g->item_menu_scroll + i);
        if (n >= s_view_count) {
            s_row_owned[i] = 0;
            s_row_decked[i] = 0;
            continue;
        }
        cu_row_sums(g, s_view_indices[n], &s_row_owned[i], &s_row_decked[i]);
    }
}

void item_group_query_banked(void)
{
    Game *g = (Game *)g_bk_ptr_a;
    uint8_t primary = g_bk_byte_a;
    uint8_t ordinal = g_bk_byte_b;
    uint8_t mode = g_bk_byte_d;
    uint8_t i, j;
    uint8_t mat, wpn, m2, w2;
    uint8_t seen = 0;
    CardId id;
    if (!g || primary >= g->state.cards.collection.count) {
        g_bk_byte_c = 0xFF;
        return;
    }
    id = g->state.cards.collection.entries[primary].id;
    if (!cu_grp_key(id, &mat, &wpn)) {
        g_bk_byte_c = 0xFF;
        return;
    }
    if (mode == 1) {
        uint8_t owned = 0;
        uint8_t decked = 0;
        uint8_t n;
        for (i = 0; i < g->state.cards.collection.count; i++) {
            id = g->state.cards.collection.entries[i].id;
            if (!cu_grp_key(id, &m2, &w2)) continue;
            if (m2 != mat || w2 != wpn) continue;
            n = g->state.cards.collection.entries[i].count;
            owned = (uint8_t)(owned + n);
            if (owned < n) owned = 255;
            for (j = 0; j < g->state.cards.deck.count; j++) {
                if (g->state.cards.deck.cards[j] == id) {
                    decked++;
                    if (decked == 0) decked = 255;
                }
            }
        }
        g_bk_byte_c = owned;
        g_bk_byte_b = decked;
        return;
    }
    for (i = 0; i < g->state.cards.collection.count; i++) {
        id = g->state.cards.collection.entries[i].id;
        if (!cu_grp_key(id, &m2, &w2)) continue;
        if (m2 != mat || w2 != wpn) continue;
        if (seen == ordinal) {
            g_bk_byte_c = i;
            return;
        }
        seen++;
    }
    g_bk_byte_c = 0xFF;
}

/* Direction step for the shop list (g_bk_byte_b 0 = up, 1 = down,
 * g_bk_byte_c = visible count): clamp the new index banked so the
 * fixed update pays no bounds branch.  Shop mode geometry (mode 2)
 * is shared with the cursor body above. */
void item_menu_step_banked(void)
{
    s_cu_game = (Game *)g_bk_ptr_a;

    if (!s_cu_game) return;
    s_cu_old_index = g_bk_byte_a;
    s_cu_new_index = s_cu_old_index;
    if (g_bk_byte_b == 0) {
        if (s_cu_old_index > 0)
            s_cu_new_index = (uint8_t)(s_cu_old_index - 1);
    } else if ((uint8_t)(s_cu_old_index + 1) < g_bk_byte_c) {
        s_cu_new_index = (uint8_t)(s_cu_old_index + 1);
    }
    cu_apply();
}

/* Group-clear removal core (bank 11): removes every decked copy of one
 * card id.  Self-contained (AGENTS.md §52.11.1): no fixed-bank calls --
 * the scan/swap mirrors deck_remove_card inline (swap-with-last,
 * DECK_MIN_CARDS floor), so the bank switch cannot strand it.  Staging:
 * g_bk_ptr_a = CardState*, g_bk_byte_a = id; returns the removed count
 * in g_bk_byte_c.  Pre-count bounded: never more iterations than the
 * copies present on entry.  The caller emits one CARD_REMOVED_FROM_DECK
 * per returned copy through its own pure counter loop. */
void deck_clear_all_banked(void)
{
    CardState *cs = (CardState *)g_bk_ptr_a;
    CardId id = (CardId)g_bk_byte_a;
    uint8_t n;
    uint8_t i;
    uint8_t found;
    uint8_t removed;
    if (!cs || id == CARD_NONE) {
        g_bk_byte_c = 0;
        return;
    }
    n = 0;
    for (i = 0; i < cs->deck.count; i++) {
        if (cs->deck.cards[i] == id) {
            n++;
            if (n == 0) {
                n = 255;
                break;
            }
        }
    }
    removed = 0;
    while (n > 0) {
        found = 0;
        if (cs->deck.count <= DECK_MIN_CARDS) break;
        for (i = 0; i < cs->deck.count; i++) {
            if (cs->deck.cards[i] == id) {
                cs->deck.cards[i] = cs->deck.cards[--cs->deck.count];
                found = 1;
                break;
            }
        }
        if (!found) break;
        removed++;
        n--;
    }
    g_bk_byte_c = removed;
}

/* DECK nav mutations: filter cycle / sort cycle / tab switch (bank 11).
 * Moved out of the fixed bank (AGENTS.md §52.18): the card-group +
 * boss-dialogue merge overflowed Bank 1, and these three are pure WRAM
 * state machines -- no ROM tables, no fixed calls -- so they run
 * verbatim here.  Staging: g_bk_ptr_a = Game*, g_bk_byte_a = mode
 * (0 filter, 1 sort, 2 tab), g_bk_byte_b = tab direction (0 back,
 * 1 forward; ignored by modes 0/1, which take no direction input).  The
 * direction is staged by the fixed caller: a banked body must never
 * call input_pressed (AGENTS.md §52.11.1).  Mirror constants live in
 * item_screen.c (TAB_DECK/TAB_GEM/ROW_TOP, FILTER_ALL,
 * SORT_NONE/SORT_COST_DESC) -- keep in sync. */
#define CYC_FILTER 0
#define CYC_SORT   1
#define CYC_TAB    2
void item_menu_cycle_banked(void)
{
    Game *g = (Game *)g_bk_ptr_a;
    uint8_t mode;
    if (!g) return;
    mode = g_bk_byte_a;
    if (mode == CYC_FILTER) {
        if (g->item_menu_filter == CYC_FILTER_ALL)
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
            g->item_menu_filter = CYC_FILTER_ALL;
    } else if (mode == CYC_SORT) {
        /* Wrap instead of % -- SM83 has no hardware divide and the SDCC
         * int-mod library would land in the tight fixed bank. */
        g->item_menu_sort = (g->item_menu_sort >= CYC_SORT_COST_DESC) ?
            CYC_SORT_NONE : (uint8_t)(g->item_menu_sort + 1);
    } else {
        /* RIGHT advances, LEFT goes back (DECK <-> QUEST <-> HERO <->
         * GEM).  Directional: with four tabs a forward-only cycle would
         * strand LEFT presses on the wrong tab. */
        if (g_bk_byte_b) {
            if (g->item_menu_tab == CYC_TAB_GEM) g->item_menu_tab = CYC_TAB_DECK;
            else g->item_menu_tab++;
        } else {
            if (g->item_menu_tab == CYC_TAB_DECK) g->item_menu_tab = CYC_TAB_GEM;
            else g->item_menu_tab--;
        }
        g->item_menu_index = CYC_ROW_TOP;
        g->item_menu_scroll = 0;
        g->render_cache.valid = false;
    }
}
