#pragma bank 2
#pragma disable_warning 110

#include <gb/gb.h>
#include <gb/cgb.h>
#include "game.h"
#include "ui.h"
#include "banked.h"
#include "battle/card.h"
#include "rpg/cards.h"
#include "rpg/loot.h"
#include "rpg/status.h"
#include "rider_tiles_generated.h"
#include "shops.h"
#include "menu.h"
#include "rpg/save.h"
#include "game_ids.h"
#include "economy.h"

/* Bank-2 self-contained shop / save-load render bodies (AGENTS.md 52.11.1):
 * the fixed _CODE/_HOME area is at its size limit (see make memmap), so the
 * full shop and save-load content draws live here, writing VRAM directly with
 * bank-local helpers (a banked body must never call fixed-bank functions --
 * see ui_battle_content.c / title_content.c for the same pattern).
 *
 * The fixed wrapper (shop_screen.c / save_load_screen.c) runs menu_draw_frame
 * (fixed: centered title + separator + full clear) and THIS body draws only
 * the dynamic content below the frame, so no bank-local clear is needed.
 *
 * Staged input: g_bk_ptr_a = Game*.
 */

extern char g_ui_screen_buf[18][21];
extern uint8_t ui_font_tile_base;
extern uint8_t g_is_cgb;

extern const CardDefinition g_cards[];
extern const ShopDefinition g_shops[];
/* Loot identity synthesis (item_content_banked.c, same ROM bank 2:
 * plain call, no trampoline).  Needed here so the one-copy shop map
 * counts ownership by card identity, not row id (docs/loot.md §34.6):
 * a loot twin already owned hides the curated one-copy row even though
 * no curated row exists to merge it under yet. */
extern const CardDefinition *ic_loot_synth(CardId id);
/* Material/weapon resolver (same file): full "<MAT> <WPN>" shop names.
 * Sets *is_special for quest items (never listed, never sold). */
extern void ic_card_mat_wpn_pub(CardId id, uint8_t *mat, uint8_t *wpn,
                                uint8_t *is_special);
extern const uint8_t g_shop_count;

/* Shop message states (mirror shop_screen.c). */
#define SC_SHOP_MSG_NONE 0
#define SC_SHOP_MSG_BOUGHT 1
#define SC_SHOP_MSG_NO_GOLD 2
#define SC_SHOP_MSG_MAX_COPIES 3


static uint8_t s_sc_txt_i;
static uint8_t s_sc_col_i;
static uint8_t s_sc_shop_i;
static uint8_t s_sc_shop_scroll;
static uint8_t s_sc_def_i;
static uint8_t s_sc_shop_pos;
static uint8_t s_sc_stock_pos;

static uint8_t s_sc_y;
static uint8_t s_sc_pos;
static uint8_t s_sc_tile_elem;
static uint8_t s_sc_tile_wpn;
static uint16_t s_sc_gold;
static const ShopDefinition *s_sc_shop_def;
static const CardDefinition *s_sc_card_def;
static char s_sc_str[8];
static char s_sc_code[8];
static volatile uint8_t *s_sc_dst;
static char *s_sc_buf;
static Game *s_sc_game;
static uint8_t s_sc_idx;

static const uint16_t s_sc_p10[4] = { 10000, 1000, 100, 10 };

static void sc_format_uint(uint16_t uval, char *out)
{
    uint8_t i, started = 0;
    for (i = 0; i < 4; i++) {
        uint16_t p = s_sc_p10[i];
        uint8_t d = 0;
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

static void sc_vram_sync_write(volatile uint8_t *dst, uint8_t tile)
{
    BANKED_VRAM_WRITE(dst, tile);
}

static void sc_put_char(uint8_t x, uint8_t y, char ch)
{
    uint8_t tile;

    if (y >= 18 || x >= 20) return;
    if (g_ui_screen_buf[y][x] == ch) return;
    tile = (uint8_t)(ui_font_tile_base + (uint8_t)(ch - ' '));
    VBK_REG = 0;
    sc_vram_sync_write((volatile uint8_t *)(0x9800 + ((uint16_t)y << 5) + x), tile);
    g_ui_screen_buf[y][x] = ch;
}

static void sc_draw_text(uint8_t x, uint8_t y, const char *text, uint8_t max_chars)
{
    char ch;
    uint8_t ended;

    if (y >= 18) return;
    if ((uint8_t)(x + max_chars) > 20) max_chars = (uint8_t)(20 - x);

    VBK_REG = 0;
    ended = (text == NULL);
    s_sc_dst = (volatile uint8_t *)(0x9800 + ((uint16_t)y << 5) + x);
    s_sc_buf = &g_ui_screen_buf[y][x];

    for (s_sc_txt_i = 0; s_sc_txt_i < max_chars; s_sc_txt_i++) {
        if (!ended) {
            ch = text[s_sc_txt_i];
            if (ch == '\0') {
                ended = 1;
                ch = ' ';
            }
        } else {
            ch = ' ';
        }
        if (*s_sc_buf != ch) {
            uint8_t tile = (uint8_t)(ui_font_tile_base + (uint8_t)(ch - ' '));
            sc_vram_sync_write(s_sc_dst, tile);
            *s_sc_buf = ch;
        }
        s_sc_dst++;
        s_sc_buf++;
    }
}

static void sc_color_span(uint8_t x, uint8_t y, uint8_t len, uint8_t palette)
{
    if (!g_is_cgb) return;
    if (y >= 18 || x >= 32) return;
    if ((uint8_t)(x + len) > 32) len = (uint8_t)(32 - x);

    s_sc_dst = (volatile uint8_t *)(0x9800 + ((uint16_t)y << 5) + x);
    VBK_REG = 1;
    for (s_sc_col_i = 0; s_sc_col_i < len; s_sc_col_i++) {
        sc_vram_sync_write(&s_sc_dst[s_sc_col_i], (uint8_t)(palette & 0x07));
    }
    VBK_REG = 0;
}

static const ShopDefinition *sc_shop_active(const Game *g)
{
    for (s_sc_shop_i = 0; s_sc_shop_i < g_shop_count; s_sc_shop_i++) {
        if (g_shops[s_sc_shop_i].id == g->shop_id) return &g_shops[s_sc_shop_i];
    }
    return NULL;
}

static const CardDefinition *sc_card_get_def(CardId id)
{
    for (s_sc_def_i = 0; s_sc_def_i < GAME_CARD_COUNT; s_sc_def_i++) {
        if (g_cards[s_sc_def_i].id == id) return &g_cards[s_sc_def_i];
    }
    return NULL;
}

/* Same-card test for the one-copy map: identical type/power/cost/uses/
 * effect/battle type/rider (the 8 identity bytes; id, max_copies, price
 * and name are NOT identity -- a curated row and its loot twin differ
 * exactly there).  The stock def is a g_cards ROM pointer (stable across
 * the synth call below, which reuses its scratch); loot rows resolve
 * through the shared bank-2 synth above. */
static uint8_t sc_row_matches_stock(const CardDefinition *s, CardId row_id)
{
    const CardDefinition *d;
    if (!s) return 0;
    if (loot_is_loot_id(row_id)) {
        d = ic_loot_synth(row_id);
    } else {
        d = sc_card_get_def(row_id);
    }
    if (!d) return 0;
    return (uint8_t)(d->type == s->type &&
                     d->power == s->power &&
                     d->cost == s->cost &&
                     d->uses_per_battle == s->uses_per_battle &&
                     d->effect == s->effect &&
                     d->battle_type == s->battle_type &&
                     d->status_id == s->status_id &&
                     d->status_chance == s->status_chance);
}

/* ── Full card names (docs/loot.md §34.1 short code -> "<MAT> <WPN>") ──
 * The shop used to print the 3-letter battle code ("RG4"); rows now
 * print what the CARDS list prints ("IRON RING", element-colored),
 * drawn in two parts so no WRAM name buffer is needed (stack-gap
 * budget, AGENTS.md §52.24).  Tables mirror item_content_banked.c;
 * keep in sync (same order, same strings). */
static const char *sc_mat_str(uint8_t m, uint8_t *len)
{
    switch (m) {
        case 1: *len = 6; return "BRONZE";
        case 2: *len = 4; return "IRON";
        case 3: *len = 5; return "STEEL";
        case 4: *len = 7; return "MYTHRIL";
        default: *len = 4; return "WOOD";
    }
}

static const char *sc_wpn_str(uint8_t w, uint8_t *len)
{
    switch (w) {
        case 1: *len = 6; return "SHIELD";
        case 2: *len = 3; return "BOW";
        case 3: *len = 6; return "DAGGER";
        case 4: *len = 4; return "RING";
        default: *len = 5; return "SWORD";
    }
}

static const char s_card_bt_codes[] = "SW\0SH\0BO\0RG\0DA";

/* Market-scaled quote S (docs/economy.md §4): bank-2-local copy of the
 * bank-11 quote body (a banked body must never call a fixed-bank fn,
 * AGENTS.md §52.11.1).  The shop BUY row and the BUY quote body share
 * it (same bank: plain call, no trampoline -- the ic_loot_synth
 * precedent) so the row shows what the till charges.
 * Same shape as economy_quote_banked: zero guard (price 0 = free basic,
 * always quotes 0 however biased -- the gate in tools/check_economy.py
 * keeps price-0 rows bias-free), 8-bit saturating base, repeated-add
 * multiply, one rounding, same index clamp (out-of-band reads neutral:
 * index 0 would otherwise quote (b*0+16)>>5 = 0, i.e. FREE, for every
 * priced row -- a single clamp here covers all readers even if a path
 * ever runs game_state_zero without game_new_game).  Render rows pass
 * the live index (read once at render entry into s_sc_idx); the quote
 * body reads it from the staged game. */
static uint8_t s_sc_market_quote(uint8_t price, int8_t bias, uint8_t idx)
{
    uint16_t p;
    uint16_t q;
    uint8_t i;
    uint8_t b;
    if (price == 0) return 0;
    if (idx < ECONOMY_INDEX_MIN || idx > ECONOMY_INDEX_MAX)
        idx = ECONOMY_INDEX_NEUTRAL;
    if (bias >= 0) {
        b = (uint8_t)(price + bias);
        if (b < price) b = 255;
    } else {
        uint8_t neg = (uint8_t)(0 - bias);
        b = (neg >= price) ? 1 : (uint8_t)(price - neg);
    }
    p = 0;
    for (i = idx; i != 0; i--) p = (uint16_t)(p + b);
    /* Saturate, never truncate: base 255 at high inflation exceeds 255
     * (255*48+16 >> 5 = 383) and a narrowing cast would wrap the quote
     * to garbage.  p itself cannot overflow (max 12240 < 65535). */
    q = (uint16_t)(((uint16_t)(p + 16)) >> 5);
    return (q > 255) ? 255 : (uint8_t)q;
}

static void sc_card_code_str(uint8_t battle_type, uint8_t power, char *out)
{
    const char *bt = (battle_type <= BATTLE_CARD_TYPE_DAGGER) ?
        (s_card_bt_codes + (battle_type * 3)) : "??";
    uint8_t tens = 0;
    uint8_t ones = power;
    while (ones >= 10) {
        ones = (uint8_t)(ones - 10);
        tens++;
    }
    out[0] = bt[0];
    out[1] = bt[1];
    if (power >= 10) {
        out[2] = (char)('0' + tens);
        out[3] = (char)('0' + ones);
        out[4] = '\0';
    } else {
        out[2] = (char)('0' + ones);
        out[3] = '\0';
    }
}

static uint8_t sc_nm_mat;
static uint8_t sc_nm_wpn;
static uint8_t sc_nm_special;
static uint8_t sc_nm_mlen;
static uint8_t sc_nm_wlen;

/* Draw "<MAT> <WPN>" at (x, y) in a 14-wide field (cols x..x+13),
 * element-colored like the CARDS list.  Quest items / unknown ids fall
 * back to the short battle code (those never appear in shop stock or
 * the loot-only SELL list; the fallback keeps the row readable). */
static void sc_draw_full_name(uint8_t x, uint8_t y,
                              const CardDefinition *def)
{
    const char *ms;
    const char *ws;
    uint8_t total;
    ic_card_mat_wpn_pub(def->id, &sc_nm_mat, &sc_nm_wpn, &sc_nm_special);
    if (!sc_nm_special) {
        ms = sc_mat_str(sc_nm_mat, &sc_nm_mlen);
        ws = sc_wpn_str(sc_nm_wpn, &sc_nm_wlen);
        sc_draw_text(x, y, ms, sc_nm_mlen);
        sc_draw_text((uint8_t)(x + sc_nm_mlen), y, " ", 1);
        sc_draw_text((uint8_t)(x + sc_nm_mlen + 1), y, ws, sc_nm_wlen);
        /* Blank the rest of the 14-wide name field (a shorter name must
         * not leave a longer previous row's tail). */
        total = (uint8_t)(sc_nm_mlen + 1 + sc_nm_wlen);
        if (total < 14)
            sc_draw_text((uint8_t)(x + total), y, NULL, (uint8_t)(14 - total));
        if (def->battle_type == BATTLE_CARD_TYPE_HEAL ||
            def->effect == CARD_EFFECT_HEAL_HP)
            sc_color_span(x, y, total, UI_COLOR_FIELD);
        else if (def->status_id == STATUS_BURN)
            sc_color_span(x, y, total, UI_COLOR_FIRE);
        else if (def->status_id == STATUS_POISON)
            sc_color_span(x, y, total, UI_COLOR_POISON);
        else if (def->status_id == STATUS_FREEZE)
            sc_color_span(x, y, total, UI_COLOR_ICE);
        return;
    }
    sc_card_code_str(def->battle_type, def->power, s_sc_code);
    sc_draw_text(x, y, s_sc_code, 14);
}

/* Shared cursor repaint moved to bank 11 (src/screens/menu_cursor_banked.c):
 * bank 2's switchable area is full, and the cursor is pure WRAM math.
 * The render below keeps its own row geometry (bm_cursor_row comment
 * moved with it -- keep in sync). */

/* Rider OAM for the shop list (entries 1-10, one per visible stock row):
 * icon over the col-1 elem cell for cards carrying a rider, hidden
 * otherwise.  Transition-hide covers screen changes; the full render
 * repaints rows on every state change, and out-of-range rows hide, so
 * scrolled-away/emptied rows never go stale.  Same bank (2), WRAM
 * shadow OAM only; rider tiles/palettes are boot-resident (ui_init) and
 * shop screens never reprogram OBJ slots. */
static void shop_rider_tile(uint8_t status, uint8_t *tile, uint8_t *slot)
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

/* One-copy stock visible map (docs/loot.md §34.6): rebuilt by the
 * bank-2 render on every full redraw (entry, purchase) into WRAM; the
 * render and rider painters below only read it.  g_shop_vis_map[k] is
 * the stock position of the k-th visible (unowned) line,
 * g_shop_vis_count their number (0 = out of stock).  WRAM is always
 * mapped, so no staging is needed -- this is what keeps the fixed bank
 * under 0x8000 (AGENTS.md §52.11.1/§52.18). */
uint8_t g_shop_vis_map[SHOP_MAX_ITEMS];
uint8_t g_shop_vis_count;

/* Shop tabs (row 4 header, LEFT/RIGHT flips): TAB_BUY lists the stock,
 * TAB_SELL lists the player's sellable cards.  The active tab rides
 * g->item_menu_tab (0/1; the item screen owns the other values and
 * always resets on open, so the shop entry setting 0 is unambiguous).
 * One WRAM map serves both tabs (rebuilt per redraw for the active
 * tab, so no second 50-byte map is needed): BUY holds STOCK positions,
 * SELL holds COLLECTION positions of sellable rows. */
#define SHOP_TAB_BUY  0
#define SHOP_TAB_SELL 1

/* Sell value of an owned card (0 = not sellable, never listed):
 * - loot ids sell at their full synthesized price (docs/loot.md §34:
 *   power + 2·material, +2 poison dagger) -- loot has no buy value,
 *   so the formula IS the merchant's appraisal;
 * - curated cards sell below their buy value on a rarity-scaled cut of
 *   the BIASED base (matched pair with the BUY quote, which scales
 *   price + bias: a cut from the raw price would sit above the buy
 *   quote on negative-bias rows): base × (5−tier)/6 (wood 5/6 …
 *   mythril 1/6), min 1 for any priced card.  Commons sell near buy
 *   price, mythril for a small fraction.
 *   The cut is market-scaled like every till price (docs/economy.md
 *   §4-§5): at the same index the cut stays under the buy quote
 *   (cut ≤ biased base and the quote is monotonic), so a same-visit
 *   buy-then-sell never prints gold.  Across visits the market moves,
 *   so a later sale at a higher index can exceed the earlier buy --
 *   that is market risk, not arbitrage (see docs/economy.md §5.1).
 * - price-0 basics (free starter rebuys) and SPECIAL quest items never
 *   sell -- selling a free card would print money (buy 0, sell 1,
 *   rebuy 0, repeat), so the SELL tab excludes them entirely.
 * No division library: banked bodies use repeated subtraction
 * (AGENTS.md §52.18); n is uint16_t (base ≤ 255, ×5 ≤ 1275), q fits
 * a byte. */
static uint8_t sc_sell_price(CardId id)
{
    const CardDefinition *d;
    uint8_t mat, wpn, special;
    uint8_t k, q;
    uint16_t n;
    uint8_t base;
    if (loot_is_loot_id(id)) {
        d = ic_loot_synth(id);
        /* Loot appraisal scales with the market like every till price;
         * at neutral this is the bare formula value. */
        return d ? s_sc_market_quote(d->price, 0,
                                     s_sc_game->state.economy.market_index)
                 : 0;
    }
    d = sc_card_get_def(id);
    if (!d || d->type == CARD_TYPE_SPECIAL || d->price == 0) return 0;
    ic_card_mat_wpn_pub(id, &mat, &wpn, &special);
    if (special || mat > 4) return 0;
    /* Biased base first (same 8-bit saturating shape as the quote
     * bodies, floored at 1 so the cut below stays ≥ 1): the cut must
     * sit under the BUY quote's base, not the raw price. */
    if (d->price_bias >= 0) {
        base = (uint8_t)(d->price + d->price_bias);
        if (base < d->price) base = 255;
    } else {
        uint8_t neg = (uint8_t)(0 - d->price_bias);
        base = (neg >= d->price) ? 1 : (uint8_t)(d->price - neg);
    }
    /* n = base × (5−tier) via repeated addition (k = 5..1). */
    k = (uint8_t)(5 - mat);
    n = 0;
    while (k--) n = (uint16_t)(n + base);
    /* q = n/6 via repeated subtraction. */
    q = 0;
    while (n >= 6) {
        n = (uint16_t)(n - 6);
        q++;
    }
    q = q ? q : 1;
    /* Market scale (docs/economy.md §4): the SELL tab and its quote
     * body share this, so the row shows what the till pays. */
    return s_sc_market_quote(q, 0,
                             s_sc_game->state.economy.market_index);
}

/* Banked row quote for the fixed-side BUY/SELL apply (shop_screen.c):
 * g_bk_ptr_a = Game*, g_bk_byte_a = VISIBLE row.  Resolves the row
 * through the active tab's map and stages the verdict:
 * g_bk_byte_b = price (0 = refuse), g_bk_byte_c = card id,
 * g_bk_byte_d = owned-after (SELL only, for CARD_SOLD telemetry).
 * BUY rows quote the market-scaled catalog price (0 = free basic,
 * always affordable); SELL rows the sc_sell_price value after the
 * decked-backing guard (a fully decked card refuses: selling it would
 * strand the deck).  WRAM-direct scans only (banked bodies must never
 * call fixed-bank functions); the fixed side applies blindly. */
void shop_row_quote_banked(void)
{
    Game *g = (Game *)g_bk_ptr_a;
    uint8_t row = g_bk_byte_a;
    CardId id = CARD_NONE;
    const CardDefinition *d;
    uint8_t i;
    uint8_t owned = 0;
    uint8_t in_deck = 0;
    g_bk_byte_b = 0;
    g_bk_byte_c = CARD_NONE;
    g_bk_byte_d = 0;
    s_sc_game = g;
    if (!g) return;
    s_sc_shop_def = sc_shop_active(g);
    if (!s_sc_shop_def || row >= g_shop_vis_count) return;
    if (g->item_menu_tab == SHOP_TAB_SELL) {
        uint8_t e = g_shop_vis_map[row];
        if (e >= g->state.cards.collection.count) return;
        id = g->state.cards.collection.entries[e].id;
        owned = g->state.cards.collection.entries[e].count;
        for (i = 0; i < g->state.cards.deck.count; i++) {
            if (g->state.cards.deck.cards[i] == id) in_deck++;
        }
        if (owned <= in_deck) return;
        g_bk_byte_b = sc_sell_price(id);
        if (!g_bk_byte_b) return;
        g_bk_byte_c = id;
        g_bk_byte_d = (uint8_t)(owned - 1);
    } else {
        uint8_t s = g_shop_vis_map[row];
        if (s >= s_sc_shop_def->count) return;
        id = s_sc_shop_def->items[s];
        d = sc_card_get_def(id);
        if (!d) return;
        /* Market-scaled BUY price (docs/economy.md §4): the till charges
         * what the row shows (SELL rows scale the same way inside
         * sc_sell_price). */
        g_bk_byte_b = s_sc_market_quote(d->price, d->price_bias,
                                        s_sc_game->state.economy.market_index);
        g_bk_byte_c = id;
    }
}

/* Visible-map rebuild: the one-copy stock shows a line only while the
 * player owns no copy (the scan mirrors collection_find in rpg/deck.c
 * since banked bodies cannot call the fixed-bank counter) -- EXCEPT
 * price-0 basics (free starter rebuys: wood sword/shield, bronze fire
 * sword), which are always listed and capped by max_copies at buy
 * time, so the same starter set can be rebought but never infinitely
 * stockpiled.  Selling the last copy of a one-copy card re-opens its
 * row.  Remap runs on every full redraw (entry, purchase), the only
 * moments the list can change, so the cursor never reads a stale
 * map.  Clamps the cursor into the visible list (a purchase can hide
 * the row under it) before painting. */
static void sc_shop_remap_buy(void)
{
    uint8_t i;
    uint8_t e;
    uint8_t owned;
    g_shop_vis_count = 0;
    if (!s_sc_game || !s_sc_shop_def) return;
    for (i = 0; i < s_sc_shop_def->count; i++) {
        const CardDefinition *sdef = sc_card_get_def(s_sc_shop_def->items[i]);
        owned = 0;
        for (e = 0; e < s_sc_game->state.cards.collection.count; e++) {
            if (s_sc_game->state.cards.collection.entries[e].id == s_sc_shop_def->items[i]) {
                owned = s_sc_game->state.cards.collection.entries[e].count;
                break;
            }
            /* Identity, not row id: a loot twin already owned hides the
             * one-copy row even though no curated row exists yet to
             * merge it under (docs/loot.md §34.6). */
            if (sc_row_matches_stock(sdef, s_sc_game->state.cards.collection.entries[e].id)) {
                owned = s_sc_game->state.cards.collection.entries[e].count;
                break;
            }
        }
        /* Free basics (price 0, non-SPECIAL) are always listed; rares
         * hide once owned. */
        if (!owned || (sdef && sdef->price == 0 &&
                       sdef->type != CARD_TYPE_SPECIAL)) {
            g_shop_vis_map[g_shop_vis_count] = i;
            g_shop_vis_count++;
        }
    }
    if (s_sc_game->item_menu_index >= g_shop_vis_count) {
        s_sc_game->item_menu_index = g_shop_vis_count > 0 ? (uint8_t)(g_shop_vis_count - 1) : 0;
    }
    if (s_sc_game->item_menu_scroll > s_sc_game->item_menu_index) {
        s_sc_game->item_menu_scroll = s_sc_game->item_menu_index;
    }
    if (g_shop_vis_count == 0) {
        s_sc_game->item_menu_scroll = 0;
    }
}

/* SELL-tab remap: the map holds COLLECTION positions of owned,
 * sellable rows (sc_sell_price > 0) -- quest items and free basics
 * never list.  Only at a buying merchant (buys flag); elsewhere the
 * tab is empty.  Same cursor-clamp contract as the BUY remap. */
static void sc_shop_remap_sell(void)
{
    uint8_t e;
    g_shop_vis_count = 0;
    if (!s_sc_game || !s_sc_shop_def) return;
    if (s_sc_game->item_menu_tab == SHOP_TAB_SELL && s_sc_shop_def->buys) {
        for (e = 0; e < s_sc_game->state.cards.collection.count; e++) {
            if (sc_sell_price(
                    s_sc_game->state.cards.collection.entries[e].id)) {
                g_shop_vis_map[g_shop_vis_count] = e;
                g_shop_vis_count++;
            }
        }
    }
    if (s_sc_game->item_menu_index >= g_shop_vis_count) {
        s_sc_game->item_menu_index = g_shop_vis_count > 0 ? (uint8_t)(g_shop_vis_count - 1) : 0;
    }
    if (s_sc_game->item_menu_scroll > s_sc_game->item_menu_index) {
        s_sc_game->item_menu_scroll = s_sc_game->item_menu_index;
    }
    if (g_shop_vis_count == 0) {
        s_sc_game->item_menu_scroll = 0;
    }
}

static void shop_rider_draw(void)
{
    uint8_t k;
    uint8_t tile, slot;
    volatile uint8_t *re;

    /* Entries 1-12: shop rows 1-10 plus the DECK-only 11-12 (stale
     * card riders must not survive entering the shop, so out-of-range
     * entries hide). */
    for (k = 0; k < 12; k++) {
        re = (volatile uint8_t *)(0xC000u + ((uint16_t)(1 + k) << 2));
        tile = 0;
        slot = 0;
        if (s_sc_shop_def != 0 &&
            (uint8_t)(s_sc_shop_scroll + k) < g_shop_vis_count) {
            if (s_sc_game &&
                s_sc_game->item_menu_tab == SHOP_TAB_SELL) {
                CardId sell_id = s_sc_game->state.cards.collection.entries[
                    g_shop_vis_map[(uint8_t)(s_sc_shop_scroll + k)]].id;
                s_sc_card_def = loot_is_loot_id(sell_id) ?
                    ic_loot_synth(sell_id) : sc_card_get_def(sell_id);
            } else {
                s_sc_card_def = sc_card_get_def(
                    s_sc_shop_def->items[g_shop_vis_map[(uint8_t)(s_sc_shop_scroll + k)]]);
            }
            if (s_sc_card_def != 0) {
                shop_rider_tile(s_sc_card_def->status_id, &tile, &slot);
                if (tile == 0 &&
                    (s_sc_card_def->battle_type == BATTLE_CARD_TYPE_HEAL ||
                     s_sc_card_def->effect == CARD_EFFECT_HEAL_HP)) {
                    tile = RIDER_TILE_HEALTH;
                    slot = RIDER_OBJ_HEALTH;
                }
            }
        }
        if (tile) {
            re[0] = (uint8_t)(((5 + k) << 3) + 16);
            re[1] = (uint8_t)((1 << 3) + 8);
            re[2] = tile;
            re[3] = slot;
        } else {
            re[0] = 0;
            re[2] = 0;
        }
    }
}

void shop_content_render(void)
{
    s_sc_game = (Game *)g_bk_ptr_a;

    if (!s_sc_game) return;
    /* Live market index, read once: every row's quote shares it
     * (s_sc_market_quote takes it as a param so the per-row path pays
     * no struct read).  Trusted 24-48 like every other reader. */
    s_sc_idx = s_sc_game->state.economy.market_index;
    s_sc_shop_def = sc_shop_active(s_sc_game);
    if (s_sc_game->item_menu_tab == SHOP_TAB_SELL)
        sc_shop_remap_sell();
    else
        sc_shop_remap_buy();
    s_sc_gold = (uint16_t)s_sc_game->state.currency.amount[0];

    sc_draw_text(0, 3, "  GOLD: ", 8);
    sc_format_uint(s_sc_gold, s_sc_str);
    sc_draw_text(8, 3, s_sc_str, 10);

    /* Tab header (row 4): BUY | SELL with a '>' caret before the
     * active tab.  LEFT/RIGHT flips (fixed-side, caret repaint via
     * full redraw). */
    sc_draw_text(1, 4, "BUY", 3);
    sc_draw_text(6, 4, "SELL", 4);
    if (s_sc_game->item_menu_tab == SHOP_TAB_SELL) {
        sc_put_char(0, 4, ' ');
        sc_put_char(5, 4, '>');
    } else {
        sc_put_char(0, 4, '>');
        sc_put_char(5, 4, ' ');
    }

    if (!s_sc_shop_def) {
        sc_draw_text(0, 5, "(nothing)", 9);
        sc_draw_text(0, 7, "[B] Leave", 9);
        shop_rider_draw();
        return;
    }

    if (g_shop_vis_count == 0) {
        if (s_sc_game->item_menu_tab == SHOP_TAB_SELL)
            sc_draw_text(0, 5, "(nothing to sell)", 17);
        else
            sc_draw_text(0, 5, "(out of stock)", 14);
        sc_draw_text(0, 7, "[B] Leave", 9);
        shop_rider_draw();
        return;
    }

    s_sc_shop_scroll = s_sc_game->item_menu_scroll;
    for (s_sc_shop_pos = s_sc_shop_scroll;
         s_sc_shop_pos < g_shop_vis_count &&
         s_sc_shop_pos < (uint8_t)(s_sc_shop_scroll + SHOP_VISIBLE);
         s_sc_shop_pos++) {
        s_sc_stock_pos = g_shop_vis_map[s_sc_shop_pos];
        /* SELL rows resolve through the owned collection (loot ids
         * synthesize through the shared bank-2 synth); BUY rows
         * through the stock list. */
        if (s_sc_game->item_menu_tab == SHOP_TAB_SELL) {
            CardId sell_id = s_sc_game->state.cards.collection.entries[
                s_sc_stock_pos].id;
            s_sc_card_def = loot_is_loot_id(sell_id) ?
                ic_loot_synth(sell_id) : sc_card_get_def(sell_id);
        } else {
            s_sc_card_def = sc_card_get_def(
                s_sc_shop_def->items[s_sc_stock_pos]);
        }
        s_sc_y = (uint8_t)(5 + (uint8_t)(s_sc_shop_pos - s_sc_shop_scroll));
        sc_put_char(0, s_sc_y, (s_sc_game->item_menu_index == s_sc_shop_pos) ? '>' : ' ');
        if (s_sc_card_def) {
            /* Icon tiles mirror the battle hand (screens/cards_skin.json):
             * weapon icon only.  Element rider OAM is painted per row by
             * shop_rider_draw below (over the col-1 elem cell): the cell
             * itself stays blank and the element also reads from the
             * color span below (FIRE/POISON/ICE slots). */
            s_sc_tile_elem = 0;

            if (s_sc_card_def->battle_type == BATTLE_CARD_TYPE_HEAL || s_sc_card_def->effect == CARD_EFFECT_HEAL_HP)
                s_sc_tile_wpn = UI_TILE_CARD_RING;
            else if (s_sc_card_def->battle_type == BATTLE_CARD_TYPE_SHIELD)
                s_sc_tile_wpn = UI_TILE_CARD_SHIELD;
            else if (s_sc_card_def->battle_type == BATTLE_CARD_TYPE_BOW)
                s_sc_tile_wpn = UI_TILE_CARD_BOW;
            else if (s_sc_card_def->battle_type == BATTLE_CARD_TYPE_DAGGER)
                s_sc_tile_wpn = UI_TILE_CARD_DAGGER;
            else
                s_sc_tile_wpn = UI_TILE_CARD_SWORD;

            /* Full "<MAT> <WPN>" name in cols 3..16 (docs/loot.md
             * §34.1), same as the CARDS list -- the old 3-letter code
             * is gone. */
            sc_draw_full_name(3, s_sc_y, s_sc_card_def);

            s_sc_dst = (volatile uint8_t *)(0x9800 + ((uint16_t)s_sc_y << 5) + 1);
            VBK_REG = 0;
            sc_vram_sync_write(s_sc_dst, s_sc_tile_elem);
            sc_vram_sync_write(s_sc_dst + 1, s_sc_tile_wpn);

            /* Icon palettes mirror the battle-hand display slots
             * (generated card_display_slots.json: weapons all WOOD/5,
             * fire 1, ice 2, poison 4): the element cell takes its
             * element slot; the weapon cell takes the FIELD slot
             * (fight3 white background, menu only -- battle keeps the
             * wood slot).  Code/price text stays on the default paper
             * slot.  Bank-2 bodies cannot read the bank-4 skin const,
             * so the skin mapping is mirrored here -- keep in sync
             * with cards_skin.json. */
            if (s_sc_card_def->status_id == STATUS_BURN)
                sc_color_span(1, s_sc_y, 1, UI_COLOR_FIRE);
            else if (s_sc_card_def->status_id == STATUS_POISON)
                sc_color_span(1, s_sc_y, 1, UI_COLOR_POISON);
            else if (s_sc_card_def->status_id == STATUS_FREEZE)
                sc_color_span(1, s_sc_y, 1, UI_COLOR_ICE);
            sc_color_span(2, s_sc_y, 1, UI_COLOR_FIELD);
        } else {
            sc_draw_text(1, s_sc_y, "???", 3);
        }
        /* Price right-aligned ending col 18 ('G' at col 19): BUY rows
         * show the market-scaled stock price (free basics read FREE
         * instead of 0G),
         * SELL rows the rarity-cut value from sc_sell_price.  All cells
         * write every row, so a shorter price never leaves a longer
         * one's tail. */
        if (s_sc_game->item_menu_tab == SHOP_TAB_SELL)
            s_sc_pos = sc_sell_price(s_sc_card_def ? s_sc_card_def->id : CARD_NONE);
        else
            s_sc_pos = s_sc_card_def ? s_sc_market_quote(s_sc_card_def->price, s_sc_card_def->price_bias, s_sc_idx) : 0;
        if (s_sc_pos == 0 &&
            s_sc_game->item_menu_tab == SHOP_TAB_BUY) {
            sc_draw_text(16, s_sc_y, "FREE", 4);
        } else {
            sc_format_uint((uint16_t)s_sc_pos, s_sc_str);
            s_sc_pos = 0;
            while (s_sc_str[s_sc_pos] != 0) s_sc_pos++;
            if (s_sc_pos >= 2) {
                sc_draw_text(17, s_sc_y, s_sc_str, 2);
            } else if (s_sc_pos == 1) {
                sc_put_char(17, s_sc_y, ' ');
                sc_put_char(18, s_sc_y, s_sc_str[0]);
            } else {
                sc_draw_text(17, s_sc_y, "  ", 2);
            }
            sc_put_char(19, s_sc_y, 'G');
        }
    }

    /* Hints always show: the post-shop equip tip moved to a normal
     * dialogue box on exit (docs/loot.md §34.6), so buying no longer
     * changes these lines.  Only failures print a message row: a buy
     * or sell leaves it blank (the row vanishing + gold changing is
     * the confirmation). */
    if (s_sc_game->item_menu_tab == SHOP_TAB_SELL)
        sc_draw_text(0, 16, "[A]Sell [B]Leave", 16);
    else
        sc_draw_text(0, 16, "[A] Buy  [B] Leave", 18);
    if (s_sc_game->shop_message == SC_SHOP_MSG_NO_GOLD) {
        sc_draw_text(0, 17, "Not enough!", 12);
    } else if (s_sc_game->shop_message == SC_SHOP_MSG_MAX_COPIES) {
        sc_draw_text(0, 17, "Too many!", 12);
    } else {
        sc_draw_text(0, 17, NULL, 12);
    }
    shop_rider_draw();
}

/* save_load_content_render now lives in save_load_banked.c (ROM bank 11);
 * the fixed wrapper in save_load_screen.c dispatches there directly. */
