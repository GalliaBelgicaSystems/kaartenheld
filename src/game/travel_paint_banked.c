#include "travel.h"
#include "game.h"
#include "screen.h"
#include "banked.h"
#include "ui.h"
#include "rpg/state.h"
#include "game_ids.h"

/* ── Travel-demo journey paint (travel-demo ROM only, bank 11) ─────
 * The fixed bank cannot host the travel screens (AGENTS.md 52.18).
 * This banked body owns the whole journey paint behind the thin
 * wrapper in travel_screen.c.
 *
 * Bank discipline (AGENTS.md 52.11.1/52.11.3): self-contained -- WRAM
 * reads (g_game state, travel staging bytes, s_route/s_event/s_event_fail),
 * raw VRAM writes via BANKED_VRAM_WRITE.  No fixed-bank calls, no di/ei.
 * First entry (same_screen == 0, LCD off) clears VRAM + g_ui_screen_buf;
 * same-screen updates (same_screen == 1, LCD on) diff-skip against
 * g_ui_screen_buf so cursor moves and view switches have zero flash.
 */

#pragma bank 11

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

static void tp_hline(uint8_t y, char ch)
{
    uint8_t x;
    volatile uint8_t *vram;
    if (y >= 18) return;
    s_row_used[y] = 1;
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

/* First-entry full clear + centered title + separator (LCD off). */
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
    tp_hline(1, '-');
}

static void tp_gold(void)
{
    int16_t gold;
    gold = g_game.state.currency.amount[CURRENCY_ID_GOLD - 1];
    if (gold < 0) gold = 0;
    s_n = 0;
    tp_puts("GOLD ");
    tp_put_u16((uint16_t)gold);
    tp_line_done(6);
}

static void tp_days(void)
{
    int16_t day;
    day = g_game.state.variables.values[VARIABLE_ID_DAY - 1];
    if (day < 0) day = 0;
    s_n = 0;
    tp_puts("DAYS ");
    tp_put_u16((uint16_t)day);
    if (day == 0) {
        while (s_n < 9) tp_putc(' ');
        tp_puts("LATE!");
    }
    tp_line_done(7);
}

static void tp_supplies(void)
{
    int16_t prov;
    prov = g_game.state.variables.values[VARIABLE_ID_PROVISIONS - 1];
    if (prov < 0) prov = 0;
    s_n = 0;
    tp_puts("PROV ");
    tp_put_u16((uint16_t)prov);
    if (prov == 0) {
        tp_putc('!');
    }
    tp_line_done(8);
}

/* "HP 15/15" (hero pool, so the REST heal is visible). */
static void tp_hp(void)
{
    s_n = 0;
    tp_puts("HP ");
    tp_put_u16(g_game.state.party.members[0].hp);
    tp_putc('/');
    tp_put_u16(g_game.state.party.members[0].max_hp);
    tp_line_done(4);
}

static void tp_step(void)
{
    uint8_t done;
    done = (uint8_t)(g_travel_total - g_travel_left);
    s_n = 0;
    tp_puts("STEP ");
    tp_put_u16(done);
    tp_puts(" OF ");
    tp_put_u16(g_travel_total);
    tp_line_done(5);
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

/* Route line with the active weather appended ("FIELD ROAD CLEAR"). */
static void tp_route(void)
{
    s_n = 0;
    tp_puts(s_route.name);
    if (g_travel_weather != TRAVEL_WEATHER_NONE) {
        tp_putc(' ');
        tp_puts(tp_wx_name(g_travel_weather));
    }
    tp_line_done(3);
}

static void tp_method_duration(void)
{
    uint8_t steps;
    steps = s_route.methods[g_travel_choice].steps;
    if (steps == 0) {
        tp_text(0, 5, "INSTANT", 20);
    } else {
        s_n = 0;
        tp_put_u16(steps);
        tp_puts(" STEPS");
        tp_line_done(5);
    }
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

/* One camp menu row: "> HUNT", "  REST", ... (IDLE options;
 * walking on is the B button, never a menu row). */
static void tp_camp(uint8_t idx, uint8_t y, const char *label)
{
    s_n = 0;
    tp_putc((idx == g_travel_choice) ? '>' : ' ');
    tp_putc(' ');
    tp_puts(label);
    tp_line_done(y);
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
        tp_frame_init("TRAVEL");
    } else {
        s_row_used[0] = 1;
        s_row_used[1] = 1;
    }
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
        tp_camp(0, 9, "HUNT");
        tp_camp(1, 10, "REST");
        tp_camp(2, 11, "STUDY MAP");
        tp_text(0, 13, "[B] CONTINUE", 20);
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
