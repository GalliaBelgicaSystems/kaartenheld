#pragma bank 11

#include "game.h"
#include "game_ids.h"
#include "banked.h"
#include "travel.h"

/* ── Travel day-burn body, bank-11 ───────────────────────────────
 * The fixed bank sits hard against 0x8000 (AGENTS.md §52.18), so the
 * pure day-burn arithmetic lives here behind the thin fixed-bank
 * wrapper travel_burn(): the wrapper stages the day count and
 * calls in.  Follows the actor_load_banked.c precedent (same-bank
 * table reads, no banked_copy, no fixed-bank calls).
 *
 * Self-contained: WRAM only (g_game state, g_travel_* staging) plus
 * same-bank ROM (nothing read from ROM here at all).  Never calls a
 * fixed-bank function (AGENTS.md §52.11.1): the two tiny variable
 * accessors below are deliberate local replicas of
 * game_variable_get()/game_variable_set() (src/rpg/state.c) -- KEEP
 * IN SYNC with those if their storage layout ever changes.  No
 * telemetry replica: telemetry_emit() compiles out of the
 * travel-demo ROM (no DEBUG_BUILD), so the plain store is identical.
 * No di/ei anywhere (timer-ISR music-clock rule, AGENTS.md §52.11.3).
 */

extern TravelRoute s_route;

static int16_t tb_get(VariableId v)
{
    if (v == 0 || v > MAX_STATE_VARIABLES) return 0;
    return g_game.state.variables.values[v - 1];
}

static void tb_set(VariableId v, int16_t value)
{
    if (v == 0 || v > MAX_STATE_VARIABLES) return;
    g_game.state.variables.values[v - 1] = value;
}

/* Inline xorshift step on the travel stream (the LOOT_RNG_STEP shape
 * from src/rpg/loot.h, owned here so camp rolls never touch the
 * shared gameplay stream).  Seeded once per journey by the fixed
 * wrapper; nonzero seeds can never hit the zero fixed point. */
static void tb_rng_step(void)
{
    g_travel_rng ^= (uint16_t)(g_travel_rng << 7);
    g_travel_rng ^= (uint16_t)(g_travel_rng >> 9);
    g_travel_rng ^= (uint16_t)(g_travel_rng << 8);
}

/* Uniform range roll without % or /: masked rejection-sample a byte
 * under the span, like the fixed-bank pool pick. */
static uint8_t tb_roll(uint8_t lo, uint8_t hi)
{
    uint8_t span;
    uint8_t mask;
    uint8_t r;
    if (hi <= lo) return lo;
    span = (uint8_t)(hi - lo + 1);
    if (span == 0) return lo;
    mask = 1;
    while (mask < (uint8_t)(span - 1)) {
        mask = (uint8_t)((mask << 1) | 1);
    }
    do {
        tb_rng_step();
        r = (uint8_t)((uint8_t)g_travel_rng & mask);
    } while (r >= span);
    return (uint8_t)(lo + r);
}

/* Grow both journey counters by the same clamped delta so
 * steps_done (total - left) stays invariant at the 255 ceiling. */
static void tb_add_steps(uint8_t delay)
{
    uint8_t room;
    room = (uint8_t)(255 - g_travel_total);
    if (delay > room) delay = room;
    g_travel_total = (uint8_t)(g_travel_total + delay);
    g_travel_left = (uint8_t)(g_travel_left + delay);
}

static void tb_starve(uint8_t noprov);

static void tb_starve(uint8_t noprov)
{
    uint8_t hp;
    hp = g_game.state.party.members[0].hp;
    /* Modulo-free "every Nth" cadence (no % or / anywhere, banked
     * bodies share the fixed-bank budget rules): count up, pay out
     * on reaching the beat.  No floor: hunger can kill outright (a
     * travel death is a cold hard game over, routed by the callers). */
    if (noprov) {
        g_travel_noprov++;
        if (g_travel_noprov >= s_route.starve_prov_every) {
            g_travel_noprov = 0;
            if (hp > s_route.starve_prov_hp) {
                hp = (uint8_t)(hp - s_route.starve_prov_hp);
            } else {
                hp = 0;
            }
        }
    } else {
        g_travel_noprov = 0;
    }
    g_game.state.party.members[0].hp = hp;
    g_game.world.player.hp = hp;
}

/* Banked camp-action body (bank 11).  Action id arrives in
 * g_bk_byte_a (0 hunt, 1 rest, 2 study); results go straight to
 * WRAM (g_travel_amount/extra/msg, counters, GameState) with
 * the arrive flag in g_bk_byte_b and the sfx id in g_bk_byte_c.
 * Same discipline as travel_burn_banked below: WRAM + same-bank
 * route rows only, inline xorshift on the travel stream (seeded by
 * the fixed wrapper once per journey), local variable replicas,
 * no fixed-bank calls, no di/ei. */
void travel_burn_banked(void);
void travel_camp_banked(void)
{
    uint8_t action;
    uint8_t found;
    uint8_t delay;
    uint8_t saved;
    uint8_t lost;
    uint8_t old_hp;
    int16_t net;
    int16_t v;
    int16_t healed;
    int16_t chance;
    action = g_bk_byte_a;
    g_bk_byte_b = 0;
    g_bk_byte_c = SFX_CONFIRM;
    if (action == 0) {
        /* Hunt/forage: finds provisions before the day's burn so a
         * starving hero can eat what they just found; may add a
         * detour (no road event).  A jackpot roll (base chance plus
         * the active weather's delta, clamped 0..100) pays the big
         * haul instead of the base yield: clear skies hunt well,
         * storms and snow barely at all, so the same road feeds
         * differently under different skies. */
        chance = (int16_t)s_route.hunt_jackpot_chance;
        if (g_travel_weather != TRAVEL_WEATHER_NONE) {
            chance += (int16_t)s_route.weather_hunt[g_travel_weather];
        }
        if (chance < 0) chance = 0;
        if (chance > 100) chance = 100;
        if (tb_roll(1, 100) <= (uint8_t)chance) {
            found = tb_roll(s_route.hunt_jackpot_min,
                            s_route.hunt_jackpot_max);
        } else {
            found = tb_roll(s_route.hunt_prov_min, s_route.hunt_prov_max);
        }
        delay = tb_roll(s_route.hunt_delay_min, s_route.hunt_delay_max);
        v = tb_get(VARIABLE_ID_PROVISIONS);
        tb_set(VARIABLE_ID_PROVISIONS, (int16_t)(v + (int16_t)found));
        g_bk_byte_a = 1;
        g_bk_byte_b = 0;
        travel_burn_banked();
        if (g_game.state.party.members[0].hp == 0) return;
        tb_add_steps(delay);
        g_travel_amount = found;
        g_travel_extra = delay;
        g_travel_msg = TRAVEL_MSG_HUNTED;
        g_bk_byte_c = SFX_CASH;
        return;
    }
    if (action == 1) {
        /* Rest: a day off that heals and still costs the schedule. */
        g_bk_byte_a = 1;
        g_bk_byte_b = 0;
        travel_burn_banked();
        if (g_game.state.party.members[0].hp == 0) return;
        old_hp = g_game.state.party.members[0].hp;
        healed = (int16_t)old_hp;
        healed = (int16_t)(healed + (int16_t)s_route.rest_heal);
        if (healed > (int16_t)g_game.state.party.members[0].max_hp) {
            healed = (int16_t)g_game.state.party.members[0].max_hp;
        }
        g_game.state.party.members[0].hp = (uint8_t)healed;
        g_game.world.player.hp = (uint8_t)healed;
        tb_add_steps(s_route.rest_delay);
        g_travel_amount = (uint8_t)((uint8_t)healed - old_hp);
        g_travel_extra = s_route.rest_delay;
        g_travel_msg = TRAVEL_MSG_RESTED;
        return;
    }
    /* Study the map: survey costs extra rations, may shave steps or
     * add lost ones, and never promises a shortcut. */
    saved = tb_roll(s_route.study_save_min, s_route.study_save_max);
    lost = tb_roll(s_route.study_lost_min, s_route.study_lost_max);
    net = (int16_t)((int16_t)saved - (int16_t)lost);
    g_bk_byte_a = 1;
    g_bk_byte_b = 0;
    travel_burn_banked();
    if (g_game.state.party.members[0].hp == 0) return;
    v = tb_get(VARIABLE_ID_PROVISIONS);
    if (v > (int16_t)s_route.study_prov) {
        tb_set(VARIABLE_ID_PROVISIONS, (int16_t)(v - (int16_t)s_route.study_prov));
    } else if (v > 0) {
        tb_set(VARIABLE_ID_PROVISIONS, 0);
    }
    if (net > 0) {
        g_travel_amount = (uint8_t)net;
        g_travel_extra = 0;
        if (net >= g_travel_left) {
            g_bk_byte_b = 1;
            return;
        }
        g_travel_left = (uint8_t)((int16_t)g_travel_left - net);
        g_travel_total = (uint8_t)((int16_t)g_travel_total - net);
        g_travel_msg = TRAVEL_MSG_STUDIED;
        return;
    }
    if (net < 0) {
        tb_add_steps((uint8_t)(0 - net));
        g_travel_amount = 0;
        g_travel_extra = (uint8_t)(0 - net);
        g_travel_msg = TRAVEL_MSG_LOST;
        return;
    }
    g_travel_amount = 0;
    g_travel_extra = 0;
    g_travel_msg = TRAVEL_MSG_STUDIED;
}

/* Roll the ambient weather for a new day (travel stream).  No-op
 * while a condition is still in force or the road runs no climate
 * (all weights zero keeps old routes weather-free). */
static void tb_weather_roll(void)
{
    uint8_t i;
    uint8_t sum;
    uint8_t mask;
    uint8_t r;
    uint8_t cum;
    uint8_t w;
    w = 0;
    if (g_travel_weather != TRAVEL_WEATHER_NONE &&
        g_travel_weather_left > 0) return;
    sum = 0;
    for (i = 0; i < TRAVEL_WEATHER_COUNT; i++) {
        sum = (uint8_t)(sum + s_route.weather_weight[i]);
    }
    if (sum == 0) {
        g_travel_weather = TRAVEL_WEATHER_NONE;
        g_travel_weather_left = 0;
        return;
    }
    if (sum == 1) {
        r = 0;
    } else {
        mask = 1;
        while (mask < (uint8_t)(sum - 1)) {
            mask = (uint8_t)((mask << 1) | 1);
        }
        do {
            tb_rng_step();
            r = (uint8_t)((uint8_t)g_travel_rng & mask);
        } while (r >= sum);
    }
    cum = 0;
    for (i = 0; i < TRAVEL_WEATHER_COUNT; i++) {
        cum = (uint8_t)(cum + s_route.weather_weight[i]);
        if (r < cum) {
            w = i;
            break;
        }
    }
    g_travel_weather = w;
    g_travel_weather_left = tb_roll(s_route.weather_days_min[w],
                                    s_route.weather_days_max[w]);
}

/* No-arg dispatch body: day count arrives staged in g_bk_byte_a,
 * step_mode in g_bk_byte_b (1 = travelled step: roll today's weather
 * first and burn 2 days if SNOW; 0 = explicit n days).  Burns
 * Convergence days, route rations, starvation tallies, and flips the
 * Lord supreme at zero.  The camp body calls it directly (same bank). */
void travel_burn_banked(void)
{
    uint8_t n;
    uint8_t step_mode;
    uint8_t i;
    uint8_t hungry;
    int16_t day;
    int16_t prov;
    int16_t rate;
    uint8_t w;
    n = g_bk_byte_a;
    step_mode = g_bk_byte_b;
    if (step_mode) {
        tb_weather_roll();
        n = (g_travel_weather == TRAVEL_WEATHER_SNOW) ? 2 : 1;
    }
    for (i = 0; i < n; i++) {
        if (!step_mode) {
            tb_weather_roll();
        }
        w = g_travel_weather;
        day = tb_get(VARIABLE_ID_DAY);
        if (day > 0) {
            tb_set(VARIABLE_ID_DAY, (int16_t)(day - 1));
        }
        prov = tb_get(VARIABLE_ID_PROVISIONS);
        rate = (int16_t)s_route.provisions_per_day;
        if (w != TRAVEL_WEATHER_NONE) {
            rate = (int16_t)(rate + (int16_t)s_route.weather_prov[w]);
            if (rate < 0) rate = 0;
        }
        hungry = (uint8_t)(prov < rate);
        if (prov > rate) {
            tb_set(VARIABLE_ID_PROVISIONS, (int16_t)(prov - rate));
        } else if (prov > 0) {
            tb_set(VARIABLE_ID_PROVISIONS, 0);
        }
        tb_starve(hungry);
        if (w != TRAVEL_WEATHER_NONE && g_travel_weather_left > 0) {
            g_travel_weather_left--;
        }
    }
    day = tb_get(VARIABLE_ID_DAY);
    if (day == 0 && tb_get(VARIABLE_ID_LORD_POWER) == 0) {
        tb_set(VARIABLE_ID_LORD_POWER, 1);
    }
}
