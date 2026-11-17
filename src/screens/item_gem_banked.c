#pragma bank 13
#pragma disable_warning 110

#include <gb/gb.h>
#include <gb/cgb.h>
#include "game.h"
#include "gems.h"
#include "ui.h"
#include "banked.h"

/* ── GEM-tab content, bank-13 body ─────────────────────────────────
 * The gem list renderer + equip navigator live here (not bank 2 with the
 * cards renderer) for the same structural reason as the quest tab
 * (item_quest_banked.c): the registry (g_gems[] in gems_content.c) is
 * banked content, and a banked body must share its tables' bank
 * (AGENTS.md 52.11.1).  Bank 13 is free in both builds (bank 8 filled
 * up) with room for the feature to grow (more gems, per-gem detail).
 *
 * ABI: the fixed dispatch in item_screen.c sends the GEM tab here and
 * the QUEST tab to item_quest_nav_banked (same bank); the GEM page
 * renders here in bank 13 with its registry (never bank 2).  Reads only
 * this bank's data, the staged Game*, and always-mapped WRAM; calls no
 * fixed-bank functions (variable writes go straight to GameState --
 * same v1 gap as the gamble table's gold moves, no VARIABLE_SET event;
 * the owned/equipped bits stay snapshot-assertable and the skill itself
 * is fully observable through COMBO_RESOLVED).
 *
 * The text/color/rider-cell helper BODIES mirror the bank-2 item
 * renderer (same buffer-checked VRAM protocol): a banked body cannot
 * call across banks, so sharing one copy is impossible -- but their
 * WRAM scratch (s_ic_* globals owned by item_content_banked.c) IS
 * shared, since the trampoline is non-reentrant and only one tab
 * renders at a time.  KEEP IN SYNC if that protocol changes. */

#define GEM_MSG_MAX 4   /* transient "MAX 3 GEMS" (see item_menu_message) */
#define GEM_MSG_TTL 45

extern const GemDefinition g_gems[GEM_COUNT];
extern char g_ui_screen_buf[18][21];
extern uint8_t ui_font_tile_base;
extern uint8_t g_is_cgb;

/* Shared draw scratch owned by item_content_banked.c (see the note
 * there). */
extern uint8_t s_ic_txt_i;
extern uint8_t s_ic_col_i;

extern uint8_t s_ic_y;
extern volatile uint8_t *s_ic_dst;
extern char *s_ic_buf;

extern uint8_t s_ic_arg_x;
extern uint8_t s_ic_arg_y;
extern uint8_t s_ic_arg_len;
extern uint8_t s_ic_arg_palette;
extern const char *s_ic_arg_text;

static void gem_draw_text_fast(void)
{
    uint8_t ended;
    char ch;
    uint8_t max_chars = s_ic_arg_len;
    uint8_t x = s_ic_arg_x;
    uint8_t y = s_ic_arg_y;

    if (y >= 18) return;
    if ((uint8_t)(x + max_chars) > 20) max_chars = (uint8_t)(20 - x);

    VBK_REG = 0;
    ended = (s_ic_arg_text == NULL);
    s_ic_dst = (volatile uint8_t *)(0x9800 + ((uint16_t)y << 5) + x);
    s_ic_buf = &g_ui_screen_buf[y][x];

    for (s_ic_txt_i = 0; s_ic_txt_i < max_chars; s_ic_txt_i++) {
        if (!ended) {
            ch = s_ic_arg_text[s_ic_txt_i];
            if (ch == '\0') {
                ended = 1;
                ch = ' ';
            }
        } else {
            ch = ' ';
        }
        if (*s_ic_buf != ch) {
            uint8_t tile = (uint8_t)(ui_font_tile_base + (uint8_t)(ch - ' '));
            BANKED_VRAM_WRITE(s_ic_dst, tile);
            *s_ic_buf = ch;
        }
        s_ic_dst++;
        s_ic_buf++;
    }
}

#define GEM_DRAW_TEXT(x, y, text, len) do { \
    s_ic_arg_x = (x); s_ic_arg_y = (y); s_ic_arg_text = (text); s_ic_arg_len = (len); \
    gem_draw_text_fast(); \
} while(0)

/* Tab header: single source of truth is menu_tabs_snippet.h ──────────
 * (compiled per bank: banked bodies cannot call across banks,
 * AGENTS.md 52.11.1).  GEMS cursor col 16. */
#define MENU_TAB_DRAW_TEXT GEM_DRAW_TEXT
#include "menu_tabs_snippet.h"

static void gem_draw_header(void)
{
    menu_draw_tabs(16);
}

/* Rider OAM sweep for the GEM tab (entries 1-12): the CARDS list paints
 * element icons over per-row cells, so entering this tab must hide them
 * (the quest tab sweeps the same way).  WRAM shadow OAM only. */
static void gem_rider_hide(uint8_t entry)
{
    volatile uint8_t *re;
    re = (volatile uint8_t *)(0xC000u + ((uint16_t)entry << 2));
    re[0] = 0;
    re[2] = 0;
}

/* Visible-to-bit map: item_menu_index addresses the OWNED list, so
 * equip needs the gem bit of the n-th owned gem. */
static uint8_t gem_owned_bit(Game *g, uint8_t vis, uint8_t *total_out)
{
    uint8_t vis_count = 0;
    uint8_t i;
    int16_t owned;
    uint8_t found = 0;
    if (!g) {
        if (total_out) *total_out = 0;
        return 0;
    }
    owned = g->state.variables.values[VARIABLE_ID_GEM_OWNED - 1];
    for (i = 0; i < GEM_COUNT; i++) {
        if ((owned & (int16_t)(1 << i)) == 0) continue;
        if (vis_count == vis) found = i;
        vis_count++;
    }
    if (total_out) *total_out = vis_count;
    return found;
}

static uint8_t gem_ptr_bit(uint8_t bit)
{
    uint8_t i;
    if (bit >= GEM_COUNT) return 0;
    for (i = 0; i < GEM_COUNT; i++) {
        if (g_gems[i].bit == bit) return i;
    }
    return 0;
}

static uint8_t gem_equipped_count(int16_t equipped)
{
    uint8_t n = 0;
    uint8_t i;
    for (i = 0; i < 16; i++) {
        if (equipped & (int16_t)(1 << i)) n++;
    }
    return n;
}

static void gem_draw_gem(Game *g, uint8_t bit, uint8_t y, uint8_t selected)
{
    uint8_t reg = gem_ptr_bit(bit);
    int16_t equipped;
    equipped = g->state.variables.values[VARIABLE_ID_GEM_EQUIPPED - 1];
    if (selected) GEM_DRAW_TEXT(0, y, ">", 1);
    else GEM_DRAW_TEXT(0, y, " ", 1);
    GEM_DRAW_TEXT(2, y, g_gems[reg].name, 12);
    if ((equipped & (int16_t)(1 << bit)) != 0) {
        GEM_DRAW_TEXT(15, y, "ON", 2);
    } else {
        GEM_DRAW_TEXT(15, y, "OFF", 3);
    }
    GEM_DRAW_TEXT(2, (uint8_t)(y + 1), g_gems[reg].skill, 15);
}

/* Tab ids mirror item_screen.c (fixed bank): TAB_DECK 0, TAB_QUEST 1,
 * TAB_HERO 2, TAB_GEM 3.  KEEP IN SYNC -- the router branches below and
 * the fixed dispatch both depend on them. */
#define GEM_TAB_QUEST 1
#define GEM_TAB_HERO  2
#define GEM_TAB_GEM   3

/* Detail mode shares the fixed-bank MODE_QUEST_DETAIL value (2): the
 * fixed dispatch early-returns for it (no LR interference) and the fixed
 * B-handler closes it back to LIST -- so the GEM detail page costs the
 * fixed bank nothing (AGENTS.md 52.18). */
#define GEM_MODE_DETAIL 2

/* QUEST/HERO bodies (same bank: direct call, no trampoline).  The fixed
 * dispatch routes every non-DECK tab here; GEM handles its own tab and
 * forwards QUEST and HERO, so the fixed bank pays no extra branch. */
void item_quest_nav_banked(void);
void item_quest_render_banked(void);
void item_status_render_banked(void);

/* ── Gem detail (SELECT on a gem row) ─────────────────────────────
 *   row 3: gem name
 *   row 5: what it unlocks (skill)
 *   rows 6-8: per-gem explainer from the registry (GemDefinition.detail,
 *             so gem #2's text never hard-codes gem #1's rules)
 *   row 15: bottom box border (same as every list tab)
 *   row 16: "B:BACK" (B closes via the fixed handler, same as quests) */
static void gem_draw_detail(uint8_t bit)
{
    uint8_t reg = gem_ptr_bit(bit);
    uint8_t r;
    uint8_t i;
    GEM_DRAW_TEXT(0, 3, g_gems[reg].name, 12);
    GEM_DRAW_TEXT(0, 5, g_gems[reg].skill, 15);
    for (r = 0; r < 3; r++) {
        GEM_DRAW_TEXT(0, (uint8_t)(6 + r), g_gems[reg].detail[r], 20);
    }
    for (i = 0; i < 20; i++)
        menu_stamp_chrome(15, i, UI_TILE_MENUS_BOTTOM_BORDER);
    GEM_DRAW_TEXT(0, 16, "B:BACK", 6);
}

/* Gem-block window: same geometry as the QUEST tab (AGENTS.md 52.11.1
 * lets both renderers share bank 13, but the row content differs, so
 * the layout is mirrored, not shared).  Each owned gem takes 3 rows
 * (name, skill, quests-separator rule), rows 3-14 hold exactly 4
 * blocks.  Longer pockets scroll via item_menu_scroll (reset on tab
 * switch).  Rows 2/15 are free for the scroll indicators; row 15 also
 * carries the bottom box border like every other list tab; row 16
 * keeps the footer. */
#define GEM_WINDOW  4
#define GEM_ROW_TOP 3
#define GEM_BLOCK   3

/* Title row for a window slot (shifts/adds only: no multiply, which
 * would pull the SDCC mult lib into fixed _CODE, AGENTS.md 52.18). */
static uint8_t gem_slot_row(uint8_t slot)
{
    return (uint8_t)(GEM_ROW_TOP + (uint8_t)((uint8_t)(slot << 1) + slot));
}

/* Scroll-indicator cell (VRAM list-scroll tiles: the 180-degree
 * rotated copy for the row-2 up caret, the sheet art for the row-15
 * down caret; buffer keeps '^' up / 'v' down for harness asserts).
 * Hiding restores hide_tile (font space on row 2, the bottom border
 * on row 15 so the rule stays unbroken when nothing sits below the
 * window). */
static void gem_stamp_scroll(uint8_t y, uint8_t x, uint8_t show,
                             char letter, uint8_t show_tile,
                             uint8_t hide_tile)
{
    s_ic_buf = &g_ui_screen_buf[y][x];
    *s_ic_buf = show ? letter : ' ';
    s_ic_dst = (volatile uint8_t *)(0x9800u + ((uint16_t)y << 5) + x);
    VBK_REG = 0;
    BANKED_VRAM_WRITE(s_ic_dst, show ? show_tile : hide_tile);
}

/* GEM-tab input behind the thin fixed dispatch in item_screen.c.
 * Pad edges come straight from WRAM (always mapped, like the staged
 * Game*).  UP/DOWN moves an inline caret; A toggles the equip bit
 * manually (never auto-equipped -- winning the gem only sets OWNED).
 * Longer pockets scroll a 4-block window (item_menu_scroll, reset on
 * tab switch): the cursor repaint stays inline while the window holds,
 * and a window move takes the full redraw instead.  The toggle writes
 * GameState directly (no fixed-bank call, no telemetry event -- same
 * v1 gap as the gamble table's gold) and forces a full redraw so
 * ON/OFF flips. */
void item_gem_nav_banked(void)
{
    Game *g = (Game *)g_bk_ptr_a;
    uint8_t total = 0;
    uint8_t edges;
    uint8_t bit;
    int16_t equipped;
    uint8_t old_index;
    uint8_t new_index;
    uint8_t old_scroll;
    uint8_t old_row;
    uint8_t new_row;
    if (!g) return;
    if (g->item_menu_tab != GEM_TAB_GEM) {
        item_quest_nav_banked();
        return;
    }
    bit = gem_owned_bit(g, g->item_menu_index, &total);
    if (total == 0) {
        /* Empty gem pocket: nothing to select or equip. */
        return;
    }
    if (g->item_menu_index >= total) {
        g->item_menu_index = 0;
        bit = gem_owned_bit(g, 0, &total);
    }
    /* Pressed edges in one pass (same pattern as the quest nav). */
    edges = (uint8_t)(g_inp_pad & (uint8_t)~g_inp_prev);
    if (edges & (uint8_t)(1u << INPUT_SELECT)) {
        /* Detail resolves the visible position through gem_owned_bit
         * (index addresses the OWNED list, like the equip toggle). */
        g->item_menu_mode = GEM_MODE_DETAIL;
        g->render_cache.valid = false;
        return;
    }
    if (edges & (uint8_t)(1u << INPUT_A)) {
        equipped = g->state.variables.values[VARIABLE_ID_GEM_EQUIPPED - 1];
        if ((equipped & (int16_t)(1 << bit)) != 0) {
            equipped = (int16_t)(equipped & (int16_t)~(int16_t)(1 << bit));
            g->state.variables.values[VARIABLE_ID_GEM_EQUIPPED - 1] = equipped;
        } else {
            if (gem_equipped_count(equipped) >= MAX_EQUIPPED_GEMS) {
                g->item_menu_message = GEM_MSG_MAX;
                g->item_menu_msg_ttl = GEM_MSG_TTL;
                g->render_cache.valid = false;
                return;
            }
            equipped = (int16_t)(equipped | (int16_t)(1 << bit));
            g->state.variables.values[VARIABLE_ID_GEM_EQUIPPED - 1] = equipped;
        }
        g->render_cache.valid = false;
        return;
    }
    old_index = g->item_menu_index;
    if (edges & (uint8_t)(1u << INPUT_UP)) {
        new_index = (old_index > 0) ? (uint8_t)(old_index - 1) : (uint8_t)(total - 1);
    } else if (edges & (uint8_t)(1u << INPUT_DOWN)) {
        new_index = (old_index < (uint8_t)(total - 1)) ? (uint8_t)(old_index + 1) : 0;
    } else {
        return;
    }
    g->item_menu_index = new_index;
    /* Blip only on a real move: with a single owned gem UP/DOWN wraps
     * back onto the same row, and a no-op press must stay silent. */
    if (new_index != old_index) {
        g_bk_sfx_req = (uint8_t)(SFX_CURSOR + 1);
    }
    /* Scroll follow: keep the selection inside the 4-block window
     * (wraps land back at the edges, so no separate wrap case). */
    old_scroll = g->item_menu_scroll;
    if (new_index < old_scroll) {
        g->item_menu_scroll = new_index;
    } else if (new_index > (uint8_t)(old_scroll + (GEM_WINDOW - 1))) {
        g->item_menu_scroll = (uint8_t)(new_index - (GEM_WINDOW - 1));
    }
    if (g->item_menu_scroll != old_scroll) {
        g->render_cache.valid = false;
        return;
    }
    old_row = gem_slot_row((uint8_t)(old_index - old_scroll));
    new_row = gem_slot_row((uint8_t)(new_index - old_scroll));
    if (old_row != new_row) {
        GEM_DRAW_TEXT(0, old_row, " ", 1);
        GEM_DRAW_TEXT(0, new_row, ">", 1);
    }
}

void item_gem_render_banked(void)
{
    Game *g = (Game *)g_bk_ptr_a;
    uint8_t e;
    uint8_t total = 0;
    uint8_t v;
    uint8_t vis;
    uint8_t drawn;
    uint8_t y;
    uint8_t i;
    uint8_t max_scroll;
    uint8_t bit;
    int16_t owned;
    if (!g) return;

    /* The other side tabs belong to their own renderers (same bank). */
    if (g->item_menu_tab == GEM_TAB_HERO) {
        item_status_render_banked();
        return;
    }
    if (g->item_menu_tab != GEM_TAB_GEM) {
        item_quest_render_banked();
        return;
    }

    /* Sweep the card rider entries (same as the quest tab, plus the
     * DECK material ingots on entries 12-32). */
    for (e = 1; e <= 32; e++) {
        gem_rider_hide(e);
    }

    gem_draw_header();

    owned = g->state.variables.values[VARIABLE_ID_GEM_OWNED - 1];
    total = 0;
    for (v = 0; v < GEM_COUNT; v++) {
        if ((owned & (int16_t)(1 << v)) != 0) total++;
    }
    if (g->item_menu_mode == GEM_MODE_DETAIL) {
        if (total != 0) {
            bit = gem_owned_bit(g, g->item_menu_index, &total);
            gem_draw_detail(bit);
        }
        return;
    }

    max_scroll = (total > GEM_WINDOW) ?
        (uint8_t)(total - GEM_WINDOW) : 0;
    if (g->item_menu_scroll > max_scroll)
        g->item_menu_scroll = max_scroll;
    /* Bottom box border on row 15, same row as the other list tabs
     * (the down indicator below overlays col 19 only while more gems
     * sit below the window). */
    for (i = 0; i < 20; i++)
        menu_stamp_chrome(15, i, UI_TILE_MENUS_BOTTOM_BORDER);
    /* Scroll indicators on the free rows (buffer '^'/'v'). */
    gem_stamp_scroll(2, 19, (uint8_t)(g->item_menu_scroll > 0), '^',
                     UI_TILE_MENUS_LIST_SCROLL_UP, ui_font_tile_base);
    gem_stamp_scroll(15, 19,
        (uint8_t)((uint8_t)(g->item_menu_scroll + GEM_WINDOW) < total),
        'v', UI_TILE_MENUS_LIST_SCROLL_ARROW,
        UI_TILE_MENUS_BOTTOM_BORDER);

    if (total == 0) {
        GEM_DRAW_TEXT(2, 3, "NO GEMS YET", 11);
    } else {
        drawn = 0;
        vis = 0;
        for (bit = 0; bit < GEM_COUNT; bit++) {
            if ((owned & (int16_t)(1 << bit)) == 0) continue;
            if (vis < g->item_menu_scroll ||
                vis >= (uint8_t)(g->item_menu_scroll + GEM_WINDOW)) {
                vis++;
                continue;
            }
            y = gem_slot_row(drawn);
            gem_draw_gem(g, bit, y, (g->item_menu_index == vis) ? 1 : 0);
            /* Separator rule between blocks (cols 2-10, under the
             * name text): every drawn block ends with one. */
            for (i = 2; i <= 10; i++)
                menu_stamp_chrome((uint8_t)(y + 2),
                                  i, UI_TILE_MENUS_QUESTS_SEPARATOR);
            drawn++;
            vis++;
        }
    }
    if (g->item_menu_message == GEM_MSG_MAX)
        GEM_DRAW_TEXT(0, 16, "MAX 3 GEMS", 10);
    else if (total == 0)
        GEM_DRAW_TEXT(0, 16, "B:BACK", 6);
    else
        GEM_DRAW_TEXT(0, 16, "A:EQUIP SEL:INFO", 16);
}
