#pragma bank 13
#pragma disable_warning 110

#include <gb/gb.h>
#include <gb/cgb.h>
#include "game.h"
#include "quest.h"
#include "ui.h"
#include "banked.h"

/* ── Quest-tab content, bank-13 body ───────────────────────────────
 * The quest list renderer + input navigator live here (not bank 2 with
 * the cards renderer) for two structural reasons (AGENTS.md 52.18):
 *
 * 1. The quest registry (g_quests[]) is banked content: a banked body
 *    must share its table's bank (AGENTS.md 52.11.1), and the registry
 *    outgrew bank 2 alongside the event table.  quests_content.c carries
 *    `#pragma bank 13` for the same reason.
 * 2. Banks 2 and 5 sit hard against 16 KB, and bank 8 (the old overflow
 *    bank, AGENTS.md 55.5) filled up with the separator/scroll chrome;
 *    bank 13 is free in both builds with room for the feature to grow
 *    (more quests, per-quest detail).
 *
 * ABI: fixed-bank wrappers in item_screen.c dispatch here (QUEST tab)
 * and to item_content_banked.c (DECK tab) through the WRAM trampoline:
 * g_bk_ptr_a = Game*.  Reads only this bank's code/data, the staged
 * Game*, and always-mapped WRAM; calls no fixed-bank functions (the
 * variable lookup below is a bank-local replica, same pattern as
 * actor_load_banked.c).  No banked-to-banked calls: the caret repaint
 * is inline and the quest marker glyph write is direct VRAM.
 *
 * The text/color/rider-cell helper BODIES mirror the bank-2 item
 * renderer above (same buffer-checked VRAM protocol): a banked body
 * cannot call across banks, so sharing one copy is impossible -- but
 * their WRAM scratch (s_ic_* globals owned by item_content_banked.c)
 * IS shared, since the trampoline is non-reentrant and only one tab
 * renders at a time.  KEEP IN SYNC if that protocol changes. */

#define MODE_QUEST_DETAIL 2

/* Quest-block window: each tracked quest takes 3 rows (title, tracker,
 * quests-separator rule), rows 3-14 hold exactly 4 blocks.  Longer logs
 * scroll (item_menu_scroll = first visible quest position, reset on tab
 * switch like the DECK scrolls).  Rows 2/15 are free for the scroll
 * indicators; row 16 keeps the footer. */
#define QUEST_WINDOW  4
#define QUEST_ROW_TOP 3
#define QUEST_BLOCK   3

extern const QuestDefinition g_quests[];
extern char g_ui_screen_buf[18][21];
extern uint8_t ui_font_tile_base;
extern uint8_t g_is_cgb;

/* Shared draw scratch owned by item_content_banked.c (see the note
 * there); the quest-table cursor + row pointer stay local to this body
 * (they replace the s_ic_qst_pos/s_ic_q the cards file dropped). */
extern uint8_t s_ic_txt_i;
extern uint8_t s_ic_col_i;
static uint8_t s_iq_qst_pos;

extern uint8_t s_ic_y;
static const QuestDefinition *s_iq_q;
extern volatile uint8_t *s_ic_dst;
extern char *s_ic_buf;

extern uint8_t s_ic_arg_x;
extern uint8_t s_ic_arg_y;
extern uint8_t s_ic_arg_len;
extern uint8_t s_ic_arg_palette;
extern const char *s_ic_arg_text;

static void iq_draw_text_fast(void)
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
        /* Unconditional VRAM write (no skip guard): icon/chrome tiles
         * carry space buffers, so a guarded paint would leave stale
         * art in VRAM while the buffer looks correct.  Row-local
         * blank+paint never shows a full-screen wipe (AGENTS.md 36). */
        {
            uint8_t tile = (uint8_t)(ui_font_tile_base + (uint8_t)(ch - ' '));
            BANKED_VRAM_WRITE(s_ic_dst, tile);
            *s_ic_buf = ch;
        }
        s_ic_dst++;
        s_ic_buf++;
    }
}

#define IQ_DRAW_TEXT(x, y, text, len) do { \
    s_ic_arg_x = (x); s_ic_arg_y = (y); s_ic_arg_text = (text); s_ic_arg_len = (len); \
    iq_draw_text_fast(); \
} while(0)

static void iq_color_span_fast(void)
{
    uint8_t x = s_ic_arg_x;
    uint8_t y = s_ic_arg_y;
    uint8_t len = s_ic_arg_len;
    uint8_t pal = s_ic_arg_palette & 0x07;

    if (!g_is_cgb) return;
    if (y >= 18 || x >= 32) return;
    if ((uint8_t)(x + len) > 32) len = (uint8_t)(32 - x);

    s_ic_dst = (volatile uint8_t *)(0x9800 + ((uint16_t)y << 5) + x);
    VBK_REG = 1;
    for (s_ic_col_i = 0; s_ic_col_i < len; s_ic_col_i++) {
        BANKED_VRAM_WRITE(&s_ic_dst[s_ic_col_i], (uint8_t)(pal & 0x07));
    }
    VBK_REG = 0;
}

#define IQ_COLOR_SPAN(x, y, len, pal) do { \
    s_ic_arg_x = (x); s_ic_arg_y = (y); s_ic_arg_len = (len); s_ic_arg_palette = (pal); \
    iq_color_span_fast(); \
} while(0)

/* Tab header: single source of truth is menu_tabs_snippet.h ──────────
 * (compiled per bank: banked bodies cannot call across banks,
 * AGENTS.md 52.11.1).  QUEST cursor col 5. */
#define MENU_TAB_DRAW_TEXT IQ_DRAW_TEXT
#include "menu_tabs_snippet.h"

static void iq_draw_header(void)
{
    menu_draw_tabs(5);
}

/* Visible-to-table index map: item_menu_index addresses the VISIBLE
 * list (NOT_STARTED quests hidden), so detail/navigate needs the
 * table position of the n-th tracked quest. */
static QuestStatus iq_quest_status(const GameState *state,
                                   const QuestDefinition *q);
static const QuestDefinition *iq_visible_quest(Game *g, uint8_t vis,
                                              uint8_t *total_out)
{
    uint8_t vis_count = 0;
    uint8_t i;
    const QuestDefinition *found = NULL;
    for (i = 0; i < GAME_QUEST_COUNT; i++) {
        if (iq_quest_status(&g->state, &g_quests[i]) ==
            QUEST_STATUS_NOT_STARTED)
            continue;
        if (vis_count == vis) found = &g_quests[i];
        vis_count++;
    }
    if (total_out) *total_out = vis_count;
    return found;
}

/* Rider OAM sweep for the quest tab (entries 1-5): the DECK list paints
 * element icons over per-row cells, so entering the quest tab must hide
 * them (the cards body sweeps on its own renders the same way).  WRAM
 * shadow OAM only, like its bank-2 twin. */
static void iq_rider_hide(uint8_t entry)
{
    volatile uint8_t *re;
    re = (volatile uint8_t *)(0xC000u + ((uint16_t)entry << 2));
    re[0] = 0;
    re[2] = 0;
}

/* Bank-local replica of game_variable_get() (src/rpg/state.c): a banked
 * body must never call fixed-bank code (AGENTS.md 52.11.1) -- the call
 * only works today because _game_variable_get happens to link into ROM0,
 * and any placement shift would jump into unmapped bytes.  Same pattern
 * as actor_load_banked.c.  KEEP IN SYNC with src/rpg/state.c if the
 * storage layout ever changes. */
static int16_t iq_variable_get(const GameState *state, VariableId variable)
{
    if (!state || variable < 1 || variable > MAX_STATE_VARIABLES) return 0;
    return state->variables.values[variable - 1];
}

static QuestStatus iq_quest_status(const GameState *state, const QuestDefinition *q)
{
    int16_t v;
    if (!state || !q || q->status_variable == 0) {
        return QUEST_STATUS_NOT_STARTED;
    }
    v = iq_variable_get(state, q->status_variable);
    if (v == q->status_complete) return QUEST_STATUS_COMPLETE;
    if (v == q->status_active) return QUEST_STATUS_ACTIVE;
    return QUEST_STATUS_NOT_STARTED;
}

static void iq_quest_draw_status_line(Game *g, const QuestDefinition *q, uint8_t y)
{
    QuestStatus st;
    char b[6];
    uint8_t val;
    if (!q) return;
    st = iq_quest_status(&g->state, q);

    /* NOT_STARTED never reaches here: iq_draw_quest only draws tracked
     * (ACTIVE/COMPLETE) quests, so anything else is COMPLETE. */
    if (st == QUEST_STATUS_ACTIVE) {
        if (q->progress_variable != 0) {
            val = (uint8_t)iq_variable_get(&g->state, q->progress_variable);
            IQ_DRAW_TEXT(2, y, q->progress_label ? q->progress_label : "", 8);
            b[0] = ':';
            b[1] = ' ';
            b[2] = (char)('0' + val);
            b[3] = '/';
            b[4] = (char)('0' + (uint8_t)q->progress_target);
            b[5] = '\0';
            IQ_DRAW_TEXT(10, y, b, 5);
            /* Gaps around the counter (DECK digits run full width). */
            IQ_DRAW_TEXT(0, y, "  ", 2);
            IQ_DRAW_TEXT(15, y, "     ", 5);
        } else {
            IQ_DRAW_TEXT(2, y, "active", 6);
            IQ_DRAW_TEXT(0, y, "  ", 2);
            IQ_DRAW_TEXT(8, y, "            ", 12);
        }
    } else {
        IQ_DRAW_TEXT(1, y, "complete - ", 11);
        IQ_DRAW_TEXT(12, y, q->complete_note ? q->complete_note : "", 8);
        IQ_DRAW_TEXT(0, y, " ", 1);
    }
}

/* Scroll-indicator cell (VRAM list-scroll tiles: the 180-degree
 * rotated copy for the row-2 up caret, the sheet art for the row-15
 * down caret; buffer keeps '^' up / 'v' down for harness asserts).
 * VRAM always writes: the buffer glyph never matches the PPU tile, so
 * the text skip-guard would stale-hide it (AGENTS.md 52.25).  Hiding
 * restores hide_tile (font space on row 2, the bottom border on row 15
 * so the rule stays unbroken when nothing sits below the window). */
static void iq_stamp_scroll(uint8_t y, uint8_t x, uint8_t show, char letter,
                            uint8_t show_tile, uint8_t hide_tile)
{
    s_ic_buf = &g_ui_screen_buf[y][x];
    *s_ic_buf = show ? letter : ' ';
    s_ic_dst = (volatile uint8_t *)(0x9800u + ((uint16_t)y << 5) + x);
    VBK_REG = 0;
    BANKED_VRAM_WRITE(s_ic_dst, show ? show_tile : hide_tile);
}

/* Title row for a window slot (shifts/adds only: no multiply, which
 * would pull the SDCC mult lib into fixed _CODE, AGENTS.md 52.18). */
static uint8_t iq_slot_row(uint8_t slot)
{
    return (uint8_t)(QUEST_ROW_TOP + (uint8_t)((uint8_t)(slot << 1) + slot));
}

static void iq_draw_quest(Game *g)
{
    uint8_t total = 0;
    uint8_t max_scroll;
    uint8_t vis;
    uint8_t drawn;
    uint8_t y;
    uint8_t i;
    /* Count tracked quests first: the window clamp needs the total. */
    for (s_iq_qst_pos = 0; s_iq_qst_pos < GAME_QUEST_COUNT; s_iq_qst_pos++) {
        if (iq_quest_status(&g->state, &g_quests[s_iq_qst_pos]) !=
            QUEST_STATUS_NOT_STARTED)
            total++;
    }
    max_scroll = (total > QUEST_WINDOW) ?
        (uint8_t)(total - QUEST_WINDOW) : 0;
    if (g->item_menu_scroll > max_scroll)
        g->item_menu_scroll = max_scroll;
    /* Bottom box border on row 15, same row as the DECK screen
     * (full width; the down indicator below overlays col 19 only
     * while more blocks sit below the window). */
    for (i = 0; i < 20; i++)
        menu_stamp_chrome(15, i, UI_TILE_MENUS_BOTTOM_BORDER);
    /* Row 2 holds only the up caret; the DECK filter row would
     * survive here otherwise. */
    IQ_DRAW_TEXT(0, 2, "                   ", 19);
    /* Scroll indicators on the free rows (buffer '^'/'v'). */
    iq_stamp_scroll(2, 19, (uint8_t)(g->item_menu_scroll > 0), '^',
                    UI_TILE_MENUS_LIST_SCROLL_UP, ui_font_tile_base);
    iq_stamp_scroll(15, 19,
        (uint8_t)((uint8_t)(g->item_menu_scroll + QUEST_WINDOW) < total),
        'v', UI_TILE_MENUS_LIST_SCROLL_ARROW,
        UI_TILE_MENUS_BOTTOM_BORDER);
    /* Only accepted quests are tracked: NOT_STARTED entries stay hidden
     * (an empty quest log shows a placeholder instead of "not started"
     * rows).  item_menu_index addresses the VISIBLE list, so the caret
     * compares against the visible position, not the table position. */
    drawn = 0;
    vis = 0;
    for (s_iq_qst_pos = 0; s_iq_qst_pos < GAME_QUEST_COUNT; s_iq_qst_pos++) {
        s_iq_q = &g_quests[s_iq_qst_pos];
        if (iq_quest_status(&g->state, s_iq_q) == QUEST_STATUS_NOT_STARTED)
            continue;
        if (vis < g->item_menu_scroll ||
            vis >= (uint8_t)(g->item_menu_scroll + QUEST_WINDOW)) {
            vis++;
            continue;
        }
        y = iq_slot_row(drawn);
        if (g->item_menu_index == vis) IQ_DRAW_TEXT(0, y, ">", 1);
        else IQ_DRAW_TEXT(0, y, " ", 1);
        IQ_DRAW_TEXT(2, y, s_iq_q->name, 18);

        s_ic_dst = (volatile uint8_t *)(0x9800 + ((uint16_t)y << 5) + 1);
        VBK_REG = 0;
        /* Quest marker as a font glyph ('!' active, '*' complete): the
         * former deck-icon tile read poorly at 8x8 (AGENTS.md 52.22
         * loader now overwrites tile 116 with the combat deck art). */
        if (iq_quest_status(&g->state, s_iq_q) == QUEST_STATUS_COMPLETE) {
            BANKED_VRAM_WRITE(s_ic_dst, (uint8_t)(ui_font_tile_base + (uint8_t)('*' - ' ')));
            IQ_COLOR_SPAN(1, y, 1, UI_COLOR_GOLD);
        } else {
            BANKED_VRAM_WRITE(s_ic_dst, (uint8_t)(ui_font_tile_base + (uint8_t)('!' - ' ')));
            IQ_COLOR_SPAN(1, y, 1, UI_COLOR_WOOD);
        }

        iq_quest_draw_status_line(g, s_iq_q, (uint8_t)(y + 1));
        /* Separator-row non-rule cells (card rows run full width
         * here): clear before stamping the rule. */
        IQ_DRAW_TEXT(0, (uint8_t)(y + 2), "  ", 2);
        IQ_DRAW_TEXT(11, (uint8_t)(y + 2), "         ", 9);
        for (i = 2; i <= 10; i++)
            menu_stamp_chrome((uint8_t)(y + 2),
                              i, UI_TILE_MENUS_QUESTS_SEPARATOR);
        drawn++;
        vis++;
    }
    /* Blank empty slots (short logs): card rows would survive in the
     * undrawn blocks. */
    for (; drawn < QUEST_WINDOW; drawn++) {
        y = iq_slot_row(drawn);
        IQ_DRAW_TEXT(0, y, "                    ", 20);
        IQ_DRAW_TEXT(0, (uint8_t)(y + 1), "                    ", 20);
        IQ_DRAW_TEXT(0, (uint8_t)(y + 2), "                    ", 20);
    }
    if (vis == 0) {
        IQ_DRAW_TEXT(2, QUEST_ROW_TOP, "no quests yet", 13);
    }
    /* Quit hint: A anywhere on this tab opens the save & quit
     * confirm (overworld only, where quitting cannot strand a
     * battle).  Short footer needs its tail blanked (DECK footer
     * runs 15+). */
    if (g->prev_screen == SCREEN_OVERWORLD) {
        IQ_DRAW_TEXT(0, 16, "SEL:INFO A:SAVE+QUIT", 20);
    } else {
        IQ_DRAW_TEXT(0, 16, "SEL:INFO", 8);
        IQ_DRAW_TEXT(8, 16, "            ", 12);
    }
    /* Row 17 is DECK-footer territory; the quest tab owns nothing
     * here. */
    IQ_DRAW_TEXT(0, 17, "                    ", 20);
}

/* ── Quest detail (SELECT on a quest row) ─────────────────────────
 *   row 3: quest name
 *   row 5: who gave it (giver)
 *   row 6: the condition (objective)
 *   row 7: where you stand (progress "v/t label", "DONE", or
 *           "ACTIVE" for counter-less quests)
 *   row 8: what the reward was ("GOT <reward>", completed quests only)
 *   row 15: bottom box border (same as every list tab)
 *   row 16: "B:BACK" */
static void iq_draw_quest_detail(Game *g)
{
    const QuestDefinition *q;
    QuestStatus st;
    uint8_t val;
    uint8_t i;
    char b[6];
    q = iq_visible_quest(g, g->item_menu_index, 0);
    if (!q) return;
    /* Unpainted rows first (DECK card rows run full width here). */
    IQ_DRAW_TEXT(0, 2, "                    ", 20);
    IQ_DRAW_TEXT(0, 4, "                    ", 20);
    IQ_DRAW_TEXT(0, 9, "                    ", 20);
    IQ_DRAW_TEXT(0, 10, "                    ", 20);
    IQ_DRAW_TEXT(0, 11, "                    ", 20);
    IQ_DRAW_TEXT(0, 12, "                    ", 20);
    IQ_DRAW_TEXT(0, 13, "                    ", 20);
    IQ_DRAW_TEXT(0, 14, "                    ", 20);
    IQ_DRAW_TEXT(0, 17, "                    ", 20);
    IQ_DRAW_TEXT(0, 3, q->name, 14);
    IQ_DRAW_TEXT(14, 3, "      ", 6);
    IQ_DRAW_TEXT(0, 5, q->giver, 16);
    IQ_DRAW_TEXT(16, 5, "    ", 4);
    IQ_DRAW_TEXT(0, 6, q->objective, 19);
    IQ_DRAW_TEXT(19, 6, " ", 1);
    st = iq_quest_status(&g->state, q);
    if (st == QUEST_STATUS_COMPLETE) {
        IQ_DRAW_TEXT(2, 7, "DONE", 4);
        IQ_DRAW_TEXT(2, 8, "GOT ", 4);
        IQ_DRAW_TEXT(6, 8, q->reward, 12);
        IQ_DRAW_TEXT(0, 7, "  ", 2);
        IQ_DRAW_TEXT(6, 7, "              ", 14);
        IQ_DRAW_TEXT(0, 8, "  ", 2);
        IQ_DRAW_TEXT(18, 8, "  ", 2);
    } else if (q->progress_variable != 0) {
        val = (uint8_t)iq_variable_get(&g->state, q->progress_variable);
        b[0] = (char)('0' + val);
        b[1] = '/';
        b[2] = (char)('0' + (uint8_t)q->progress_target);
        b[3] = '\0';
        IQ_DRAW_TEXT(0, 7, "  ", 2);
        IQ_DRAW_TEXT(2, 7, b, 3);
        IQ_DRAW_TEXT(6, 7, q->progress_label, 10);
        IQ_DRAW_TEXT(16, 7, "    ", 4);
        IQ_DRAW_TEXT(0, 8, "                    ", 20);
    } else {
        IQ_DRAW_TEXT(2, 7, "ACTIVE", 6);
        IQ_DRAW_TEXT(0, 7, "  ", 2);
        IQ_DRAW_TEXT(8, 7, "            ", 12);
        IQ_DRAW_TEXT(0, 8, "                    ", 20);
    }
    /* Same bottom as the list tabs: full-width bottom border on row 15
     * and the back hint on row 16 (B closes the detail everywhere). */
    for (i = 0; i < 20; i++)
        menu_stamp_chrome(15, i, UI_TILE_MENUS_BOTTOM_BORDER);
    IQ_DRAW_TEXT(0, 16, "B:BACK", 6);
    IQ_DRAW_TEXT(6, 16, "              ", 14);
}

/* Quest-tab input behind the thin fixed dispatch in item_screen.c.
 * Pad edges are read directly from WRAM (always mapped, like the
 * staged Game*).  LR-before-SELECT, like the DECK tab.  Longer logs
 * scroll a 4-block window (item_menu_scroll, reset on tab switch):
 * the cursor repaint stays inline while the window holds, and a window
 * move takes the full redraw instead. */
void item_quest_nav_banked(void)
{
    Game *g = (Game *)g_bk_ptr_a;
    uint8_t total = 0;
    uint8_t edges;
    uint8_t i;
    uint8_t old_index;
    uint8_t new_index;
    uint8_t old_scroll;
    uint8_t old_row;
    uint8_t new_row;
    if (!g) return;
    /* Pressed edges in one pass: compile-time bitmasks, no per-button
     * shift loops and no helper call (bank headroom, AGENTS.md 52.18). */
    edges = (uint8_t)(g_inp_pad & (uint8_t)~g_inp_prev);
    for (i = 0; i < GAME_QUEST_COUNT; i++) {
        if (iq_quest_status(&g->state, &g_quests[i]) != QUEST_STATUS_NOT_STARTED)
            total++;
    }
    if (total == 0) {
        /* Empty quest log: nothing to select or cursor through.
         * Tab-switching is handled fixed-bank before this dispatch. */
        return;
    }
    if (g->item_menu_index >= total)
        g->item_menu_index = 0;
    if (edges & (uint8_t)(1u << INPUT_SELECT)) {
        /* Detail resolves the visible position through
         * iq_visible_quest (index addresses the VISIBLE list). */
        g->item_menu_mode = MODE_QUEST_DETAIL;
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
    } else if (new_index > (uint8_t)(old_scroll + (QUEST_WINDOW - 1))) {
        g->item_menu_scroll = (uint8_t)(new_index - (QUEST_WINDOW - 1));
    }
    if (g->item_menu_scroll != old_scroll) {
        g->render_cache.valid = false;
        return;
    }
    old_row = iq_slot_row((uint8_t)(old_index - old_scroll));
    new_row = iq_slot_row((uint8_t)(new_index - old_scroll));
    if (old_row != new_row) {
        IQ_DRAW_TEXT(0, old_row, " ", 1);
        IQ_DRAW_TEXT(0, new_row, ">", 1);
    }
}

void item_quest_render_banked(void)
{
    Game *g = (Game *)g_bk_ptr_a;
    uint8_t e;
    if (!g) return;

    /* Direct paint, no pre-blank: each draw path covers its rows in
     * one pass (see DECK list), so tab switches never show an
     * all-blank intermediate. */

    /* Sweep the card rider entries: the DECK list paints element icons
     * per row (entries 1-12), and the quest tab must leave none behind.
     * Material entries 12-32 go too (same rows, both-column ingots). */
    for (e = 1; e <= 32; e++) {
        iq_rider_hide(e);
    }

    iq_draw_header();

    if (g->item_menu_mode == MODE_QUEST_DETAIL)
        iq_draw_quest_detail(g);
    else
        iq_draw_quest(g);
}
