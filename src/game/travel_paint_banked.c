#include "travel.h"
#include "game.h"
#include "screen.h"
#include "banked.h"
#include "ui.h"
#include "rpg/state.h"
#include "game_ids.h"

/* ── Travel-demo journey paint (travel-demo ROM only, bank 13) ─────
 * The fixed bank cannot host the travel screens (AGENTS.md 52.18).
 * Bank 13 (not 11: the paint + chrome no longer fit the bank-11
 * budget next to ow_hud/menu-chrome/save-load). This banked body owns
 * the whole journey paint behind the thin wrapper in travel_screen.c.
 *
 * Bank discipline (AGENTS.md 52.11.1/52.11.3): self-contained -- WRAM
 * reads (g_game state, travel staging bytes, s_route/s_event/s_event_fail),
 * raw VRAM writes via BANKED_VRAM_WRITE.  No fixed-bank calls, no di/ei.
 * First entry (same_screen == 0, LCD off) clears VRAM + g_ui_screen_buf;
 * same-screen updates (same_screen == 1, LCD on) diff-skip against
 * g_ui_screen_buf so cursor moves and view switches have zero flash.
 */

#pragma bank 13

/* File-static scratch (WRAM, never stack: AGENTS.md 52.14). */
static char s_line[21];
static uint8_t s_row_used[18];
static uint8_t s_i;
static uint8_t s_n;
static volatile uint8_t *s_dst;
static char *s_buf;

/* Decimal, subtraction only (no / or % in banked bodies either). */
static uint8_t tp_u16(uint16_t v, char *dst)
{
    static const uint16_t places[] = { 10000, 1000, 100, 10, 1 };
    uint8_t n;
    uint8_t i;
    uint8_t started;
    uint8_t d;
    n = 0;
    started = 0;
    for (i = 0; i < 5; i++) {
        d = 0;
        while (v >= places[i]) {
            v = (uint16_t)(v - places[i]);
            d++;
        }
        if (d != 0 || started != 0 || i == 4) {
            dst[n] = (char)('0' + d);
            n++;
            started = 1;
        }
    }
    dst[n] = '\0';
    return n;
}

static void tp_putc(char ch)
{
    if (s_n < 20) {
        s_line[s_n] = ch;
        s_n++;
    }
}

static void tp_puts(const char *s)
{
    if (!s) return;
    while (*s != '\0' && s_n < 20) {
        s_line[s_n] = *s;
        s_n++;
        s++;
    }
}

static void tp_put_u16(uint16_t v)
{
    char num[6];
    tp_u16(v, num);
    tp_puts(num);
}

static void tp_sync_write(volatile uint8_t *dst, uint8_t tile)
{
    BANKED_VRAM_WRITE(dst, tile);
}

void travel_chrome_load_banked(void);

/* Stamp one chrome tile with diff-skip against the screen buffer.
 * Buffer marker is (idx + 1): never collides with font text (0x20+)
 * or the space clear, so cursor swaps repaint exactly once. */
static void tp_chrome_cell(uint8_t x, uint8_t y, uint8_t idx)
{
    uint8_t id;
    char mark;
    if (x >= 20 || y >= 18) return;
    s_row_used[y] = 1;
    id = (uint8_t)(UI_TILE_TRAVEL_CHROME_BASE + idx);
    mark = (char)(idx + 1);
    VBK_REG = 0;
    s_dst = (volatile uint8_t *)(0x9800 + ((uint16_t)y << 5) + x);
    s_buf = &g_ui_screen_buf[y][x];
    if (*s_buf != mark) {
        tp_sync_write(s_dst, id);
        *s_buf = mark;
    }
}

/* Placeholder scenery wash (rows 2-4, cols 1-18): tan wash with a
 * forest-floor texture row. Real biome strips (dual-tileset staging
 * + palettes) are follow-up work; geometry (origin cols 1-10,
 * destination cols 11-18) already matches the spec split. */
typedef struct {
    uint8_t x;
    uint8_t y;
    uint8_t idx;
} ChromeCell;

/* 0xFE marks the cursor-swappable bottom-middle cell of a button:
 * selected ? CURSOR shaft : plain bottom edge.
 * 0xFF marks a gap cell that must be blanked (space): view switches
 * otherwise leave the previous view's text behind inside button rows.
 * Table indices are TCHROME_* (travel.h) in CSV row-major order. */
#define CHROME_CURSOR_CELL 0xFEu
#define CHROME_BLANK_CELL 0xFFu

static const ChromeCell s_wash_cells[] = {
    { 1, 2, 4 }, { 2, 2, 4 }, { 3, 2, 4 }, { 4, 2, 4 },
    { 5, 2, 4 }, { 6, 2, 4 }, { 7, 2, 4 }, { 8, 2, 4 },
    { 9, 2, 4 }, { 10, 2, 4 }, { 11, 2, 4 }, { 12, 2, 4 },
    { 13, 2, 4 }, { 14, 2, 4 }, { 15, 2, 4 }, { 16, 2, 4 },
    { 17, 2, 4 }, { 18, 2, 4 },
    { 1, 3, 4 }, { 2, 3, 4 }, { 3, 3, 4 }, { 4, 3, 4 },
    { 5, 3, 4 }, { 6, 3, 4 }, { 7, 3, 4 }, { 8, 3, 4 },
    { 9, 3, 4 }, { 10, 3, 4 }, { 11, 3, 4 }, { 12, 3, 4 },
    { 13, 3, 4 }, { 14, 3, 4 }, { 15, 3, 4 }, { 16, 3, 4 },
    { 17, 3, 4 }, { 18, 3, 4 },
    { 1, 4, 29 }, { 2, 4, 30 }, { 3, 4, 31 }, { 4, 4, 29 },
    { 5, 4, 30 }, { 6, 4, 31 }, { 7, 4, 29 }, { 8, 4, 30 },
    { 9, 4, 31 }, { 10, 4, 29 }, { 11, 4, 30 }, { 12, 4, 31 },
    { 13, 4, 29 }, { 14, 4, 30 }, { 15, 4, 31 }, { 16, 4, 29 },
    { 17, 4, 30 }, { 18, 4, 31 }
};

/* Static progress markers (row 5): brown-top origin half (1-9),
 * grey-top destination half (10-18). Reached/upcoming states are
 * wiring-step work (same as the old dot art: static until then). */
static const ChromeCell s_marker_cells[] = {
    { 1, 5, 32 }, { 2, 5, 33 }, { 3, 5, 33 }, { 4, 5, 33 },
    { 5, 5, 34 }, { 6, 5, 33 }, { 7, 5, 33 }, { 8, 5, 34 },
    { 9, 5, 33 }, { 10, 5, 37 }, { 11, 5, 38 }, { 12, 5, 37 },
    { 13, 5, 37 }, { 14, 5, 38 }, { 15, 5, 37 }, { 16, 5, 37 },
    { 17, 5, 37 }, { 18, 5, 39 }
};

/* HUNT button (cols 2-6): bow icon + H/UN/T label. Bow sits at col 4;
 * gap col 1 blanked (button starts at col 2). */
static const ChromeCell s_hunt_cells[] = {
    { 0, 7, 0xFF }, { 1, 7, 0xFF }, { 2, 7, 0 }, { 3, 7, 1 }, { 4, 7, 1 },
    { 5, 7, 1 }, { 6, 7, 2 },
    { 0, 8, 0xFF }, { 1, 8, 0xFF }, { 2, 8, 3 }, { 3, 8, 4 }, { 4, 8, 10 },
    { 5, 8, 4 }, { 6, 8, 5 },
    { 0, 9, 0xFF }, { 1, 9, 0xFF }, { 2, 9, 3 }, { 3, 9, 14 }, { 4, 9, 15 },
    { 5, 9, 16 }, { 6, 9, 5 },
    { 0, 10, 0xFF }, { 1, 10, 0xFF }, { 2, 10, 6 }, { 3, 10, 7 },
    { 4, 10, 0xFE }, { 5, 10, 7 }, { 6, 10, 9 }
};

/* REST button (cols 7-11): centered bed icon + R/ES/T label. Gap col 12
 * blanked for the same reason. */
static const ChromeCell s_rest_cells[] = {
    { 7, 7, 0 }, { 8, 7, 1 }, { 9, 7, 1 }, { 10, 7, 1 },
    { 11, 7, 2 }, { 12, 7, 0xFF },
    { 7, 8, 3 }, { 8, 8, 4 }, { 9, 8, 11 }, { 10, 8, 4 },
    { 11, 8, 5 }, { 12, 8, 0xFF },
    { 7, 9, 3 }, { 8, 9, 17 }, { 9, 9, 18 }, { 10, 9, 19 },
    { 11, 9, 5 }, { 12, 9, 0xFF },
    { 7, 10, 6 }, { 8, 10, 7 }, { 9, 10, 0xFE }, { 10, 10, 7 },
    { 11, 10, 9 }, { 12, 10, 0xFF }
};

/* STUDY MAP button (cols 13-18, 6 wide): glass+map + ST/UD/YM/AP.
 * Gap col 19 blanked. */
static const ChromeCell s_study_cells[] = {
    { 13, 7, 0 }, { 14, 7, 1 }, { 15, 7, 1 }, { 16, 7, 1 },
    { 17, 7, 1 }, { 18, 7, 2 }, { 19, 7, 0xFF },
    { 13, 8, 3 }, { 14, 8, 12 }, { 15, 8, 13 }, { 16, 8, 4 },
    { 17, 8, 4 }, { 18, 8, 5 }, { 19, 8, 0xFF },
    { 13, 9, 3 }, { 14, 9, 20 }, { 15, 9, 21 }, { 16, 9, 22 },
    { 17, 9, 23 }, { 18, 9, 5 }, { 19, 9, 0xFF },
    { 13, 10, 6 }, { 14, 10, 7 }, { 15, 10, 0xFE }, { 16, 10, 7 },
    { 17, 10, 7 }, { 18, 10, 9 }, { 19, 10, 0xFF }
};

/* CONTINUE button (cols 6-13, rows 11-13): B icon + CO/NT/IN/UE.
 * The B row sits on row 12 (top edge above it, bottom edge below).
 * Only stamped where continuing is the B button (camp idle);
 * A-key continues keep their text prompt (no A icon in the sheet).
 * Side cols blanked: SUPPLY/METHOD/MESSAGE text would show through. */
static const ChromeCell s_continue_cells[] = {
    { 0, 11, 0xFF }, { 1, 11, 0xFF }, { 2, 11, 0xFF }, { 3, 11, 0xFF },
    { 4, 11, 0xFF }, { 5, 11, 0xFF },
    { 6, 11, 0 }, { 7, 11, 1 }, { 8, 11, 1 }, { 9, 11, 1 },
    { 10, 11, 1 }, { 11, 11, 1 }, { 12, 11, 1 }, { 13, 11, 2 },
    { 14, 11, 0xFF }, { 15, 11, 0xFF }, { 16, 11, 0xFF },
    { 17, 11, 0xFF }, { 18, 11, 0xFF }, { 19, 11, 0xFF },
    { 0, 12, 0xFF }, { 1, 12, 0xFF }, { 2, 12, 0xFF }, { 3, 12, 0xFF },
    { 4, 12, 0xFF }, { 5, 12, 0xFF },
    { 6, 12, 3 }, { 7, 12, 24 }, { 8, 12, 25 }, { 9, 12, 26 },
    { 10, 12, 27 }, { 11, 12, 28 }, { 12, 12, 4 }, { 13, 12, 5 },
    { 14, 12, 0xFF }, { 15, 12, 0xFF }, { 16, 12, 0xFF },
    { 17, 12, 0xFF }, { 18, 12, 0xFF }, { 19, 12, 0xFF },
    { 0, 13, 0xFF }, { 1, 13, 0xFF }, { 2, 13, 0xFF }, { 3, 13, 0xFF },
    { 4, 13, 0xFF }, { 5, 13, 0xFF },
    { 6, 13, 6 }, { 7, 13, 7 }, { 8, 13, 7 }, { 9, 13, 7 },
    { 10, 13, 7 }, { 11, 13, 7 }, { 12, 13, 7 }, { 13, 13, 9 },
    { 14, 13, 0xFF }, { 15, 13, 0xFF }, { 16, 13, 0xFF },
    { 17, 13, 0xFF }, { 18, 13, 0xFF }, { 19, 13, 0xFF }
};

static void tp_chrome_run(const ChromeCell *cells, uint8_t n, uint8_t selected)
{
    uint8_t i;
    uint8_t idx;
    for (i = 0; i < n; i++) {
        idx = cells[i].idx;
        if (idx == CHROME_CURSOR_CELL) {
            idx = selected ? 8u : 7u;
            tp_chrome_cell(cells[i].x, cells[i].y, idx);
        } else if (idx == CHROME_BLANK_CELL) {
            VBK_REG = 0;
            s_dst = (volatile uint8_t *)(0x9800 + ((uint16_t)cells[i].y << 5) + cells[i].x);
            s_buf = &g_ui_screen_buf[cells[i].y][cells[i].x];
            if (*s_buf != ' ') {
                tp_sync_write(s_dst, ui_font_tile_base);
                *s_buf = ' ';
            }
            s_row_used[cells[i].y] = 1;
        } else {
            tp_chrome_cell(cells[i].x, cells[i].y, idx);
        }
    }
}

static void tp_wash(void)
{
    tp_chrome_run(s_wash_cells, 54, 0);
}

static void tp_markers(void)
{
    tp_chrome_run(s_marker_cells, 18, 0);
}

static void tp_hunt_button(uint8_t selected)
{
    tp_chrome_run(s_hunt_cells, 28, selected);
}

static void tp_rest_button(uint8_t selected)
{
    tp_chrome_run(s_rest_cells, 24, selected);
}

static void tp_study_button(uint8_t selected)
{
    tp_chrome_run(s_study_cells, 28, selected);
}

static void tp_continue_button(void)
{
    tp_chrome_run(s_continue_cells, 60, 0);
}

/* Diff-skipping text row with space padding (ui_draw_text_line shape). */
static void tp_text(uint8_t x, uint8_t y, const char *text, uint8_t max_chars)
{
    char ch;
    uint8_t ended;
    if (y >= 18 || x >= 20) return;
    s_row_used[y] = 1;
    if ((uint8_t)(x + max_chars) > 20) max_chars = (uint8_t)(20 - x);
    VBK_REG = 0;
    ended = (text == NULL);
    s_dst = (volatile uint8_t *)(0x9800 + ((uint16_t)y << 5) + x);
    s_buf = &g_ui_screen_buf[y][x];
    for (s_i = 0; s_i < max_chars; s_i++) {
        if (!ended) {
            ch = text[s_i];
            if (ch == '\0') {
                ended = 1;
                ch = ' ';
            }
        } else {
            ch = ' ';
        }
        if (*s_buf != ch) {
            tp_sync_write(s_dst, (uint8_t)(ui_font_tile_base + (uint8_t)(ch - ' ')));
            *s_buf = ch;
        }
        s_dst++;
        s_buf++;
    }
}

static void tp_line_done(uint8_t y)
{
    s_line[s_n] = '\0';
    tp_text(0, y, s_line, 20);
}

/* First-entry full clear + centered title (LCD off). Row 1 stays
 * empty: no separator line under the title. */
static void tp_frame_init(const char *title)
{
    uint8_t x;
    uint8_t y;
    uint8_t len;
    volatile uint8_t *v;
    VBK_REG = 0;
    for (y = 0; y < 18; y++) {
        v = (volatile uint8_t *)(0x9800 + ((uint16_t)y << 5));
        for (x = 0; x < 20; x++) {
            tp_sync_write(&v[x], ui_font_tile_base);
            g_ui_screen_buf[y][x] = ' ';
        }
        g_ui_screen_buf[y][20] = '\0';
    }
    len = 0;
    if (title) {
        while (title[len] != '\0' && len < 20) len++;
    }
    tp_text((uint8_t)((20 - len) >> 1), 0, title, 20);
}

/* Last row (row 17): empty square, heart, current HP, money with G,
 * DAYS: + days left. "[ ][heart]15 121G DAYS: 180". No max-HP readout.
 * The heart is the boot-loaded HUD icon (UI_TILE_TRAVEL_HEART). */
static void tp_lastrow(void)
{
    int16_t gold;
    int16_t day;
    gold = g_game.state.currency.amount[CURRENCY_ID_GOLD - 1];
    if (gold < 0) gold = 0;
    day = g_game.state.variables.values[VARIABLE_ID_DAY - 1];
    if (day < 0) day = 0;
    VBK_REG = 0;
    s_row_used[17] = 1;
    s_dst = (volatile uint8_t *)(0x9800 + ((uint16_t)17 << 5));
    s_buf = &g_ui_screen_buf[17][0];
    if (*s_buf != ' ') {
        tp_sync_write(s_dst, ui_font_tile_base);
        *s_buf = ' ';
    }
    s_dst++;
    s_buf++;
    if (*s_buf != 'H') {
        tp_sync_write(s_dst, UI_TILE_TRAVEL_HEART);
        *s_buf = 'H';
    }
    s_n = 0;
    tp_put_u16(g_game.state.party.members[0].hp);
    tp_putc(' ');
    tp_put_u16((uint16_t)gold);
    tp_putc('G');
    tp_putc(' ');
    tp_puts("DAYS:");
    tp_putc(' ');
    tp_put_u16((uint16_t)day);
    tp_text(2, 17, s_line, 18);
}

/* Supply stock (row 15): "SUPPLIES: 36". */
static void tp_supplies(void)
{
    int16_t prov;
    prov = g_game.state.variables.values[VARIABLE_ID_PROVISIONS - 1];
    if (prov < 0) prov = 0;
    s_n = 0;
    tp_puts("SUPPLIES: ");
    tp_put_u16((uint16_t)prov);
    if (prov == 0) {
        tp_putc('!');
    }
    tp_line_done(15);
}

/* Append one signed delta ("+15G", "-2P") with a unit char. */
static void tp_delta_part(int16_t v, char unit)
{
    uint16_t mag;
    if (v == 0) return;
    if (s_n != 0) tp_putc(' ');
    if (v < 0) {
        tp_putc('-');
        mag = (uint16_t)(0 - v);
    } else {
        tp_putc('+');
        mag = (uint16_t)v;
    }
    tp_put_u16(mag);
    tp_putc(unit);
}

/* Append net steps ("+1 STEP", "-2 STEPS"). */
static void tp_delta_steps(int16_t net)
{
    uint16_t mag;
    if (net == 0) return;
    if (s_n != 0) tp_putc(' ');
    if (net < 0) {
        tp_putc('-');
        mag = (uint16_t)(0 - net);
    } else {
        tp_putc('+');
        mag = (uint16_t)net;
    }
    tp_put_u16(mag);
    tp_puts(" STEP");
    if (mag != 1) tp_putc('S');
}

/* Append net days ("+1 DAY", "-2 DAYS"). */
static void tp_delta_days(int16_t net)
{
    uint16_t mag;
    if (net == 0) return;
    if (s_n != 0) tp_putc(' ');
    if (net < 0) {
        tp_putc('-');
        mag = (uint16_t)(0 - net);
    } else {
        tp_putc('+');
        mag = (uint16_t)net;
    }
    tp_put_u16(mag);
    tp_puts(" DAY");
    if (mag != 1) tp_putc('S');
}

/* Generic choice-outcome line (gold, provisions, HP, net steps,
 * net days; zero parts skipped, clamped safely at 20 cols). */
static void tp_delta_core(int16_t gold, int8_t prov,
                          int16_t hp, int16_t steps, int8_t days)
{
    s_n = 0;
    tp_delta_part(gold, 'G');
    tp_delta_part(prov, 'P');
    if (hp != 0) {
        tp_delta_part(hp, 'H');
        tp_putc('P');
    }
    tp_delta_steps(steps);
    tp_delta_days(days);
    tp_line_done(11);
}

static void tp_delta(const TravelEvent *e, uint8_t opt1)
{
    int16_t hp;
    int16_t steps;
    hp = (int16_t)((int16_t)(opt1 ? e->opt1_heal : e->opt2_heal) -
                   (int16_t)(opt1 ? e->opt1_hurt : e->opt2_hurt));
    steps = (int16_t)((int16_t)(opt1 ? e->opt1_delay : e->opt2_delay) -
                      (int16_t)(opt1 ? e->opt1_save : e->opt2_save));
    tp_delta_core(opt1 ? e->opt1_gold : e->opt2_gold,
                  opt1 ? e->opt1_prov : e->opt2_prov,
                  hp, steps,
                  (int8_t)(opt1 ? e->opt1_days : e->opt2_days));
}

static void tp_fail_delta(uint8_t opt1)
{
    const TravelEventFailOpt *f;
    int16_t hp;
    int16_t steps;
    f = opt1 ? &s_event_fail.opt1 : &s_event_fail.opt2;
    hp = (int16_t)((int16_t)f->heal - (int16_t)f->hurt);
    steps = (int16_t)((int16_t)f->delay - (int16_t)f->save);
    tp_delta_core(f->gold, f->prov, hp, steps, f->days);
}

/* "> HELP IGNORE" / "  HELP>IGNORE" (choice options from the row). */
static void tp_options(const TravelEvent *e)
{
    s_n = 0;
    tp_putc((g_travel_choice == 0) ? '>' : ' ');
    tp_putc(' ');
    tp_puts(e->opt1);
    tp_putc((g_travel_choice == 0) ? ' ' : '>');
    tp_puts(e->opt2);
    tp_line_done(11);
}

/* "+8G LOOT!" (ambush victory line). */
static void tp_loot(const TravelEvent *e)
{
    s_n = 0;
    tp_putc('+');
    tp_put_u16((uint16_t)e->victory_gold);
    tp_puts("G LOOT!");
    tp_line_done(9);
}

/* "+1 DAY" (ambush time-cost line). */
static void tp_daycost(const TravelEvent *e)
{
    s_n = 0;
    tp_putc('+');
    tp_put_u16(e->victory_days);
    tp_puts(" DAY");
    if (e->victory_days != 1) tp_putc('S');
    tp_line_done(11);
}

/* "+N PROV" (hunt yield, staged in g_travel_amount). */
static void tp_found(void)
{
    s_n = 0;
    tp_putc('+');
    tp_put_u16(g_travel_amount);
    tp_puts(" PROV");
    tp_line_done(11);
}

/* "+N STEPS" (hunt/rest/lost detour, staged in g_travel_extra). */
static void tp_detour(uint8_t y)
{
    s_n = 0;
    tp_putc('+');
    tp_put_u16(g_travel_extra);
    tp_puts(" STEP");
    if (g_travel_extra != 1) tp_putc('S');
    tp_line_done(y);
}

/* "+N HP" (rest heal, staged in g_travel_amount). */
static void tp_healed(void)
{
    s_n = 0;
    tp_putc('+');
    tp_put_u16(g_travel_amount);
    tp_puts(" HP");
    tp_line_done(11);
}

/* "-N STEPS" (study shortcut, staged in g_travel_amount). */
static void tp_shortcut(void)
{
    s_n = 0;
    tp_putc('-');
    tp_put_u16(g_travel_amount);
    tp_puts(" STEP");
    if (g_travel_amount != 1) tp_putc('S');
    tp_line_done(11);
}

/* One method row: "> CARRIAGE 50G" or "  ON FOOT". */
static void tp_method(uint8_t idx, uint8_t y)
{
    const TravelMethod *m;
    m = &s_route.methods[idx];
    s_n = 0;
    tp_putc((idx == g_travel_choice) ? '>' : ' ');
    tp_putc(' ');
    tp_puts(m->name);
    if (m->cost_gold > 0) {
        tp_putc(' ');
        tp_put_u16((uint16_t)m->cost_gold);
        tp_putc('G');
    }
    tp_line_done(y);
}

/* One supply row: "> PROV 6 2G". */
static void tp_supply(uint8_t idx, uint8_t y, const char *label)
{
    int16_t count;
    if (!label) return;
    count = g_game.state.variables.values[VARIABLE_ID_PROVISIONS - 1];
    if (count < 0) count = 0;
    s_n = 0;
    tp_putc((idx == g_travel_choice) ? '>' : ' ');
    tp_putc(' ');
    tp_puts(label);
    tp_putc(' ');
    tp_put_u16((uint16_t)count);
    tp_putc(' ');
    tp_put_u16(s_route.provisions_price);
    tp_putc('G');
    tp_line_done(y);
}

/* "NEED 50G!" (premium refusal line, cost from the staged method). */
static void tp_cost(void)
{
    const TravelMethod *m;
    m = &s_route.methods[g_travel_choice];
    s_n = 0;
    tp_puts("NEED ");
    tp_put_u16((uint16_t)m->cost_gold);
    tp_puts("G!");
    tp_line_done(11);
}

/* No-arg dispatch body: reads same_screen in g_bk_byte_a and staged
 * travel state straight from WRAM.  First entry clears VRAM + buffer;
 * same-screen updates diff-skip and blank unused rows without flashing. */
void travel_paint_banked(void)
{
    const TravelRoute *r;
    const TravelEvent *e;
    uint8_t same_screen;
    uint8_t view;
    uint8_t y;
    same_screen = g_bk_byte_a;
    r = &s_route;
    e = &s_event;
    for (y = 0; y < 18; y++) {
        s_row_used[y] = 0;
    }
    if (g_travel_supply) {
        view = TRAVEL_VIEW_SUPPLY;
    } else if (g_travel_msg != TRAVEL_MSG_NONE) {
        view = TRAVEL_VIEW_MSG;
    } else if (g_travel_method == 0xFF) {
        view = TRAVEL_VIEW_METHOD;
    } else if (g_travel_event == TRAVEL_EVENT_NONE) {
        view = TRAVEL_VIEW_IDLE;
    } else if (e->kind == TRAVEL_EVENT_BATTLE) {
        view = TRAVEL_VIEW_AMBUSH;
    } else if (e->kind == TRAVEL_EVENT_RUINS) {
        view = TRAVEL_VIEW_RUINS;
    } else {
        view = TRAVEL_VIEW_HELP;
    }
    if (!same_screen) {
        tp_frame_init("TRAVELING...");
        travel_chrome_load_banked();
    } else {
        s_row_used[0] = 1;
        s_row_used[1] = 1;
    }
    tp_wash();
    tp_markers();
    tp_supplies();
    tp_lastrow();
    if (view == TRAVEL_VIEW_IDLE) {
        tp_hunt_button((uint8_t)(g_travel_choice == TRAVEL_CAMP_HUNT));
        tp_rest_button((uint8_t)(g_travel_choice == TRAVEL_CAMP_REST));
        tp_study_button((uint8_t)(g_travel_choice == TRAVEL_CAMP_STUDY));
        tp_continue_button();
    } else if (view == TRAVEL_VIEW_SUPPLY) {
        tp_supply(0, 9, "PROV");
        tp_text(0, 13, (g_travel_choice == 1) ? "> LEAVE" : "  LEAVE", 20);
    } else if (view == TRAVEL_VIEW_METHOD) {
        tp_method(0, 9);
        if (r->method_count > 1) {
            tp_method(1, 11);
        }
    } else if (view == TRAVEL_VIEW_AMBUSH) {
        tp_text(0, 9, e->text, 20);
        tp_text(0, 11, "[A] FIGHT", 20);
    } else if (view == TRAVEL_VIEW_HELP) {
        tp_text(0, 9, e->text, 20);
        tp_options(e);
    } else if (view == TRAVEL_VIEW_RUINS) {
        tp_text(0, 9, e->text, 20);
        tp_text(0, 11, (g_travel_choice == 0) ? ">EXPLORE IGNORE" : " EXPLORE>IGNORE", 20);
    } else {
        if (g_travel_msg == TRAVEL_MSG_HELPED) {
            if (g_travel_failed) {
                tp_text(0, 9, s_event_fail.opt1.text, 20);
                tp_fail_delta(1);
            } else {
                tp_text(0, 9, e->text2, 20);
                tp_delta(e, 1);
            }
        } else if (g_travel_msg == TRAVEL_MSG_IGNORED) {
            if (g_travel_failed) {
                tp_text(0, 9, s_event_fail.opt2.text, 20);
                tp_fail_delta(0);
            } else {
                tp_text(0, 9, e->text3, 20);
                tp_delta(e, 0);
            }
        } else if (g_travel_msg == TRAVEL_MSG_WON) {
            tp_loot(e);
            if (e->victory_days > 0) {
                tp_daycost(e);
            }
        } else if (g_travel_msg == TRAVEL_MSG_NOGOLD) {
            tp_text(0, 9, "NOT ENOUGH GOLD.", 20);
            tp_cost();
        } else if (g_travel_msg == TRAVEL_MSG_HUNTED) {
            tp_text(0, 9, "YOU FORAGE.", 20);
            tp_found();
            if (g_travel_extra > 0) tp_detour(12);
        } else if (g_travel_msg == TRAVEL_MSG_RESTED) {
            tp_text(0, 9, "YOU REST.", 20);
            if (g_travel_amount > 0) {
                tp_healed();
            } else {
                tp_text(0, 11, "FULL HP.", 20);
            }
            if (g_travel_extra > 0) tp_detour(12);
        } else if (g_travel_msg == TRAVEL_MSG_STUDIED) {
            tp_text(0, 9, "YOU STUDY MAP.", 20);
            if (g_travel_amount == 0) {
                tp_text(0, 11, "NO SHORTCUT.", 20);
            } else {
                tp_shortcut();
            }
        } else if (g_travel_msg == TRAVEL_MSG_LOST) {
            tp_text(0, 9, "YOU GET LOST.", 20);
            if (g_travel_extra > 0) tp_detour(12);
        }
        tp_text(0, 13, "[A] CONTINUE", 20);
    }
    if (same_screen) {
        for (y = 2; y < 18; y++) {
            if (!s_row_used[y]) {
                tp_text(0, y, "", 20);
            }
        }
    }
}
