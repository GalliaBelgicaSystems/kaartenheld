#pragma bank 14

#include "game.h"
#include "game_ids.h"
#include "banked.h"
#include "travel.h"

/* ── Travel day-burn body, bank-14 ───────────────────────────────
 * The fixed bank sits hard against 0x8000 (AGENTS.md §52.18), so the
 * pure day-burn arithmetic lives here behind the thin fixed-bank
 * wrapper travel_burn_days(): the wrapper stages the day count and
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

/* Uniform range roll without % or /: rejection-sample a byte under
 * the span, like the fixed-bank pool pick. */
static uint8_t tb_roll(uint8_t lo, uint8_t hi)
{
    uint8_t span;
    uint8_t r;
    if (hi <= lo) return lo;
    span = (uint8_t)(hi - lo + 1);
    if (span == 0) return lo;
    do {
        tb_rng_step();
        r = (uint8_t)g_travel_rng;
    } while (r >= span);
    return (uint8_t)(lo + r);
}

/* Grow both journey counters (clamped, never wraps). */
static void tb_add_steps(uint8_t delay)
{
    if (g_travel_total <= (uint8_t)(255 - delay)) {
        g_travel_total = (uint8_t)(g_travel_total + delay);
    } else {
        g_travel_total = 255;
    }
    if (g_travel_left <= (uint8_t)(255 - delay)) {
        g_travel_left = (uint8_t)(g_travel_left + delay);
    } else {
        g_travel_left = 255;
    }
}

static void tb_starve(uint8_t nofood, uint8_t nowater);

static void tb_starve(uint8_t nofood, uint8_t nowater)
{
    uint8_t hp;
    hp = g_game.state.party.members[0].hp;
    /* Modulo-free "every Nth" cadence (no % or / anywhere, banked
     * bodies share the fixed-bank budget rules): count up, pay out
     * on reaching the beat.  No floor: hunger can kill outright (a
     * travel death is a cold hard game over, routed by the callers). */
    if (nofood) {
        g_travel_nofood++;
        if (g_travel_nofood >= s_route.starve_food_every) {
            g_travel_nofood = 0;
            if (hp > s_route.starve_food_hp) {
                hp = (uint8_t)(hp - s_route.starve_food_hp);
            } else {
                hp = 0;
            }
        }
    } else {
        g_travel_nofood = 0;
    }
    if (nowater) {
        g_travel_nowater++;
        if (g_travel_nowater >= s_route.starve_water_every) {
            g_travel_nowater = 0;
            if (hp > s_route.starve_water_hp) {
                hp = (uint8_t)(hp - s_route.starve_water_hp);
            } else {
                hp = 0;
            }
        }
    } else {
        g_travel_nowater = 0;
    }
    g_game.state.party.members[0].hp = hp;
    g_game.world.player.hp = hp;
}

/* Banked camp-action body (bank 14).  Action id arrives in
 * g_bk_byte_a (0 hunt, 1 rest, 2 study); results go straight to
 * WRAM (g_travel_amount/extra/water/msg, counters, GameState) with
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
    uint8_t drunk;
    uint8_t delay;
    uint8_t saved;
    uint8_t lost;
    int16_t net;
    int16_t v;
    int16_t healed;
    action = g_bk_byte_a;
    g_bk_byte_b = 0;
    g_bk_byte_c = SFX_CONFIRM;
    if (action == 0) {
        /* Hunt/forage: always finds food plus some water, may add a
         * detour; the day is spent off the trail (no road event). */
        found = tb_roll(s_route.hunt_food_min, s_route.hunt_food_max);
        drunk = tb_roll(s_route.hunt_water_min, s_route.hunt_water_max);
        delay = tb_roll(s_route.hunt_delay_min, s_route.hunt_delay_max);
        g_bk_byte_a = 1;
        travel_burn_banked();
        if (g_game.state.party.members[0].hp == 0) return;
        v = tb_get(VARIABLE_ID_FOOD);
        tb_set(VARIABLE_ID_FOOD, (int16_t)(v + (int16_t)found));
        v = tb_get(VARIABLE_ID_WATER);
        tb_set(VARIABLE_ID_WATER, (int16_t)(v + (int16_t)drunk));
        tb_add_steps(delay);
        g_travel_amount = found;
        g_travel_extra = delay;
        g_travel_water = drunk;
        g_travel_msg = TRAVEL_MSG_HUNTED;
        g_bk_byte_c = SFX_CASH;
        return;
    }
    if (action == 1) {
        /* Rest: a day off that heals and still costs the schedule. */
        g_bk_byte_a = 1;
        travel_burn_banked();
        if (g_game.state.party.members[0].hp == 0) return;
        healed = (int16_t)g_game.state.party.members[0].hp;
        healed = (int16_t)(healed + (int16_t)s_route.rest_heal);
        if (healed > (int16_t)g_game.state.party.members[0].max_hp) {
            healed = (int16_t)g_game.state.party.members[0].max_hp;
        }
        g_game.state.party.members[0].hp = (uint8_t)healed;
        g_game.world.player.hp = (uint8_t)healed;
        tb_add_steps(s_route.rest_delay);
        g_travel_amount = s_route.rest_heal;
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
    travel_burn_banked();
    if (g_game.state.party.members[0].hp == 0) return;
    v = tb_get(VARIABLE_ID_FOOD);
    if (v > (int16_t)s_route.study_food) {
        tb_set(VARIABLE_ID_FOOD, (int16_t)(v - (int16_t)s_route.study_food));
    } else if (v > 0) {
        tb_set(VARIABLE_ID_FOOD, 0);
    }
    v = tb_get(VARIABLE_ID_WATER);
    if (v > (int16_t)s_route.study_water) {
        tb_set(VARIABLE_ID_WATER, (int16_t)(v - (int16_t)s_route.study_water));
    } else if (v > 0) {
        tb_set(VARIABLE_ID_WATER, 0);
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

/* No-arg dispatch body: day count arrives staged in g_bk_byte_a.
 * Burns Convergence days, route rations, starvation tallies, and
 * flips the Lord supreme at zero -- the fixed-bank loop it replaces,
 * minus the call layer.  The camp body calls it directly: same-bank
 * plain call, never through the WRAM trampoline. */
void travel_burn_banked(void)
{
    uint8_t n;
    uint8_t i;
    int16_t day;
    int16_t food;
    int16_t water;
    int16_t rate;
    n = g_bk_byte_a;
    for (i = 0; i < n; i++) {
        day = tb_get(VARIABLE_ID_DAY);
        if (day > 0) {
            tb_set(VARIABLE_ID_DAY, (int16_t)(day - 1));
        }
        food = tb_get(VARIABLE_ID_FOOD);
        rate = (int16_t)s_route.food_per_day;
        if (food > rate) {
            tb_set(VARIABLE_ID_FOOD, (int16_t)(food - rate));
        } else if (food > 0) {
            tb_set(VARIABLE_ID_FOOD, 0);
        }
        water = tb_get(VARIABLE_ID_WATER);
        rate = (int16_t)s_route.water_per_day;
        if (water > rate) {
            tb_set(VARIABLE_ID_WATER, (int16_t)(water - rate));
        } else if (water > 0) {
            tb_set(VARIABLE_ID_WATER, 0);
        }
        tb_starve((uint8_t)(tb_get(VARIABLE_ID_FOOD) <= 0),
                  (uint8_t)(tb_get(VARIABLE_ID_WATER) <= 0));
    }
    day = tb_get(VARIABLE_ID_DAY);
    if (day == 0 && tb_get(VARIABLE_ID_LORD_POWER) == 0) {
        tb_set(VARIABLE_ID_LORD_POWER, 1);
    }
}
