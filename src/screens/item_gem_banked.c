#pragma bank 8
#pragma disable_warning 110

#include <gb/gb.h>
#include <gb/cgb.h>
#include "game.h"
#include "gems.h"
#include "ui.h"
#include "banked.h"

/* ── GEM-tab content, bank-8 body ──────────────────────────────────
 * The gem list renderer + equip navigator live here (not bank 2 with the
 * cards renderer) for the same structural reason as the quest tab
 * (item_quest_banked.c): the registry (g_gems[] in gems_content.c) is
 * banked content, and a banked body must share its tables' bank
 * (AGENTS.md 52.11.1).  Bank 8 is the overflow bank (AGENTS.md 55.5)
 * with room for the feature to grow (more gems, per-gem detail).
 *
 * ABI: the fixed dispatch in item_screen.c sends the GEM tab here and
 * the QUEST tab to item_quest_nav_banked (same bank); the GEM page
 * renders here in bank 8 with its registry (never bank 2).  Reads only
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

/* Tab header (banked: fixed _HOME is nearly full, AGENTS.md 52.18) --
 * same row as the other tabs: row 0 labels, row 1 caret under GEM. */
static void gem_draw_header(void)
{
    GEM_DRAW_TEXT(0, 0, "DECK QUEST HERO GEM", 19);
    GEM_DRAW_TEXT(16, 1, "^", 1);
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
#define GEM_TAB_GEM 3

/* Detail mode shares the fixed-bank MODE_QUEST_DETAIL value (2): the
 * fixed dispatch early-returns for it (no LR interference) and the fixed
 * B-handler closes it back to LIST -- so the GEM detail page costs the
 * fixed bank nothing (AGENTS.md 52.18). */
#define GEM_MODE_DETAIL 2

/* QUEST-tab bodies (same bank: direct call, no trampoline).  The fixed
 * dispatch routes both side tabs here; GEM handles its own tab and
 * forwards QUEST back, so the fixed bank pays no extra branch. */
void item_quest_nav_banked(void);
void item_quest_render_banked(void);

/* ── Gem detail (SELECT on a gem row) ─────────────────────────────
 *   row 3: gem name
 *   row 5: what it unlocks (skill)
 *   rows 6-8: per-gem explainer from the registry (GemDefinition.detail,
 *             so gem #2's text never hard-codes gem #1's rules)
 *   row 9: "SEL:BACK" (B closes via the fixed handler, same as quests) */
static void gem_draw_detail(uint8_t bit)
{
    uint8_t reg = gem_ptr_bit(bit);
    uint8_t r;
    GEM_DRAW_TEXT(0, 3, g_gems[reg].name, 12);
    GEM_DRAW_TEXT(0, 5, g_gems[reg].skill, 15);
    for (r = 0; r < 3; r++) {
        GEM_DRAW_TEXT(0, (uint8_t)(6 + r), g_gems[reg].detail[r], 20);
    }
    GEM_DRAW_TEXT(0, 9, "SEL:BACK", 8);
}

/* GEM-tab input behind the thin fixed dispatch in item_screen.c.
 * Pad edges come straight from WRAM (always mapped, like the staged
 * Game*).  UP/DOWN moves an inline caret (no scroll: the owned list is
 * short); A toggles the equip bit manually (never auto-equipped --
 * winning the gem only sets OWNED).  The toggle writes GameState
 * directly (no fixed-bank call, no telemetry event -- same v1 gap as
 * the gamble table's gold) and forces a full redraw so ON/OFF flips. */
void item_gem_nav_banked(void)
{
    Game *g = (Game *)g_bk_ptr_a;
    uint8_t total = 0;
    uint8_t edges;
    uint8_t bit;
    int16_t equipped;
    uint8_t old_index;
    uint8_t new_index;
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
    old_row = (uint8_t)(3 + (old_index << 1));
    new_row = (uint8_t)(3 + (new_index << 1));
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
    uint8_t bit;
    int16_t owned;
    if (!g) return;

    /* The other side tab (QUEST) belongs to the quest renderer. */
    if (g->item_menu_tab != GEM_TAB_GEM) {
        item_quest_render_banked();
        return;
    }

    /* Sweep the card rider entries (same as the quest tab). */
    for (e = 1; e <= 12; e++) {
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
    } else if (total == 0) {
        GEM_DRAW_TEXT(2, 3, "NO GEMS YET", 11);
    } else {
        v = 0;
        for (bit = 0; bit < GEM_COUNT; bit++) {
            if ((owned & (int16_t)(1 << bit)) == 0) continue;
            gem_draw_gem(g, bit, (uint8_t)(3 + (v << 1)),
                         (g->item_menu_index == v) ? 1 : 0);
            v++;
        }
    }
    if (g->item_menu_message == GEM_MSG_MAX)
        GEM_DRAW_TEXT(0, 16, "MAX 3 GEMS", 10);
    else if (total == 0)
        GEM_DRAW_TEXT(0, 16, "B:BACK", 6);
    else
        GEM_DRAW_TEXT(0, 16, "A:EQUIP SEL:INFO", 16);
}
