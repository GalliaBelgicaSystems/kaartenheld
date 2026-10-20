#pragma bank 8
#pragma disable_warning 110

#include <gb/gb.h>
#include <gb/cgb.h>
#include "game.h"
#include "quest.h"
#include "ui.h"
#include "banked.h"

/* ── Quest-tab content, bank-8 body ────────────────────────────────
 * The quest list renderer + input navigator live here (not bank 2 with
 * the cards renderer) for two structural reasons (AGENTS.md 52.18):
 *
 * 1. The quest registry (g_quests[]) is banked content: a banked body
 *    must share its table's bank (AGENTS.md 52.11.1), and the registry
 *    outgrew bank 2 alongside the event table.  quests_content.c carries
 *    `#pragma bank 8` for the same reason.
 * 2. Banks 2 and 5 sit hard against 16 KB; bank 8 is the overflow bank
 *    (AGENTS.md 55.5) with room for the feature to grow (more quests,
 *    per-quest detail).
 *
 * ABI: fixed-bank wrappers in item_screen.c dispatch here (QUEST tab)
 * and to item_content_banked.c (CARDS tab) through the WRAM trampoline:
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

#define QUEST_VISIBLE_MAX 5   /* rows 5..13 step 2 (loop bound y < 15) */

/* The quest list draws at most QUEST_VISIBLE_MAX entries: the nav caret
 * math assumes the visible list fits on screen.  NOTE: a typedef
 * negative-size check does NOT fire under this SDCC (it silently
 * accepts char[-1]), so the bound uses #error, which the preprocessor
 * enforces.  Extend the #if if the registry grows. */
#if GAME_QUEST_COUNT > QUEST_VISIBLE_MAX
#error "quest registry outgrew the quest-tab layout"
#endif

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
        if (*s_ic_buf != ch) {
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

/* Tab header (banked: fixed _HOME is nearly full, AGENTS.md 52.18) --
 * same rows as the bank-2 CARDS/STATUS header: row 0 tabs, row 1
 * caret under QUEST (col 6). */
static void iq_draw_header(void)
{
    IQ_DRAW_TEXT(0, 0, "CARDS QUEST STATUS", 18);
    IQ_DRAW_TEXT(6, 1, "^", 1);
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

/* Rider OAM sweep for the quest tab (entries 1-5): the CARDS list paints
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
        } else {
            IQ_DRAW_TEXT(2, y, "active", 6);
        }
    } else {
        IQ_DRAW_TEXT(1, y, "complete - ", 11);
        IQ_DRAW_TEXT(12, y, q->complete_note ? q->complete_note : "", 8);
    }
}

static void iq_draw_quest(Game *g)
{
    uint8_t vis;
    /* Only accepted quests are tracked: NOT_STARTED entries stay hidden
     * (an empty quest log shows a placeholder instead of two
     * "not started" rows).  item_menu_index addresses the VISIBLE list,
     * so the caret compares against the visible position, not the table
     * position. */
    s_ic_y = 3;
    vis = 0;
    for (s_iq_qst_pos = 0; s_iq_qst_pos < GAME_QUEST_COUNT && s_ic_y < 15; s_iq_qst_pos++) {
        s_iq_q = &g_quests[s_iq_qst_pos];
        if (iq_quest_status(&g->state, s_iq_q) == QUEST_STATUS_NOT_STARTED)
            continue;
        if (g->item_menu_index == vis) IQ_DRAW_TEXT(0, s_ic_y, ">", 1);
        else IQ_DRAW_TEXT(0, s_ic_y, " ", 1);
        IQ_DRAW_TEXT(2, s_ic_y, s_iq_q->name, 18);

        s_ic_dst = (volatile uint8_t *)(0x9800 + ((uint16_t)s_ic_y << 5) + 1);
        VBK_REG = 0;
        /* Quest marker as a font glyph ('!' active, '*' complete): the
         * former deck-icon tile read poorly at 8x8 (AGENTS.md 52.22
         * loader now overwrites tile 116 with the combat deck art). */
        if (iq_quest_status(&g->state, s_iq_q) == QUEST_STATUS_COMPLETE) {
            BANKED_VRAM_WRITE(s_ic_dst, (uint8_t)(ui_font_tile_base + (uint8_t)('*' - ' ')));
            IQ_COLOR_SPAN(1, s_ic_y, 1, UI_COLOR_GOLD);
        } else {
            BANKED_VRAM_WRITE(s_ic_dst, (uint8_t)(ui_font_tile_base + (uint8_t)('!' - ' ')));
            IQ_COLOR_SPAN(1, s_ic_y, 1, UI_COLOR_WOOD);
        }

        s_ic_y++;
        iq_quest_draw_status_line(g, s_iq_q, s_ic_y);
        s_ic_y++;
        vis++;
    }
    if (vis == 0) {
        IQ_DRAW_TEXT(2, s_ic_y, "no quests yet", 13);
    }
    IQ_DRAW_TEXT(0, 16, "SEL:INFO", 8);
}

/* ── Quest detail (SELECT on a quest row) ─────────────────────────
 *   row 3: quest name
 *   row 5: who gave it (giver)
 *   row 6: the condition (objective)
 *   row 7: where you stand (progress "v/t label", "DONE", or
 *           "ACTIVE" for counter-less quests)
 *   row 8: what the reward was ("GOT <reward>", completed quests only)
 *   row 9: "SEL:BACK" */
static void iq_draw_quest_detail(Game *g)
{
    const QuestDefinition *q;
    QuestStatus st;
    uint8_t val;
    char b[6];
    q = iq_visible_quest(g, g->item_menu_index, 0);
    if (!q) return;
    IQ_DRAW_TEXT(0, 3, q->name, 14);
    IQ_DRAW_TEXT(0, 5, q->giver, 16);
    IQ_DRAW_TEXT(0, 6, q->objective, 19);
    st = iq_quest_status(&g->state, q);
    if (st == QUEST_STATUS_COMPLETE) {
        IQ_DRAW_TEXT(2, 7, "DONE", 4);
        IQ_DRAW_TEXT(2, 8, "GOT ", 4);
        IQ_DRAW_TEXT(6, 8, q->reward, 12);
    } else if (q->progress_variable != 0) {
        val = (uint8_t)iq_variable_get(&g->state, q->progress_variable);
        b[0] = (char)('0' + val);
        b[1] = '/';
        b[2] = (char)('0' + (uint8_t)q->progress_target);
        b[3] = '\0';
        IQ_DRAW_TEXT(2, 7, b, 3);
        IQ_DRAW_TEXT(6, 7, q->progress_label, 10);
    } else {
        IQ_DRAW_TEXT(2, 7, "ACTIVE", 6);
    }
    IQ_DRAW_TEXT(0, 9, "SEL:BACK", 8);
}

/* Quest-tab input behind the thin fixed dispatch in item_screen.c.
 * Pad edges are read directly from WRAM (always mapped, like the
 * staged Game*).  LR-before-SELECT, like the CARDS tab.  The cursor
 * repaint is inline (two buffer-checked cells -- the quest list never
 * scrolls), so there is no cross-file banked call. */
void item_quest_nav_banked(void)
{
    Game *g = (Game *)g_bk_ptr_a;
    uint8_t total = 0;
    uint8_t edges;
    uint8_t i;
    uint8_t old_index;
    uint8_t new_index;
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
    old_row = (uint8_t)(3 + (old_index << 1));
    new_row = (uint8_t)(3 + (new_index << 1));
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

    /* Sweep the card rider entries: the CARDS list paints element icons
     * per row (entries 1-12), and the quest tab must leave none behind. */
    for (e = 1; e <= 12; e++) {
        iq_rider_hide(e);
    }

    iq_draw_header();

    if (g->item_menu_mode == MODE_QUEST_DETAIL)
        iq_draw_quest_detail(g);
    else
        iq_draw_quest(g);
}
