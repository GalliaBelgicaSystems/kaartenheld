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

/* Shared SHOP cursor repaint (AGENTS.md 36): a cursor-only UP/DOWN
 * repaints just the `>` glyph instead of invalidating the render cache.
 * (The DECK tab's per-side navigation lives further down, see
 * item_deck_nav_banked.)
 *
 * Lives in bank 11 (not bank 2 with the renders that call it): bank 2's
 * switchable area is full, and this body is pure WRAM math -- mode/row
 * computation plus two VRAM cell writes -- needing no bank-2 ROM tables
 * (AGENTS.md §52.11.1).  Staging per item_menu_step_banked; the body
 * owns the mutation (scroll + caret rows), callers keep their
 * single-call shape so the fixed bank stays under 0x8000 (AGENTS.md
 * §52.18). */

extern char g_ui_screen_buf[18][21];
extern uint8_t ui_font_tile_base;

/* Item menu navigation layout constants (mirror item_screen.c). */
#define CUR_FIRST_CARD 1
/* Visible DECK rows (5-12): 8 rows x 4 OAM sprites (rider+ingot, both
 * sides) = 32 entries, inside the 40-entry OAM budget. */
#define CUR_VISIBLE_CARD 8
/* First card row (mirror item_content_banked.c ROW_CARDS_TOP -- keep in
 * sync): the diff-clear below addresses rows by slot. */
#define CUR_CARDS_TOP 5

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

/* Shop scroll-window + caret repaint for staged old/new.  The shop is
 * the only remaining caller: the DECK tab navigates through
 * item_deck_nav_banked (state only; the fixed caller repaints the
 * whole list in place) and QUEST/GEM through their own bank-8
 * bodies. */
static void cu_apply(void)
{
    s_cu_game->item_menu_index = s_cu_new_index;

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

    s_cu_old_row = (uint8_t)(5u + (uint8_t)(s_cu_old_index - s_cu_game->item_menu_scroll));
    s_cu_new_row = (uint8_t)(5u + (uint8_t)(s_cu_new_index - s_cu_game->item_menu_scroll));
    if (s_cu_old_row != s_cu_new_row) {
        cu_put_char(0, s_cu_old_row, ' ');
        cu_put_char(0, s_cu_new_row, '>');
    }
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
    if (sort == 1) return type;      /* SORT_TYPE */
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
 * key logic).  Per side, drawn-indexed: slots 0-7 hold the left
 * (pile) block's rows, slots 8-15 the right (deck) block's; detail
 * mode uses slot 0 for the selected row. */
#define ROW_SUMS 16
uint8_t s_row_owned[ROW_SUMS];
uint8_t s_row_decked[ROW_SUMS];

/* Per-side packed draw maps: the first 8 view positions visible on
 * that side (left: owned - decked > 0; right: decked > 0) at/after
 * item_menu_scroll, 0xFF past the end.  A zero count hides that side's
 * cell -- an emptied side shows blank rows -- and the cursor never
 * parks on a hidden cell.  Built here (same bank as the group tables)
 * on every DECK render; read by the bank-2 row painter. */
uint8_t s_ic_draw_pos[8];
uint8_t s_ic_draw_pos_r[8];

/* Previous packed maps (snapshot at build entry): the diff-clear at the
 * end of the build erases VRAM/OAM/buffer cells for slots/sides that
 * just lost their row, so the lean bank-2 repaint only ever draws live
 * content.  Lives in bank 11 with the maps (not bank 2 with the renders
 * that call it): bank 2's switchable area is full, and every op here is
 * bank-independent (WRAM maps/buffer, direct VRAM/OAM writes), needing
 * no bank-2 ROM tables (AGENTS.md 52.11.1). */
static uint8_t s_cu_prev_pos[8];
static uint8_t s_cu_prev_pos_r[8];

/* Unconditional VRAM+buffer span clear (no same-char skip): icon stamps
 * write VRAM tiles the buffer never sees (rider/weapon/chrome cells,
 * AP pips), so a skip-based blank would leave their tiles haunting
 * emptied rows (AGENTS.md 52.25 family). */
static void cu_blank_span(uint8_t y, uint8_t x, uint8_t n)
{
    uint8_t i;
    volatile uint8_t *dst;
    char *buf;
    dst = (volatile uint8_t *)(0x9800u + ((uint16_t)y << 5) + x);
    buf = &g_ui_screen_buf[y][x];
    VBK_REG = 0;
    for (i = 0; i < n; i++) {
        BANKED_VRAM_WRITE(dst, ui_font_tile_base);
        *buf = ' ';
        dst++;
        buf++;
    }
}

/* Hide one side's OAM pair for a slot (rider + material entries live in
 * WRAM shadow OAM: side 0 = entries 1/17 + k, side 1 = 9/25 + k). */
static void cu_hide_side(uint8_t k, uint8_t side)
{
    volatile uint8_t *re;
    re = (volatile uint8_t *)(0xC000u + ((uint16_t)((side ? 9 : 1) + k) << 2));
    re[0] = 0;
    re[2] = 0;
    re = (volatile uint8_t *)(0xC000u + ((uint16_t)((side ? 25 : 17) + k) << 2));
    re[0] = 0;
    re[2] = 0;
}

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
        if (*owned < n || *owned > 99) *owned = 99;
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
    /* Snapshot the maps before rebuilding: the diff-clear at the end
     * erases slots/sides that lose their row (BSS zeros read as vpos 0
     * on the very first build -- harmless, a full draw follows). */
    for (i = 0; i < 8; i++) {
        s_cu_prev_pos[i] = s_ic_draw_pos[i];
        s_cu_prev_pos_r[i] = s_ic_draw_pos_r[i];
    }
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
    /* Per-side packed lists: the left block packs groups with a pile
     * remainder (owned - decked > 0), the right block groups with decked
     * copies -- first 8 of each at/after the scroll position.  A zero
     * count hides that side's cell (an emptied side shows blank rows)
     * and the cursor never parks on a hidden cell. */
    {
        uint8_t k = 0;
        uint8_t kr = 0;
        uint8_t start = g->item_menu_scroll;
        uint8_t owned;
        uint8_t decked;
        if (start >= s_view_count && s_view_count > 0) start = 0;
        for (n = start; n < s_view_count && (k < 8 || kr < 8); n++) {
            cu_row_sums(g, s_view_indices[n], &owned, &decked);
            if (owned > decked && k < 8) {
                s_ic_draw_pos[k] = n;
                s_row_owned[k] = owned;
                s_row_decked[k] = decked;
                k++;
            }
            if (decked > 0 && kr < 8) {
                s_ic_draw_pos_r[kr] = n;
                s_row_owned[8 + kr] = owned;
                s_row_decked[8 + kr] = decked;
                kr++;
            }
        }
        for (; k < 8; k++) {
            s_ic_draw_pos[k] = 0xFF;
            s_row_owned[k] = 0;
            s_row_decked[k] = 0;
        }
        for (; kr < 8; kr++) {
            s_ic_draw_pos_r[kr] = 0xFF;
            s_row_owned[8 + kr] = 0;
            s_row_decked[8 + kr] = 0;
        }
    }
    /* Erase what the rebuild just emptied: a slot/side that held a row
     * before and holds none now is blanked unconditionally (VRAM +
     * buffer + OAM) so the lean repaint never inherits stale icon
     * tiles.  Live slots are redrawn by the caller and still-dead
     * slots were already blank.  Every op here is bank-independent. */
    {
        uint8_t y;
        uint8_t oL;
        uint8_t oR;
        for (k = 0; k < 8; k++) {
            oL = s_cu_prev_pos[k];
            oR = s_cu_prev_pos_r[k];
            y = (uint8_t)(CUR_CARDS_TOP + k);
            if (oL != 0xFF && s_ic_draw_pos[k] == 0xFF &&
                oR != 0xFF && s_ic_draw_pos_r[k] == 0xFF) {
                cu_blank_span(y, 0, 20);
                cu_hide_side(k, 0);
                cu_hide_side(k, 1);
            } else {
                if (oL != 0xFF && s_ic_draw_pos[k] == 0xFF) {
                    cu_blank_span(y, 0, 9);
                    cu_hide_side(k, 0);
                }
                if (oR != 0xFF && s_ic_draw_pos_r[k] == 0xFF) {
                    cu_blank_span(y, 10, 9);
                    cu_hide_side(k, 1);
                }
            }
        }
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
            if (owned < n || owned > 99) owned = 99;
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

/* Grouped A-action scan (bank 11): finds the first group member that
 * can take another copy, verdict priority FULL > LOCKED > capped.
 * Same-bank verdict calls (deck_equip_verdict_banked lives in
 * deck_merge_banked.c, also bank 11); the fixed caller applies the
 * single add through deck_equip_card (re-verdicts deterministically)
 * and owns all telemetry/SFX.  Staging: g_bk_ptr_a = Game*,
 * g_bk_byte_a = primary collection index, g_bk_byte_d = pool.
 * Returns g_bk_byte_c = 0 + g_bk_byte_b = member collection index
 * when addable; otherwise c = 1 (FULL) / 2 (LOCKED) / 3 (capped) with
 * b = the first member index hit by that verdict (0xFF when no member
 * was tried).  The fixed caller routes FULL/LOCKED through
 * deck_equip_card once so its telemetry (CARD_EQUIP_LOCKED) still
 * fires; a fully-capped group (3) is a silent no-op on the LEFT side
 * (removal happens from the deck side instead of a clear-all). */
void deck_equip_verdict_banked(void);
void item_group_scan_banked(void)
{
    Game *g = (Game *)g_bk_ptr_a;
    uint8_t primary = g_bk_byte_a;
    uint8_t pool = g_bk_byte_d;
    CardState *cs;
    uint8_t mat, wpn, m2, w2;
    uint8_t i;
    uint8_t r;
    uint8_t full_idx = 0xFF;
    uint8_t locked_idx = 0xFF;
    uint8_t saw_any = 0;
    CardId id;
    if (!g || primary >= g->state.cards.collection.count) {
        g_bk_byte_c = 3;
        g_bk_byte_b = 0xFF;
        return;
    }
    cs = &g->state.cards;
    id = cs->collection.entries[primary].id;
    if (!cu_grp_key(id, &mat, &wpn)) {
        g_bk_byte_c = 3;
        g_bk_byte_b = 0xFF;
        return;
    }
    for (i = 0; i < cs->collection.count; i++) {
        id = cs->collection.entries[i].id;
        if (!cu_grp_key(id, &m2, &w2)) continue;
        if (m2 != mat || w2 != wpn) continue;
        g_bk_ptr_a = (void *)cs;
        g_bk_byte_a = (uint8_t)id;
        g_bk_byte_d = pool;
        deck_equip_verdict_banked();
        r = g_bk_byte_c;
        if (r == 1) {
            g_bk_byte_c = 0;
            g_bk_byte_b = i;
            g_bk_ptr_a = (void *)g;
            return;
        }
        saw_any = 1;
        if (r == 3) {
            full_idx = i;
            break;
        }
        if (r == 2 && locked_idx == 0xFF) locked_idx = i;
    }
    g_bk_ptr_a = (void *)g;
    if (full_idx != 0xFF) {
        g_bk_byte_c = 1;
        g_bk_byte_b = full_idx;
    } else if (locked_idx != 0xFF) {
        g_bk_byte_c = 2;
        g_bk_byte_b = locked_idx;
    } else {
        g_bk_byte_c = 3;
        g_bk_byte_b = saw_any ? primary : 0xFF;
    }
}
/* Row visibility for the two per-side pile/deck lists: a group row
 * shows on a side only when its count there is nonzero (pile side:
 * owned > decked; deck side: decked > 0).  Shared by the nav, the
 * post-A cursor park and the scroll window below. */
static uint8_t item_row_visible(Game *g, uint8_t pos, uint8_t side)
{
    uint8_t owned = 0;
    uint8_t decked = 0;
    if (!g || pos >= s_view_count) return 0;
    cu_row_sums(g, s_view_indices[pos], &owned, &decked);
    if (side == 0) return (uint8_t)(owned > decked);
    return (uint8_t)(decked > 0);
}

/* Scroll window for the active side's packed list: the smallest scroll
 * >= the current one that puts view position npos among the first 8
 * rows drawn on its side (a position above the window scrolls up onto
 * it).  One backward walk over [scroll, npos): stops at the 8th
 * visible row above npos, so it costs O(npos - scroll) visibility
 * checks -- not the O(n * 8) of re-ranking at every scroll step.
 * npos itself is assumed visible on the active side (callers land the
 * cursor on visible rows only). */
static void cu_scroll_into_view(Game *g, uint8_t npos)
{
    uint8_t side = g->item_menu_side;
    uint8_t lo = g->item_menu_scroll;
    uint8_t s = npos;
    uint8_t c = 0;
    if (lo >= npos) {
        g->item_menu_scroll = npos;
        return;
    }
    while (s > lo) {
        if (item_row_visible(g, (uint8_t)(s - 1), side)) {
            if (c == (uint8_t)(CUR_VISIBLE_CARD - 1)) break;
            c++;
        }
        s--;
    }
    g->item_menu_scroll = s;
}

/* Post-A reconcile (bank 11, staging: g_bk_ptr_a = Game*): rebuilds the
 * sums + per-side draw maps (the view itself never changes on A: only
 * deck counts move, so no filter/sort work is redone), then parks a
 * vanished acted row's cursor on the next visible row of the same side
 * (or FILTER when no visible row follows -- the bottom row or an
 * emptied side; never a side flip on A).
 * The fixed caller then repaints the list in place (no blink). */
void item_row_resum_banked(void)
{
    Game *g = (Game *)g_bk_ptr_a;
    uint8_t side;
    uint8_t cpos;
    uint8_t ni;
    uint8_t n;
    if (!g) return;
    item_view_build_banked();
    if (g->item_menu_index < CUR_FIRST_CARD) return;
    cpos = (uint8_t)(g->item_menu_index - CUR_FIRST_CARD);
    if (cpos >= s_view_count) return;
    side = g->item_menu_side;
    if (item_row_visible(g, cpos, side)) return;
    ni = 0;
    for (n = (uint8_t)(cpos + 1); n < s_view_count; n++) {
        if (item_row_visible(g, n, side)) {
            ni = (uint8_t)(n + CUR_FIRST_CARD);
            break;
        }
    }
    g->item_menu_index = ni;
    if (ni == 0)
        g->item_menu_scroll = 0;
    else
        cu_scroll_into_view(g, (uint8_t)(ni - CUR_FIRST_CARD));
}

/* Direction step for the shop list (g_bk_byte_b 0 = up, 1 = down,
 * g_bk_byte_c = visible count): clamp the new index banked so the
 * fixed update pays no bounds branch.  Staging: g_bk_ptr_a = Game*,
 * g_bk_byte_a = old index. */
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
    g_bk_sfx_req = (uint8_t)(SFX_CURSOR + 1);
}

/* DECK per-side navigation (bank 11): UP/DOWN move over the active
 * side's packed list (a zero count on that side hides the row,
 * wrapping at the ends); the FILTER/SORT row (index 0) always counts.
 * LEFT/RIGHT (mode 2) steps between the two cells: same group when it
 * shows on the target side (scrolled into that side's window -- its
 * packed rank there can differ), else the target side's top drawn row,
 * else its first visible row anywhere; an empty target side holds
 * position.  State only (index/side/scroll): the fixed caller repaints
 * the list in place, so nothing blinks.
 * Staging: g_bk_ptr_a = Game*, g_bk_byte_a = 0 up / 1 down / 2 step. */
void item_deck_nav_banked(void)
{
    Game *g = (Game *)g_bk_ptr_a;
    uint8_t mode = g_bk_byte_a;
    uint8_t total;
    uint8_t idx;
    uint8_t guard;
    if (!g) return;
    if (mode == 2) {
        uint8_t newside = (uint8_t)(g->item_menu_side ? 0 : 1);
        uint8_t i;
        if (g->item_menu_index < CUR_FIRST_CARD ||
            (uint8_t)(g->item_menu_index - CUR_FIRST_CARD) >= s_view_count)
            return;
        idx = (uint8_t)(g->item_menu_index - CUR_FIRST_CARD);
        if (item_row_visible(g, idx, newside)) {
            g->item_menu_side = newside;
            cu_scroll_into_view(g, idx);
            g_bk_sfx_req = (uint8_t)(SFX_CURSOR + 1);
            return;
        }
        /* Top drawn row of the target side (maps are current: every
         * DECK list render/repaint rebuilds them) keeps the window. */
        i = newside ? s_ic_draw_pos_r[0] : s_ic_draw_pos[0];
        if (i == 0xFF || i >= s_view_count) {
            for (i = 0; i < s_view_count; i++) {
                if (item_row_visible(g, i, newside)) break;
            }
            if (i >= s_view_count) return;
            g->item_menu_scroll = i;
        }
        g->item_menu_side = newside;
        g->item_menu_index = (uint8_t)(i + CUR_FIRST_CARD);
        g_bk_sfx_req = (uint8_t)(SFX_CURSOR + 1);
        return;
    }
    total = (uint8_t)(s_view_count + CUR_FIRST_CARD);
    if (total <= 1) return;
    /* Lazy side flip: the FILTER row never flips (LEFT/RIGHT keeps tab
     * switching there), so an emptied active side would strand UP/DOWN
     * on FILTER.  If the active side shows nothing but the other side
     * does, step over first -- both sides stay drawn, only the cursor
     * moves. */
    {
        uint8_t side = g->item_menu_side;
        uint8_t i;
        uint8_t found = 0;
        for (i = 0; i < s_view_count; i++) {
            if (item_row_visible(g, i, side)) {
                found = 1;
                break;
            }
        }
        if (!found && s_view_count > 0) {
            uint8_t other = (uint8_t)(side ? 0 : 1);
            for (i = 0; i < s_view_count; i++) {
                if (item_row_visible(g, i, other)) break;
            }
            if (i < s_view_count)
                g->item_menu_side = other;
        }
    }
    idx = g->item_menu_index;
    guard = (uint8_t)(total + 1);
    do {
        if (mode == 0)
            idx = idx > 0 ? (uint8_t)(idx - 1) : (uint8_t)(total - 1);
        else
            idx = idx < (uint8_t)(total - 1) ? (uint8_t)(idx + 1) : 0;
        if (idx == 0) break;
        if (item_row_visible(g, (uint8_t)(idx - CUR_FIRST_CARD),
                             g->item_menu_side))
            break;
    } while (--guard);
    /* No caret paint or cache touch here (AGENTS.md 36): the fixed
     * caller repaints the whole menu incrementally, so scrolling and
     * stepping never blink.  Just land the index; keep it drawable. */
    g->item_menu_index = idx;
    g_bk_sfx_req = (uint8_t)(SFX_CURSOR + 1);
    if (idx == 0)
        g->item_menu_scroll = 0;
    else
        cu_scroll_into_view(g, (uint8_t)(idx - CUR_FIRST_CARD));
}

/* Single-copy removal for the DECK right column (bank 11): sends the
 * first decked member of the group (collection order) back to the
 * owned pool.  Self-contained (AGENTS.md §52.11.1): no fixed-bank calls
 * -- the scan/swap mirrors deck_remove_card inline (swap-with-last +
 * DECK_MIN_CARDS floor), so the bank switch cannot strand it.
 * Staging: g_bk_ptr_a = Game*, g_bk_byte_a = primary collection index;
 * returns the member collection index in g_bk_byte_c (0xFF = none
 * decked) and the removed CardId in g_bk_byte_b.  The fixed caller
 * emits CARD_REMOVED_FROM_DECK itself. */
void item_group_remove_one_banked(void)
{
    Game *g = (Game *)g_bk_ptr_a;
    uint8_t primary = g_bk_byte_a;
    CardState *cs;
    uint8_t mat, wpn, m2, w2;
    uint8_t i, j;
    CardId id;
    if (!g || primary >= g->state.cards.collection.count) {
        g_bk_byte_c = 0xFF;
        return;
    }
    cs = &g->state.cards;
    id = cs->collection.entries[primary].id;
    if (!cu_grp_key(id, &mat, &wpn)) {
        g_bk_byte_c = 0xFF;
        return;
    }
    for (i = 0; i < cs->collection.count; i++) {
        id = cs->collection.entries[i].id;
        if (!cu_grp_key(id, &m2, &w2)) continue;
        if (m2 != mat || w2 != wpn) continue;
        for (j = 0; j < cs->deck.count; j++) {
            if (cs->deck.cards[j] != id) continue;
            if (cs->deck.count <= DECK_MIN_CARDS) {
                g_bk_byte_c = 0xFF;
                return;
            }
            cs->deck.cards[j] = cs->deck.cards[--cs->deck.count];
            g_bk_byte_c = i;
            g_bk_byte_b = (uint8_t)id;
            return;
        }
    }
    g_bk_byte_c = 0xFF;
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
        g->item_menu_side = 0;
        g->render_cache.valid = false;
    }
    g_bk_sfx_req = (uint8_t)(SFX_CURSOR + 1);
}
