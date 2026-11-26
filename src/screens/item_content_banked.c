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
#include "ui.h"
#include "menu.h"
#include "card.h"
#include "game_ids.h"
#include "banked.h"
#include "shops.h"
#include "menu_palette_generated.h"

#define TAB_DECK 0
#define TAB_GEM 3
#define NUM_TABS  4

#define ROW_TOP     0
#define FIRST_CARD  1
/* Row 3 is the CARD + deck-icon /OWN column header, row 4 carries the
 * scroll-up carets (blank when at the top); grouped card rows run
 * 5-13 (VISIBLE_CARDS rows: 9 rows x 4 OAM sprites = 36 entries,
 * inside the 40-entry OAM budget); row 14 the scroll-down carets
 * (blank when at the bottom), row 15 a blank gap before the
 * row-16 message/footer. */
#define ROW_FILTER  2
#define ROW_COL_HEAD 3
#define ROW_SEP 4
#define ROW_CARDS_TOP 5
#define VISIBLE_CARDS 9
#define ROW_SCROLL_UP 4
#define ROW_SCROLL_DOWN 14
#define ROW_GAP_BOT 15

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
/* Compacted per-side draw maps (menu_cursor_banked.c, same bank-2-readable
 * WRAM): the first 9 view positions visible on each side at/after the
 * scroll position, 0xFF past the end. */
extern uint8_t s_ic_draw_pos[9];
extern uint8_t s_ic_draw_pos_r[9];
/* Per-side scroll-overflow flags (same file, built by the view build
 * on every DECK render): bit 0 up-pile, 1 up-deck, 2
 * down-pile, 3 down-deck. */
extern uint8_t s_ic_scroll_flags;

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

/* Shared draw scratch, also used by the bank-13 QUEST/GEMS/HERO bodies
 * (item_quest_banked.c / item_gem_banked.c / item_status_banked.c): the trampoline is non-reentrant and only one
 * tab renders at a time, so one copy serves both (AGENTS.md 52.18:
 * no duplicate WRAM for bank-split mirrors). */
uint8_t s_ic_txt_i;
uint8_t s_ic_col_i;
static uint8_t s_ic_def_i;
static uint8_t s_ic_lst_pos;

uint8_t s_ic_y;
/* Drawn-slot counts per side for the OAM sweep below (independent
 * packing: each side hides only its own unused entries). */
static uint8_t s_ic_end_l;
static uint8_t s_ic_end_r;
static uint8_t s_ic_tile_elem;
static uint8_t s_ic_tile_wpn;
static uint8_t s_ic_in_deck;
static CardId s_ic_id;
static const CardDefinition *s_ic_def;
volatile uint8_t *s_ic_dst;
char *s_ic_buf;
static char s_ic_code[3]; /* 2-wide count cells + NUL (ic_draw_pair2). */

/* Card-menu element rider icon (BG tile over the card's elem cell).  The
 * menu carries dedicated 8x8 menu art now (assets/menu_riders.png,
 * UI_TILE_MENU_RIDER_*, boot-loaded): the recycled enemy-sprite OAM art
 * this screen used before was cropped off-center.  Shared with the shop
 * body (screen_content.c, same ROM bank 2: plain call, no trampoline).
 * Real status riders (fire/ice/poison) win; a riderless heal card shows
 * the green health icon (display-only, keyed off the heal role -- never
 * a status, no on-hit roll). */
uint8_t ic_rider_menu_tile(const CardDefinition *def)
{
    if (!def) return 0;
    if (def->status_id == STATUS_BURN) return UI_TILE_MENU_RIDER_FIRE;
    if (def->status_id == STATUS_FREEZE) return UI_TILE_MENU_RIDER_ICE;
    if (def->status_id == STATUS_POISON) return UI_TILE_MENU_RIDER_POISON;
    if (def->battle_type == BATTLE_CARD_TYPE_HEAL ||
        def->effect == CARD_EFFECT_HEAL_HP)
        return UI_TILE_MENU_RIDER_HEALTH;
    return 0;
}

/* Material icon OAM for the DECK list (entries 17-25 owned, 26-34
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
        uint8_t tile;
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
         * carry space/letter buffers, so a guarded space/letter paint
         * would leave stale art in VRAM while the buffer (and every
         * harness assert reading it) looks correct.  Blank+paint stays
         * row-local, so no full-screen wipe flashes (AGENTS.md 36). */
        tile = (uint8_t)(ui_font_tile_base + (uint8_t)(ch - ' '));
        BANKED_VRAM_WRITE(s_ic_dst, tile);
        *s_ic_buf = ch;
        s_ic_dst++;
        s_ic_buf++;
    }
}

#define IC_DRAW_TEXT(x, y, text, len) do { \
    s_ic_arg_x = (x); s_ic_arg_y = (y); s_ic_arg_text = (text); s_ic_arg_len = (len); \
    ic_draw_text_fast(); \
} while(0)

/* Full-row blank (shared by the list/detail paints: NULL text pads
 * 20 spaces without a 20-byte literal). */
static void ic_blank_row(uint8_t y)
{
    IC_DRAW_TEXT(0, y, NULL, 20);
}

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

/* ── Tab header: single source of truth is menu_tabs_snippet.h ──────
 * (compiled per bank: banked bodies cannot call across banks,
 * AGENTS.md 52.11.1).  DECK-only body (cursor col 0); QUEST, GEM and
 * HERO render in bank 13. */
#define MENU_TAB_DRAW_TEXT IC_DRAW_TEXT
#include "menu_tabs_snippet.h"

static void ic_draw_rjust(uint8_t y, uint8_t start_col, uint8_t end_col, uint16_t v);

static void ic_draw_header(void)
{
    menu_draw_tabs(0);
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

/* Card-menu heal-rider green: program BG slot UI_COLOR_HEALTH with the
 * generated fighthealth ramp (menu_palette_generated.h).  Called at the
 * top of every full menu render, i.e. right after the fixed wrapper's
 * ui_set_cram_palette(0) restored the base set -- slot 7 (DIM) is never
 * painted by a menu, and battle/overworld full redraws program their own
 * set again, so the grey-out is untouched.  Shared with the shop body
 * (screen_content.c, same ROM bank 2).  Counts as deck-management chrome:
 * one 8-byte CRAM ramp, no new WRAM. */
void ic_menu_cram(void)
{
    uint8_t c;

    if (!g_is_cgb) return;
    BCPS_REG = (uint8_t)(0x80 | (UI_COLOR_HEALTH << 3));
    for (c = 0; c < 8; c++) {
        BANKED_VRAM_WRITE(&BCPD_REG, menu_slot_health_ramp[c]);
    }
}

/* Rider-cell color: the palette slot the menu rider icon renders on.
 * Elements take their element slot; a heal card's icon takes
 * UI_COLOR_HEALTH (the menu CRAM kind's green slot 7) while its NAME
 * keeps the field slot, so only the icon reads green. */
static void ic_rider_color(uint8_t x, uint8_t y, const CardDefinition *def)
{
    if (def->status_id == STATUS_BURN)
        IC_COLOR_SPAN(x, y, 1, UI_COLOR_FIRE);
    else if (def->status_id == STATUS_FREEZE)
        IC_COLOR_SPAN(x, y, 1, UI_COLOR_ICE);
    else if (def->status_id == STATUS_POISON)
        IC_COLOR_SPAN(x, y, 1, UI_COLOR_POISON);
    else if (def->battle_type == BATTLE_CARD_TYPE_HEAL ||
             def->effect == CARD_EFFECT_HEAL_HP)
        IC_COLOR_SPAN(x, y, 1, UI_COLOR_HEALTH);
    else
        IC_COLOR_SPAN(x, y, 1, UI_COLOR_NONE);
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
    else
        IC_COLOR_SPAN(x, y, len, UI_COLOR_NONE);
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
static const uint8_t s_ic_loot_chance[4] = { 0, 208, 208, 176 };

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
 * bank 15): s_row_owned/s_row_decked hold one entry per view row for
 * the visible window, and slot 0 for the selected row in detail mode.
 * Keeping the scan out of this bank is what paid for the row redesign
 * (bank 2 sits at its ceiling, AGENTS.md 52.18). */
extern uint8_t s_row_owned[18];
extern uint8_t s_row_decked[18];

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

/* AP pip at column x (gold-styled energy cost pip 1-5).  The BG cell
 * shows the pip tile (236-240, streamed on full redraw) while the text
 * buffer keeps the digit for harness screen_row asserts (same split as
 * the weapon icon/letter cells).  VRAM always writes: the buffer holds
 * a digit the PPU never shows, so the text skip-guard would stale-hide
 * the pip (AGENTS.md 52.25).  An unaffordable cost still draws its own
 * pip -- the pool gate is flagged by painting the cell's palette slot
 * red (see the caller's FIRE span), never by hiding the value.
 * Cost 0 / above 5 falls back to font. */
static void ic_draw_ap_at(Game *g, uint8_t y, uint8_t x)
{
    (void)g;
    if (s_ic_def->cost >= 1 && s_ic_def->cost <= 5) {
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

/* 2-wide right-aligned count cell (" 4", "10"); len=3 on the right
 * column pads col 19 with a space in the same pass.  No division
 * (AGENTS.md 52.18: / and % pull the SDCC div/mod library into the
 * fixed bank). */
static void ic_draw_pair2(uint8_t x, uint8_t y, uint8_t v, uint8_t len)
{
    uint8_t tens = 0;
    while (v >= 10) {
        v -= 10;
        tens++;
    }
    s_ic_code[0] = tens ? (char)('0' + tens) : ' ';
    s_ic_code[1] = (char)('0' + v);
    s_ic_code[2] = '\0';
    IC_DRAW_TEXT(x, y, s_ic_code, len);
}


/* Icon cells for a DECK card row: weapon cell `wx` (icon tile in VRAM,
 * weapon letter in the text buffer for harness asserts), rider cell
 * `rx` (menu rider BG tile + element color), and AP unit icon
 * `ap_icon_x` (UI_TILE_AP_ICON in VRAM, buffer blank so screen_row
 * sees spaces).  Writes directly via non-negative column offsets off row
 * base `0x9800 + (y << 5)` (never `wx - rx`, which wrapped under unsigned
 * 16-bit arithmetic when weapon moved ahead of rider). */
static void ic_stamp_icons(uint8_t y, uint8_t wx, uint8_t rx,
                           uint8_t ap_icon_x)
{
    s_ic_dst = (volatile uint8_t *)(0x9800u + ((uint16_t)y << 5));
    VBK_REG = 0;
    BANKED_VRAM_WRITE(s_ic_dst + wx, s_ic_tile_wpn);
    BANKED_VRAM_WRITE(s_ic_dst + rx, s_ic_tile_elem);
    BANKED_VRAM_WRITE(s_ic_dst + ap_icon_x, UI_TILE_AP_ICON);
    s_ic_buf = &g_ui_screen_buf[y][wx];
    *s_ic_buf = s_ic_wpn_letters[s_ic_wpn];
    s_ic_buf = &g_ui_screen_buf[y][ap_icon_x];
    *s_ic_buf = ' ';
    /* Rider cell takes its element slot (fire/ice/poison, green for the
     * heal icon); the weapon cell takes the field slot (fight3 white
     * background, menu only -- battle keeps wood). */
    ic_rider_color(rx, y, s_ic_def);
    IC_COLOR_SPAN(wx, y, 1, UI_COLOR_FIELD);
}


/* Two per-side packed lists share each screen row: the pile (left,
 * cols 0-8) lists groups with copies to add (owned - decked > 0), the
 * deck (right, cols 10-18) groups with decked copies; col 9 is a blank
 * gap between the lists, col 19 pads.  Per cell (pile/deck):
 *   col 0/10: cursor caret (selected cell only)
 *   col 1/11: blank (caret breathing room)
 *   col 2/12: weapon icon tile (BG); buffer holds S/H/B/D/R
 *   col 3/13: rider cell (blank BG; rider/health OAM overlays it)
 *   col 4/14: material ingot (blank BG; OAM tinted per inventory ramp)
 *   col 5/15: AP pip tile (BG); buffer holds the cost digit; the cell
 *     takes the FIRE slot when the pool locks the card
 *   col 6/16: AP icon tile (BG); buffer blank
 *   col 7-8/17-18: side count, 2-wide (" 4"): left = owned - decked,
 *     right = decked
 * Row 3 is the fixed column header (PILE / DECK labels).  Row 4
 * carries the scroll-up carets, cards run rows 5-13 (VISIBLE_CARDS
 * rows), row 14 the scroll-down carets and row 15 a blank gap.
 * VRAM shows the icon tiles; the buffer keeps weapon letters, digits
 * and counts for harness asserts (material/rider cells stay blank,
 * OAM art).  Display cost/rider/color read the primary (first)
 * member; A tries members in collection order, so display and behavior
 * agree.  Note: same-name variants share one row only when their
 * on-hit riders match (fire and ice bows are different rows); see
 * docs/deck-management.md.
 *
 * ic_draw_card_cell paints one cell.  Cells that lose their group are
 * never blanked here: the view build erases
 * emptied slots (VRAM + buffer + OAM) when it rebuilds. */
static void ic_draw_card_cell(Game *g, uint8_t y, uint8_t vpos, uint8_t side, uint8_t slot)
{
    uint8_t curx = side ? 10 : 0;
    uint8_t oamx = side ? 14 : 4;
    uint8_t ridx = side ? 13 : 3;
    uint8_t wxx = side ? 12 : 2;
    uint8_t apx = side ? 15 : 5;
    uint8_t cntx = side ? 17 : 7;
    uint8_t csum = (uint8_t)((side ? 9 : 0) + slot);
    uint8_t ment = (uint8_t)((side ? 26 : 17) + slot);
    uint8_t avail;
    /* Grouped row: the view holds the primary collection index. */
    s_ic_id = ic_view_card_id(g, vpos);
    s_ic_def = ic_card_get_def(s_ic_id);
    if (!s_ic_def) return;
    ic_card_mat_wpn(s_ic_id, &s_ic_mat, &s_ic_wpn, &s_ic_shopkeys);

    s_ic_shopkeys = s_row_owned[csum];
    s_ic_in_deck = s_row_decked[csum];
    avail = (s_ic_shopkeys > s_ic_in_deck) ?
        (uint8_t)(s_ic_shopkeys - s_ic_in_deck) : 0;

    /* Cursor on this cell only (col 0 pile, col 10 deck); col 1/11
     * stays blank so the caret breathes (len 2 pads the gap in the
     * same pass). */
    IC_DRAW_TEXT(curx, y,
                 ((uint8_t)(vpos + FIRST_CARD) == g->item_menu_index &&
                  g->item_menu_side == side) ? ">" : " ", 2);

    /* Element rider icon tile for the elem cell (0 = no rider).  Stamped
     * as BG art by ic_stamp_icons; the element still reads from the cell
     * color spans (FIRE/POISON/ICE/HEALTH slots). */
    s_ic_tile_elem = ic_rider_menu_tile(s_ic_def);

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

    /* Blanks under the two OAM cells (material + rider): VRAM space
     * tiles, buffer spaces (OAM art on top).  Then stamp the weapon,
     * rider blank, and AP icon tiles directly without touching oamx. */
    IC_DRAW_TEXT(oamx, y, NULL, 1);
    IC_DRAW_TEXT(ridx, y, NULL, 1);
    ic_stamp_icons(y, wxx, ridx, (uint8_t)(apx + 1));
    ic_draw_ap_at(g, y, apx);
    /* An unaffordable cost keeps its pip but renders the cell in the
     * fire slot: the value stays readable, the red flags the gate
     * (the equip attempt still reports NOT ENOUGH AP on row 16). */
    if (s_ic_def->cost > ic_pool(g)) {
        IC_COLOR_SPAN(apx, y, 2, UI_COLOR_FIRE);
    } else {
        IC_COLOR_SPAN(apx, y, 2, UI_COLOR_GOLD);
    }
    ic_draw_pair2(cntx, y, (uint8_t)(side ? s_ic_in_deck : avail),
                  (uint8_t)(2 + side));

    /* Material ingot over the material cell (entries per visible row):
     * wood shows the log icon, every other material the ingot tinted by
     * its inventory ramp.  Quest items have no material.  Buffer keeps
     * spaces (harness asserts match letters, digits and counts, never
     * the icons).  The rider is BG art (above), so its old OAM entries
     * stay hidden -- the sweep in the render body keeps them that way. */
    if (s_ic_id == CARD_BONE || s_ic_id == CARD_OLD_KEY) {
        ic_mat_hide(ment);
    } else {
        ic_mat_show(ment, oamx, y, s_ic_mat);
    }
}

/* One grouped row across both pile/deck lists: each side draws its own
 * cell when the group shows there, or blanks its half (cols 0-8 / 10-19)
 * in a single pass with no full-row pre-blank.  Col 9 carries the
 * inventory-separator chrome between the lists. */
static void ic_draw_card_pair(Game *g, uint8_t y, uint8_t vL, uint8_t vR, uint8_t lst)
{
    if (vL != 0xFF) ic_draw_card_cell(g, y, vL, 0, lst);
    else IC_DRAW_TEXT(0, y, NULL, 9);
    menu_stamp_chrome(y, 9, UI_TILE_MENUS_INV_SEPARATOR);
    if (vR != 0xFF) ic_draw_card_cell(g, y, vR, 1, lst);
    else IC_DRAW_TEXT(10, y, NULL, 10);
}

static void ic_draw_row16(Game *g);

/* FILTER/SORT row (row 2) with its caret, covered in one left-to-right pass. */
static void ic_draw_filter_row(Game *g)
{
    IC_DRAW_TEXT(0, ROW_FILTER,
                 g->item_menu_index == ROW_TOP ? ">" : " ", 2);
    IC_DRAW_TEXT(2, ROW_FILTER, "* FILTER/SORT *", 18);
}

/* Scroll caret cells: the list scroll arrow (sheet (0,7)) for both
 * the row-4 up carets and the row-14 down carets, same as the QUEST
 * and GEMS tabs.
 * Buffer keeps '^'/'v' for harness screen_row asserts.  VRAM always
 * writes: the buffer glyph never matches the PPU tile, so the text
 * skip-guard would stale-hide the caret (same hazard as the AP pips,
 * AGENTS.md 52.25).  Hiding restores the font space tile through the
 * same unconditional write. */
static void ic_stamp_scroll_up(uint8_t y, uint8_t x, uint8_t show)
{
    s_ic_buf = &g_ui_screen_buf[y][x];
    *s_ic_buf = show ? '^' : ' ';
    s_ic_dst = (volatile uint8_t *)(0x9800u + ((uint16_t)y << 5) + x);
    VBK_REG = 0;
    BANKED_VRAM_WRITE(s_ic_dst,
        show ? UI_TILE_MENUS_LIST_SCROLL_UP : ui_font_tile_base);
}

static void ic_stamp_scroll(uint8_t y, uint8_t x, uint8_t show)
{
    s_ic_buf = &g_ui_screen_buf[y][x];
    *s_ic_buf = show ? 'v' : ' ';
    s_ic_dst = (volatile uint8_t *)(0x9800u + ((uint16_t)y << 5) + x);
    VBK_REG = 0;
    BANKED_VRAM_WRITE(s_ic_dst,
        show ? UI_TILE_MENUS_LIST_SCROLL_ARROW : ui_font_tile_base);
}

/* Scroll-indicator rows for the full list render: row-4 up arrows and row-14 down arrows (list
 * scroll arrow) at the material columns (4 pile / 14 deck), per side,
 * from the bank-15 overflow flags rebuilt by the view build just
 * before this render.  Row 13 is the inventory-separator column gap
 * (stamped by the list body); row 15 is the full-width bottom box
 * border.  Only the caret cells are ever written (a full clear blanks
 * the rows first, and nothing else addresses them, so no stale glyphs
 * survive). */
static void ic_draw_scroll_rows(Game *g)
{
    uint8_t flags;
    (void)g;
    flags = s_ic_scroll_flags;
    ic_stamp_scroll_up(ROW_SCROLL_UP, 4, (uint8_t)(flags & 0x01));
    ic_stamp_scroll_up(ROW_SCROLL_UP, 14, (uint8_t)(flags & 0x02));
    ic_stamp_scroll(ROW_SCROLL_DOWN, 4, (uint8_t)(flags & 0x04));
    ic_stamp_scroll(ROW_SCROLL_DOWN, 14, (uint8_t)(flags & 0x08));
}

static void ic_draw_cards_list(Game *g)
{
    uint8_t x;
    /* Strict top-to-bottom paint (rows 2..17), no card-row pre-blank:
     * every card cell is written once with its final tile. */
    ic_draw_filter_row(g);
    IC_DRAW_TEXT(0, ROW_COL_HEAD, "  PILE       DECK   ", 20);
    menu_stamp_chrome(ROW_COL_HEAD, 6, UI_TILE_MENUS_INV_ARROW);
    menu_stamp_chrome(ROW_COL_HEAD, 17, UI_TILE_MENUS_INV_ARROW);

    ic_blank_row(ROW_SCROLL_UP);

    s_ic_end_l = 0;
    s_ic_end_r = 0;
    for (s_ic_lst_pos = 0; s_ic_lst_pos < VISIBLE_CARDS; s_ic_lst_pos++) {
        uint8_t vL = s_ic_draw_pos[s_ic_lst_pos];
        uint8_t vR = s_ic_draw_pos_r[s_ic_lst_pos];
        ic_draw_card_pair(g, (uint8_t)(ROW_CARDS_TOP + s_ic_lst_pos),
                          vL, vR, s_ic_lst_pos);
        if (vL != 0xFF) s_ic_end_l = (uint8_t)(s_ic_lst_pos + 1);
        if (vR != 0xFF) s_ic_end_r = (uint8_t)(s_ic_lst_pos + 1);
    }
    for (s_ic_lst_pos = s_ic_end_l;
         s_ic_lst_pos < VISIBLE_CARDS; s_ic_lst_pos++) {
        ic_mat_hide((uint8_t)(1 + s_ic_lst_pos));
        ic_mat_hide((uint8_t)(17 + s_ic_lst_pos));
    }
    for (s_ic_lst_pos = s_ic_end_r;
         s_ic_lst_pos < VISIBLE_CARDS; s_ic_lst_pos++) {
        ic_mat_hide((uint8_t)(1 + s_ic_lst_pos));
        ic_mat_hide((uint8_t)(26 + s_ic_lst_pos));
    }

    ic_blank_row(ROW_SCROLL_DOWN);
    ic_draw_scroll_rows(g);
    menu_stamp_chrome(ROW_SCROLL_DOWN, 9, UI_TILE_MENUS_INV_SEPARATOR);

    for (x = 0; x < 20; x++)
        menu_stamp_chrome(ROW_GAP_BOT, x, UI_TILE_MENUS_BOTTOM_BORDER);

    ic_draw_row16(g);
    IC_DRAW_TEXT(0, 17, NULL, 20);
    menu_stamp_chrome(17, 0, UI_TILE_BUTTON_DPAD_LR);
    IC_DRAW_TEXT(1, 17, "SIDE", 4);
    menu_stamp_chrome(17, 6, UI_TILE_BUTTON_B);
    IC_DRAW_TEXT(7, 17, "BACK", 13);
}

/* Row-16 message/side-footer draw: pads to 20 cols in one pass. */
static void ic_draw_row16(Game *g)
{
    if (g->item_menu_message == MSG_DECK_FULL) {
        IC_DRAW_TEXT(0, 16, "DECK FULL", 20);
    } else if (g->item_menu_message == MSG_DECK_MIN) {
        IC_DRAW_TEXT(0, 16, "DECK MIN 5", 20);
    } else if (g->item_menu_message == MSG_DECK_CAPPED) {
        IC_DRAW_TEXT(0, 16, "AT CAP", 20);
    } else if (g->item_menu_message == MSG_QUEST_ITEM) {
        IC_DRAW_TEXT(0, 16, "QUEST ITEM", 20);
    } else if (g->item_menu_message == MSG_NEED_AP) {
        uint8_t need = 0;
        if (g->item_menu_index >= FIRST_CARD &&
            (uint8_t)(g->item_menu_index - FIRST_CARD) < s_view_count) {
            const CardDefinition *nd =
                ic_card_get_def(ic_view_card_id(g, (uint8_t)(g->item_menu_index - FIRST_CARD)));
            if (nd) need = nd->cost;
        }
        IC_DRAW_TEXT(0, 16, "NOT ENOUGH AP ", 14);
        s_ic_code[0] = (char)('0' + need);
        s_ic_code[1] = '\0';
        IC_DRAW_TEXT(14, 16, s_ic_code, 6);
    } else if (g->item_menu_side == 0) {
        IC_DRAW_TEXT(0, 16, NULL, 20);
        menu_stamp_chrome(16, 0, UI_TILE_BUTTON_A);
        IC_DRAW_TEXT(1, 16, "TO DECK", 7);
        menu_stamp_chrome(16, 9, UI_TILE_BUTTON_SELECT_1);
        menu_stamp_chrome(16, 10, UI_TILE_BUTTON_SELECT_2);
        menu_stamp_chrome(16, 11, UI_TILE_BUTTON_SELECT_3);
        IC_DRAW_TEXT(12, 16, "INFO", 8);
    } else {
        IC_DRAW_TEXT(0, 16, NULL, 20);
        menu_stamp_chrome(16, 0, UI_TILE_BUTTON_A);
        IC_DRAW_TEXT(1, 16, "TO PILE", 7);
        menu_stamp_chrome(16, 9, UI_TILE_BUTTON_SELECT_1);
        menu_stamp_chrome(16, 10, UI_TILE_BUTTON_SELECT_2);
        menu_stamp_chrome(16, 11, UI_TILE_BUTTON_SELECT_3);
        IC_DRAW_TEXT(12, 16, "INFO", 8);
    }
}

/* Incremental DECK list repaint (AGENTS.md 36), run after DECK nav:
 * FILTER row, every live list slot and row 16, without touching the
 * render cache -- nothing blinks.
 * Precondition: the bank-15 view build ran for the current
 * state/scroll (rebuilt maps + emptied slots erased), or the moved
 * banked nav just re-windowed for the new scroll.
 * Staging: g_bk_ptr_a = Game*. */
void item_card_refresh_banked(void)
{
    Game *g = (Game *)g_bk_ptr_a;
    uint8_t k;
    uint8_t vL;
    uint8_t vR;
    uint8_t y;
    if (!g) return;
    /* FILTER row + caret (only the list uses it; picker/detail draw
     * their own rows). */
    ic_draw_filter_row(g);
    /* Scroll indicators (rows 4/14): the view build already recomputed
     * the overflow flags for the current state/scroll. */
    ic_draw_scroll_rows(g);
    /* Lean in-place repaint (AGENTS.md 36): the build (bank 15) already
     * erased slots/sides that lost their row -- unconditionally, so no
     * icon tile survives -- and this loop only draws live slots, one
     * pass each, with no full-area blank (nav never flashes the list).
     * Skip-based text writes are exact here: cleared cells match the
     * buffer, and redrawn cells re-stamp their constant icon tiles
     * (rider cell tile 0, chrome AP icon) over identical VRAM.
     * Still-dead slots stay blank. */
    for (k = 0; k < 9; k++) {
        vL = s_ic_draw_pos[k];
        vR = s_ic_draw_pos_r[k];
        y = (uint8_t)(ROW_CARDS_TOP + k);
        if (vL != 0xFF || vR != 0xFF)
            ic_draw_card_pair(g, y, vL, vR, k);
    }
    ic_draw_row16(g);
}

static void ic_draw_card_detail_page(Game *g)
{
    uint8_t detail_uses;
    uint8_t bx;
    if (g->item_menu_index < FIRST_CARD ||
        (uint8_t)(g->item_menu_index - FIRST_CARD) >= s_view_count)
        return;
    s_ic_id = ic_view_card_id(g, (uint8_t)(g->item_menu_index - FIRST_CARD));
    s_ic_def = ic_card_get_def(s_ic_id);
    if (!s_ic_def) return;

    /* Row 2: blank. */
    ic_blank_row(2);

    /* Row 3: icons + element-colored full name (strict left-to-right
     * single pass, no row pre-blank). */
    s_ic_tile_elem = ic_rider_menu_tile(s_ic_def);

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

    IC_DRAW_TEXT(0, 3, NULL, 1);
    g_ui_screen_buf[3][1] = ' ';
    g_ui_screen_buf[3][2] = ' ';
    s_ic_dst = (volatile uint8_t *)(0x9800 + ((uint16_t)3 << 5) + 1);
    VBK_REG = 0;
    BANKED_VRAM_WRITE(s_ic_dst, s_ic_tile_elem);
    BANKED_VRAM_WRITE(s_ic_dst + 1, s_ic_tile_wpn);
    IC_COLOR_SPAN(2, 3, 1, UI_COLOR_FIELD);
    ic_rider_color(1, 3, s_ic_def);
    IC_DRAW_TEXT(3, 3, NULL, 1);
    bx = ic_draw_full_name(4, 3, s_ic_def);
    IC_DRAW_TEXT((uint8_t)(4 + bx), 3, NULL, (uint8_t)(16 - bx));

    if (s_ic_shopkeys || s_ic_id == CARD_BONE || s_ic_id == CARD_OLD_KEY)
        ic_mat_hide(IC_MAT_ENTRY_BASE);
    else
        ic_mat_show(IC_MAT_ENTRY_BASE, 3, 3, s_ic_mat);
    for (bx = 1; bx <= 20; bx++)
        ic_mat_hide((uint8_t)(IC_MAT_ENTRY_BASE + bx));

    /* Row 4: blank. */
    ic_blank_row(4);

    /* Row 5: on-hit element rider, else the health-rider display for
     * heal cards of any material (single pass padded to 20 cols). */
    if (s_ic_def->status_id == STATUS_BURN)
        IC_DRAW_TEXT(0, 5, "FIRE RIDER", 20);
    else if (s_ic_def->status_id == STATUS_FREEZE)
        IC_DRAW_TEXT(0, 5, "ICE RIDER", 20);
    else if (s_ic_def->status_id == STATUS_POISON)
        IC_DRAW_TEXT(0, 5, "POISON RIDER", 20);
    else if (ic_is_health_rider())
        IC_DRAW_TEXT(0, 5, "HEALTH RIDER", 20);
    else
        IC_DRAW_TEXT(0, 5, "NO RIDER", 20);
    /* Row 6: combat role. */
    if (s_ic_def->type == CARD_TYPE_DEFENSE)
        IC_DRAW_TEXT(0, 6, "TYPE DEFEND", 20);
    else if (s_ic_def->type == CARD_TYPE_HEAL)
        IC_DRAW_TEXT(0, 6, "TYPE HEAL", 20);
    else
        IC_DRAW_TEXT(0, 6, "TYPE ATTACK", 20);
    /* Row 7: AP cost (right-aligned). */
    IC_DRAW_TEXT(0, 7, "AP COST", 7);
    ic_draw_rjust(7, 7, 18, s_ic_def->cost);
    IC_DRAW_TEXT(19, 7, NULL, 1);
    /* Row 8: power (right-aligned). */
    IC_DRAW_TEXT(0, 8, "POWER", 5);
    ic_draw_rjust(8, 5, 18, s_ic_def->power);
    IC_DRAW_TEXT(19, 8, NULL, 1);
    /* Row 9: uses per battle (INF = unlimited, never depletes). */
    detail_uses = s_ic_def->uses_per_battle;
    IC_DRAW_TEXT(0, 9, "USES", 4);
    if (detail_uses) {
        ic_draw_rjust(9, 4, 18, detail_uses);
        IC_DRAW_TEXT(19, 9, NULL, 1);
    } else {
        IC_DRAW_TEXT(4, 9, NULL, 12);
        IC_DRAW_TEXT(16, 9, "INF", 4);
    }
    /* Row 10: group collection vs deck counts. */
    IC_DRAW_TEXT(0, 10, "OWN", 3);
    ic_draw_rjust(10, 3, 7, s_row_owned[0]);
    IC_DRAW_TEXT(8, 10, " DECK", 5);
    ic_draw_rjust(10, 13, 18, s_row_decked[0]);
    IC_DRAW_TEXT(19, 10, NULL, 1);
    /* Row 11: shop value reminder. */
    IC_DRAW_TEXT(0, 11, "VALUE", 5);
    if (s_ic_def->price && s_ic_def->type != CARD_TYPE_SPECIAL) {
        ic_draw_rjust(11, 5, 17, s_ic_def->price);
        IC_DRAW_TEXT(18, 11, "G", 2);
    } else {
        IC_DRAW_TEXT(5, 11, NULL, 12);
        IC_DRAW_TEXT(17, 11, "--", 3);
    }
    /* Rows 12-14: blank. */
    for (bx = 12; bx <= 14; bx++)
        ic_blank_row(bx);
    /* Row 15-17: bottom border + back hint + blank footer. */
    for (bx = 0; bx < 20; bx++)
        menu_stamp_chrome(15, bx, UI_TILE_MENUS_BOTTOM_BORDER);
    IC_DRAW_TEXT(0, 16, NULL, 20);
    menu_stamp_chrome(16, 0, UI_TILE_BUTTON_B);
    IC_DRAW_TEXT(1, 16, "BACK", 19);
    ic_blank_row(17);
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

/* Draw an unsigned value right-aligned in [start_col..end_col], padding
 * leading cells with spaces in one left-to-right pass. */
static void ic_draw_rjust(uint8_t y, uint8_t start_col, uint8_t end_col, uint16_t v)
{
    uint8_t len = 0;
    uint8_t num_col;
    ic_format_uint(v, s_ic_big_num);
    while (s_ic_big_num[len] != (char)0) len++;
    if (len == 0) len = 1;
    num_col = (uint8_t)(end_col - len + 1);
    if (num_col > start_col)
        IC_DRAW_TEXT(start_col, y, NULL, (uint8_t)(num_col - start_col));
    IC_DRAW_TEXT(num_col, y, s_ic_big_num, len);
}

void item_screen_render_banked(void)
{
    Game *g = (Game *)g_bk_ptr_a;
    uint8_t e;
    if (!g) return;

    /* One-shot per menu stay (g_menu_riders_loaded is cleared by
     * menus_entry_invalidate_banked on every SCREEN_ITEM entry):
     * programs slot-7 heal-rider green, streams the 4 rider BG tiles
     * and the 5 material OAM tiles + OBJ ramps.  Tab switches back to
     * DECK pay zero CRAM/tile-data stream writes. */
    if (!g_menu_riders_loaded) {
        ic_menu_cram();
        ic_menu_riders_load();
        ic_mat_stream();
    }

    for (e = 1; e <= 34; e++) {
        ic_mat_hide(e);
    }

    ic_draw_header();

    switch (g->item_menu_mode) {
        case MODE_CARD_DETAIL: ic_draw_card_detail_page(g); break;
        default:               ic_draw_cards_list(g); break;
    }
}
