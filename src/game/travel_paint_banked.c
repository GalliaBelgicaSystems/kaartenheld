#include "travel.h"
#include "game.h"
#include "screen.h"
#include "banked.h"
#include "ui.h"
#include "rpg/state.h"
#include "game_ids.h"

/* ── Travel-demo journey paint (travel-demo ROM only, bank 9) ──────
 * The fixed bank cannot host the travel screens (AGENTS.md 52.18: the
 * travel slice overflows _CODE into bank 2).  This banked body owns the
 * whole journey paint behind one thin fixed-bank wrapper
 * (travel_screen_render stages nothing -- the body reads its inputs
 * straight from WRAM + same-bank ROM, then the wrapper validates the
 * render cache).
 *
 * Bank discipline (AGENTS.md 52.11.1/52.11.3): self-contained -- WRAM
 * reads (g_game state, travel staging bytes, ROM tables in THIS bank),
 * raw VRAM writes only.  Never calls a fixed-bank function (ui_*,
 * menu_*, currency_*, game_* are all fixed): text goes out through a
 * local diff-skip writer plus BANKED_VRAM_WRITE (bounded Mode wait +
 * per-access di, LCD-off fast path), mirroring screen_content.c.
 */

#pragma bank 11

/* File-static scratch (WRAM, never stack: AGENTS.md 52.14). */
static char s_line[21];
static uint8_t s_i;
static uint8_t s_n;
static uint8_t s_k;
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

static void tp_sync_write(volatile uint8_t *dst, uint8_t tile)
{
    BANKED_VRAM_WRITE(dst, tile);
}

/* Diff-skipping text row with space padding (ui_draw_text_line shape). */
static void tp_text(uint8_t x, uint8_t y, const char *text, uint8_t max_chars)
{
    char ch;
    uint8_t ended;
    if (y >= 18 || x >= 20) return;
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

static void tp_hline(uint8_t y, char ch)
{
    uint8_t x;
    volatile uint8_t *vram;
    if (y >= 18) return;
    vram = (volatile uint8_t *)(0x9800 + ((uint16_t)y << 5));
    VBK_REG = 0;
    for (x = 0; x < 20; x++) {
        if (g_ui_screen_buf[y][x] != ch) {
            tp_sync_write(&vram[x],
                          (uint8_t)(ui_font_tile_base + (uint8_t)(ch - ' ')));
            g_ui_screen_buf[y][x] = ch;
        }
    }
}

/* Full clear + centered title + separator (menu_draw_frame shape). */
static void tp_frame(const char *title)
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
    tp_hline(1, '-');
}

static void tp_gold(void)
{
    int16_t gold;
    uint8_t n;
    gold = g_game.state.currency.amount[CURRENCY_ID_GOLD - 1];
    s_line[0] = 'G';
    s_line[1] = 'O';
    s_line[2] = 'L';
    s_line[3] = 'D';
    s_line[4] = ' ';
    if (gold < 0) gold = 0;
    n = tp_u16((uint16_t)gold, &s_line[5]);
    (void)n;
    tp_text(2, 6, s_line, 20);
}

static void tp_days(void)
{
    int16_t day;
    day = g_game.state.variables.values[VARIABLE_ID_DAY - 1];
    s_line[0] = 'D';
    s_line[1] = 'A';
    s_line[2] = 'Y';
    s_line[3] = 'S';
    s_line[4] = ' ';
    if (day < 0) day = 0;
    tp_u16((uint16_t)day, &s_line[5]);
    if (day == 0) {
        tp_text(2, 7, s_line, 9);
        tp_text(11, 7, "LATE!", 20);
    } else {
        tp_text(2, 7, s_line, 20);
    }
}

static void tp_supplies(void)
{
    int16_t food;
    int16_t water;
    food = g_game.state.variables.values[VARIABLE_ID_FOOD - 1];
    water = g_game.state.variables.values[VARIABLE_ID_WATER - 1];
    if (food < 0) food = 0;
    if (water < 0) water = 0;
    s_line[0] = 'F';
    s_line[1] = 'O';
    s_line[2] = 'O';
    s_line[3] = 'D';
    s_line[4] = ' ';
    s_n = tp_u16((uint16_t)food, &s_line[5]);
    s_n = (uint8_t)(5 + s_n);
    if (food == 0) {
        s_line[s_n] = '!';
        s_n++;
    }
    s_line[s_n] = ' ';
    s_n++;
    s_line[s_n] = 'W';
    s_line[(uint8_t)(s_n + 1)] = 'A';
    s_line[(uint8_t)(s_n + 2)] = 'T';
    s_line[(uint8_t)(s_n + 3)] = 'E';
    s_line[(uint8_t)(s_n + 4)] = 'R';
    s_line[(uint8_t)(s_n + 5)] = ' ';
    s_n = (uint8_t)(s_n + 6);
    s_n = (uint8_t)(s_n + tp_u16((uint16_t)water, &s_line[s_n]));
    if (water == 0) {
        s_line[s_n] = '!';
        s_n++;
    }
    s_line[s_n] = '\0';
    tp_text(2, 8, s_line, 20);
}

/* "HP 15/15" (hero pool, so the REST heal is visible). */
static void tp_hp(void)
{
    uint8_t hp;
    uint8_t max_hp;
    uint8_t n;
    hp = g_game.state.party.members[0].hp;
    max_hp = g_game.state.party.members[0].max_hp;
    s_line[0] = 'H';
    s_line[1] = 'P';
    s_line[2] = ' ';
    n = tp_u16(hp, &s_line[3]);
    n = (uint8_t)(3 + n);
    s_line[n] = '/';
    n++;
    n = (uint8_t)(n + tp_u16(max_hp, &s_line[n]));
    s_line[n] = '\0';
    tp_text(2, 4, s_line, 20);
}

static void tp_step(void)
{
    uint8_t n;
    uint8_t m;
    uint8_t done;    done = (uint8_t)(g_travel_total - g_travel_left);
    s_line[0] = 'S';
    s_line[1] = 'T';
    s_line[2] = 'E';
    s_line[3] = 'P';
    s_line[4] = ' ';
    n = tp_u16(done, &s_line[5]);
    m = (uint8_t)(5 + n);
    s_line[m] = ' ';
    s_line[(uint8_t)(m + 1)] = 'O';
    s_line[(uint8_t)(m + 2)] = 'F';
    s_line[(uint8_t)(m + 3)] = ' ';
    tp_u16(g_travel_total, &s_line[(uint8_t)(m + 4)]);
    tp_text(2, 5, s_line, 20);
}

/* Ambient weather name (same vocabulary as travel.h; literals live in
 * this bank, so no cross-bank pointer table). */
static const char *tp_wx_name(uint8_t w)
{
    if (w == TRAVEL_WEATHER_CLEAR) return "CLEAR";
    if (w == TRAVEL_WEATHER_RAIN) return "RAIN";
    if (w == TRAVEL_WEATHER_STORM) return "STORM";
    if (w == TRAVEL_WEATHER_HEAT) return "HEAT";
    if (w == TRAVEL_WEATHER_COLD) return "COLD";
    if (w == TRAVEL_WEATHER_WIND) return "WIND";
    if (w == TRAVEL_WEATHER_SNOW) return "SNOW";
    if (w == TRAVEL_WEATHER_FOG) return "FOG";
    return "";
}

/* Route line with the active weather appended ("FIELD ROAD STORM").
 * Truncated at the 20-col row; clear days show the road alone. */
static void tp_route(void)
{
    uint8_t n;
    uint8_t k;
    const char *wx;
    n = 0;
    while (n < 20 && s_route.name[n] != '\0') {
        s_line[n] = s_route.name[n];
        n++;
    }
    if (g_travel_weather != TRAVEL_WEATHER_NONE) {
        wx = tp_wx_name(g_travel_weather);
        if (n < 19) {
            s_line[n] = ' ';
            n++;
        }
        k = 0;
        while (n < 20 && wx[k] != '\0') {
            s_line[n] = wx[k];
            n++;
            k++;
        }
    }
    s_line[n] = '\0';
    tp_text(2, 3, s_line, 20);
}

static void tp_method_duration(void)
{
    uint8_t steps;
    steps = s_route.methods[g_travel_choice].steps;
    if (steps == 0) {
        tp_text(2, 5, "INSTANT", 20);
    } else {
        tp_u16(steps, s_line);
        s_n = 0;
        while (s_line[s_n] != '\0') s_n++;
        s_line[s_n] = ' ';
        s_line[(uint8_t)(s_n + 1)] = 'S';
        s_line[(uint8_t)(s_n + 2)] = 'T';
        s_line[(uint8_t)(s_n + 3)] = 'E';
        s_line[(uint8_t)(s_n + 4)] = 'P';
        s_line[(uint8_t)(s_n + 5)] = 'S';
        s_line[(uint8_t)(s_n + 6)] = '\0';
        tp_text(2, 5, s_line, 20);
    }
}

/* Append one signed delta ("+15G", "-2F") with a unit char. */
static void tp_delta_part(int16_t v, char unit)
{
    uint16_t mag;
    if (v == 0) return;
    if (s_n != 0) {
        s_line[s_n] = ' ';
        s_n = (uint8_t)(s_n + 1);
    }
    if (v < 0) {
        s_line[s_n] = '-';
        mag = (uint16_t)(0 - v);
    } else {
        s_line[s_n] = '+';
        mag = (uint16_t)v;
    }
    s_n = (uint8_t)(s_n + 1);
    s_n = (uint8_t)(s_n + tp_u16(mag, &s_line[s_n]));
    s_line[s_n] = unit;
    s_n = (uint8_t)(s_n + 1);
}

/* Append net steps ("+1 STEP", "-2 STEPS"). */
static void tp_delta_steps(int16_t net)
{
    uint16_t mag;
    if (net == 0) return;
    if (s_n != 0) {
        s_line[s_n] = ' ';
        s_n = (uint8_t)(s_n + 1);
    }
    if (net < 0) {
        s_line[s_n] = '-';
        mag = (uint16_t)(0 - net);
    } else {
        s_line[s_n] = '+';
        mag = (uint16_t)net;
    }
    s_n = (uint8_t)(s_n + 1);
    s_n = (uint8_t)(s_n + tp_u16(mag, &s_line[s_n]));
    s_line[s_n] = ' ';
    s_line[(uint8_t)(s_n + 1)] = 'S';
    s_line[(uint8_t)(s_n + 2)] = 'T';
    s_line[(uint8_t)(s_n + 3)] = 'E';
    s_line[(uint8_t)(s_n + 4)] = 'P';
    s_n = (uint8_t)(s_n + 5);
    if (mag != 1) {
        s_line[s_n] = 'S';
        s_n = (uint8_t)(s_n + 1);
    }
}

/* Append net days ("+1 DAY", "-2 DAYS"). */
static void tp_delta_days(int16_t net)
{
    uint16_t mag;
    if (net == 0) return;
    if (s_n != 0) {
        s_line[s_n] = ' ';
        s_n = (uint8_t)(s_n + 1);
    }
    if (net < 0) {
        s_line[s_n] = '-';
        mag = (uint16_t)(0 - net);
    } else {
        s_line[s_n] = '+';
        mag = (uint16_t)net;
    }
    s_n = (uint8_t)(s_n + 1);
    s_n = (uint8_t)(s_n + tp_u16(mag, &s_line[s_n]));
    s_line[s_n] = ' ';
    s_line[(uint8_t)(s_n + 1)] = 'D';
    s_line[(uint8_t)(s_n + 2)] = 'A';
    s_line[(uint8_t)(s_n + 3)] = 'Y';
    s_n = (uint8_t)(s_n + 4);
    if (mag != 1) {
        s_line[s_n] = 'S';
        s_n = (uint8_t)(s_n + 1);
    }
}

/* Generic choice-outcome line (gold, food, water, HP, net steps,
 * net days; zero parts skipped, capped at the 18-col row by the
 * diff-skipping writer). */
static void tp_delta_core(int16_t gold, int8_t food, int8_t water,
                          int16_t hp, int16_t steps, int8_t days)
{
    s_n = 0;
    tp_delta_part(gold, 'G');
    tp_delta_part(food, 'F');
    tp_delta_part(water, 'W');
    if (hp != 0) {
        tp_delta_part(hp, 'H');
        s_line[s_n] = 'P';
        s_n = (uint8_t)(s_n + 1);
    }
    tp_delta_steps(steps);
    tp_delta_days(days);
    s_line[s_n] = '\0';
    tp_text(2, 11, s_line, 20);
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
                  opt1 ? e->opt1_food : e->opt2_food,
                  opt1 ? e->opt1_water : e->opt2_water,
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
    tp_delta_core(f->gold, f->food, f->water, hp, steps, f->days);
}

/* "> HELP IGNORE" / "  HELP>IGNORE" (choice options from the row). */
static void tp_options(const TravelEvent *e)
{
    uint8_t i;
    uint8_t k;
    s_line[0] = (g_travel_choice == 0) ? '>' : ' ';
    s_line[1] = ' ';
    i = 0;
    while (i < TRAVEL_OPT_LEN && e->opt1[i] != '\0') {
        s_line[(uint8_t)(2 + i)] = e->opt1[i];
        i = (uint8_t)(i + 1);
    }
    k = (uint8_t)(2 + i);
    s_line[k] = (g_travel_choice == 0) ? ' ' : '>';
    k = (uint8_t)(k + 1);
    i = 0;
    while (i < TRAVEL_OPT_LEN && e->opt2[i] != '\0') {
        s_line[(uint8_t)(k + i)] = e->opt2[i];
        i = (uint8_t)(i + 1);
    }
    s_line[(uint8_t)(k + i)] = '\0';
    tp_text(2, 11, s_line, 20);
}

/* "+8G LOOT!" (ambush victory line). */
static void tp_loot(const TravelEvent *e)
{
    uint8_t n;
    s_line[0] = '+';
    n = tp_u16((uint16_t)e->victory_gold, &s_line[1]);
    n = (uint8_t)(1 + n);
    s_line[n] = 'G';
    s_line[(uint8_t)(n + 1)] = ' ';
    s_line[(uint8_t)(n + 2)] = 'L';
    s_line[(uint8_t)(n + 3)] = 'O';
    s_line[(uint8_t)(n + 4)] = 'O';
    s_line[(uint8_t)(n + 5)] = 'T';
    s_line[(uint8_t)(n + 6)] = '!';
    s_line[(uint8_t)(n + 7)] = '\0';
    tp_text(2, 9, s_line, 20);
}

/* "+1 DAY" (ambush time-cost line). */
static void tp_daycost(const TravelEvent *e)
{
    uint8_t n;
    s_line[0] = '+';
    n = tp_u16(e->victory_days, &s_line[1]);
    n = (uint8_t)(1 + n);
    s_line[n] = ' ';
    s_line[(uint8_t)(n + 1)] = 'D';
    s_line[(uint8_t)(n + 2)] = 'A';
    s_line[(uint8_t)(n + 3)] = 'Y';
    if (e->victory_days == 1) {
        s_line[(uint8_t)(n + 4)] = '\0';
    } else {
        s_line[(uint8_t)(n + 4)] = 'S';
        s_line[(uint8_t)(n + 5)] = '\0';
    }
    tp_text(2, 11, s_line, 20);
}

/* One camp menu row: "> CONTINUE", "  HUNT", ... (IDLE options). */
static void tp_camp(uint8_t idx, uint8_t y, const char *label)
{
    uint8_t i;
    s_line[0] = (idx == g_travel_choice) ? '>' : ' ';
    s_line[1] = ' ';
    i = 0;
    while (label[i] != '\0' && i < 16) {
        s_line[(uint8_t)(2 + i)] = label[i];
        i++;
    }
    s_line[(uint8_t)(2 + i)] = '\0';
    tp_text(2, y, s_line, 20);
}

/* "+N FOOD" (hunt yield, staged in g_travel_amount). */
static void tp_found(void)
{
    uint8_t n;
    s_line[0] = '+';
    n = tp_u16(g_travel_amount, &s_line[1]);
    n = (uint8_t)(1 + n);
    s_line[n] = ' ';
    s_line[(uint8_t)(n + 1)] = 'F';
    s_line[(uint8_t)(n + 2)] = 'O';
    s_line[(uint8_t)(n + 3)] = 'O';
    s_line[(uint8_t)(n + 4)] = 'D';
    s_line[(uint8_t)(n + 5)] = '\0';
    tp_text(2, 11, s_line, 20);
}

/* "+N WATER" (hunt yield, staged in g_travel_water). */
static void tp_drunk(void)
{
    uint8_t n;
    s_line[0] = '+';
    n = tp_u16(g_travel_water, &s_line[1]);
    n = (uint8_t)(1 + n);
    s_line[n] = ' ';
    s_line[(uint8_t)(n + 1)] = 'W';
    s_line[(uint8_t)(n + 2)] = 'A';
    s_line[(uint8_t)(n + 3)] = 'T';
    s_line[(uint8_t)(n + 4)] = 'E';
    s_line[(uint8_t)(n + 5)] = 'R';
    s_line[(uint8_t)(n + 6)] = '\0';
    tp_text(2, 10, s_line, 20);
}

/* "+N STEPS" (hunt/rest detour, staged in g_travel_extra). */
static void tp_detour(void)
{
    uint8_t n;
    s_line[0] = '+';
    n = tp_u16(g_travel_extra, &s_line[1]);
    n = (uint8_t)(1 + n);
    s_line[n] = ' ';
    s_line[(uint8_t)(n + 1)] = 'S';
    s_line[(uint8_t)(n + 2)] = 'T';
    s_line[(uint8_t)(n + 3)] = 'E';
    s_line[(uint8_t)(n + 4)] = 'P';
    if (g_travel_extra == 1) {
        s_line[(uint8_t)(n + 5)] = '\0';
    } else {
        s_line[(uint8_t)(n + 5)] = 'S';
        s_line[(uint8_t)(n + 6)] = '\0';
    }
    tp_text(2, 12, s_line, 20);
}

/* "+N HP" (rest heal, staged in g_travel_amount). */
static void tp_healed(void)
{
    uint8_t n;
    s_line[0] = '+';
    n = tp_u16(g_travel_amount, &s_line[1]);
    n = (uint8_t)(1 + n);
    s_line[n] = ' ';
    s_line[(uint8_t)(n + 1)] = 'H';
    s_line[(uint8_t)(n + 2)] = 'P';
    s_line[(uint8_t)(n + 3)] = '\0';
    tp_text(2, 11, s_line, 20);
}

/* "-N STEPS" (study shortcut, staged in g_travel_amount). */
static void tp_shortcut(void)
{
    uint8_t n;
    s_line[0] = '-';
    n = tp_u16(g_travel_amount, &s_line[1]);
    n = (uint8_t)(1 + n);
    s_line[n] = ' ';
    s_line[(uint8_t)(n + 1)] = 'S';
    s_line[(uint8_t)(n + 2)] = 'T';
    s_line[(uint8_t)(n + 3)] = 'E';
    s_line[(uint8_t)(n + 4)] = 'P';
    if (g_travel_amount == 1) {
        s_line[(uint8_t)(n + 5)] = '\0';
    } else {
        s_line[(uint8_t)(n + 5)] = 'S';
        s_line[(uint8_t)(n + 6)] = '\0';
    }
    tp_text(2, 11, s_line, 20);
}

/* One method row: "> CARRIAGE 50G" or "  ON FOOT". */
static void tp_method(uint8_t idx, uint8_t y)
{
    const TravelMethod *m;
    uint8_t k;
    uint8_t i;
    m = &s_route.methods[idx];
    s_line[0] = (idx == g_travel_choice) ? '>' : ' ';
    s_line[1] = ' ';
    i = 0;
    while (i < TRAVEL_NAME_LEN && m->name[i] != '\0') {
        s_line[(uint8_t)(2 + i)] = m->name[i];
        i++;
    }
    k = (uint8_t)(2 + i);
    if (m->cost_gold > 0) {
        s_line[k] = ' ';
        k = (uint8_t)(k + 1);
        k = (uint8_t)(k + tp_u16((uint16_t)m->cost_gold, &s_line[k]));
        s_line[k] = 'G';
        k = (uint8_t)(k + 1);
    }
    s_line[k] = '\0';
    tp_text(2, y, s_line, 20);
}

/* One supply row: "> FOOD 6 2G". */
static void tp_supply(uint8_t idx, uint8_t y, const char *label)
{
    int16_t count;
    uint8_t price;
    uint8_t k;
    uint8_t i;
    const TravelRoute *r;
    r = &s_route;
    if (!label) return;
    count = g_game.state.variables.values[idx == 0 ? VARIABLE_ID_FOOD - 1
                                                   : VARIABLE_ID_WATER - 1];
    price = (idx == 0) ? r->food_price : r->water_price;
    if (count < 0) count = 0;
    s_line[0] = (idx == g_travel_choice) ? '>' : ' ';
    s_line[1] = ' ';
    i = 0;
    while (i < TRAVEL_NAME_LEN && label[i] != '\0') {
        s_line[(uint8_t)(2 + i)] = label[i];
        i++;
    }
    k = (uint8_t)(2 + i);
    s_line[k] = ' ';
    k = (uint8_t)(k + 1);
    k = (uint8_t)(k + tp_u16((uint16_t)count, &s_line[k]));
    s_line[k] = ' ';
    k = (uint8_t)(k + 1);
    k = (uint8_t)(k + tp_u16(price, &s_line[k]));
    s_line[k] = 'G';
    s_line[(uint8_t)(k + 1)] = '\0';
    tp_text(2, y, s_line, 20);
}

/* "NEED 50G!" (premium refusal line, cost from the staged method). */
static void tp_cost(void)
{
    const TravelMethod *m;
    uint8_t n;
    m = &s_route.methods[g_travel_choice];
    s_line[0] = 'N';
    s_line[1] = 'E';
    s_line[2] = 'E';
    s_line[3] = 'D';
    s_line[4] = ' ';
    n = tp_u16((uint16_t)m->cost_gold, &s_line[5]);
    s_line[(uint8_t)(5 + n)] = 'G';
    s_line[(uint8_t)(6 + n)] = '!';
    s_line[(uint8_t)(7 + n)] = '\0';
    tp_text(2, 11, s_line, 20);
}

/* No-arg dispatch body: reads view + staged state straight from WRAM
 * (route/event/fail rows are staged by the fixed loader, so this body
 * runs from any bank).  The fixed wrapper validates the render cache. */
void travel_paint_banked(void)
{
    const TravelRoute *r;
    const TravelEvent *e;
    uint8_t view;
    r = &s_route;
    e = &s_event;
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
    tp_frame("TRAVEL");
    tp_route();
    if (view == TRAVEL_VIEW_METHOD) {
        tp_method_duration();
    } else if (!g_travel_supply) {
        tp_step();
    }
    tp_gold();
    tp_days();
    tp_supplies();
    tp_hp();
    if (view == TRAVEL_VIEW_IDLE) {
        tp_camp(0, 9, "CONTINUE");
        tp_camp(1, 10, "HUNT");
        tp_camp(2, 11, "REST");
        tp_camp(3, 12, "STUDY MAP");
    } else if (view == TRAVEL_VIEW_SUPPLY) {
        tp_supply(0, 9, "FOOD");
        tp_supply(1, 11, "WATER");
        if (g_travel_choice == 2) {
            tp_text(2, 13, "> LEAVE", 20);
        } else {
            tp_text(2, 13, "  LEAVE", 20);
        }
    } else if (view == TRAVEL_VIEW_METHOD) {
        tp_method(0, 9);
        if (r->method_count > 1) {
            tp_method(1, 11);
        }
    } else if (view == TRAVEL_VIEW_AMBUSH) {
        tp_text(2, 9, e->text, 20);
        tp_text(2, 11, "[A] FIGHT", 20);
    } else if (view == TRAVEL_VIEW_HELP) {
        tp_text(2, 9, e->text, 20);
        tp_options(e);
    } else if (view == TRAVEL_VIEW_RUINS) {
        tp_text(2, 9, e->text, 20);
        if (g_travel_choice == 0) {
            tp_text(2, 11, ">EXPLORE IGNORE", 20);
        } else {
            tp_text(2, 11, " EXPLORE>IGNORE", 20);
        }
    } else {
        if (g_travel_msg == TRAVEL_MSG_HELPED) {
            if (g_travel_failed) {
                tp_text(2, 9, s_event_fail.opt1.text, 20);
                tp_fail_delta(1);
            } else {
                tp_text(2, 9, e->text2, 20);
                tp_delta(e, 1);
            }
        } else if (g_travel_msg == TRAVEL_MSG_IGNORED) {
            if (g_travel_failed) {
                tp_text(2, 9, s_event_fail.opt2.text, 20);
                tp_fail_delta(0);
            } else {
                tp_text(2, 9, e->text3, 20);
                tp_delta(e, 0);
            }
        } else if (g_travel_msg == TRAVEL_MSG_WON) {
            tp_loot(e);
            tp_daycost(e);
        } else if (g_travel_msg == TRAVEL_MSG_NOGOLD) {
            tp_text(2, 9, "NOT ENOUGH GOLD.", 20);
            tp_cost();
        } else if (g_travel_msg == TRAVEL_MSG_HUNTED) {
            tp_text(2, 9, "YOU FORAGE.", 20);
            tp_drunk();
            tp_found();
            tp_detour();
        } else if (g_travel_msg == TRAVEL_MSG_RESTED) {
            tp_text(2, 9, "YOU REST.", 20);
            tp_healed();
            tp_detour();
        } else if (g_travel_msg == TRAVEL_MSG_STUDIED) {
            tp_text(2, 9, "YOU STUDY MAP.", 20);
            if (g_travel_amount == 0) {
                tp_text(2, 11, "NO SHORTCUT.", 20);
            } else {
                tp_shortcut();
            }
        } else if (g_travel_msg == TRAVEL_MSG_LOST) {
            tp_text(2, 9, "YOU GET LOST.", 20);
            tp_detour();
        } else {
            tp_text(2, 9, "YOU WAKE UP HURT.", 20);
            tp_text(2, 10, "+1 STEP RECOVERY", 20);
        }
        tp_text(2, 13, "[A] CONTINUE", 20);
    }
}
