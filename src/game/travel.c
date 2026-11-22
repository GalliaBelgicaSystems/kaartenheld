#include "travel.h"
#include "game.h"
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
uint8_t g_travel_fought;
uint8_t g_travel_choice;
uint8_t g_travel_msg;
uint8_t g_travel_seen;
uint8_t g_travel_in_ruins;
uint8_t g_travel_amount;
uint8_t g_travel_extra;
uint8_t g_travel_water;
uint8_t g_travel_nofood;
uint8_t g_travel_nowater;
uint16_t g_travel_rng;

/* Staged table rows: copied from bank 9 on stage, read by logic and
 * render from WRAM (never map a ROM window around a render).  Non-
 * static: the banked day-burn body (travel_logic_banked.c, bank 14)
 * reads the route tuning straight from WRAM. */
TravelRoute s_route;
static TravelMethod s_method;
static TravelEvent s_event;

static void travel_arrive(Game *g);
void travel_abandon(Game *g);
static void travel_add_steps(uint8_t delay);
static void travel_clear_state(void);
static void travel_show_msg(Game *g, uint8_t msg, uint8_t sfx);
static void travel_cycle_choice(Game *g, uint8_t max, uint8_t down);

const TravelRoute *travel_route_row(void)
{
    return &s_route;
}

const TravelMethod *travel_method_row(void)
{
    return &s_method;
}

const TravelEvent *travel_event_row(void)
{
    return &s_event;
}

uint8_t travel_view(void)
{
    if (g_travel_supply) return TRAVEL_VIEW_SUPPLY;
    if (g_travel_msg != TRAVEL_MSG_NONE) return TRAVEL_VIEW_MSG;
    if (g_travel_method == 0xFF) return TRAVEL_VIEW_METHOD;
    if (g_travel_event == TRAVEL_EVENT_NONE) return TRAVEL_VIEW_IDLE;
    if (s_event.kind == TRAVEL_EVENT_BATTLE) return TRAVEL_VIEW_AMBUSH;
    if (s_event.kind == TRAVEL_EVENT_RUINS) return TRAVEL_VIEW_RUINS;
    return TRAVEL_VIEW_HELP;
}

uint8_t travel_steps_done(void)
{
    return (uint8_t)(g_travel_total - g_travel_left);
}

static void travel_load_route(uint8_t route)
{
    banked_copy(14, (uint8_t *)&s_route,
                (const uint8_t *)&g_travel_routes[route],
                (uint8_t)sizeof(TravelRoute));
}

static void travel_load_event(uint8_t event)
{
    banked_copy(14, (uint8_t *)&s_event,
                (const uint8_t *)&g_travel_events[event],
                (uint8_t)sizeof(TravelEvent));
}

static void travel_load_method(uint8_t method)
{
    banked_copy(14, (uint8_t *)&s_method,
                (const uint8_t *)&g_travel_routes[g_travel_route].methods[method],
                (uint8_t)sizeof(TravelMethod));
}

/* Start a route (event-executor hook, content.h).  Opens the method
 * pick (carriage vs foot); the default is the first free method so a
 * stray confirm never spends gold.  Out-of-range ids are a
 * compiler-rejected case; the runtime guard is belt and braces. */
void travel_start(uint8_t route)
{
    uint8_t i;
    if (route >= g_travel_route_count) return;
    travel_load_route(route);
    g_travel_route = route;
    g_travel_method = 0xFF;
    g_travel_supply = 0;
    travel_clear_state();
    for (i = 0; i < s_route.method_count; i++) {
        if (s_route.methods[i].cost_gold == 0) {
            g_travel_choice = i;
            break;
        }
    }
    g_travel_msg = TRAVEL_MSG_NONE;
    g_travel_active = 1;
    screen_change(&g_game, SCREEN_JOURNEY);
}

bool game_travel_active(void)
{
    return g_travel_active != 0;
}

/* Burn n days off the Convergence clock (floor at zero, never
 * negative) plus the route's road rations (food/water per day from
 * the route table, floored -- a desert route drinks more).  Days
 * spent with no food or no water count up their own starvation
 * tally; every Nth such day costs HP outright (a travel death is a
 * cold hard game over, routed by the callers).  A day with supplies
 * resets that tally.  Crossing zero flips the Lord supreme (one-way):
 * late ambushes use their buffed HP and the end fight reads the same
 * flag.
 *
 * Thin fixed-bank wrapper (fixed bank sits hard against 0x8000,
 * AGENTS.md §52.18): the loop lives as the bank-14 body
 * travel_burn_banked() (travel_logic_banked.c); only the staging
 * call stays fixed. */
void travel_burn_banked(void);

static void travel_burn_days(Game *g, uint8_t n)
{
    if (!g) return;
    (void)g;
    g_bk_call_bank = 14;
    g_bk_call_target = (uint16_t)&travel_burn_banked;
    g_bk_byte_a = n;
    banked_call_run();
}

/* Confirm the picked method.  Paid instant methods deduct and arrive
 * at once (no steps, no events, no days); foot methods stage their own
 * duration and event pool.  Too poor stays put with a refusal line. */
static void travel_confirm_method(Game *g)
{
    int16_t gold;
    if (!g) return;
    travel_load_method(g_travel_choice);
    gold = currency_get(&g->state, CURRENCY_ID_GOLD);
    if (gold < s_method.cost_gold) {
        g_travel_msg = TRAVEL_MSG_NOGOLD;
        audio_play_sfx(SFX_BACK);
        game_render_reset(g);
        return;
    }
    if (s_method.cost_gold > 0) {
        currency_add(&g->state, CURRENCY_ID_GOLD,
                     (int16_t)(0 - s_method.cost_gold));
    }
    audio_play_sfx(SFX_CONFIRM);
    g_travel_method = g_travel_choice;
    g_travel_choice = 0;
    if (s_method.steps == 0) {
        travel_arrive(g);
        return;
    }
    travel_clear_state();
    g_travel_total = s_method.steps;
    g_travel_left = s_method.steps;
    /* Seed the camp-action stream from the shared RNG (one draw per
     * journey; nonzero seeds can never hit the xorshift fixed point). */
    g_travel_rng = rng_next();
    if (!g_travel_rng) g_travel_rng = 0xACE1;
    game_render_reset(g);
}

/* Open the route's supply merchant (event-executor hook, content.h).
 * Pre-travel shop on the field: buy rations with gold, LEAVE (or B)
 * walks away.  Out-of-range ids are rejected like travel_start. */
void travel_open_supply(uint8_t route)
{
    if (route >= g_travel_route_count) return;
    travel_load_route(route);
    g_travel_route = route;
    g_travel_method = 0xFF;
    g_travel_supply = 1;
    travel_clear_state();
    g_travel_active = 1;
    screen_change(&g_game, SCREEN_JOURNEY);
}

/* Leave the supply merchant without buying (back to the field). */
static void travel_close_supply(Game *g)
{
    if (!g) return;
    g_travel_supply = 0;
    g_travel_active = 0;
    g_travel_method = 0xFF;
    g_travel_choice = 0;
    audio_play_sfx(SFX_BACK);
    screen_change(g, SCREEN_OVERWORLD);
}

/* Buy one unit of the picked supply (0 = food, 1 = water) if affordable. */
static void travel_buy_supply(Game *g)
{
    int16_t price;
    int16_t gold;
    if (!g) return;
    price = (g_travel_choice == 0) ? (int16_t)s_route.food_price
                                  : (int16_t)s_route.water_price;
    gold = currency_get(&g->state, CURRENCY_ID_GOLD);
    if (gold < price) {
        audio_play_sfx(SFX_BACK);
        return;
    }
    if (price > 0) {
        currency_add(&g->state, CURRENCY_ID_GOLD, (int16_t)(0 - price));
    }
    if (g_travel_choice == 0) {
        game_variable_add(&g->state, VARIABLE_ID_FOOD, 1);
    } else {
        game_variable_add(&g->state, VARIABLE_ID_WATER, 1);
    }
    audio_play_sfx(SFX_CASH);
    game_render_reset(g);
}

static void travel_update_supply(Game *g)
{
    if (!g) return;
    if (input_pressed(INPUT_UP) || input_pressed(INPUT_LEFT)) {
        travel_cycle_choice(g, 2, 0);
        return;
    }
    if (input_pressed(INPUT_DOWN) || input_pressed(INPUT_RIGHT)) {
        travel_cycle_choice(g, 2, 1);
        return;
    }
    if (input_pressed(INPUT_B)) {
        travel_close_supply(g);
        return;
    }
    if (input_pressed(INPUT_A) || input_pressed(INPUT_START)) {
        if (g_travel_choice >= 2) {
            travel_close_supply(g);
        } else {
            travel_buy_supply(g);
        }
    }
}

/* Arrive: load the route destination, stop travel.  The scene load
 * forces a tileset revalidate, so no battle art leaks into town. */
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
    def = scene_definition_for_map(g->world.map_id);
    audio_play_music(def ? def->music : MUSIC_OVERWORLD);
}

/* Drop the whole journey (terminal game over): the road is done,
 * win or lose, so no suspended ruins, staged event, message, menu
 * cursor, or starvation tally may leak into whatever runs next. */
void travel_abandon(Game *g)
{
    (void)g;
    g_travel_active = 0;
    g_travel_method = 0xFF;
    g_travel_supply = 0;
    g_travel_total = 0;
    g_travel_left = 0;
    g_travel_event = TRAVEL_EVENT_NONE;
    g_travel_fought = 0;
    g_travel_msg = TRAVEL_MSG_NONE;
    g_travel_choice = 0;
    g_travel_seen = 0;
    g_travel_in_ruins = 0;
    g_travel_amount = 0;
    g_travel_extra = 0;
    g_travel_water = 0;
    g_travel_nofood = 0;
    g_travel_nowater = 0;
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
    game_render_reset(g);
}

/* Launch the staged ambush as a solo battle.  The name was copied to
 * WRAM by battle_start, so the bank-9 pointer dies safely here.  Post-
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
    g_travel_fought = 1;
    battle_start(&g->battle, s_event.enemy_name,
                 g->state.party.members[0].hp,
                 g->state.party.members[0].max_hp,
                 hp, max_hp,
                 &g->state.cards.deck, s_event.battle, 1);
    audio_play_music(MUSIC_BATTLE);
    screen_change(g, SCREEN_BATTLE);
}

/* Resolve the staged road-event choice.  One generic path for every
 * choice event: the picked option may start a fight (staged battle
 * event), burn days, move gold/rations, heal or hurt, and add or
 * shave steps.  Resources floor at zero, heals cap at max, and any
 * death (day toll or hurt) is a cold game over.  The event stays
 * staged so the message render still sees its row (cleared on
 * dismiss); the cursor is left on the picked option for the paint. */
static void travel_resolve_help(Game *g)
{
    int16_t v;
    int16_t healed;
    uint8_t hurt;
    uint8_t delay;
    uint8_t save;
    uint8_t is_opt1;
    if (!g) return;
    is_opt1 = (uint8_t)(g_travel_choice == 0);
    if (is_opt1 ? (s_event.opt1_battle != 0xFF)
                : (s_event.opt2_battle != 0xFF)) {
        g_travel_event = is_opt1 ? s_event.opt1_battle : s_event.opt2_battle;
        g_travel_choice = 0;
        travel_open_ambush(g);
        return;
    }
    travel_burn_days(g, is_opt1 ? (uint8_t)s_event.opt1_days
                                : (uint8_t)s_event.opt2_days);
    if (travel_hero_dead(g)) {
        travel_game_over(g);
        return;
    }
    v = (int16_t)currency_get(&g->state, CURRENCY_ID_GOLD);
    v = (int16_t)(v + (is_opt1 ? s_event.opt1_gold : s_event.opt2_gold));
    if (v < 0) {
        /* Pay what you have: clamp the spend at zero. */
        currency_add(&g->state, CURRENCY_ID_GOLD,
                     (int16_t)(0 - currency_get(&g->state, CURRENCY_ID_GOLD)));
    } else {
        currency_add(&g->state, CURRENCY_ID_GOLD,
                     is_opt1 ? s_event.opt1_gold : s_event.opt2_gold);
    }
    v = game_variable_get(&g->state, VARIABLE_ID_FOOD);
    v = (int16_t)(v + (is_opt1 ? s_event.opt1_food : s_event.opt2_food));
    if (v < 0) v = 0;
    game_variable_set(&g->state, VARIABLE_ID_FOOD, v);
    v = game_variable_get(&g->state, VARIABLE_ID_WATER);
    v = (int16_t)(v + (is_opt1 ? s_event.opt1_water : s_event.opt2_water));
    if (v < 0) v = 0;
    game_variable_set(&g->state, VARIABLE_ID_WATER, v);
    healed = (int16_t)g->state.party.members[0].hp;
    healed = (int16_t)(healed + (is_opt1 ? s_event.opt1_heal
                                         : s_event.opt2_heal));
    if (healed > (int16_t)g->state.party.members[0].max_hp) {
        healed = (int16_t)g->state.party.members[0].max_hp;
    }
    hurt = is_opt1 ? (uint8_t)s_event.opt1_hurt : (uint8_t)s_event.opt2_hurt;
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
    delay = is_opt1 ? (uint8_t)s_event.opt1_delay
                    : (uint8_t)s_event.opt2_delay;
    travel_add_steps(delay);
    save = is_opt1 ? (uint8_t)s_event.opt1_save : (uint8_t)s_event.opt2_save;
    if (save >= g_travel_left) {
        travel_arrive(g);
        return;
    }
    g_travel_left = (uint8_t)(g_travel_left - save);
    g_travel_total = (uint8_t)(g_travel_total - save);
    travel_show_msg(g, is_opt1 ? TRAVEL_MSG_HELPED : TRAVEL_MSG_IGNORED,
                   (is_opt1 ? s_event.opt1_gold : s_event.opt2_gold) > 0
                       ? SFX_CASH : SFX_CONFIRM);
}

/* Grow both journey counters by a detour (clamped, never wraps):
 * one helper for the help/hunt/rest delays (fixed-bank budget). */
static void travel_add_steps(uint8_t delay)
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

/* Clear the transient journey counters (fixed-bank budget: one copy
 * for the start/supply/confirm/arrive resets).  Route, method and
 * active stay site-owned. */
static void travel_clear_state(void)
{
    g_travel_total = 0;
    g_travel_left = 0;
    g_travel_event = TRAVEL_EVENT_NONE;
    g_travel_fought = 0;
    g_travel_msg = TRAVEL_MSG_NONE;
    g_travel_choice = 0;
    g_travel_seen = 0;
    g_travel_in_ruins = 0;
    g_travel_amount = 0;
    g_travel_extra = 0;
    g_travel_water = 0;
    g_travel_nofood = 0;
    g_travel_nowater = 0;
}

/* Show a result line: stage the message, blip, repaint (one copy
 * for the camp/event resolves).  The menu cursor is left alone: the
 * choice paint reads the picked option until the dismiss resets it. */
static void travel_show_msg(Game *g, uint8_t msg, uint8_t sfx)
{
    g_travel_msg = msg;
    audio_play_sfx(sfx);
    game_render_reset(g);
}

/* Cycle the menu cursor 0..max with wrap (one copy for the
 * event toggles and the camp menu).  max 1 toggles YES/NO. */
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
    game_render_reset(g);
}

void travel_camp_banked(void);

/* Camp menu actions (0 hunt, 1 rest, 2 study): thin fixed-bank
 * wrapper -- the bank-14 travel_camp_banked() body owns the rolls,
 * rations, HP and step math behind one staging call (the fixed bank
 * sits hard against 0x8000, AGENTS.md §52.18); this plays the
 * staged jingle and routes the fixed-bank exits (death, arrival). */
static void travel_do_camp(Game *g, uint8_t action)
{
    uint8_t sfx;
    if (!g) return;
    g_bk_call_bank = 14;
    g_bk_call_target = (uint16_t)&travel_camp_banked;
    g_bk_byte_a = action;
    banked_call_run();
    sfx = g_bk_byte_c;
    audio_play_sfx(sfx);
    if (travel_hero_dead(g)) {
        travel_game_over(g);
        return;
    }
    if (g_bk_byte_b) {
        travel_arrive(g);
        return;
    }
    game_render_reset(g);
}

/* Weighted pool pick: rejection-sample a byte under the weight
 * sum, walk the cumulative weights, and stage the picked event
 * with the alert.  One copy for the guarantee and the normal roll.
 * The compiler guarantees a non-empty pool (and nonzero weights)
 * wherever this runs, so the sample always terminates. */
static void travel_stage_pick(Game *g)
{
    uint8_t pick;
    uint8_t i;
    uint8_t cum;
    uint8_t sum;
    sum = 0;
    for (i = 0; i < s_method.pool_count; i++) {
        sum = (uint8_t)(sum + s_method.pool_weight[i]);
    }
    do {
        pick = (uint8_t)rng_next();
    } while (pick >= sum);
    cum = 0;
    g_travel_event = 0;
    for (i = 0; i < s_method.pool_count; i++) {
        cum = (uint8_t)(cum + s_method.pool_weight[i]);
        if (pick < cum) {
            g_travel_event = s_method.pool_event[i];
            break;
        }
    }
    travel_load_event(g_travel_event);
    g_travel_choice = 0;
    g_travel_seen = 1;
    audio_play_sfx(SFX_ALERT);
    game_render_reset(g);
}

/* Advance one step: count down, arrive on the last step, otherwise
 * roll the route pool.  The pick rejection-samples under the weight
 * sum (compiler-capped at 255), so no %, /, or * in fixed bank. */
static void travel_advance(Game *g)
{
    uint16_t roll;
    uint8_t i;
    if (!g) return;
    if (g_travel_left == 0) {
        travel_arrive(g);
        return;
    }
    g_travel_left--;
    travel_burn_days(g, 1); /* every travelled step costs a day */
    if (travel_hero_dead(g)) {
        travel_game_over(g);
        return;
    }
    if (g_travel_left == 0) {
        travel_arrive(g);
        return;
    }
    roll = rng_next();
    /* Starving parties draw trouble: with no food or no water the next
     * step is always an ambush (first battle event in the pool).  A pool
     * with no battle falls back to the normal roll below. */
    if (game_variable_get(&g->state, VARIABLE_ID_FOOD) <= 0 ||
        game_variable_get(&g->state, VARIABLE_ID_WATER) <= 0) {
        for (i = 0; i < s_method.pool_count; i++) {
            travel_load_event(s_method.pool_event[i]);
            if (s_event.kind == TRAVEL_EVENT_BATTLE) {
                g_travel_event = s_method.pool_event[i];
                break;
            }
        }
        if (g_travel_event != TRAVEL_EVENT_NONE) {
            g_travel_seen = 1;
            travel_show_msg(g, TRAVEL_MSG_NONE, SFX_ALERT);
            return;
        }
    }
    /* On-foot guarantee: the last rollable step forces an event when
     * nothing has fired yet (pool must be non-empty; carriage methods
     * never reach here with steps).  Starvation above already covers
     * empty-belly trips. */
    if (g_travel_left == 1 && !g_travel_seen && s_method.pool_count > 0) {
        travel_stage_pick(g);
        return;
    }
    if ((uint8_t)roll >= s_method.frequency) {
        g_travel_choice = 0;
        game_render_reset(g);
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
 * g_travel_in_ruins keeps the battle exits routed to the overworld
 * while the journey state (steps left, seen flag) waits untouched. */
static void travel_enter_ruins(Game *g)
{
    if (!g) return;
    g_travel_in_ruins = 1;
    g_travel_fought = 0;
    audio_play_sfx(SFX_CONFIRM);
    scene_load(g, SCENE_ACT2_RUINS, 2, 2);
    audio_play_music(MUSIC_DUNGEON);
    screen_change(g, SCREEN_OVERWORLD);
    game_render_reset(g);
}

/* Leave the ruins back to the journey.  The trip itself costs no
 * days; a looted treasure chest adds one day (its flag is set by the
 * ruins chest event row).  Called from the YES/NO leave dialogue. */
void travel_resume_from_ruins(Game *g)
{
    if (!g) return;
    if (game_flag_is_set(&g->state, (FlagId)STORY_FLAG_ID_RUINS_CHEST)) {
        travel_burn_days(g, 1);
        if (travel_hero_dead(g)) {
            travel_game_over(g);
            return;
        }
    }
    g_travel_in_ruins = 0;
    g_travel_event = TRAVEL_EVENT_NONE;
    g_travel_fought = 0;
    g_travel_msg = TRAVEL_MSG_NONE;
    g_travel_choice = 0;
    audio_play_sfx(SFX_CONFIRM);
    screen_change(g, SCREEN_JOURNEY);
    game_render_reset(g);
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
    /* A staged event with fought set means the battle just exited.
     * Victory/defeat messages ride g_travel_msg instead: keep the event
     * staged so the message render still sees its row (it clears on
     * dismiss).  A flee (no message) drops straight back to IDLE. */
    if (g_travel_fought) {
        g_travel_fought = 0;
        if (g_travel_msg == TRAVEL_MSG_NONE) {
            g_travel_event = TRAVEL_EVENT_NONE;
            g_travel_choice = 0;
        }
        game_render_reset(g);
        return;
    }
    if (g_travel_msg != TRAVEL_MSG_NONE) {
        if (input_pressed(INPUT_A) || input_pressed(INPUT_START)) {
            g_travel_event = TRAVEL_EVENT_NONE;
            g_travel_choice = 0;
            travel_show_msg(g, TRAVEL_MSG_NONE, SFX_CONFIRM);
        }
        return;
    }
    if (g_travel_method == 0xFF) {
        /* Method pick: toggle the way, confirm to ride or walk. */
        if (input_pressed(INPUT_UP) || input_pressed(INPUT_DOWN) ||
            input_pressed(INPUT_LEFT) || input_pressed(INPUT_RIGHT)) {
            if (s_route.method_count > 1) {
                travel_cycle_choice(g, 1, 1);
            }
            return;
        }
        if (input_pressed(INPUT_A) || input_pressed(INPUT_START)) {
            travel_confirm_method(g);
        }
        return;
    }
    if (g_travel_event != TRAVEL_EVENT_NONE) {
        travel_update_event(g);
        return;
    }
    /* Camp menu: one action per day.  UP/LEFT move up the list,
     * DOWN/RIGHT move down (wraps over the four options); A/START
     * works the selected day. */
    if (input_pressed(INPUT_UP) || input_pressed(INPUT_LEFT)) {
        travel_cycle_choice(g, 3, 0);
        return;
    }
    if (input_pressed(INPUT_DOWN) || input_pressed(INPUT_RIGHT)) {
        travel_cycle_choice(g, 3, 1);
        return;
    }
    if (input_pressed(INPUT_A) || input_pressed(INPUT_START)) {
        if (g_travel_choice == TRAVEL_CAMP_HUNT) {
            travel_do_camp(g, 0);
        } else if (g_travel_choice == TRAVEL_CAMP_REST) {
            travel_do_camp(g, 1);
        } else if (g_travel_choice == TRAVEL_CAMP_STUDY) {
            travel_do_camp(g, 2);
        } else {
            audio_play_sfx(SFX_CONFIRM);
            travel_advance(g);
            g_travel_choice = 0;
        }
    }
}

/* Victory hook (game_screen_after_victory): the ambush pays its
 * data-driven gold and burns its data-driven days, then shows the loot
 * line on return.  world_on_battle_end already no-ops for actor-less
 * fights, so no double grant. */
void travel_on_victory(void)
{
    travel_load_event(g_travel_event);
    currency_add(&g_game.state, CURRENCY_ID_GOLD, s_event.victory_gold);
    travel_burn_days(&g_game, s_event.victory_days);
    g_travel_msg = TRAVEL_MSG_WON;
}

/* Defeat hook (battle-screen defeat branch): travel deaths are
 * terminal -- abandon the journey and decline, so the battle screen
 * runs its normal cold game over (GAME_OVER screen, continue-from-
 * save or thanks).  Always false: the journey never resumes. */
bool game_travel_consume_defeat(Game *g)
{
    if (!g || !g_travel_active) return false;
    travel_abandon(g);
    return false;
}
