#pragma bank 13
#pragma disable_warning 110

#include <gb/gb.h>
#include <gb/cgb.h>
#include "game.h"
#include "ui.h"
#include "banked.h"
#include "game_ids.h"

/* ── HERO-tab content, bank-13 body ────────────────────────────────
 * The hero/wallet summary + hero-status portrait moved out of
 * bank 2 (item_content_banked.c), which sits hard against 16 KB in the
 * debug build (AGENTS.md 52.18/55.5).  It reads no banked tables -- only
 * the staged Game*, always-mapped WRAM and UI tile ids -- so any bank
 * with room works; bank 13 (fresh for the QUEST/GEMS cluster) has it.
 * The bank-2 header/dispatch it left behind keeps DECK only.
 *
 * ABI: the fixed dispatch in item_screen.c sends TAB_DECK to bank 2 and
 * everything else to bank 13's item_gem_render_banked, which forwards
 * TAB_HERO here in-bank (same pattern as the QUEST forward).  Reads the
 * staged Game* (g_bk_ptr_a) and always-mapped WRAM; calls no
 * fixed-bank functions. */

/* Shared draw scratch owned by item_content_banked.c (same note as the
 * QUEST/GEMS bodies): the trampoline is non-reentrant and only one tab
 * renders at a time. */
extern char g_ui_screen_buf[18][21];
extern uint8_t ui_font_tile_base;
extern volatile uint8_t *s_ic_dst;
extern char *s_ic_buf;
extern uint8_t s_ic_txt_i;
extern uint8_t s_ic_arg_x;
extern uint8_t s_ic_arg_y;
extern uint8_t s_ic_arg_len;
extern const char *s_ic_arg_text;

static void st_draw_text_fast(void)
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

#define ST_DRAW_TEXT(x, y, text, len) do { \
    s_ic_arg_x = (x); s_ic_arg_y = (y); s_ic_arg_text = (text); s_ic_arg_len = (len); \
    st_draw_text_fast(); \
} while(0)

/* Tab header: single source of truth is menu_tabs_snippet.h (compiled
 * per bank: banked bodies cannot call across banks, AGENTS.md 52.11.1).
 * HERO cursor col 11. */
#define MENU_TAB_DRAW_TEXT ST_DRAW_TEXT
#include "menu_tabs_snippet.h"

/* Decimal formatter without division (same repeated-subtraction
 * pattern as the bank-2 original: banked bodies must not pull the SDCC
 * div/mod library, AGENTS.md 52.18). */
static void st_format_uint(uint16_t uval, char *out)
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

/* Right-aligned number ending at end_col (6-byte stack scratch: no WRAM
 * growth, AGENTS.md 52.24). */
static void st_draw_rjust(uint8_t y, uint8_t end_col, uint16_t v)
{
    char b[6];
    uint8_t len = 0;
    st_format_uint(v, b);
    while (b[len] != (char)0) len++;
    if (len == 0) len = 1;
    ST_DRAW_TEXT((uint8_t)(end_col - len + 1), y, b, len);
}

/* Icon cell stamp (VRAM tile + buffer space, LCD-on safe). */
static void st_stamp_icon(uint8_t y, uint8_t x, uint8_t tile)
{
    s_ic_buf = &g_ui_screen_buf[y][x];
    *s_ic_buf = ' ';
    s_ic_dst = (volatile uint8_t *)(0x9800u + ((uint16_t)y << 5) + x);
    VBK_REG = 0;
    BANKED_VRAM_WRITE(s_ic_dst, tile);
}

/* Hero-status portrait slot table (bank-13 copy of the bank-11 stream
 * table s_menus_hero_slots: 16 cells, header order row-major from
 * sheet (12,0).  KEEP IN SYNC -- banked bodies cannot share tables
 * across banks (AGENTS.md 52.11.1). */
static const uint8_t s_st_hero_slots[16] = {
    220u, 221u, 222u, 223u, 224u, 225u, 226u, 227u,
    232u, 233u, 234u, 235u, 242u, 243u, 244u, 246u
};

/* OAM sweep for the HERO tab (entries 1-32): the DECK list paints
 * element icons over per-row cells, so entering this tab must hide
 * them (same as the QUEST/GEMS tabs).  WRAM shadow OAM only. */
static void st_actor_hide(uint8_t entry)
{
    volatile uint8_t *re;
    re = (volatile uint8_t *)(0xC000u + ((uint16_t)entry << 2));
    re[0] = 0;
    re[2] = 0;
}

void item_status_render_banked(void)
{
    Game *g = (Game *)g_bk_ptr_a;
    uint8_t e;
    uint8_t hx;
    uint8_t hy;
    if (!g) return;

    /* Direct paint, no pre-blank: unpainted rows blanked below, each
     * painted row covers its full width in one pass, so tab switches
     * never show an all-blank intermediate. */
    ST_DRAW_TEXT(0, 2, "                    ", 20);
    {
        uint8_t by;
        for (by = 8; by <= 14; by++)
            ST_DRAW_TEXT(0, by, "                    ", 20);
    }
    ST_DRAW_TEXT(0, 17, "                    ", 20);

    /* Hide any card art OAM inherited from the DECK list. */
    for (e = 1; e <= 32; e++) {
        st_actor_hide(e);
    }

    menu_draw_tabs(11);

    /* Left block (cols 0-9, or 0-10 when HP/max >= 100): HP/AP/GOLD/DECK
     * with icon cells.  Keep "HP  15/15" + col-9 heart for 2-digit HP,
     * and shift max_hp/heart right by 1 col for 3-digit HP so it never
     * overwrites the '/' at col 6. */
    {
        uint8_t hp = g->state.party.members[0].hp;
        uint8_t max_hp = g->state.party.members[0].max_hp;
        if (hp >= 100 || max_hp >= 100) {
            ST_DRAW_TEXT(0, 3, "HP ", 3);
            st_draw_rjust(3, 5, (uint16_t)hp);
            ST_DRAW_TEXT(6, 3, "/", 1);
            st_draw_rjust(3, 9, (uint16_t)max_hp);
            st_stamp_icon(3, 10, UI_TILE_MENUS_HP_ICON);
            ST_DRAW_TEXT(11, 3, "  ", 2);
        } else {
            ST_DRAW_TEXT(0, 3, "HP  ", 4);
            st_draw_rjust(3, 5, (uint16_t)hp);
            ST_DRAW_TEXT(6, 3, "/", 1);
            st_draw_rjust(3, 8, (uint16_t)max_hp);
            st_stamp_icon(3, 9, UI_TILE_MENUS_HP_ICON);
            ST_DRAW_TEXT(10, 3, "   ", 3);
        }
        /* Right of the HP block (card rows run full width). */
        ST_DRAW_TEXT(17, 3, "   ", 3);
    }
    ST_DRAW_TEXT(0, 4, "AP ", 3);
    ST_DRAW_TEXT(3, 4, "     ", 5);
    ST_DRAW_TEXT(10, 4, "   ", 3);
    ST_DRAW_TEXT(17, 4, "   ", 3);
    st_draw_rjust(4, 8,
        (uint16_t)g->state.variables.values[VARIABLE_ID_ENERGY_POOL - 1]);
    st_stamp_icon(4, 9, UI_TILE_MENUS_AP_ICON);
    ST_DRAW_TEXT(0, 5, "GOLD   ", 7);
    ST_DRAW_TEXT(10, 5, "   ", 3);
    ST_DRAW_TEXT(17, 5, "   ", 3);
    st_draw_rjust(5, 8, (uint16_t)g->state.currency.amount[0]);
    ST_DRAW_TEXT(9, 5, "G", 1);
    ST_DRAW_TEXT(0, 6, "DECK   ", 7);
    ST_DRAW_TEXT(10, 6, "   ", 3);
    ST_DRAW_TEXT(17, 6, "   ", 3);
    st_draw_rjust(6, 8, (uint16_t)g->state.cards.deck.count);
    st_stamp_icon(6, 9, UI_TILE_DECK_ICON);
    /* Shared bow-ammo pool (limited-arrows currency slot): clamped at
     * zero for display, like the shop row.  Tail to full width. */
    {
        int16_t arrows =
            g->state.currency.amount[CURRENCY_ID_ARROWS - 1];
        ST_DRAW_TEXT(0, 7, "ARROWS ", 7);
        st_draw_rjust(7, 8,
            (uint16_t)(arrows < 0 ? 0 : arrows));
        ST_DRAW_TEXT(9, 7, "           ", 11);
    }
    /* Hero-status portrait ("hero status RC", sheet cols 12-15): 4x4
     * at cols 13-16, rows 3-6, in sheet order. */
    for (hy = 0; hy < 4; hy++) {
        for (hx = 0; hx < 4; hx++)
            st_stamp_icon((uint8_t)(3 + hy), (uint8_t)(13 + hx),
                s_st_hero_slots[(uint8_t)((uint8_t)(hy << 2) + hx)]);
    }
    /* Same bottom chrome as the other tabs: row-15 border + B:BACK. */
    for (hx = 0; hx < 20; hx++)
        menu_stamp_chrome(15, hx, UI_TILE_MENUS_BOTTOM_BORDER);
    ST_DRAW_TEXT(0, 16, "B:BACK", 6);
    ST_DRAW_TEXT(6, 16, "              ", 14);
}
