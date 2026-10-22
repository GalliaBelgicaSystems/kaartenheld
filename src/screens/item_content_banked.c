#pragma bank 2
#pragma disable_warning 110

#include <gb/gb.h>
#include <gb/cgb.h>
#include "game.h"
#include "screen.h"
#include "rpg/cards.h"
#include "rpg/status.h"
#include "rider_tiles_generated.h"
#include "rpg/loot.h"
#include "rpg/deck.h"
#include "ui.h"
#include "menu.h"
#include "card.h"
#include "game_ids.h"
#include "banked.h"
#include "rpg/loot.h"
#include "shops.h"

#define TAB_CARDS 0
#define TAB_QUEST 1
#define TAB_STATUS 2
#define NUM_TABS  3

#define ROW_TOP     0
#define FIRST_CARD  1
/* Header at row 3, one card per row at rows 4-14. */
#define ROW_FILTER  2
#define ROW_CARDS_TOP 4
#define VISIBLE_CARDS 11

#define MODE_LIST         0
#define MODE_CARD_DETAIL  1
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
extern const ShopDefinition g_shops[];
extern const uint8_t g_shop_count;
extern char g_ui_screen_buf[18][21];
extern uint8_t ui_font_tile_base;
extern uint8_t g_is_cgb;

/* Shared draw scratch, also used by the bank-8 quest body
 * (item_quest_banked.c): the trampoline is non-reentrant and only one
 * tab renders at a time, so one copy serves both (AGENTS.md 52.18:
 * no duplicate WRAM for bank-split mirrors). */
uint8_t s_ic_txt_i;
uint8_t s_ic_col_i;
static uint8_t s_ic_def_i;
static uint8_t s_ic_deck_i;
static uint8_t s_ic_coll_i;
static uint8_t s_ic_lst_pos;

uint8_t s_ic_y;
static uint8_t s_ic_tile_elem;
static uint8_t s_ic_tile_wpn;
static uint8_t s_ic_in_deck;
static CardId s_ic_id;
static const CardDefinition *s_ic_def;
volatile uint8_t *s_ic_dst;
char *s_ic_buf;
static char s_ic_code[3];
static char s_ic_num_buf[4];

/* Rider OAM for the item screen (entries 1-5): icon over a card's elem
 * cell for cards carrying a rider, hidden otherwise.  Transition-hide
 * covers screen changes; the render dispatch below sweeps entries
 * first so quest/picker/emptied rows never go stale.  Same bank (2),
 * WRAM shadow OAM only; rider tiles/palettes are boot-resident and
 * item screens never reprogram OBJ slots. */
static void ic_rider_tile(uint8_t status, uint8_t *tile, uint8_t *slot)
{
    *tile = 0;
    *slot = 0;
    if (status == STATUS_BURN) {
        *tile = RIDER_TILE_BURN;
        *slot = RIDER_OBJ_BURN;
    } else if (status == STATUS_FREEZE) {
        *tile = RIDER_TILE_FREEZE;
        *slot = RIDER_OBJ_FREEZE;
    } else if (status == STATUS_POISON) {
        *tile = RIDER_TILE_POISON;
        *slot = RIDER_OBJ_POISON;
    }
}

static void ic_rider_hide(uint8_t entry)
{
    volatile uint8_t *re;
    re = (volatile uint8_t *)(0xC000u + ((uint16_t)entry << 2));
    re[0] = 0;
    re[2] = 0;
}

static void ic_rider_show(uint8_t entry, uint8_t x, uint8_t y, uint8_t status)
{
    uint8_t tile, slot;
    volatile uint8_t *re;
    ic_rider_tile(status, &tile, &slot);
    re = (volatile uint8_t *)(0xC000u + ((uint16_t)entry << 2));
    if (tile) {
        re[0] = (uint8_t)((y << 3) + 16);
        re[1] = (uint8_t)((x << 3) + 8);
        re[2] = tile;
        re[3] = slot;
    } else {
        re[0] = 0;
        re[2] = 0;
    }
}

uint8_t s_ic_arg_x;
uint8_t s_ic_arg_y;
uint8_t s_ic_arg_len;
static uint8_t s_ic_arg_val;
uint8_t s_ic_arg_palette;
const char *s_ic_arg_text;

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
            BANKED_VRAM_WRITE(s_ic_dst, tile);
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
        BANKED_VRAM_WRITE(&s_ic_dst[s_ic_col_i], pal);
    }
    VBK_REG = 0;
}

#define IC_COLOR_SPAN(x, y, len, pal) do { \
    s_ic_arg_x = (x); s_ic_arg_y = (y); s_ic_arg_len = (len); s_ic_arg_palette = (pal); \
    ic_color_span_fast(); \
} while(0)

/* ── Tab header (banked: fixed _HOME is nearly full, AGENTS.md 52.18) ──────
 * Row 0: the three tabs; row 1: the caret under the active tab.
 * No multiply (would pull the SDCC mult lib into fixed _CODE). */
static void ic_draw_rjust(uint8_t y, uint8_t end_col, uint16_t v);

static void ic_draw_header(Game *g)
{
    uint8_t cx = 0;
    if (g->item_menu_tab == TAB_QUEST) cx = 6;
    else if (g->item_menu_tab == TAB_STATUS) cx = 12;
    IC_DRAW_TEXT(0, 0, "CARDS QUEST STATUS", 18);
    IC_DRAW_TEXT(cx, 1, "^", 1);
}

/* ── Full card names (display only; mechanics keep def->name) ──────
 * "<MATERIAL> <WEAPON>" (e.g. "IRON SWORD", "MYTHRIL BOW"); the
 * element rider is NOT part of the name -- it shows as the rider OAM
 * icon plus the name's element color.  Drawn in two parts so no WRAM
 * name buffer is needed (stack-gap budget, AGENTS.md 52.24). */

static const char *ic_mat_str(uint8_t m, uint8_t *len)
{
    switch (m) {
        case 1: *len = 6; return "BRONZE";
        case 2: *len = 4; return "IRON";
        case 3: *len = 5; return "STEEL";
        case 4: *len = 7; return "MYTHRIL";
        default: *len = 4; return "WOOD";
    }
}

static const char *ic_wpn_str(uint8_t w, uint8_t *len)
{
    switch (w) {
        case 1: *len = 6; return "SHIELD";
        case 2: *len = 3; return "BOW";
        case 3: *len = 6; return "DAGGER";
        case 4: *len = 4; return "RING";
        default: *len = 5; return "SWORD";
    }
}

/* Resolve a card to material/weapon indices (loot.h axes).  Amulets
 * (SPECIAL, never listed) report through is_amulet instead. */
static void ic_card_mat_wpn(CardId id, uint8_t *mat, uint8_t *wpn,
                            uint8_t *is_amulet)
{
    uint8_t w;
    *is_amulet = 0;
    if (loot_is_loot_id(id)) {
        *mat = loot_id_material(id);
        w = loot_id_weapon(id);
        /* Reserved weapons (5-7) have no art or name table entry:
         * display as swords (those ids never spawn as loot; this is
         * display-only armor). */
        *wpn = (w <= 4) ? w : 0;
        return;
    }
    switch (id) {
        case CARD_WOOD_SWORD: *mat = 0; *wpn = 0; break;
        case CARD_WOODEN_SHIELD: *mat = 0; *wpn = 1; break;
        case CARD_IRON_SWORD: *mat = 2; *wpn = 0; break;
        case CARD_IRON_RING: *mat = 2; *wpn = 4; break;
        case CARD_FIRE_SWORD: *mat = 1; *wpn = 0; break;
        case CARD_POISON_DAGGER: *mat = 1; *wpn = 3; break;
        case CARD_BOW_9: *mat = 4; *wpn = 2; break;
        case CARD_AMULET: *is_amulet = 1; *mat = 2; *wpn = 0; break;
        default: *mat = 2; *wpn = 0; break;
    }
}

/* Health-rider display (docs/loot.md §34.1): every healing card (only
 * rings heal, and only rings ever will) shows a green '+' OAM icon over
 * the elem cell plus a HEALTH RIDER detail line.  Display-only by
 * design: it keys off the heal role, never a status -- no on-hit roll, no
 * mechanic change.  Tile/slot shared with battle + shop via battle/card.h.
 * Real status riders (fire/ice/poison) keep precedence wherever both
 * could apply. */
static uint8_t ic_is_health_rider(void)
{
    if (!s_ic_def) return 0;
    return (uint8_t)(s_ic_def->battle_type == BATTLE_CARD_TYPE_HEAL ||
                     s_ic_def->effect == CARD_EFFECT_HEAL_HP);
}

static void ic_health_show(uint8_t entry, uint8_t x, uint8_t y)
{
    volatile uint8_t *re;
    re = (volatile uint8_t *)(0xC000u + ((uint16_t)entry << 2));
    re[0] = (uint8_t)((y << 3) + 16);
    re[1] = (uint8_t)((x << 3) + 8);
    re[2] = RIDER_TILE_HEALTH;
    re[3] = RIDER_OBJ_HEALTH;
}

/* Name color by on-hit element: fire red, ice blue, poison purple;
 * plain cards stay on the default paper slot. */
static void ic_name_color(uint8_t x, uint8_t y, uint8_t len, uint8_t status)
{
    if (status == STATUS_BURN)
        IC_COLOR_SPAN(x, y, len, UI_COLOR_FIRE);
    else if (status == STATUS_FREEZE)
        IC_COLOR_SPAN(x, y, len, UI_COLOR_ICE);
    else if (status == STATUS_POISON)
        IC_COLOR_SPAN(x, y, len, UI_COLOR_POISON);
}

static uint8_t s_ic_mat;
static uint8_t s_ic_wpn;
static uint8_t s_ic_amulet;
static uint8_t s_ic_mlen;
static uint8_t s_ic_wlen;
static const char *s_ic_mstr;
static const char *s_ic_wstr;

/* Draw "<MAT> <WPN>" at (x, y), element-colored.  Returns the drawn
 * length (for spans); amulet draws "AMULET". */
static uint8_t ic_draw_full_name(uint8_t x, uint8_t y,
                                 const CardDefinition *def)
{
    uint8_t total;
    ic_card_mat_wpn(def->id, &s_ic_mat, &s_ic_wpn, &s_ic_amulet);
    if (s_ic_amulet) {
        IC_DRAW_TEXT(x, y, "AMULET", 6);
        return 6;
    }
    s_ic_mstr = ic_mat_str(s_ic_mat, &s_ic_mlen);
    s_ic_wstr = ic_wpn_str(s_ic_wpn, &s_ic_wlen);
    IC_DRAW_TEXT(x, y, s_ic_mstr, s_ic_mlen);
    IC_DRAW_TEXT((uint8_t)(x + s_ic_mlen), y, " ", 1);
    IC_DRAW_TEXT((uint8_t)(x + s_ic_mlen + 1), y, s_ic_wstr, s_ic_wlen);
    total = (uint8_t)(s_ic_mlen + 1 + s_ic_wlen);
    ic_name_color(x, y, total, def->status_id);
    return total;
}

/* Loot display synthesis (bank-2-local mirror of loot_synth_banked) ──
 * The fixed-bank view builder resolves loot ids through the bank-3
 * tables, but this bank cannot: selecting another ROM bank would unmap
 * the running body, and banked_copy() restores the home bank mid-body
 * (same reason battle_hud_load_banked reads its tables in-bank).  Bank 2
 * has no room for full table duplicates either, so this synthesizes only
 * the display fields from the shift/mask id layout (loot.h) with three
 * packed tables.  Every VALUE must match src/rpg/loot_banked.c exactly
 * (name format, power, cost, price, rider legality): the SELL price and
 * the detail page read through here, and a mismatch would show one
 * price and charge another. */
static const char s_ic_loot_mat[LOOT_NMATERIALS] =
    { 'W', 'B', 'I', 'S', 'M' };
static const char s_ic_loot_sym[17] = "SWSHBODARG??????";
static const uint8_t s_ic_loot_status[4] =
    { STATUS_NONE, STATUS_POISON, STATUS_BURN, STATUS_FREEZE };
static const uint8_t s_ic_loot_chance[4] = { 0, 128, 128, 96 };

static const CardDefinition *ic_loot_synth(CardId id)
{
    uint8_t material = loot_id_material(id);
    uint8_t effect = loot_id_effect(id);
    uint8_t weapon = loot_id_weapon(id);
    uint8_t wi = (weapon <= WPN_RING) ? weapon : WPN_SWORD;
    uint8_t has_rider;
    uint8_t i;
    uint8_t *d;
    char *n;
    char ec;

    d = (uint8_t *)&g_card_scratch;
    for (i = 0; i < sizeof(CardDefinition); i++) d[i] = 0;

    g_card_scratch.id = id;
    g_card_scratch.type = CARD_TYPE_ATTACK;
    if (weapon == WPN_SWORD) {
        g_card_scratch.battle_type = BATTLE_CARD_TYPE_SWORD;
        g_card_scratch.effect = CARD_EFFECT_DAMAGE_TARGET;
    } else if (weapon == WPN_SHIELD) {
        g_card_scratch.type = CARD_TYPE_DEFENSE;
        g_card_scratch.battle_type = BATTLE_CARD_TYPE_SHIELD;
        g_card_scratch.effect = CARD_EFFECT_BLOCK_DAMAGE;
    } else if (weapon == WPN_BOW) {
        g_card_scratch.battle_type = BATTLE_CARD_TYPE_BOW;
        g_card_scratch.effect = CARD_EFFECT_DAMAGE_TARGET;
    } else if (weapon == WPN_DAGGER) {
        g_card_scratch.battle_type = BATTLE_CARD_TYPE_DAGGER;
        g_card_scratch.effect = CARD_EFFECT_DAMAGE_TARGET;
    } else {
        /* Ring and reserved weapons heal (mirrors the synth default). */
        g_card_scratch.type = CARD_TYPE_HEAL;
        g_card_scratch.battle_type = BATTLE_CARD_TYPE_HEAL;
        g_card_scratch.effect = CARD_EFFECT_HEAL_HP;
        /* Wild rings are single-use per battle (mirrors the synth
         * ring branch). */
        g_card_scratch.uses_per_battle = 1;
    }
    g_card_scratch.power = LOOT_POWER(material, wi);
    g_card_scratch.cost = (weapon <= WPN_RING) ? 1 : 0;
    g_card_scratch.price = (uint8_t)(g_card_scratch.power +
        (uint8_t)(material << 1) +
        ((effect == EFF_POISON && weapon == WPN_DAGGER) ? 2 : 0));
    has_rider = (material >= MAT_BRONZE &&
        s_ic_loot_status[effect] != STATUS_NONE &&
        ((effect == EFF_POISON) ? (weapon == WPN_DAGGER) :
         ((effect == EFF_FIRE || effect == EFF_ICE) ?
             (weapon == WPN_SWORD) : 0)));
    if (has_rider) {
        g_card_scratch.status_id = s_ic_loot_status[effect];
        g_card_scratch.status_chance = s_ic_loot_chance[effect];
    }
    n = g_card_scratch.name;
    *n++ = s_ic_loot_mat[material];
    *n++ = ' ';
    if (has_rider) {
        ec = (effect == EFF_POISON) ? 'P' :
            ((effect == EFF_FIRE) ? 'F' : 'I');
        *n++ = ec;
        *n++ = ' ';
    }
    *n++ = s_ic_loot_sym[(uint8_t)(weapon << 1)];
    *n++ = s_ic_loot_sym[(uint8_t)((weapon << 1) + 1)];
    *n = '\0';
    return &g_card_scratch;
}

static const CardDefinition *ic_card_get_def(CardId id)
{
    if (loot_is_loot_id(id)) return ic_loot_synth(id);
    for (s_ic_def_i = 0; s_ic_def_i < GAME_CARD_COUNT; s_ic_def_i++) {
        if (g_cards[s_ic_def_i].id == id) return &g_cards[s_ic_def_i];
    }
    return NULL;
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

/* NOTE: the filtered/sorted view (s_view_indices/s_view_count) is built
 * by the fixed-bank side (item_screen.c: on every full redraw, plus
 * explicitly after picker filter/sort changes) and consumed here
 * read-only.  A bank-2 duplicate used to rebuild it on every full
 * redraw; besides wasting ROM it could DIVERGE from the action view
 * (it resolved loot ids to NULL while the fixed side synthesizes them),
 * shifting every selection at/after a loot card onto the wrong card. */

static const char *const s_card_type_names[CARD_TYPE_COUNT] = {
    "ATK", "DEF", "HEL", "STS", "UTL", "SPL"
};

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

/* One card per row (new layout):
 *   col 0: cursor caret
 *   col 1: element cell (blank BG tile; rider OAM overlays it) + element color
 *   col 2: weapon BG tile (wood palette)
 *   col 3-4: AP cost (2-wide, right-aligned)
 *   col 5-18: full "<MAT> <WPN>" name, element-colored
 *   col 19: decked-copy count (as before) */
static void ic_draw_card_pair(Game *g, uint8_t y, uint8_t pos)
{
    s_ic_id = ic_view_card_id(g, pos);
    s_ic_def = ic_card_get_def(s_ic_id);
    if (!s_ic_def) return;

    if ((uint8_t)(pos + FIRST_CARD) == g->item_menu_index)
        IC_DRAW_TEXT(0, y, ">", 1);
    else
        IC_DRAW_TEXT(0, y, " ", 1);

    /* Element rider OAM is painted per row by the list loop below (over
     * the col-1 elem cell): the cell itself stays blank and the element
     * also reads from the color span below (FIRE/POISON/ICE slots). */
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

    IC_DRAW_NUM2(3, y, s_ic_def->cost);
    ic_draw_full_name(5, y, s_ic_def);

    s_ic_dst = (volatile uint8_t *)(0x9800 + ((uint16_t)y << 5) + 1);
    VBK_REG = 0;
    BANKED_VRAM_WRITE(s_ic_dst, s_ic_tile_elem);
    BANKED_VRAM_WRITE(s_ic_dst + 1, s_ic_tile_wpn);

    IC_COLOR_SPAN(2, y, 1, UI_COLOR_WOOD);
    if (s_ic_def->status_id == STATUS_BURN)
        IC_COLOR_SPAN(1, y, 1, UI_COLOR_FIRE);
    else if (s_ic_def->status_id == STATUS_POISON)
        IC_COLOR_SPAN(1, y, 1, UI_COLOR_POISON);
    else if (s_ic_def->status_id == STATUS_FREEZE)
        IC_COLOR_SPAN(1, y, 1, UI_COLOR_ICE);

    s_ic_in_deck = ic_deck_count(&g->state.cards.deck, s_ic_id);
    s_ic_code[0] = (char)('0' + s_ic_in_deck);
    s_ic_code[1] = '\0';
    IC_DRAW_TEXT(19, y, s_ic_code, 1);
}

static void ic_draw_cards_list(Game *g)
{
    /* View is fixed-bank-built (see note above); the render only walks it.
     * Row 3 is the column header (always drawn in list mode). */
    IC_DRAW_TEXT(3, 3, "AP", 2);
    IC_DRAW_TEXT(6, 3, "MATERIAL", 8);
    IC_DRAW_TEXT(15, 3, "TYPE", 4);
    s_ic_y = ROW_CARDS_TOP;
    if (g->item_menu_scroll == 0) {
        if (g->item_menu_index == ROW_TOP)
            IC_DRAW_TEXT(0, ROW_FILTER, ">", 1);
        else
            IC_DRAW_TEXT(0, ROW_FILTER, " ", 1);
        IC_DRAW_TEXT(2, ROW_FILTER, "* FILTER/SORT *", 15);
        s_ic_y = ROW_CARDS_TOP;
    }
    for (s_ic_lst_pos = 0; s_ic_lst_pos < VISIBLE_CARDS && s_ic_y <= 14; s_ic_lst_pos++) {
        uint8_t vpos = (uint8_t)(g->item_menu_scroll + s_ic_lst_pos);
        if (vpos >= s_view_count) break;
        ic_draw_card_pair(g, s_ic_y, vpos);
        ic_rider_show((uint8_t)(1 + s_ic_lst_pos), 1, s_ic_y,
                      s_ic_def ? s_ic_def->status_id : 0);
        if ((!s_ic_def || s_ic_def->status_id == STATUS_NONE) &&
            ic_is_health_rider())
            ic_health_show((uint8_t)(1 + s_ic_lst_pos), 1, s_ic_y);
        s_ic_y = (uint8_t)(s_ic_y + 1);
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

/* Card merchant buy-back (docs/loot.md §24/§34.6): the detail page
 * offers SELL while the player has engaged a buying shop (g->shop_id
 * set by the shop actor).  Bank-local scan of the bank-2 shop table
 * (a banked body must never call the fixed-bank game_shop_for_id). */
static uint8_t ic_merchant_buys(const Game *g)
{
    if (!g || g->shop_id == 0) return 0;
    for (s_ic_def_i = 0; s_ic_def_i < g_shop_count; s_ic_def_i++) {
        if (g_shops[s_ic_def_i].id == g->shop_id)
            return g_shops[s_ic_def_i].buys;
    }
    return 0;
}

static void ic_draw_card_detail_page(Game *g)
{
    uint8_t detail_uses;
    if (g->item_menu_index < FIRST_CARD ||
        (uint8_t)(g->item_menu_index - FIRST_CARD) >= s_view_count)
        return;
    s_ic_id = ic_view_card_id(g, (uint8_t)(g->item_menu_index - FIRST_CARD));
    s_ic_def = ic_card_get_def(s_ic_id);
    if (!s_ic_def) return;

    /* Row 3: icons + element-colored full name (same cells as a list
     * row; the detail rider shows over the col-1 elem cell, entry 1 --
     * the list is not drawn in detail mode, and the dispatch sweep hid
     * the rest). */
    ic_rider_show(1, 1, 3, s_ic_def->status_id);
    if (s_ic_def->status_id == STATUS_NONE && ic_is_health_rider())
        ic_health_show(1, 1, 3);

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

    ic_draw_full_name(4, 3, s_ic_def);

    s_ic_dst = (volatile uint8_t *)(0x9800 + ((uint16_t)3 << 5) + 1);
    VBK_REG = 0;
    BANKED_VRAM_WRITE(s_ic_dst, s_ic_tile_elem);
    BANKED_VRAM_WRITE(s_ic_dst + 1, s_ic_tile_wpn);

    IC_COLOR_SPAN(2, 3, 1, UI_COLOR_WOOD);
    if (s_ic_def->status_id == STATUS_BURN)
        IC_COLOR_SPAN(1, 3, 1, UI_COLOR_FIRE);
    else if (s_ic_def->status_id == STATUS_POISON)
        IC_COLOR_SPAN(1, 3, 1, UI_COLOR_POISON);
    else if (s_ic_def->status_id == STATUS_FREEZE)
        IC_COLOR_SPAN(1, 3, 1, UI_COLOR_ICE);

    /* Row 5: on-hit element rider, else the health-rider display for
     * bronze-and-above heal cards (display-only, no on-hit roll). */
    if (s_ic_def->status_id == STATUS_BURN)
        IC_DRAW_TEXT(0, 5, "FIRE RIDER", 10);
    else if (s_ic_def->status_id == STATUS_FREEZE)
        IC_DRAW_TEXT(0, 5, "ICE RIDER", 9);
    else if (s_ic_def->status_id == STATUS_POISON)
        IC_DRAW_TEXT(0, 5, "POISON RIDER", 12);
    else if (ic_is_health_rider())
        IC_DRAW_TEXT(0, 5, "HEALTH RIDER", 12);
    else
        IC_DRAW_TEXT(0, 5, "NO RIDER", 8);
    /* Row 6: combat role (shields Defend, everything else Attacks;
     * rings Heal). */
    if (s_ic_def->type == CARD_TYPE_DEFENSE)
        IC_DRAW_TEXT(0, 6, "TYPE DEFEND", 11);
    else if (s_ic_def->type == CARD_TYPE_HEAL)
        IC_DRAW_TEXT(0, 6, "TYPE HEAL", 9);
    else
        IC_DRAW_TEXT(0, 6, "TYPE ATTACK", 11);
    /* Row 7: AP cost (right-aligned). */
    IC_DRAW_TEXT(0, 7, "AP COST", 7);
    ic_draw_rjust(7, 18, s_ic_def->cost);
    /* Row 8: power (right-aligned). */
    IC_DRAW_TEXT(0, 8, "POWER", 5);
    ic_draw_rjust(8, 18, s_ic_def->power);
    /* Row 9: uses per battle (INF = unlimited, never depletes). */
    detail_uses = s_ic_def->uses_per_battle;
    IC_DRAW_TEXT(0, 9, "USES", 4);
    if (detail_uses) {
        ic_draw_rjust(9, 18, detail_uses);
    } else {
        IC_DRAW_TEXT(16, 9, "INF", 3);
    }
    /* Row 10: collection vs deck counts (both right-aligned). */
    IC_DRAW_TEXT(0, 10, "OWN", 3);
    ic_draw_rjust(10, 7, ic_collection_count(&g->state.cards, s_ic_id));
    IC_DRAW_TEXT(9, 10, "DECK", 4);
    ic_draw_rjust(10, 18, ic_deck_count(&g->state.cards.deck, s_ic_id));
    /* Row 11: merchant sell value (price 0 = not buyable). */
    IC_DRAW_TEXT(0, 11, "SELL", 4);
    if (s_ic_def->price) {
        ic_draw_rjust(11, 17, s_ic_def->price);
        IC_DRAW_TEXT(18, 11, "G", 1);
    } else {
        IC_DRAW_TEXT(17, 11, "--", 2);
    }
    /* Row 13: hints (SELL only at a buying merchant). */
    if (loot_is_loot_id(s_ic_id) && ic_merchant_buys(g))
        IC_DRAW_TEXT(0, 13, "A:SELL SEL:BACK", 15);
    else
        IC_DRAW_TEXT(0, 13, "SEL:BACK", 8);
}

static char s_ic_big_num[6];

/* Decimal formatter without division (same repeated-subtraction
 * pattern as screen_content.c: banked bodies must not pull the SDCC
 * div/mod library, AGENTS.md 52.18). */
static void ic_format_uint(uint16_t uval, char *out)
{
    uint8_t i;
    uint8_t started = 0;
    uint16_t p;
    uint8_t d;
    for (i = 0; i < 4; i++) {
        if (i == 0) p = 10000;
        else if (i == 1) p = 1000;
        else if (i == 2) p = 100;
        else p = 10;
        d = 0;
        while (uval >= p) {
            uval -= p;
            d++;
        }
        if (d != 0 || started) {
            *out++ = (char)('0' + d);
            started = 1;
        }
    }
    *out++ = (char)('0' + (uint8_t)uval);
    *out = '\0';
}

/* ── STATUS tab: hero + wallet summary ───────────────────────────
 *   HP <hp>/<max>   (party slot 0, the hero)
 *   AP <pool>        (battle energy pool variable)
 *   GOLD <n>         (currency slot 0)
 *   CARDS <n>        (total owned copies) */
/* Draw an unsigned value right-aligned ending at col end_col
 * (labels sit at col 0; all detail numbers share the right edge). */
static void ic_draw_rjust(uint8_t y, uint8_t end_col, uint16_t v)
{
    uint8_t len = 0;
    ic_format_uint(v, s_ic_big_num);
    while (s_ic_big_num[len] != (char)0) len++;
    if (len == 0) len = 1;
    IC_DRAW_TEXT((uint8_t)(end_col - len + 1), y, s_ic_big_num, len);
}

static void ic_draw_status_page(Game *g)
{
    uint8_t i;
    uint8_t total = 0;
    for (i = 0; i < g->state.cards.collection.count; i++)
        total = (uint8_t)(total + g->state.cards.collection.entries[i].count);
    IC_DRAW_TEXT(0, 3, "HP ", 3);
    IC_DRAW_NUM2(3, 3, g->state.party.members[0].hp);
    IC_DRAW_TEXT(5, 3, "/", 1);
    IC_DRAW_NUM2(6, 3, g->state.party.members[0].max_hp);
    IC_DRAW_TEXT(0, 4, "AP ", 3);
    IC_DRAW_NUM2(3, 4,
        (uint8_t)g->state.variables.values[VARIABLE_ID_ENERGY_POOL - 1]);
    IC_DRAW_TEXT(0, 5, "GOLD ", 5);
    ic_format_uint((uint16_t)g->state.currency.amount[0], s_ic_big_num);
    IC_DRAW_TEXT(5, 5, s_ic_big_num, 5);
    IC_DRAW_TEXT(0, 6, "CARDS ", 6);
    ic_format_uint(total, s_ic_big_num);
    IC_DRAW_TEXT(6, 6, s_ic_big_num, 5);
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
    uint8_t e;
    if (!g) return;

    /* Sweep rider entries first: picker/emptied rows leave nothing
     * behind, and list/detail set exactly what they show below.  (The
     * QUEST tab dispatches to the bank-8 quest body, which sweeps for
     * itself on entry.) */
    for (e = 1; e <= VISIBLE_CARDS; e++) {
        ic_rider_hide(e);
    }

    ic_draw_header(g);

    if (g->item_menu_tab == TAB_STATUS) {
        ic_draw_status_page(g);
        return;
    }

    switch (g->item_menu_mode) {
        case MODE_CARD_DETAIL: ic_draw_card_detail_page(g); break;
        case MODE_PICKER:      ic_draw_picker(g); break;
        default:               ic_draw_cards_list(g); break;
    }
}
