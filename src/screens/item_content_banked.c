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
#include "shops.h"

#define TAB_DECK 0
#define TAB_QUEST 1
#define TAB_HERO 2
#define TAB_GEM 3
#define NUM_TABS  4

#define ROW_TOP     0
#define FIRST_CARD  1
/* Row 3 is the CARD + deck-icon /OWN column header, row 4 a blank paper
 * separator; grouped card rows run 5-12 (VISIBLE_CARDS rows: 8 rows
 * x 4 OAM sprites = 32 entries, inside the 40-entry OAM budget). */
#define ROW_FILTER  2
#define ROW_COL_HEAD 3
#define ROW_SEP 4
#define ROW_CARDS_TOP 5
#define VISIBLE_CARDS 8

#define MODE_LIST         0
#define MODE_CARD_DETAIL  1
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

extern uint8_t s_view_indices[MAX_CARD_COLLECTION];
extern uint8_t s_view_count;
/* Compacted draw map (menu_cursor_banked.c, same bank-2-readable WRAM):
 * view position per drawn row, 0xFF past the end. */
extern uint8_t s_ic_draw_pos[8];

extern const CardDefinition g_cards[];
extern const ShopDefinition g_shops[];
extern const uint8_t g_shop_count;
extern char g_ui_screen_buf[18][21];
extern uint8_t ui_font_tile_base;
extern uint8_t g_is_cgb;
/* Card-menu material icons: tile bytes + ids/slots/ramp CRAM bytes
 * (same-bank data: this body streams and programs them on DECK renders,
 * so bank 8 never carries menu art). */
#define MAT_NEED_RAMP_BYTES
#include "material_tiles_generated.h"
#include "gfx/material_tiles.h"

/* Shared draw scratch, also used by the bank-8 quest body
 * (item_quest_banked.c): the trampoline is non-reentrant and only one
 * tab renders at a time, so one copy serves both (AGENTS.md 52.18:
 * no duplicate WRAM for bank-split mirrors). */
uint8_t s_ic_txt_i;
uint8_t s_ic_col_i;
static uint8_t s_ic_def_i;
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

/* Rider OAM for the item screen (entries 1-8 left, 9-16 right): icon
 * over a card's elem cell for cards carrying a rider, hidden
 * otherwise.  Transition-hide covers screen changes; the render
 * dispatch below sweeps entries first so quest/picker/emptied rows
 * never go stale.  Same bank (2), WRAM shadow OAM only; rider tiles /
 * palettes are boot-resident, and the menu OBJ slots the riders use
 * (2-4) are never reprogrammed -- only the material slots
 * (0/1/5/6/7, programmed on DECK renders) are. */
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

/* Material icon OAM for the DECK list (entries 17-24 owned, 25-32
 * deck) and the detail page (entry 12): the card's material ingot over
 * the col-1/col-10 cells (list) or the col-3 gap (detail), tinted per
 * material (inventory1-5 OBJ ramps programmed by the render/stream
 * bodies).  Same WRAM shadow-OAM pattern as the riders; the quest/gem
 * renderers sweep entries 1-32 on tab switches. */
#define IC_MAT_ENTRY_BASE 12
void ic_mat_hide(uint8_t entry)
{
    volatile uint8_t *re;
    re = (volatile uint8_t *)(0xC000u + ((uint16_t)entry << 2));
    re[0] = 0;
    re[2] = 0;
}

/* Stream one OAM tile (16 bytes) + program one OBJ slot, LCD-safely
 * (bounded Mode wait per access: DECK renders repaint with the LCD on,
 * so the timer ISR could otherwise eclipse the wait->store window).
 * Same pattern as the bank-8 OAM loaders (marks_banked.c). */
static void ic_stream_oam_cell(uint8_t oam_id, const uint8_t *src)
{
    uint8_t j;
    volatile uint8_t *dst;
    dst = (volatile uint8_t *)(0x8000u + ((uint16_t)oam_id << 4));
    for (j = 0; j < 16; j++) {
        BANKED_VRAM_WRITE(dst, src[j]);
        dst++;
    }
}

static void ic_obj_program(uint8_t slot, const uint8_t *src)
{
    uint8_t c;
    OCPS_REG = (uint8_t)(0x80 | (slot << 3));
    for (c = 0; c < 8; c++) {
        BANKED_VRAM_WRITE(&OCPD_REG, src[c]);
    }
}

/* Material index (ic_mat_str order: 0 wood, 1 bronze, 2 iron, 3 steel,
 * 4 mythril) to the staged OAM tile id + OBJ slot. */
static void ic_mat_tile(uint8_t mat, uint8_t *tile, uint8_t *slot)
{
    if (mat == 0) {
        *tile = MAT_TILE_WOOD;
        *slot = MAT_OBJ_WOOD;
    } else if (mat == 1) {
        *tile = MAT_TILE_BRONZE;
        *slot = MAT_OBJ_BRONZE;
    } else if (mat == 3) {
        *tile = MAT_TILE_STEEL;
        *slot = MAT_OBJ_STEEL;
    } else if (mat == 4) {
        *tile = MAT_TILE_MYTHRIL;
        *slot = MAT_OBJ_MYTHRIL;
    } else {
        *tile = MAT_TILE_IRON;
        *slot = MAT_OBJ_IRON;
    }
}

void ic_mat_show(uint8_t entry, uint8_t x, uint8_t y, uint8_t mat)
{
    uint8_t tile, slot;
    volatile uint8_t *re;
    ic_mat_tile(mat, &tile, &slot);
    re = (volatile uint8_t *)(0xC000u + ((uint16_t)entry << 2));
    re[0] = (uint8_t)((y << 3) + 16);
    re[1] = (uint8_t)((x << 3) + 8);
    re[2] = tile;
    re[3] = slot;
}

/* Tile + OBJ stream for the menu material icons, shared with the shop
 * renderer (same bank 2, plain call): fixed OAM ids 117-121 (time-share
 * blob offsets 17-21) + alias OBJ slots with the inventory ramps.  The
 * caller sweeps and stamps its own OAM entries; entry programming runs
 * here so overworld/battle never see half-set state. */
void ic_mat_stream(void)
{
    uint8_t e;
    for (e = 0; e < 5; e++) {
        ic_stream_oam_cell((uint8_t)(MAT_TILE_STEEL + e),
                           material_tiles + ((uint16_t)e << 4));
    }
    VBK_REG = 0;
    if (g_is_cgb) {
        ic_obj_program(MAT_OBJ_STEEL, mat_ramp_steel);
        ic_obj_program(MAT_OBJ_BRONZE, mat_ramp_bronze);
        ic_obj_program(MAT_OBJ_IRON, mat_ramp_iron);
        ic_obj_program(MAT_OBJ_MYTHRIL, mat_ramp_mythril);
        ic_obj_program(MAT_OBJ_WOOD, mat_ramp_wood);
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

/* WRAM attribute mirror (ui.c): the harness asserts CGB palette
 * bytes via tilemap_attr.  ic_color_span_fast keeps it in sync
 * (DEBUG only, like the battle/gamble spans). */
#ifdef DEBUG_BUILD
extern volatile uint8_t g_tilemap_attr_mirror[32 * 32];
#endif

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
#ifdef DEBUG_BUILD
        g_tilemap_attr_mirror[(uint16_t)y * 32 + s_ic_col_i + x] = pal;
#endif
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
    if (g->item_menu_tab == TAB_HERO) cx = 11;
    /* QUEST and GEM never reach this body (bank 8 renders them). */
    IC_DRAW_TEXT(0, 0, "DECK QUEST HERO GEM", 19);
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

/* Resolve a card to material/weapon indices (loot.h axes).  Shop keys
 * (SPECIAL, never listed) report through is_shopkeys instead. */
static void ic_card_mat_wpn(CardId id, uint8_t *mat, uint8_t *wpn,
                            uint8_t *is_shopkeys)
{
    uint8_t w;
    *is_shopkeys = 0;
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
        case CARD_BRONZE_SWORD: *mat = 1; *wpn = 0; break;
        case CARD_POISON_DAGGER: *mat = 1; *wpn = 3; break;
        case CARD_BOW_9: *mat = 4; *wpn = 2; break;
        case CARD_STEEL_SHIELD: *mat = 3; *wpn = 1; break;
        case CARD_SHOPKEYS: *is_shopkeys = 1; *mat = 2; *wpn = 0; break;
        default: *mat = 2; *wpn = 0; break;
    }
}

/* Shared with the bank-2 shop renderer (screen_content.c, same bank:
 * plain call, no trampoline): full "<MAT> <WPN>" names on shop rows.
 * Loot ids resolve from the id axes; curated ids from the catalog table
 * below (mirrors the shop stock, which is curated-only).  Quest items
 * report through is_shopkeys/is_special instead of a name. */
void ic_card_mat_wpn_pub(CardId id, uint8_t *mat, uint8_t *wpn,
                         uint8_t *is_special)
{
    uint8_t shopkeys = 0;
    ic_card_mat_wpn(id, mat, wpn, &shopkeys);
    *is_special = (uint8_t)(shopkeys || id == CARD_BONE || id == CARD_OLD_KEY);
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

/* Name color by role: healing cards read field green; on-hit
 * elements read fire red / ice blue / poison purple; plain cards stay
 * on the default paper slot. */
static void ic_name_color(uint8_t x, uint8_t y, uint8_t len,
                          const CardDefinition *def)
{
    if (def->battle_type == BATTLE_CARD_TYPE_HEAL ||
        def->effect == CARD_EFFECT_HEAL_HP)
        IC_COLOR_SPAN(x, y, len, UI_COLOR_FIELD);
    else if (def->status_id == STATUS_BURN)
        IC_COLOR_SPAN(x, y, len, UI_COLOR_FIRE);
    else if (def->status_id == STATUS_FREEZE)
        IC_COLOR_SPAN(x, y, len, UI_COLOR_ICE);
    else if (def->status_id == STATUS_POISON)
        IC_COLOR_SPAN(x, y, len, UI_COLOR_POISON);
}

static uint8_t s_ic_mat;
static uint8_t s_ic_wpn;
/* Dual-use scratch: the shop-keys flag out of ic_card_mat_wpn, then the
 * group owned total for the row being drawn.  Never live across the two
 * uses (the row drawer re-reads it right after the mat/wpn call). */
static uint8_t s_ic_shopkeys;
static uint8_t s_ic_mlen;
static uint8_t s_ic_wlen;
static const char *s_ic_mstr;
static const char *s_ic_wstr;

/* Draw "<MAT> <WPN>" at (x, y), element-colored.  Returns the drawn
 * length (for spans); shop keys draw "SHOP KEYS". */
static uint8_t ic_draw_full_name(uint8_t x, uint8_t y,
                                 const CardDefinition *def)
{
    uint8_t total;
    ic_card_mat_wpn(def->id, &s_ic_mat, &s_ic_wpn, &s_ic_shopkeys);
    if (s_ic_shopkeys) {
        IC_DRAW_TEXT(x, y, "SHOP KEYS", 9);
        return 9;
    }
    /* Quest items share the shop-keys' sword-icon path (no new art);
     * only the name differs.  Shown in the DECK list and detail
     * page exactly as the event dialogues name them. */
    if (def->id == CARD_BONE) {
        IC_DRAW_TEXT(x, y, "BONE", 4);
        return 4;
    }
    if (def->id == CARD_OLD_KEY) {
        IC_DRAW_TEXT(x, y, "OLD KEY", 7);
        return 7;
    }
    s_ic_mstr = ic_mat_str(s_ic_mat, &s_ic_mlen);
    s_ic_wstr = ic_wpn_str(s_ic_wpn, &s_ic_wlen);
    IC_DRAW_TEXT(x, y, s_ic_mstr, s_ic_mlen);
    IC_DRAW_TEXT((uint8_t)(x + s_ic_mlen), y, " ", 1);
    IC_DRAW_TEXT((uint8_t)(x + s_ic_mlen + 1), y, s_ic_wstr, s_ic_wlen);
    total = (uint8_t)(s_ic_mlen + 1 + s_ic_wlen);
    ic_name_color(x, y, total, def);
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

/* Shared with the bank-2 shop map (screen_content.c, same bank: plain
 * call, no trampoline): identity checks for one-copy rows resolve loot
 * twins through here.  External linkage for that single user. */
const CardDefinition *ic_loot_synth(CardId id)
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
        /* Wild rings hold material-tier uses per battle (mirrors the
         * synth ring branch: wood 1 .. mythril 5). */
        g_card_scratch.uses_per_battle = LOOT_RING_USES(material);
    }
    g_card_scratch.power = LOOT_POWER(material, wi);
    g_card_scratch.price = (uint8_t)(g_card_scratch.power +
        (uint8_t)(material << 1) +
        ((effect == EFF_POISON && weapon == WPN_DAGGER) ? 2 : 0));
    has_rider = (material >= MAT_BRONZE &&
        s_ic_loot_status[effect] != STATUS_NONE &&
        ((effect == EFF_POISON) ? (weapon == WPN_DAGGER) :
         ((effect == EFF_FIRE || effect == EFF_ICE) ?
             (weapon == WPN_BOW) : 0)));
    if (has_rider) {
        g_card_scratch.status_id = s_ic_loot_status[effect];
        g_card_scratch.status_chance = s_ic_loot_chance[effect];
    }
    /* Derived AP cost (docs/card-ap-cost-formula.md), staged after the
     * rider above so the status bit is final; reserved weapon ids keep
     * cost 0 (not playable, §5.3). */
    g_card_scratch.cost = (weapon <= WPN_RING)
        ? CARD_AP(g_card_scratch.power, material, has_rider) : 0;
    /* Bow arrows per battle (docs/loot.md §34.6): AP-derived, inverse
     * to power (wood 6 .. mythril 2, floored at 2).  Staged after the
     * rider above so the uses track CARD_AP with no second ladder. */
    if (weapon == WPN_BOW)
        g_card_scratch.uses_per_battle =
            LOOT_BOW_USES_FROM_AP(g_card_scratch.cost);
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

/* ── Grouped view ───────────────────────
 * One DECK row per MATERIAL+WEAPON name ("all Steel Shields are one
 * row"), whichever rider/power/cost variants or capped remainder rows
 * the collection holds.  The build lives in BANK 11 with the cursor
 * geometry (item_view_build_banked, menu_cursor_banked.c): the fixed
 * bank cannot host it (0x8000) and bank 2 is at its ceiling -- the
 * grouping pass cost nearly 1 KB here.  The fixed-bank caller runs the
 * build and then the render as two sequential banked calls in the same
 * frame, so the view is always fresh before anything reads it. */

/* SPECIAL cards (quest items like the Lost Shop Keys) are listed by the
 * collection but never deckable, so they are not DECK rows. */
static uint8_t ic_card_deckable(const CardDefinition *def)
{
    return (uint8_t)(def != NULL && def->type != CARD_TYPE_SPECIAL);
}

/* Row sums come from the grouped view build (item_view_build_banked,
 * bank 11): s_row_owned/s_row_decked hold one entry per view row for
 * the visible window, and slot 0 for the selected row in detail mode.
 * Keeping the scan out of this bank is what paid for the row redesign
 * (bank 2 sits at its ceiling, AGENTS.md 52.18). */
extern uint8_t s_row_owned[12];
extern uint8_t s_row_decked[12];

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

/* Live energy pool for the equip gate (docs/enemies_ranking.md §4.8):
 * an unset variable means the default pool (same normalization as the
 * battle HUD loader), never AP 0. */
static uint8_t ic_pool(const Game *g)
{
    int16_t p = g->state.variables.values[VARIABLE_ID_ENERGY_POOL - 1];
    if (p < 1) return 1;
    if (p > 255) return 255;
    return (uint8_t)p;
}

/* Weapon letter for the harness text buffer (the BG cell itself shows
 * the weapon icon tile; the letter keeps the weapon readable to
 * screen_row asserts). */
static const char s_ic_wpn_letters[5] = { 'S', 'H', 'B', 'D', 'R' };

/* AP pip at column x (gold-styled energy cost pip 1-5; '!' when the pool
 * gate locks the primary -- the NEED AP message names the cost).  The BG
 * cell shows the pip tile (236-240, streamed on full redraw) while the
 * text buffer keeps the digit for harness screen_row asserts (same split
 * as the weapon icon/letter cells).  VRAM always writes: the buffer holds
 * a digit the PPU never shows, so the text skip-guard would stale-hide
 * the pip (AGENTS.md 52.25).  Cost 0 / above 5 falls back to font. */
static void ic_draw_ap_at(Game *g, uint8_t y, uint8_t x)
{
    if (s_ic_def->cost > ic_pool(g)) {
        IC_DRAW_TEXT(x, y, "!", 1);
    } else if (s_ic_def->cost >= 1 && s_ic_def->cost <= 5) {
        s_ic_buf = &g_ui_screen_buf[y][x];
        *s_ic_buf = (char)('0' + s_ic_def->cost);
        s_ic_dst = (volatile uint8_t *)(0x9800 + ((uint16_t)y << 5) + x);
        VBK_REG = 0;
        BANKED_VRAM_WRITE(s_ic_dst,
            (uint8_t)(UI_TILE_AP_PIP_BASE + s_ic_def->cost - 1));
    } else {
        s_ic_code[0] = (char)('0' + s_ic_def->cost);
        s_ic_code[1] = '\0';
        IC_DRAW_TEXT(x, y, s_ic_code, 1);
    }
}

/* 2-wide right-aligned count cell (" 4", "10").  No division
 * (AGENTS.md 52.18: / and % pull the SDCC div/mod library into the
 * fixed bank). */
static void ic_draw_pair2(uint8_t x, uint8_t y, uint8_t v)
{
    uint8_t tens = 0;
    while (v >= 10) {
        v -= 10;
        tens++;
    }
    s_ic_code[0] = tens ? (char)('0' + tens) : ' ';
    s_ic_code[1] = (char)('0' + v);
    s_ic_code[2] = '\0';
    IC_DRAW_TEXT(x, y, s_ic_code, 2);
}


/* Row chrome tiles: AP icon first, then a blank (per-side chrome is
 * stamped by ic_stamp_icons at wx + 2; the digit cells own their pips
 * via ic_draw_ap_at, so an unconditional stamp past them would blank
 * decked digits in VRAM while the buffer stayed truthful (AGENTS.md
 * 52.25).  Boot-loaded ids, so no streaming (see ui.h). */
static const uint8_t s_ic_chrome_tiles[2] = {
    UI_TILE_AP_ICON, UI_TILE_MENU_BLANK
};

/* Icon cells: rider cell rx (blank BG + element color; the row painter
 * shows the OAM rider) and weapon cell wx (icon tile in VRAM, weapon
 * letter in the text buffer for harness asserts).  The chrome loop
 * stamps n cells VRAM-only starting at wx + 2 (AP icon, then blanks);
 * one write expansion in a loop -- bank 2 has no room for unrolled
 * (AGENTS.md 52.18).  Rows stamp 1 (the AP icon); digit cells keep
 * their pip tiles from ic_draw_ap_at. */
static void ic_stamp_icons(uint8_t y, uint8_t rx, uint8_t wx, uint8_t n)
{
    s_ic_dst = (volatile uint8_t *)(0x9800 + ((uint16_t)y << 5) + rx);
    VBK_REG = 0;
    BANKED_VRAM_WRITE(s_ic_dst, s_ic_tile_elem);
    BANKED_VRAM_WRITE(s_ic_dst + (uint16_t)(wx - rx), s_ic_tile_wpn);
    s_ic_dst += (uint16_t)(wx - rx + 2);
    for (s_ic_col_i = 0; s_ic_col_i < n; s_ic_col_i++)
        BANKED_VRAM_WRITE(&s_ic_dst[s_ic_col_i],
                          s_ic_chrome_tiles[s_ic_col_i]);
    s_ic_buf = &g_ui_screen_buf[y][wx];
    *s_ic_buf = s_ic_wpn_letters[s_ic_wpn];
    /* Rider cell takes the same role color as the name (fire/ice/poison
     * rider, field green for heal); the weapon cell takes the field
     * slot (fight3 white background, menu only -- battle keeps wood). */
    ic_name_color(rx, y, 1, s_ic_def);
    IC_COLOR_SPAN(wx, y, 1, UI_COLOR_FIELD);
}


/* One GROUP per row, drawn twice: owned pool (left, cols 0-8) and
 * deck (right, cols 9-17); cols 18-19 pad.
 *   col 0/9: cursor caret (active side only)
 *   col 1/10: material ingot (blank BG; OAM tinted per inventory ramp)
 *   col 2/11: rider cell (blank BG; rider/health OAM overlays it)
 *   col 3/12: weapon icon tile (BG); buffer holds S/H/B/D/R
 *   col 4/13: AP pip digit ('!' when above the pool); pip tile behind
 *   col 5/14: AP icon tile (BG); buffer blank
 *   col 6-7/15-16: side count, 2-wide (" 4"): left = owned - decked,
 *     right = decked
 *   col 8: '|' separator (static text)
 * Row 3 is the fixed column header (OWNED / DECK labels).  Row 4 is a
 * blank paper separator; cards run rows 5-12 (VISIBLE_CARDS rows).
 * VRAM shows the icon tiles; the buffer keeps weapon letters, digits
 * and counts for harness asserts (material/rider cells stay blank,
 * OAM art).  Display cost/rider/color read the primary (first)
 * member; A tries members in collection order, so display and behavior
 * agree.  Note: same-name variants (e.g. rider vs plain) share one row
 * by design; see docs/deck-management.md. */
static void ic_draw_card_pair(Game *g, uint8_t y, uint8_t pos, uint8_t lst)
{
    uint8_t avail;
    /* Grouped row: the view holds the primary collection index. */
    s_ic_id = ic_view_card_id(g, pos);
    s_ic_def = ic_card_get_def(s_ic_id);
    if (!s_ic_def) return;
    ic_card_mat_wpn(s_ic_id, &s_ic_mat, &s_ic_wpn, &s_ic_shopkeys);

    s_ic_shopkeys = s_row_owned[lst];
    s_ic_in_deck = s_row_decked[lst];
    avail = (s_ic_shopkeys > s_ic_in_deck) ?
        (uint8_t)(s_ic_shopkeys - s_ic_in_deck) : 0;

    /* Cursor on the active cell only (col 0 pile, col 10 deck);
     * col 1 stays blank so the caret breathes; clear the other cell. */
    if ((uint8_t)(pos + FIRST_CARD) == g->item_menu_index) {
        if (g->item_menu_side == 0) {
            IC_DRAW_TEXT(0, y, ">", 1);
            IC_DRAW_TEXT(10, y, " ", 1);
        } else {
            IC_DRAW_TEXT(0, y, " ", 1);
            IC_DRAW_TEXT(10, y, ">", 1);
        }
    } else {
        IC_DRAW_TEXT(0, y, " ", 1);
        IC_DRAW_TEXT(10, y, " ", 1);
    }

    /* Element rider OAM is shown per side below (over the col-2/col-11
     * rider cells): the cells stay blank and the element reads from the
     * color spans (FIRE/POISON/ICE slots). */
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

    /* Blanks under the four OAM cells (material + rider, both
     * sides): VRAM space tiles, buffer spaces (OAM art on top).  The
     * pile block runs cols 2-8 (cursor breathing room at col 1),
     * separator at col 9, deck block at cols 12-18. */
    IC_DRAW_TEXT(2, y, " ", 1);
    IC_DRAW_TEXT(3, y, " ", 1);
    IC_DRAW_TEXT(9, y, "|", 1);
    IC_DRAW_TEXT(12, y, " ", 1);
    IC_DRAW_TEXT(13, y, " ", 1);
    ic_stamp_icons(y, 3, 4, 1);
    ic_stamp_icons(y, 13, 14, 1);
    ic_draw_ap_at(g, y, 5);
    ic_draw_ap_at(g, y, 15);
    IC_DRAW_TEXT(6, y, " ", 1);
    IC_DRAW_TEXT(16, y, " ", 1);
    IC_COLOR_SPAN(5, y, 2, UI_COLOR_GOLD);
    IC_COLOR_SPAN(15, y, 2, UI_COLOR_GOLD);
    ic_draw_pair2(7, y, avail);
    ic_draw_pair2(17, y, s_ic_in_deck);
    IC_DRAW_TEXT(19, y, " ", 1);

    /* Material ingots over the col-1 (owned) and col-10 (deck) cells
     * (entries per visible row): wood shows the log icon, every other
     * material the ingot tinted by its inventory ramp.  Rider icons
     * over col-2/col-11 (shared with the health '+' when no status
     * rider is staged).  Quest items have no material.  Buffer keeps
     * spaces (harness asserts match letters, digits and counts, never
     * the icons). */
    if (s_ic_id == CARD_BONE || s_ic_id == CARD_OLD_KEY) {
        ic_mat_hide((uint8_t)(17 + lst));
        ic_mat_hide((uint8_t)(25 + lst));
    } else {
        ic_mat_show((uint8_t)(17 + lst), 2, y, s_ic_mat);
        ic_mat_show((uint8_t)(25 + lst), 12, y, s_ic_mat);
    }
    ic_rider_show((uint8_t)(1 + lst), 3, y, s_ic_def->status_id);
    ic_rider_show((uint8_t)(9 + lst), 13, y, s_ic_def->status_id);
    if ((!s_ic_def || s_ic_def->status_id == STATUS_NONE) &&
        ic_is_health_rider()) {
        ic_health_show((uint8_t)(1 + lst), 3, y);
        ic_health_show((uint8_t)(9 + lst), 13, y);
    }
}

static void ic_draw_row16(Game *g);
static void ic_draw_cards_list(Game *g)
{
    /* Fixed column header: PILE over the left (owned) block, DECK over
     * the right block, '|' separator between (cols 0-19):
     * "  PILE    |   DECK    ".  Row 4 is a blank paper separator. */
    IC_DRAW_TEXT(0, ROW_COL_HEAD, "  PILE   |   DECK   ", 20);
    IC_DRAW_TEXT(0, ROW_SEP, "                    ", 20);
    s_ic_y = ROW_CARDS_TOP;
    if (g->item_menu_scroll == 0) {
        if (g->item_menu_index == ROW_TOP)
            IC_DRAW_TEXT(0, ROW_FILTER, ">", 1);
        else
            IC_DRAW_TEXT(0, ROW_FILTER, " ", 1);
        IC_DRAW_TEXT(2, ROW_FILTER, "* FILTER/SORT *", 15);
        s_ic_y = ROW_CARDS_TOP;
    }
    for (s_ic_lst_pos = 0; s_ic_lst_pos < VISIBLE_CARDS; s_ic_lst_pos++) {
        uint8_t vpos = s_ic_draw_pos[s_ic_lst_pos];
        if (vpos == 0xFF) break;
        ic_draw_card_pair(g, s_ic_y, vpos, s_ic_lst_pos);
        s_ic_y = (uint8_t)(s_ic_y + 1);
    }
    /* Sweep sprite entries past the last drawn row (short lists and
     * scroll bottoms must not keep a stale icon): riders 1-8/9-16 and
     * ingots 17-24/25-32 per visible row. */
    for (s_ic_lst_pos = (uint8_t)(s_ic_y - ROW_CARDS_TOP);
         s_ic_lst_pos < VISIBLE_CARDS; s_ic_lst_pos++) {
        ic_rider_hide((uint8_t)(1 + s_ic_lst_pos));
        ic_rider_hide((uint8_t)(9 + s_ic_lst_pos));
        ic_mat_hide((uint8_t)(17 + s_ic_lst_pos));
        ic_mat_hide((uint8_t)(25 + s_ic_lst_pos));
    }

    /* Row 16 (message or side footer) goes through the shared helper
     * so the incremental post-A refresh paints exactly what the full
     * render paints (the helper blanks stale tails itself). */
    ic_draw_row16(g);
    IC_DRAW_TEXT(0, 17, "L/R:SIDE B:BACK", 15);
}

/* Row-16 message/side-footer draw, shared by the full list render and
 * the incremental post-A refresh: short messages blank their tail so
 * no stale glyphs survive without a full clear. */
static void ic_draw_row16(Game *g)
{
    uint8_t len = 0;
    uint8_t i;
    if (g->item_menu_message == MSG_DECK_FULL) {
        IC_DRAW_TEXT(0, 16, "DECK FULL", 9);
        len = 9;
    } else if (g->item_menu_message == MSG_DECK_MIN) {
        IC_DRAW_TEXT(0, 16, "DECK MIN 5", 10);
        len = 10;
    } else if (g->item_menu_message == MSG_DECK_CAPPED) {
        IC_DRAW_TEXT(0, 16, "AT CAP", 6);
        len = 6;
    } else if (g->item_menu_message == MSG_QUEST_ITEM) {
        IC_DRAW_TEXT(0, 16, "QUEST ITEM", 10);
        len = 10;
    } else if (g->item_menu_message == MSG_NEED_AP) {
        /* The digit reads the selected card's cost at draw time -- no
         * new Game field, no WRAM growth (AGENTS.md §52.24). */
        uint8_t need = 0;
        if (g->item_menu_index >= FIRST_CARD &&
            (uint8_t)(g->item_menu_index - FIRST_CARD) < s_view_count) {
            const CardDefinition *nd =
                ic_card_get_def(ic_view_card_id(g, (uint8_t)(g->item_menu_index - FIRST_CARD)));
            if (nd) need = nd->cost;
        }
        IC_DRAW_TEXT(0, 16, "NEED AP ", 8);
        s_ic_code[0] = (char)('0' + need);
        s_ic_code[1] = '\0';
        IC_DRAW_TEXT(8, 16, s_ic_code, 1);
        len = 9;
    } else if (g->item_menu_side == 0) {
        IC_DRAW_TEXT(0, 16, "A:TO DECK SEL:INFO", 17);
        len = 17;
    } else {
        IC_DRAW_TEXT(0, 16, "A:TO PILE SEL:INFO", 17);
        len = 17;
    }
    for (i = len; i < 20; i++)
        IC_DRAW_TEXT(i, 16, " ", 1);
}

/* Incremental post-A refresh (AGENTS.md 36): repaints the acted row's
 * two count cells and row 16 (message or side footer) without touching
 * the render cache -- moving cards never blinks.  A neither creates
 * nor removes groups, so the view, draw map, scroll, OAM and icons
 * all stay exactly as drawn; only counts and row 16 can change.
 * Staging: g_bk_ptr_a = Game* (row derived from item_menu_index). */
void item_card_refresh_banked(void)
{
    Game *g = (Game *)g_bk_ptr_a;
    uint8_t vpos;
    uint8_t k;
    uint8_t y;
    uint8_t avail;
    if (!g) return;
    if (g->item_menu_index >= FIRST_CARD) {
        vpos = (uint8_t)(g->item_menu_index - FIRST_CARD);
        for (k = 0; k < 8; k++) {
            if (s_ic_draw_pos[k] != vpos) continue;
            y = (uint8_t)(ROW_CARDS_TOP + k);
            avail = s_row_owned[k] > s_row_decked[k] ?
                (uint8_t)(s_row_owned[k] - s_row_decked[k]) : 0;
            ic_draw_pair2(7, y, avail);
            ic_draw_pair2(17, y, s_row_decked[k]);
            break;
        }
    }
    ic_draw_row16(g);
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

    /* Material ingot over the col-3 gap (entry 12; quest items and shop
     * keys have no material, so they hide it).  Buffer keeps the space;
     * the weapon cell takes the field slot (fight3 white background,
     * menu only -- battle keeps wood). */
    IC_DRAW_TEXT(3, 3, " ", 1);
    if (s_ic_shopkeys || s_ic_id == CARD_BONE || s_ic_id == CARD_OLD_KEY)
        ic_mat_hide(IC_MAT_ENTRY_BASE);
    else
        ic_mat_show(IC_MAT_ENTRY_BASE, 3, 3, s_ic_mat);
    /* Hide material entries past the detail ingot (list rows use
     * 17-32 now; shop uses 13-22 on its own screen). */
    {
        uint8_t hide_i;
        for (hide_i = 1; hide_i <= 20; hide_i++)
            ic_mat_hide((uint8_t)(IC_MAT_ENTRY_BASE + hide_i));
    }

    IC_COLOR_SPAN(2, 3, 1, UI_COLOR_FIELD);
    if (s_ic_def->status_id == STATUS_BURN)
        IC_COLOR_SPAN(1, 3, 1, UI_COLOR_FIRE);
    else if (s_ic_def->status_id == STATUS_POISON)
        IC_COLOR_SPAN(1, 3, 1, UI_COLOR_POISON);
    else if (s_ic_def->status_id == STATUS_FREEZE)
        IC_COLOR_SPAN(1, 3, 1, UI_COLOR_ICE);

    /* Row 5: on-hit element rider, else the health-rider display for
     * heal cards of any material (display-only, no on-hit roll). */
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
    /* Row 10: group collection vs deck counts (the whole same-name
     * row, not just the primary id -- matches the list's DECK d/o). */
    IC_DRAW_TEXT(0, 10, "OWN", 3);
    ic_draw_rjust(10, 7, s_row_owned[0]);
    IC_DRAW_TEXT(9, 10, "DECK", 4);
    ic_draw_rjust(10, 18, s_row_decked[0]);
    /* Row 11: shop value reminder.  Selling moved to the shop's SELL
     * tab (docs/loot.md §34.6), so the detail page no longer sells:
     * loot shows its formula value, curated its catalog price, quest
     * items `--`.  Display-only (what the merchant WOULD pay). */
    IC_DRAW_TEXT(0, 11, "VALUE", 5);
    if (s_ic_def->price && s_ic_def->type != CARD_TYPE_SPECIAL) {
        ic_draw_rjust(11, 17, s_ic_def->price);
        IC_DRAW_TEXT(18, 11, "G", 1);
    } else {
        IC_DRAW_TEXT(17, 11, "--", 2);
    }
    /* Row 13: hints. */
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

/* ── HERO tab: hero + wallet summary ───────────────────────────
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


    /* The grouped DECK view was just rebuilt by the fixed-bank caller
     * (item_view_build_banked, bank 11) immediately before this body ran,
     * so s_view_indices/s_view_count are fresh for everything below. */

    /* Sweep rider + material entries first: picker/emptied rows leave
     * nothing behind, and list/detail set exactly what they show below.
     * (The QUEST tab dispatches to the bank-8 quest body, which sweeps
     * for itself on entry.)  Entries 1-8 left riders, 9-16 right riders,
     * 17-24 left ingots, 25-32 right ingots. */
    for (e = 1; e <= 32; e++) {
        ic_rider_hide(e);
    }

    /* Card-menu material icons (assets/material_tiles.png, cells in
     * steel/bronze/iron/mythril/wood order): stream here; the per-row
     * stamps below only write OAM entries.  Every DECK render
     * (redraw-gated, so bounded): renders always precede their stamps,
     * so the first frame already shows resident art.  Battle/overworld
     * never display these ids, actor OAM stays hidden while a menu is
     * open, and overworld entry restores tiles + canonical OBJ
     * (marks_banked.c). */
    if (g->item_menu_tab == TAB_DECK) {
        ic_mat_stream();
    }

    ic_draw_header(g);

    if (g->item_menu_tab == TAB_HERO) {
        ic_draw_status_page(g);
        return;
    }

    switch (g->item_menu_mode) {
        case MODE_CARD_DETAIL: ic_draw_card_detail_page(g); break;
        case MODE_PICKER:      ic_draw_picker(g); break;
        default:               ic_draw_cards_list(g); break;
    }
}
