#pragma bank 2
#pragma disable_warning 110

#include <gb/gb.h>
#include <gb/cgb.h>
#include "game.h"
#include "screen.h"
#include "rpg/cards.h"
#include "rpg/status.h"
#include "rpg/loot.h"
#include "rpg/deck.h"
#include "quest.h"
#include "ui.h"
#include "menu.h"
#include "card.h"
#include "game_ids.h"
#include "banked.h"

#define TAB_CARDS 0
#define TAB_QUEST 1
#define NUM_TABS  2
#define GAME_QUEST_COUNT 2

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

extern uint8_t s_view_indices[MAX_CARD_COLLECTION];
extern uint8_t s_view_count;

extern const CardDefinition g_cards[];
extern const uint8_t s_card_desc_blob[CARD_DESC_TYPES][CARD_DESC_STRIDE];
extern const QuestDefinition g_quests[];
extern char g_ui_screen_buf[18][21];
extern uint8_t ui_font_tile_base;
extern uint8_t g_is_cgb;

static uint8_t s_ic_txt_i;
static uint8_t s_ic_col_i;
static uint8_t s_ic_def_i;
static uint8_t s_ic_deck_i;
static uint8_t s_ic_coll_i;
static uint8_t s_ic_bld_i;
static uint8_t s_ic_sort_i;
static uint8_t s_ic_sort_j;
static uint8_t s_ic_lst_pos;
static uint8_t s_ic_qst_pos;

static uint8_t s_ic_y;
static uint8_t s_ic_tmp;
static uint8_t s_ic_va;
static uint8_t s_ic_vb;
static uint8_t s_ic_desc;
static uint8_t s_ic_total;
static uint8_t s_ic_tile_elem;
static uint8_t s_ic_tile_wpn;
static uint8_t s_ic_in_deck;
static CardId s_ic_id;
static const CardDefinition *s_ic_def;
static const QuestDefinition *s_ic_q;
static volatile uint8_t *s_ic_dst;
static char *s_ic_buf;
static char s_ic_code[3];
static char s_ic_num_buf[4];

static uint8_t s_ic_arg_x;
static uint8_t s_ic_arg_y;
static uint8_t s_ic_arg_len;
static uint8_t s_ic_arg_val;
static uint8_t s_ic_arg_palette;
static const char *s_ic_arg_text;

static void ic_draw_text_fast(void)
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
            if (LCDC_REG & 0x80) {
                while (STAT_REG & 0x02);
            }
            *s_ic_dst = tile;
            *s_ic_buf = ch;
        }
        s_ic_dst++;
        s_ic_buf++;
    }
}

#define IC_DRAW_TEXT(x, y, text, len) do { \
    s_ic_arg_x = (x); s_ic_arg_y = (y); s_ic_arg_text = (text); s_ic_arg_len = (len); \
    ic_draw_text_fast(); \
} while(0)

static void ic_draw_num2_fast(void)
{
    uint8_t tens = 0;
    uint8_t ones = s_ic_arg_val;
    while (ones >= 10) {
        ones -= 10;
        tens++;
    }
    if (tens > 0) {
        s_ic_num_buf[0] = (char)('0' + tens);
        s_ic_num_buf[1] = (char)('0' + ones);
    } else {
        s_ic_num_buf[0] = ' ';
        s_ic_num_buf[1] = (char)('0' + ones);
    }
    s_ic_num_buf[2] = '\0';
    IC_DRAW_TEXT(s_ic_arg_x, s_ic_arg_y, s_ic_num_buf, 2);
}

#define IC_DRAW_NUM2(x, y, val) do { \
    s_ic_arg_x = (x); s_ic_arg_y = (y); s_ic_arg_val = (val); \
    ic_draw_num2_fast(); \
} while(0)

static void ic_color_span_fast(void)
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
        if (LCDC_REG & 0x80) {
            while (STAT_REG & 0x02);
        }
        s_ic_dst[s_ic_col_i] = pal;
    }
    VBK_REG = 0;
}

#define IC_COLOR_SPAN(x, y, len, pal) do { \
    s_ic_arg_x = (x); s_ic_arg_y = (y); s_ic_arg_len = (len); s_ic_arg_palette = (pal); \
    ic_color_span_fast(); \
} while(0)

static const CardDefinition *ic_card_get_def(CardId id)
{
    for (s_ic_def_i = 0; s_ic_def_i < GAME_CARD_COUNT; s_ic_def_i++) {
        if (g_cards[s_ic_def_i].id == id) return &g_cards[s_ic_def_i];
    }
    return NULL;
}

static const char *ic_card_desc(uint8_t type)
{
    return (type < CARD_DESC_TYPES) ? (const char *)&s_card_desc_blob[type][0] : "";
}

static bool ic_card_deckable(const CardDefinition *def)
{
    return def && def->type != CARD_TYPE_SPECIAL;
}

static CardId ic_view_card_id(Game *g, uint8_t pos)
{
    return g->state.cards.collection.entries[s_view_indices[pos]].id;
}

static uint8_t ic_deck_count(const DeckState *d, CardId id)
{
    uint8_t n = 0;
    if (!d) return 0;
    for (s_ic_deck_i = 0; s_ic_deck_i < d->count; s_ic_deck_i++) {
        if (d->cards[s_ic_deck_i] == id) n++;
    }
    return n;
}

static uint8_t ic_collection_count(const CardState *cs, CardId id)
{
    if (!cs) return 0;
    for (s_ic_coll_i = 0; s_ic_coll_i < cs->collection.count; s_ic_coll_i++) {
        if (cs->collection.entries[s_ic_coll_i].id == id)
            return cs->collection.entries[s_ic_coll_i].count;
    }
    return 0;
}

static QuestStatus ic_quest_status(const GameState *state, const QuestDefinition *q)
{
    int16_t v;
    if (!state || !q || q->status_variable == 0) {
        return QUEST_STATUS_NOT_STARTED;
    }
    v = game_variable_get(state, q->status_variable);
    if (v == q->status_complete) return QUEST_STATUS_COMPLETE;
    if (v == q->status_active) return QUEST_STATUS_ACTIVE;
    return QUEST_STATUS_NOT_STARTED;
}

static bool ic_filter_matches(Game *g, CardId id)
{
    if (g->item_menu_filter == FILTER_ALL) return true;
    s_ic_def = ic_card_get_def(id);
    if (!s_ic_def) return false;
    return s_ic_def->type == g->item_menu_filter;
}

static uint8_t ic_sort_value(const CardDefinition *def, uint8_t sort)
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

static void ic_sort_view(Game *g)
{
    s_ic_desc = (g->item_menu_sort == SORT_POWER_DESC ||
                 g->item_menu_sort == SORT_COST_DESC);

    for (s_ic_sort_i = 1; s_ic_sort_i < s_view_count; s_ic_sort_i++) {
        s_ic_tmp = s_view_indices[s_ic_sort_i];
        s_ic_va = ic_sort_value(ic_card_get_def(g->state.cards.collection.entries[s_ic_tmp].id), g->item_menu_sort);
        s_ic_sort_j = s_ic_sort_i;
        while (s_ic_sort_j > 0) {
            s_ic_vb = ic_sort_value(ic_card_get_def(g->state.cards.collection.entries[s_view_indices[s_ic_sort_j - 1]].id),
                                    g->item_menu_sort);
            if (s_ic_desc ? (s_ic_vb < s_ic_va) : (s_ic_vb > s_ic_va)) {
                s_view_indices[s_ic_sort_j] = s_view_indices[s_ic_sort_j - 1];
                s_ic_sort_j--;
            } else {
                break;
            }
        }
        s_view_indices[s_ic_sort_j] = s_ic_tmp;
    }
}

void ic_build_view(Game *g)
{
    s_view_count = 0;
    s_ic_total = g->state.cards.collection.count;
    for (s_ic_bld_i = 0; s_ic_bld_i < s_ic_total; s_ic_bld_i++) {
        s_ic_def = ic_card_get_def(g->state.cards.collection.entries[s_ic_bld_i].id);
        if (!ic_card_deckable(s_ic_def)) continue;
        if (ic_filter_matches(g, g->state.cards.collection.entries[s_ic_bld_i].id))
            s_view_indices[s_view_count++] = s_ic_bld_i;
    }
    if (g->item_menu_sort != SORT_NONE && s_view_count > 1)
        ic_sort_view(g);
}

static const char *const s_card_type_names[CARD_TYPE_COUNT] = {
    "ATK", "DEF", "HEL", "STS", "UTL", "SPL"
};

static const char *ic_card_type_name(uint8_t t)
{
    return (t < CARD_TYPE_COUNT) ? s_card_type_names[t] : "SPL";
}

static const char *ic_filter_name(uint8_t f)
{
    return (f <= CARD_TYPE_UTILITY) ? s_card_type_names[f] : "ALL";
}

static const char *ic_sort_name(uint8_t s)
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

static void ic_draw_card_pair(Game *g, uint8_t y, uint8_t pos)
{
    s_ic_id = ic_view_card_id(g, pos);
    s_ic_def = ic_card_get_def(s_ic_id);
    if (!s_ic_def) return;

    if ((uint8_t)(pos + FIRST_CARD) == g->item_menu_index)
        IC_DRAW_TEXT(0, y, ">", 1);
    else
        IC_DRAW_TEXT(0, y, " ", 1);

    /* Element riders are OAM sprites on the battle screen only: the
     * list cell stays blank and the element reads from the color span
     * below (FIRE/POISON/ICE slots). */
    s_ic_tile_elem = 0;

    if (s_ic_def->battle_type == BATTLE_CARD_TYPE_HEAL || s_ic_def->effect == CARD_EFFECT_HEAL_HP) {
        s_ic_tile_wpn = UI_TILE_CARD_RING;
    } else if (s_ic_def->battle_type == BATTLE_CARD_TYPE_SHIELD) {
        s_ic_tile_wpn = UI_TILE_CARD_SHIELD;
    } else if (s_ic_def->battle_type == BATTLE_CARD_TYPE_BOW) {
        s_ic_tile_wpn = UI_TILE_CARD_BOW;
    } else if (s_ic_def->battle_type == BATTLE_CARD_TYPE_DAGGER) {
        s_ic_tile_wpn = UI_TILE_CARD_DAGGER;
    } else {
        s_ic_tile_wpn = UI_TILE_CARD_SWORD;
    }

    IC_DRAW_TEXT(4, y, s_ic_def->name, 14);

    s_ic_dst = (volatile uint8_t *)(0x9800 + ((uint16_t)y << 5) + 2);
    VBK_REG = 0;
    if (LCDC_REG & 0x80) {
        while (STAT_REG & 0x02);
    }
    *s_ic_dst = s_ic_tile_elem;
    if (LCDC_REG & 0x80) {
        while (STAT_REG & 0x02);
    }
    *(s_ic_dst + 1) = s_ic_tile_wpn;

    IC_COLOR_SPAN(3, y, 1, UI_COLOR_WOOD);
    if (s_ic_def->status_id == STATUS_BURN)
        IC_COLOR_SPAN(2, y, 1, UI_COLOR_FIRE);
    else if (s_ic_def->status_id == STATUS_POISON)
        IC_COLOR_SPAN(2, y, 1, UI_COLOR_POISON);
    else if (s_ic_def->status_id == STATUS_FREEZE)
        IC_COLOR_SPAN(2, y, 1, UI_COLOR_ICE);

    s_ic_in_deck = ic_deck_count(&g->state.cards.deck, s_ic_id);
    s_ic_code[0] = (char)('0' + s_ic_in_deck);
    s_ic_code[1] = '\0';
    IC_DRAW_TEXT(19, y, s_ic_code, 1);
    IC_DRAW_TEXT(2, (uint8_t)(y + 1),
                 ic_card_desc(s_ic_def->battle_type), 17);
}

static void ic_draw_cards_list(Game *g)
{
    ic_build_view(g);
    s_ic_y = 5;
    if (g->item_menu_scroll == 0) {
        if (g->item_menu_index == ROW_TOP)
            IC_DRAW_TEXT(0, s_ic_y, ">", 1);
        else
            IC_DRAW_TEXT(0, s_ic_y, " ", 1);
        IC_DRAW_TEXT(2, s_ic_y, "* FILTER/SORT *", 15);
        s_ic_y = 6;
    }
    for (s_ic_lst_pos = 0; s_ic_lst_pos < VISIBLE_CARDS && s_ic_y <= 15; s_ic_lst_pos++) {
        uint8_t vpos = (uint8_t)(g->item_menu_scroll + s_ic_lst_pos);
        if (vpos >= s_view_count) break;
        ic_draw_card_pair(g, s_ic_y, vpos);
        s_ic_y = (uint8_t)(s_ic_y + 2);
    }

    if (g->item_menu_message == MSG_DECK_FULL)
        IC_DRAW_TEXT(0, 16, "DECK FULL", 9);
    else if (g->item_menu_message == MSG_DECK_MIN)
        IC_DRAW_TEXT(0, 16, "DECK MIN 5", 10);
    else if (g->item_menu_message == MSG_QUEST_ITEM)
        IC_DRAW_TEXT(0, 16, "QUEST ITEM", 10);
    else
        IC_DRAW_TEXT(0, 16, "A:ADD SEL:INFO", 14);
}

static void ic_quest_draw_status_line(Game *g, const QuestDefinition *q, uint8_t y)
{
    QuestStatus st;
    char b[6];
    uint8_t val;
    if (!q) return;
    st = ic_quest_status(&g->state, q);

    if (st == QUEST_STATUS_NOT_STARTED) {
        IC_DRAW_TEXT(2, y, "not started", 11);
    } else if (st == QUEST_STATUS_ACTIVE) {
        if (q->progress_variable != 0) {
            val = (uint8_t)game_variable_get(&g->state, q->progress_variable);
            IC_DRAW_TEXT(2, y, q->progress_label ? q->progress_label : "", 8);
            b[0] = ':';
            b[1] = ' ';
            b[2] = (char)('0' + val);
            b[3] = '/';
            b[4] = (char)('0' + (uint8_t)q->progress_target);
            b[5] = '\0';
            IC_DRAW_TEXT(10, y, b, 5);
        } else {
            IC_DRAW_TEXT(2, y, "active", 6);
        }
    } else {
        IC_DRAW_TEXT(1, y, "complete - ", 11);
        IC_DRAW_TEXT(12, y, q->complete_note ? q->complete_note : "", 8);
    }
}

static void ic_draw_quest(Game *g)
{
    s_ic_y = 5;
    for (s_ic_qst_pos = 0; s_ic_qst_pos < GAME_QUEST_COUNT && s_ic_y < 15; s_ic_qst_pos++) {
        s_ic_q = &g_quests[s_ic_qst_pos];
        if (g->item_menu_index == s_ic_qst_pos) IC_DRAW_TEXT(0, s_ic_y, ">", 1);
        else IC_DRAW_TEXT(0, s_ic_y, " ", 1);
        IC_DRAW_TEXT(2, s_ic_y, s_ic_q->name, 18);

        s_ic_dst = (volatile uint8_t *)(0x9800 + ((uint16_t)s_ic_y << 5) + 1);
        VBK_REG = 0;
        if (LCDC_REG & 0x80) {
            while (STAT_REG & 0x02);
        }
        /* Quest marker as a font glyph ('!' active, '*' complete): the
         * former deck-icon tile read poorly at 8x8 (AGENTS.md 52.22
         * loader now overwrites tile 116 with the combat deck art). */
        if (ic_quest_status(&g->state, s_ic_q) == QUEST_STATUS_COMPLETE) {
            *s_ic_dst = (uint8_t)(ui_font_tile_base + (uint8_t)('*' - ' '));
            IC_COLOR_SPAN(1, s_ic_y, 1, UI_COLOR_GOLD);
        } else {
            *s_ic_dst = (uint8_t)(ui_font_tile_base + (uint8_t)('!' - ' '));
            IC_COLOR_SPAN(1, s_ic_y, 1, UI_COLOR_WOOD);
        }

        s_ic_y++;
        ic_quest_draw_status_line(g, s_ic_q, s_ic_y);
        s_ic_y++;
    }
}

static void ic_draw_card_detail_page(Game *g)
{
    s_ic_y = 5;
    if (g->item_menu_index < FIRST_CARD ||
        (uint8_t)(g->item_menu_index - FIRST_CARD) >= s_view_count)
        return;
    s_ic_id = ic_view_card_id(g, (uint8_t)(g->item_menu_index - FIRST_CARD));
    s_ic_def = ic_card_get_def(s_ic_id);
    if (!s_ic_def) return;

    /* Element riders are OAM sprites on the battle screen only: the
     * detail cell stays blank and the element reads from the color span
     * below (FIRE/POISON/ICE slots). */
    s_ic_tile_elem = 0;

    if (s_ic_def->battle_type == BATTLE_CARD_TYPE_HEAL || s_ic_def->effect == CARD_EFFECT_HEAL_HP) {
        s_ic_tile_wpn = UI_TILE_CARD_RING;
    } else if (s_ic_def->battle_type == BATTLE_CARD_TYPE_SHIELD) {
        s_ic_tile_wpn = UI_TILE_CARD_SHIELD;
    } else if (s_ic_def->battle_type == BATTLE_CARD_TYPE_BOW) {
        s_ic_tile_wpn = UI_TILE_CARD_BOW;
    } else if (s_ic_def->battle_type == BATTLE_CARD_TYPE_DAGGER) {
        s_ic_tile_wpn = UI_TILE_CARD_DAGGER;
    } else {
        s_ic_tile_wpn = UI_TILE_CARD_SWORD;
    }

    IC_DRAW_TEXT(0, s_ic_y, s_ic_def->name, 11);

    s_ic_dst = (volatile uint8_t *)(0x9800 + ((uint16_t)s_ic_y << 5));
    VBK_REG = 0;
    if (LCDC_REG & 0x80) {
        while (STAT_REG & 0x02);
    }
    *s_ic_dst = s_ic_tile_elem;
    if (LCDC_REG & 0x80) {
        while (STAT_REG & 0x02);
    }
    *(s_ic_dst + 1) = s_ic_tile_wpn;

    IC_COLOR_SPAN(1, s_ic_y, 1, UI_COLOR_WOOD);
    if (s_ic_def->status_id == STATUS_BURN)
        IC_COLOR_SPAN(0, s_ic_y, 1, UI_COLOR_FIRE);
    else if (s_ic_def->status_id == STATUS_POISON)
        IC_COLOR_SPAN(0, s_ic_y, 1, UI_COLOR_POISON);
    else if (s_ic_def->status_id == STATUS_FREEZE)
        IC_COLOR_SPAN(0, s_ic_y, 1, UI_COLOR_ICE);
    s_ic_y += 2;
    IC_DRAW_TEXT(0, s_ic_y, "TYPE", 4);
    IC_DRAW_TEXT(6, s_ic_y, ic_card_type_name(s_ic_def->type), 3);
    s_ic_y += 2;
    IC_DRAW_TEXT(0, s_ic_y, "PWR", 3);
    IC_DRAW_NUM2(4, s_ic_y, s_ic_def->power);
    IC_DRAW_TEXT(7, s_ic_y, "COST", 4);
    IC_DRAW_NUM2(12, s_ic_y, s_ic_def->cost);
    s_ic_y += 2;
    IC_DRAW_TEXT(0, s_ic_y, "USES", 4);
    if (s_ic_def->uses_per_battle) {
        IC_DRAW_NUM2(4, s_ic_y, s_ic_def->uses_per_battle);
    } else {
        IC_DRAW_TEXT(5, s_ic_y, "-", 1);
    }
    IC_DRAW_TEXT(6, s_ic_y, "/BTL", 4);
    IC_DRAW_TEXT(11, s_ic_y, "MXCP", 4);
    if (s_ic_def->max_copies) {
        IC_DRAW_NUM2(15, s_ic_y, s_ic_def->max_copies);
    } else {
        IC_DRAW_TEXT(16, s_ic_y, "-", 1);
    }
    s_ic_y += 2;
    IC_DRAW_TEXT(0, s_ic_y, "OWN", 3);
    IC_DRAW_NUM2(4, s_ic_y, ic_collection_count(&g->state.cards, s_ic_id));
    IC_DRAW_TEXT(7, s_ic_y, "DECK", 4);
    IC_DRAW_NUM2(12, s_ic_y, ic_deck_count(&g->state.cards.deck, s_ic_id));
    s_ic_y += 2;
    IC_DRAW_TEXT(0, s_ic_y, "PRICE", 5);
    IC_DRAW_NUM2(6, s_ic_y, s_ic_def->price);
}

static void ic_draw_picker(Game *g)
{
    IC_DRAW_TEXT(0, (uint8_t)(7 + (uint8_t)(g->item_menu_pick_row * 2)),
                 ">", 1);
    IC_DRAW_TEXT(2, 7, "FILTER", 6);
    IC_DRAW_TEXT(12, 7, ic_filter_name(g->item_menu_filter), 3);
    IC_DRAW_TEXT(2, 9, "SORT", 4);
    IC_DRAW_TEXT(12, 9, ic_sort_name(g->item_menu_sort), 4);
    IC_DRAW_TEXT(0, 14, "LR CYCLE  A:OK B:NO", 19);
}

void item_screen_render_banked(void)
{
    Game *g = (Game *)g_bk_ptr_a;
    if (!g) return;

    if (g->item_menu_tab == TAB_QUEST) {
        if (g->item_menu_mode == MODE_QUEST_DETAIL)
            IC_DRAW_TEXT(0, 8, "no details yet", 14);
        else
            ic_draw_quest(g);
    } else {
        switch (g->item_menu_mode) {
            case MODE_CARD_DETAIL: ic_draw_card_detail_page(g); break;
            case MODE_PICKER:      ic_draw_picker(g); break;
            default:               ic_draw_cards_list(g); break;
        }
    }
}
