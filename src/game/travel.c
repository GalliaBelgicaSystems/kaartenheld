#include "travel.h"
#include "game.h"
#include <stddef.h>
#include "screen.h"
#include "scene.h"
#include "banked.h"
#include "audio.h"
#include "battle.h"
#include "input.h"
#include "rng.h"
#include "rpg/currency.h"
#include "rpg/state.h"
#include "game_ids.h"

/* ── Transient travel state ────────────────────────────────────────
 * Demo-scoped WRAM (see travel.h): the journey never touches
 * GameState, saves, or snapshots.  All bytes BSS-zero at boot. */
uint8_t g_travel_active;
uint8_t g_travel_route;
uint8_t g_travel_method;
uint8_t g_travel_supply;
uint8_t g_travel_total;
uint8_t g_travel_left;
uint8_t g_travel_event;
uint8_t g_travel_choice;
uint8_t g_travel_msg;
uint8_t g_travel_seen;
uint8_t g_travel_in_ruins;
uint8_t g_travel_amount;
uint8_t g_travel_extra;
uint8_t g_travel_noprov;
uint16_t g_travel_rng;
uint8_t g_travel_failed;
uint8_t g_travel_weather;
uint8_t g_travel_weather_left;
TravelEventFail s_event_fail;

/* Staged table rows: copied from bank 14 on stage, read by logic
 * and render from WRAM (never map a ROM window around a render).
 * Non-static: the banked paint/logic bodies read them straight from
 * WRAM (same-bank reads would pin them to the content bank). */
TravelRoute s_route;
TravelEvent s_event;

static void travel_arrive(Game *g);
void travel_abandon(Game *g);
static void travel_add_steps(uint8_t delay);
static void travel_clear_state(void);
static void travel_show_msg(Game *g, uint8_t msg, uint8_t sfx);
static void travel_cycle_choice(Game *g, uint8_t max, uint8_t down);

uint8_t travel_view(void)
{
    if (g_travel_supply) return TRAVEL_VIEW_SUPPLY;
    if (g_travel_msg != TRAVEL_MSG_NONE) return TRAVEL_VIEW_MSG;
    if (g_travel_event == TRAVEL_EVENT_NONE) return TRAVEL_VIEW_IDLE;
    if (s_event.kind == TRAVEL_EVENT_BATTLE) return TRAVEL_VIEW_AMBUSH;
    if (s_event.kind == TRAVEL_EVENT_RUINS) return TRAVEL_VIEW_RUINS;
    return TRAVEL_VIEW_HELP;
}

uint8_t travel_steps_done(void)
{
    return (uint8_t)(g_travel_total - g_travel_left);
}

/* banked_copy() takes a uint8_t byte count; a larger row is silently
 * truncated (78 bytes kept of a 334-byte route once wiped the second
 * method).  The route row stages in three chunks that each fit, the
 * event rows in one.  Grow a chunk past 255 and the typedefs
 * below fail the build loudly instead of corrupting the journey. */
typedef char travel_methods_max_is_2[TRAVEL_METHODS_MAX == 2 ? 1 : -1];
typedef char travel_method_fits_banked_copy[sizeof(TravelMethod) <= 255 ? 1 : -1];
typedef char travel_event_fits_banked_copy[sizeof(TravelEvent) <= 255 ? 1 : -1];
typedef char travel_event_fail_fits_banked_copy[sizeof(TravelEventFail) <= 255 ? 1 : -1];
typedef char travel_route_head_fits_banked_copy[offsetof(TravelRoute, methods) <= 255 ? 1 : -1];
typedef char travel_wxmod_fits_banked_copy[sizeof(TravelWxMod) <= 255 ? 1 : -1];

static const TravelMethod *travel_method_ptr(uint8_t idx)
{
    return (idx == 0) ? &s_route.methods[0] : &s_route.methods[1];
}

static void travel_load_route(uint8_t route)
{
    uint8_t head;
    head = (uint8_t)offsetof(TravelRoute, methods);
    banked_copy(14, (uint8_t *)&s_route,
                (const uint8_t *)&g_travel_routes[route],
                head);
    banked_copy(14, (uint8_t *)&s_route.methods[0],
                (const uint8_t *)&g_travel_routes[route].methods[0],
                (uint8_t)sizeof(TravelMethod));
    banked_copy(14, (uint8_t *)&s_route.methods[1],
                (const uint8_t *)&g_travel_routes[route].methods[1],
                (uint8_t)sizeof(TravelMethod));
}

static void travel_load_event(uint8_t event)
{
    banked_copy(14, (uint8_t *)&s_event,
                (const uint8_t *)&g_travel_events[event],
                (uint8_t)sizeof(TravelEvent));
    banked_copy(14, (uint8_t *)&s_event_fail,
                (const uint8_t *)&g_travel_event_fails[event],
                (uint8_t)sizeof(TravelEventFail));
}

/* Start a route with the signpost picker's method already chosen (no
 * full-screen method view).  Paid instant methods deduct and arrive at
 * once (no steps, no events, no days); foot methods stage their own
 * duration and event pool.  Unaffordable paid methods never start (the
 * picker blocks them; this re-check is safety for direct starts). */
void travel_start_method(uint8_t route, uint8_t method)
{
    const TravelMethod *m;
    int16_t gold;
    uint8_t count;
    Game *g;
    banked_copy(14, &count, &g_travel_route_count, 1);
    if (route >= count) return;
    travel_load_route(route);
    if (method >= s_route.method_count) method = 0;
    m = travel_method_ptr(method);
    g = &g_game;
    gold = currency_get(&g->state, CURRENCY_ID_GOLD);
    if (gold < m->cost_gold) return;
    if (m->cost_gold > 0) {
        currency_add(&g->state, CURRENCY_ID_GOLD,
                     (int16_t)(0 - m->cost_gold));
    }
    audio_play_sfx(SFX_CONFIRM);
    g_travel_route = route;
    g_travel_method = method;
    g_travel_choice = 0;
    g_travel_supply = 0;
    travel_clear_state();
    if (m->steps == 0) {
        travel_arrive(g);
        return;
    }
    g_travel_total = m->steps;
    g_travel_left = m->steps;
    /* Seed the camp-action stream from the shared RNG (one draw per
     * journey; nonzero seeds can never hit the xorshift fixed point). */
    g_travel_rng = rng_next();
    if (!g_travel_rng) g_travel_rng = 0xACE1;
    g_travel_msg = TRAVEL_MSG_NONE;
    g_travel_active = 1;
    screen_change(g, SCREEN_JOURNEY);
}

/* Start a route (event-executor hook, content.h).  Legacy direct starts
 * take the first free method (a stray confirm never spends gold). */
void travel_start(uint8_t route)
{
    uint8_t i;
    uint8_t count;
    banked_copy(14, &count, &g_travel_route_count, 1);
    if (route >= count) return;
    travel_load_route(route);
    for (i = 0; i < s_route.method_count; i++) {
        if (s_route.methods[i].cost_gold == 0) {
            travel_start_method(route, i);
            return;
        }
    }
    travel_start_method(route, 0);
}

bool game_travel_active(void)
{
    return g_travel_active != 0;
}

void travel_burn_banked(void);

static void travel_burn(Game *g, uint8_t n, uint8_t step_mode)
{
    if (!g || (n == 0 && !step_mode)) return;
    g_bk_call_bank = 11;
    g_bk_call_target = (uint16_t)&travel_burn_banked;
    g_bk_byte_a = n;
    g_bk_byte_b = step_mode;
    banked_call_run();
}

/* Open the route's supply merchant (event-executor hook, content.h). */
void travel_open_supply(uint8_t route)
{
    uint8_t count;
    banked_copy(14, &count, &g_travel_route_count, 1);
    if (route >= count) return;
    travel_load_route(route);
    g_travel_route = route;
    g_travel_method = 0xFF;
    g_travel_supply = 1;
    travel_clear_state();
    g_travel_active = 1;
    screen_change(&g_game, SCREEN_JOURNEY);
}

/* Back out of supply shop or method picker to the overworld. */
static void travel_exit_to_overworld(Game *g)
{
    if (!g) return;
    travel_abandon(g);
    audio_play_sfx(SFX_BACK);
    screen_change(g, SCREEN_OVERWORLD);
}

/* Buy one unit of provisions if affordable. */
static void travel_buy_supply(Game *g)
{
    int16_t price;
    int16_t gold;
    if (!g) return;
    price = (int16_t)s_route.provisions_price;
    gold = currency_get(&g->state, CURRENCY_ID_GOLD);
    if (gold < price) {
        audio_play_sfx(SFX_BACK);
        return;
    }
    if (price > 0) {
        currency_add(&g->state, CURRENCY_ID_GOLD, (int16_t)(0 - price));
    }
    game_variable_add(&g->state, VARIABLE_ID_PROVISIONS, 1);
    audio_play_sfx(SFX_CASH);
    g->render_cache.valid = false;
}

static void travel_update_supply(Game *g)
{
    if (!g) return;
    if (input_pressed(INPUT_UP) || input_pressed(INPUT_LEFT)) {
        travel_cycle_choice(g, 1, 0);
        return;
    }
    if (input_pressed(INPUT_DOWN) || input_pressed(INPUT_RIGHT)) {
        travel_cycle_choice(g, 1, 1);
        return;
    }
    if (input_pressed(INPUT_B)) {
        travel_exit_to_overworld(g);
        return;
    }
    if (input_pressed(INPUT_A) || input_pressed(INPUT_START)) {
        if (g_travel_choice >= 1) {
            travel_exit_to_overworld(g);
        } else {
            travel_buy_supply(g);
        }
    }
}

/* Arrive: load the route destination, stop travel, switch back to
 * OVERWORLD on the same frame.  scene_load forces a tileset revalidate,
 * so no battle/UI art leaks into the arrived map. */
static void travel_arrive(Game *g)
{
    const SceneDefinition *def;
    uint8_t scene;
    uint8_t x;
    uint8_t y;
    if (!g) return;
    scene = s_route.dest_scene;
    x = s_route.dest_x;
    y = s_route.dest_y;
    travel_abandon(g);
    scene_load(g, scene, x, y);
    screen_change(g, SCREEN_OVERWORLD);
    def = scene_definition_for_map(g->world.map_id);
    audio_play_music(def ? def->music : MUSIC_OVERWORLD);
}

/* Drop the whole journey (arrival / cancel / terminal game over / load). */
void travel_abandon(Game *g)
{
    (void)g;
    g_travel_active = 0;
    g_travel_method = 0xFF;
    g_travel_supply = 0;
    travel_clear_state();
}

bool travel_hero_dead(const Game *g)
{
    return g->state.party.members[0].hp == 0;
}

/* Cold hard game over: abandon the journey and land on the shared
 * GAME_OVER screen (continue-from-save or thanks, owned by that
 * screen).  Same music/choice setup as a battlefield defeat. */
static void travel_game_over(Game *g)
{
    travel_abandon(g);
    g->game_over_choice = 0;
    audio_play_music(MUSIC_NONE);
    screen_change(g, SCREEN_GAME_OVER);
}

/* Launch the staged ambush as a solo battle.  The name was copied to
 * WRAM by battle_start, so the bank-14 pointer dies safely here.  Post-
 * Convergence the Lord's strength shows: the buffed late HP. */
static void travel_open_ambush(Game *g)
{
    uint8_t hp;
    uint8_t max_hp;
    if (!g) return;
    travel_load_event(g_travel_event);
    if (game_variable_get(&g->state, VARIABLE_ID_DAY) == 0) {
        hp = s_event.late_hp;
    } else {
        hp = s_event.enemy_hp;
    }
    max_hp = hp;
    battle_start(&g->battle, s_event.enemy_name,
                 g->state.party.members[0].hp,
                 g->state.party.members[0].max_hp,
                 hp, max_hp,
                 &g->state.cards.deck, s_event.battle, 1);
    audio_play_music(MUSIC_BATTLE);
    screen_change(g, SCREEN_BATTLE);
}

/* Resolve the staged choice.  Every option of every scenario shares
 * the same odds (the compiler enforces risk == 50): roll under the
 * pre-scaled threshold and the failure branch applies instead of the
 * success branch. */
static void travel_resolve_help(Game *g)
{
    uint8_t is_opt1;
    uint8_t failed;
    uint8_t battle;
    int16_t gold;
    int8_t prov;
    int8_t heal;
    int8_t hurt;
    int8_t delay;
    int8_t save;
    int8_t days;
    int16_t v;
    int16_t healed;
    if (!g) return;
    is_opt1 = (uint8_t)(g_travel_choice == 0);
    failed = (uint8_t)((uint8_t)rng_next() <
        (is_opt1 ? s_event.opt1_risk : s_event.opt2_risk));
    g_travel_failed = failed;
    if (failed) {
        battle = is_opt1 ? s_event_fail.opt1.battle
                         : s_event_fail.opt2.battle;
        gold = is_opt1 ? s_event_fail.opt1.gold : s_event_fail.opt2.gold;
        prov = is_opt1 ? s_event_fail.opt1.prov : s_event_fail.opt2.prov;
        heal = is_opt1 ? s_event_fail.opt1.heal : s_event_fail.opt2.heal;
        hurt = is_opt1 ? s_event_fail.opt1.hurt : s_event_fail.opt2.hurt;
        delay = is_opt1 ? s_event_fail.opt1.delay
                        : s_event_fail.opt2.delay;
        save = is_opt1 ? s_event_fail.opt1.save : s_event_fail.opt2.save;
        days = is_opt1 ? s_event_fail.opt1.days : s_event_fail.opt2.days;
    } else {
        battle = is_opt1 ? s_event.opt1_battle : s_event.opt2_battle;
        gold = is_opt1 ? s_event.opt1_gold : s_event.opt2_gold;
        prov = is_opt1 ? s_event.opt1_prov : s_event.opt2_prov;
        heal = is_opt1 ? s_event.opt1_heal : s_event.opt2_heal;
        hurt = is_opt1 ? s_event.opt1_hurt : s_event.opt2_hurt;
        delay = is_opt1 ? s_event.opt1_delay : s_event.opt2_delay;
        save = is_opt1 ? s_event.opt1_save : s_event.opt2_save;
        days = is_opt1 ? s_event.opt1_days : s_event.opt2_days;
    }
    if (battle != 0xFF) {
        g_travel_event = battle;
        g_travel_choice = 0;
        if (hurt > 0) {
            healed = (int16_t)g->state.party.members[0].hp;
            if (healed > (int16_t)hurt) {
                healed = (int16_t)(healed - (int16_t)hurt);
            } else {
                healed = 0;
            }
            g->state.party.members[0].hp = (uint8_t)healed;
            g->world.player.hp = (uint8_t)healed;
            if (travel_hero_dead(g)) {
                travel_game_over(g);
                return;
            }
        }
        audio_play_sfx(SFX_CONFIRM);
        travel_open_ambush(g);
        return;
    }
    /* Positive provisions apply before multi-day event burns so newly
     * shared meals feed the party instead of starving first. */
    if (prov > 0) {
        game_variable_add(&g->state, VARIABLE_ID_PROVISIONS, (int16_t)prov);
    }
    if (days > 0) {
        travel_burn(g, (uint8_t)days, 0);
        if (travel_hero_dead(g)) {
            travel_game_over(g);
            return;
        }
    }
    if (gold < 0) {
        v = currency_get(&g->state, CURRENCY_ID_GOLD);
        if (v < (int16_t)(0 - gold)) gold = (int16_t)(0 - v);
    }
    currency_add(&g->state, CURRENCY_ID_GOLD, gold);
    if (prov < 0) {
        v = (int16_t)(game_variable_get(&g->state, VARIABLE_ID_PROVISIONS) + (int16_t)prov);
        if (v < 0) v = 0;
        game_variable_set(&g->state, VARIABLE_ID_PROVISIONS, v);
    }
    healed = (int16_t)g->state.party.members[0].hp;
    healed = (int16_t)(healed + (int16_t)heal);
    if (healed > (int16_t)g->state.party.members[0].max_hp) {
        healed = (int16_t)g->state.party.members[0].max_hp;
    }
    if (hurt > 0) {
        if (healed > (int16_t)hurt) {
            healed = (int16_t)(healed - (int16_t)hurt);
        } else {
            healed = 0;
        }
    }
    g->state.party.members[0].hp = (uint8_t)healed;
    g->world.player.hp = (uint8_t)healed;
    if (travel_hero_dead(g)) {
        travel_game_over(g);
        return;
    }
    if (delay > 0) travel_add_steps((uint8_t)delay);
    if (save > 0) {
        if ((uint8_t)save >= g_travel_left) {
            travel_arrive(g);
            return;
        }
        g_travel_left = (uint8_t)(g_travel_left - (uint8_t)save);
        g_travel_total = (uint8_t)(g_travel_total - (uint8_t)save);
    }
    travel_show_msg(g, is_opt1 ? TRAVEL_MSG_HELPED : TRAVEL_MSG_IGNORED,
                    gold > 0 ? SFX_CASH : SFX_CONFIRM);
}

/* Grow both journey counters by the same clamped delta so
 * steps_done (total - left) stays invariant at the 255 ceiling. */
static void travel_add_steps(uint8_t delay)
{
    uint8_t room;
    room = (uint8_t)(255 - g_travel_total);
    if (delay > room) delay = room;
    g_travel_total = (uint8_t)(g_travel_total + delay);
    g_travel_left = (uint8_t)(g_travel_left + delay);
}

/* Clear the transient journey counters.  Route, method and active
 * stay site-owned. */
static void travel_clear_state(void)
{
    g_travel_total = 0;
    g_travel_left = 0;
    g_travel_event = TRAVEL_EVENT_NONE;
    g_travel_msg = TRAVEL_MSG_NONE;
    g_travel_choice = 0;
    g_travel_seen = 0;
    g_travel_in_ruins = 0;
    g_travel_amount = 0;
    g_travel_extra = 0;
    g_travel_noprov = 0;
    g_travel_failed = 0;
    g_travel_weather = TRAVEL_WEATHER_NONE;
    g_travel_weather_left = 0;
}

/* Show a result line: stage the message, blip, repaint. */
static void travel_show_msg(Game *g, uint8_t msg, uint8_t sfx)
{
    g_travel_msg = msg;
    audio_play_sfx(sfx);
    g->render_cache.valid = false;
}

/* Cycle the menu cursor 0..max with wrap. */
static void travel_cycle_choice(Game *g, uint8_t max, uint8_t down)
{
    if (down) {
        if (g_travel_choice >= max) {
            g_travel_choice = 0;
        } else {
            g_travel_choice = (uint8_t)(g_travel_choice + 1);
        }
    } else {
        if (g_travel_choice == 0) {
            g_travel_choice = max;
        } else {
            g_travel_choice = (uint8_t)(g_travel_choice - 1);
        }
    }
    audio_play_sfx(SFX_CURSOR);
    g->render_cache.valid = false;
}

void travel_camp_banked(void);

/* Camp menu actions (0 hunt, 1 rest, 2 study): thin fixed-bank
 * wrapper -- the bank-11 travel_camp_banked() body owns the rolls,
 * rations, HP and step math behind one staging call. */
static void travel_do_camp(Game *g, uint8_t action)
{
    uint8_t sfx;
    if (!g) return;
    g_bk_call_bank = 11;
    g_bk_call_target = (uint16_t)&travel_camp_banked;
    g_bk_byte_a = action;
    banked_call_run();
    sfx = g_bk_byte_c;
    if (travel_hero_dead(g)) {
        travel_game_over(g);
        return;
    }
    audio_play_sfx(sfx);
    if (g_bk_byte_b) {
        travel_arrive(g);
        return;
    }
    g->render_cache.valid = false;
}

/* Bake the active weather's interaction deltas into the staged event
 * copy (both branches) in bank 14. */
static void travel_apply_weather(void)
{
    g_bk_call_bank = 14;
    g_bk_call_target = (uint16_t)&travel_wx_apply_banked;
    banked_call_run();
}

/* Weighted pool pick: masked rejection-sample a byte under the weight
 * sum, walk the cumulative weights, and stage the picked event
 * with the alert. */
static void travel_stage_pick(Game *g)
{
    const TravelMethod *m;
    uint8_t pick;
    uint8_t i;
    uint8_t cum;
    uint8_t sum;
    uint8_t mask;
    m = travel_method_ptr(g_travel_method);
    sum = 0;
    for (i = 0; i < m->pool_count; i++) {
        sum = (uint8_t)(sum + m->pool_weight[i]);
    }
    if (sum <= 1) {
        pick = 0;
    } else {
        mask = 1;
        while (mask < (uint8_t)(sum - 1)) {
            mask = (uint8_t)((mask << 1) | 1);
        }
        do {
            pick = (uint8_t)((uint8_t)rng_next() & mask);
        } while (pick >= sum);
    }
    cum = 0;
    g_travel_event = 0;
    for (i = 0; i < m->pool_count; i++) {
        cum = (uint8_t)(cum + m->pool_weight[i]);
        if (pick < cum) {
            g_travel_event = m->pool_event[i];
            break;
        }
    }
    travel_load_event(g_travel_event);
    travel_apply_weather();
    g_travel_choice = 0;
    g_travel_seen = 1;
    audio_play_sfx(SFX_ALERT);
    g->render_cache.valid = false;
}

/* Advance one step: roll weather + burn step days, arrive on zero,
 * otherwise roll the route pool (or starving ambush). */
static void travel_advance(Game *g)
{
    const TravelMethod *m;
    uint16_t roll;
    if (!g) return;
    if (g_travel_left == 0) {
        travel_arrive(g);
        return;
    }
    m = travel_method_ptr(g_travel_method);
    g_travel_left--;
    travel_burn(g, 1, 1);
    if (travel_hero_dead(g)) {
        travel_game_over(g);
        return;
    }
    if (g_travel_left == 0) {
        travel_arrive(g);
        return;
    }
    roll = rng_next();
    /* Starving parties draw trouble: pick a battle from the pool in
     * bank 14 without 64x banked_copy calls. */
    if (game_variable_get(&g->state, VARIABLE_ID_PROVISIONS) <= 0) {
        g_bk_call_bank = 14;
        g_bk_call_target = (uint16_t)&travel_starve_battle_banked;
        g_bk_byte_a = g_travel_route;
        g_bk_byte_b = g_travel_method;
        g_bk_byte_c = (uint8_t)roll;
        banked_call_run();
        if (g_bk_byte_a != TRAVEL_EVENT_NONE) {
            g_travel_event = g_bk_byte_a;
            travel_load_event(g_travel_event);
            travel_apply_weather();
            g_travel_choice = 0;
            g_travel_seen = 1;
            travel_show_msg(g, TRAVEL_MSG_NONE, SFX_ALERT);
            return;
        }
    }
    /* On-foot guarantee: the last rollable step forces an event when
     * nothing has fired yet. */
    if (g_travel_left == 1 && !g_travel_seen && m->pool_count > 0) {
        travel_stage_pick(g);
        return;
    }
    if ((uint8_t)roll >= m->frequency) {
        g_travel_choice = 0;
        audio_play_sfx(SFX_CONFIRM);
        g->render_cache.valid = false;
        return;
    }
    travel_stage_pick(g);
}

static void travel_enter_ruins(Game *g);

/* Resolve the staged ruins choice.  EXPLORE suspends the journey and
 * loads the ruins map (no day burn on entry); IGNORE keeps the
 * schedule with no cost. */
static void travel_resolve_ruins(Game *g, uint8_t explore)
{
    if (!g) return;
    g_travel_failed = 0;
    if (explore) {
        g_travel_msg = TRAVEL_MSG_NONE;
        g_travel_event = TRAVEL_EVENT_NONE;
        g_travel_choice = 0;
        travel_enter_ruins(g);
        return;
    }
    travel_show_msg(g, TRAVEL_MSG_IGNORED, SFX_CONFIRM);
}

/* Suspend the journey inside the ruins (entry map, no day burn).
 * g_travel_in_ruins records whether the chest was unlooted (1) or
 * already looted (2) at entry so revisits never burn a second day. */
static void travel_enter_ruins(Game *g)
{
    if (!g) return;
    g_travel_in_ruins = game_flag_is_set(&g->state,
        (FlagId)STORY_FLAG_ID_RUINS_CHEST) ? 2 : 1;
    audio_play_sfx(SFX_CONFIRM);
    scene_load(g, SCENE_ACT2_RUINS, 2, 2);
    audio_play_music(MUSIC_DUNGEON);
    screen_change(g, SCREEN_OVERWORLD);
}

/* Leave the ruins back to the journey.  The trip itself costs no
 * days; looting the relic chest during THIS visit burns one extra day. */
void travel_resume_from_ruins(Game *g)
{
    if (!g) return;
    if (!g_travel_active) {
        /* Guard against any loaded save inside the detour map: exit to
         * the field instead of softlocking in ruins. */
        scene_load(g, SCENE_ACT2_FIELD, 5, 7);
        screen_change(g, SCREEN_OVERWORLD);
        audio_play_music(MUSIC_OVERWORLD);
        return;
    }
    if (g_travel_in_ruins == 1 &&
        game_flag_is_set(&g->state, (FlagId)STORY_FLAG_ID_RUINS_CHEST)) {
        travel_burn(g, 1, 0);
        if (travel_hero_dead(g)) {
            travel_game_over(g);
            return;
        }
    }
    g_travel_in_ruins = 0;
    g_travel_event = TRAVEL_EVENT_NONE;
    g_travel_msg = TRAVEL_MSG_NONE;
    g_travel_choice = 0;
    g_travel_failed = 0;
    audio_play_sfx(SFX_CONFIRM);
    audio_play_music(MUSIC_OVERWORLD);
    screen_change(g, SCREEN_JOURNEY);
}

bool game_travel_in_ruins(void)
{
    return g_travel_in_ruins != 0;
}

static void travel_update_event(Game *g)
{
    if (s_event.kind == TRAVEL_EVENT_BATTLE) {
        if (input_pressed(INPUT_A) || input_pressed(INPUT_START)) {
            audio_play_sfx(SFX_CONFIRM);
            travel_open_ambush(g);
        }
        return;
    }
    if (s_event.kind == TRAVEL_EVENT_RUINS) {
        if (input_pressed(INPUT_UP) || input_pressed(INPUT_DOWN) ||
            input_pressed(INPUT_LEFT) || input_pressed(INPUT_RIGHT)) {
            travel_cycle_choice(g, 1, 1);
            return;
        }
        if (input_pressed(INPUT_A) || input_pressed(INPUT_START)) {
            travel_resolve_ruins(g, (uint8_t)(g_travel_choice == 0));
        }
        return;
    }
    if (input_pressed(INPUT_UP) || input_pressed(INPUT_DOWN) ||
        input_pressed(INPUT_LEFT) || input_pressed(INPUT_RIGHT)) {
        travel_cycle_choice(g, 1, 1);
        return;
    }
    if (input_pressed(INPUT_A) || input_pressed(INPUT_START)) {
        travel_resolve_help(g);
    }
}

void travel_update(Game *g)
{
    if (!g) return;
    if (!g_travel_active) {
        screen_change(g, SCREEN_OVERWORLD);
        return;
    }
    /* Supply merchant first: it opens over the live field before any
     * journey starts, and its buys need no journey state. */
    if (g_travel_supply) {
        travel_update_supply(g);
        return;
    }
    if (g_travel_msg != TRAVEL_MSG_NONE) {
        if (input_pressed(INPUT_A) || input_pressed(INPUT_START)) {
            g_travel_event = TRAVEL_EVENT_NONE;
            g_travel_choice = 0;
            g_travel_failed = 0;
            travel_show_msg(g, TRAVEL_MSG_NONE, SFX_CONFIRM);
        }
        return;
    }
    /* No method view: the signpost picker stages the method before the
     * journey opens, so every live journey already has one. */
    if (g_travel_event != TRAVEL_EVENT_NONE) {
        travel_update_event(g);
        return;
    }
    /* Camp menu: HUNT/REST/STUDY via A, B walks on (advance). */
    if (input_pressed(INPUT_UP) || input_pressed(INPUT_LEFT)) {
        travel_cycle_choice(g, 2, 0);
        return;
    }
    if (input_pressed(INPUT_DOWN) || input_pressed(INPUT_RIGHT)) {
        travel_cycle_choice(g, 2, 1);
        return;
    }
    if (input_pressed(INPUT_B)) {
        travel_advance(g);
        g_travel_choice = 0;
        return;
    }
    if (input_pressed(INPUT_A) || input_pressed(INPUT_START)) {
        if (g_travel_choice == TRAVEL_CAMP_HUNT) {
            travel_do_camp(g, 0);
        } else if (g_travel_choice == TRAVEL_CAMP_REST) {
            travel_do_camp(g, 1);
        } else {
            travel_do_camp(g, 2);
        }
    }
}

/* Victory hook (game_screen_after_victory): pays loot, burns victory
 * days, restores road music, and shows the loot line on return. */
void travel_on_victory(void)
{
    travel_load_event(g_travel_event);
    currency_add(&g_game.state, CURRENCY_ID_GOLD, s_event.victory_gold);
    travel_burn(&g_game, s_event.victory_days, 0);
    g_travel_failed = 0;
    g_travel_msg = TRAVEL_MSG_WON;
    audio_play_music(MUSIC_OVERWORLD);
}

/* Flee hook (battle_screen): clears the staged ambush and restores road
 * music immediately so SCREEN_JOURNEY renders IDLE on the first frame. */
void travel_on_fled(void)
{
    g_travel_event = TRAVEL_EVENT_NONE;
    g_travel_choice = 0;
    g_travel_msg = TRAVEL_MSG_NONE;
    g_travel_failed = 0;
    audio_play_music(MUSIC_OVERWORLD);
}
