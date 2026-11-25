#ifndef TRAVEL_H
#define TRAVEL_H

/* Travel-demo journey slice (travel-demo ROM only, TRAVEL_SRCS).
 * Data-driven random road events over a fixed-step route: each
 * [B] press advances one step and may roll a pool event
 * (ambush battle or help/ignore choice).  Helping pays gold but
 * adds a detour step; ignoring keeps the schedule.  The camp menu
 * (HUNT/REST/STUDY) holds the once-a-day camp actions; walking on
 * is never a menu pick.
 *
 * Engine vs game layer: everything here is game content + game-owned
 * runtime.  The engine only reaches it through the hooks declared in
 * content.h (travel_start from the event executor, travel_update from
 * the journey screen, active/defeat probes from the battle screen).
 * Persistent GameState is untouched: travel state is transient WRAM,
 * so no save format, snapshot, or descriptor changes (the full
 * JourneyState migration is docs/travel-system.md Phase 1-3 work).
 */

#include <stdint.h>
#include <stdbool.h>
#include "screen.h"

#define TRAVEL_POOL_MAX 64
#define TRAVEL_TEXT_LEN 20
#define TRAVEL_NAME_LEN 11
#define TRAVEL_OPT_LEN 8
#define TRAVEL_METHODS_MAX 2

/* Fixed weather vocabulary (shared by route tables and event
 * interactions; the compiler resolves names to these ids). */
#define TRAVEL_WEATHER_COUNT 8
#define TRAVEL_WEATHER_NONE 0xFF
#define TRAVEL_WEATHER_CLEAR 0
#define TRAVEL_WEATHER_RAIN 1
#define TRAVEL_WEATHER_STORM 2
#define TRAVEL_WEATHER_HEAT 3
#define TRAVEL_WEATHER_COLD 4
#define TRAVEL_WEATHER_WIND 5
#define TRAVEL_WEATHER_SNOW 6
#define TRAVEL_WEATHER_FOG 7

#define TRAVEL_EVENT_BATTLE 1
#define TRAVEL_EVENT_CHOICE 2
#define TRAVEL_EVENT_RUINS 3

#define TRAVEL_EVENT_NONE 0xFF

/* Journey screen views (travel_screen.c render switch). */
#define TRAVEL_VIEW_IDLE 0
#define TRAVEL_VIEW_AMBUSH 1
#define TRAVEL_VIEW_HELP 2
#define TRAVEL_VIEW_MSG 3
#define TRAVEL_VIEW_METHOD 4
#define TRAVEL_VIEW_SUPPLY 5
#define TRAVEL_VIEW_RUINS 6

/* Result message ids (g_travel_msg). */
#define TRAVEL_MSG_NONE 0
#define TRAVEL_MSG_HELPED 1
#define TRAVEL_MSG_IGNORED 2
#define TRAVEL_MSG_WON 3
#define TRAVEL_MSG_NOGOLD 4
#define TRAVEL_MSG_HUNTED 5
#define TRAVEL_MSG_RESTED 6
#define TRAVEL_MSG_STUDIED 7
#define TRAVEL_MSG_LOST 8

/* Camp menu options (IDLE view, g_travel_choice order).  Walking on
 * is the B button, never a menu pick. */
#define TRAVEL_CAMP_HUNT 0
#define TRAVEL_CAMP_REST 1
#define TRAVEL_CAMP_STUDY 2

/* One way to travel a route: paid instant (steps 0, empty pool) or a
 * slow foot slog with its own event pool.  Fixed-size rows (bank 9,
 * read via banked_copy; no pointers).  Costs/steps validated by
 * journey_compile.py. */
typedef struct {
    char name[TRAVEL_NAME_LEN + 1];
    int16_t cost_gold;
    uint8_t steps;     /* 0 = instantaneous, no events */
    uint8_t frequency;
    uint8_t pool_count;
    uint8_t pool_event[TRAVEL_POOL_MAX];
    uint8_t pool_weight[TRAVEL_POOL_MAX];
} TravelMethod;

typedef struct {
    char name[TRAVEL_TEXT_LEN + 1];
    uint8_t dest_scene;
    uint8_t act;         /* road act (pools admit only matching events) */
    uint8_t dest_x;
    uint8_t dest_y;
    uint8_t provisions_price;  /* gold per provision (supply merchant) */
    uint8_t provisions_per_day;   /* provisions burned per travel day */
    uint8_t hunt_prov_min;  /* forage yield range (provisions found) */
    uint8_t hunt_prov_max;
    uint8_t hunt_jackpot_min; /* jackpot haul range (big forage payoff) */
    uint8_t hunt_jackpot_max;
    uint8_t hunt_jackpot_chance; /* % chance a hunt pays the jackpot */
    uint8_t starve_prov_hp;    /* HP lost without provisions ... */
    uint8_t starve_prov_every; /* ... every N starving days */
    uint8_t hunt_delay_min; /* forage detour range (extra steps) */
    uint8_t hunt_delay_max;
    uint8_t rest_heal;   /* HP restored by resting */
    uint8_t rest_delay;  /* extra steps a rest day adds */
    uint8_t study_save_min; /* map-study shortcut range (steps saved) */
    uint8_t study_save_max;
    uint8_t study_lost_min; /* map-study getting-lost range (extra steps) */
    uint8_t study_lost_max;
    uint8_t study_prov;  /* extra provisions a study day costs (on top) */
    uint8_t weather_weight[TRAVEL_WEATHER_COUNT]; /* roll weights (0 = off) */
    uint8_t weather_days_min[TRAVEL_WEATHER_COUNT]; /* duration range */
    uint8_t weather_days_max[TRAVEL_WEATHER_COUNT];
    int8_t weather_prov[TRAVEL_WEATHER_COUNT];  /* extra provisions/day */
    uint8_t method_count;
    TravelMethod methods[TRAVEL_METHODS_MAX];
} TravelRoute;

typedef struct {
    uint8_t kind;
    uint8_t battle;                 /* battle events: BATTLE_* table id */
    char enemy_name[TRAVEL_NAME_LEN + 1];
    uint8_t enemy_hp;
    int16_t victory_gold;           /* battle: loot gold on victory */
    uint8_t victory_days;           /* battle: days the fight costs */
    uint8_t late_hp;                /* battle: enemy_hp once DAY hits zero */
    char text[TRAVEL_TEXT_LEN + 1]; /* prompt (all kinds) */
    char text2[TRAVEL_TEXT_LEN + 1];/* opt1 result (choice) */
    char text3[TRAVEL_TEXT_LEN + 1];/* opt2 result (choice) */
    char opt1[TRAVEL_OPT_LEN + 1];  /* choice option labels */
    char opt2[TRAVEL_OPT_LEN + 1];
    uint8_t opt1_battle; /* choice: battle-event index or 0xFF (fight!) */
    uint8_t opt2_battle;
    uint8_t opt1_risk;   /* % chance this option FAILS (always 50) */
    uint8_t opt2_risk;
    int16_t opt1_gold;   /* choice effects, applied on confirm */
    int16_t opt2_gold;
    int8_t opt1_prov;    /* +/- provisions (floored at zero) */
    int8_t opt2_prov;
    int8_t opt1_heal;    /* HP restored (capped at max) */
    int8_t opt2_heal;
    int8_t opt1_hurt;    /* HP damage (can kill: cold game over) */
    int8_t opt2_hurt;
    int8_t opt1_delay;   /* extra steps added (detour) */
    int8_t opt2_delay;
    int8_t opt1_save;    /* steps shaved (arrives at zero) */
    int8_t opt2_save;
    int8_t opt1_days;    /* extra days burned (starvation applies) */
    int8_t opt2_days;
} TravelEvent;

/* One failed-outcome block (same fields, own result text). */
typedef struct {
    char text[TRAVEL_TEXT_LEN + 1];
    uint8_t battle;
    int16_t gold;
    int8_t prov;
    int8_t heal;
    int8_t hurt;
    int8_t delay;
    int8_t save;
    int8_t days;
} TravelEventFailOpt;

typedef struct {
    TravelEventFailOpt opt1;
    TravelEventFailOpt opt2;
} TravelEventFail;

/* One weather interaction: additive deltas applied to an option's
 * numbers (both branches) while that weather is active.  Sparse table
 * (bank 14): only declared (event, option, weather) triples emit. */
typedef struct {
    uint8_t event;
    uint8_t option;  /* 0 = opt1, 1 = opt2 */
    uint8_t weather; /* TRAVEL_WEATHER_* */
    int8_t hurt;
    int8_t delay;
    int8_t save;
    int8_t days;
    int8_t prov;
} TravelWxMod;

extern const TravelRoute g_travel_routes[];
extern const uint8_t g_travel_route_count;
extern const TravelEvent g_travel_events[];
extern const uint8_t g_travel_event_count;
/* Parallel fail-outcome table (same generated file, ROM bank 14):
 * fail[i] holds both options' failure branches for event i. */
extern const TravelEventFail g_travel_event_fails[];
/* Sparse weather-interaction table (same generated file, bank 14). */
extern const TravelWxMod g_travel_wxmods[];
extern const uint8_t g_travel_wxmod_count;

/* Transient travel state (WRAM BSS, travel.c).  Zero at boot covers
 * inactive (AGENTS.md 52.27: no nonzero initializer). */
extern uint8_t g_travel_active;
extern uint8_t g_travel_route;
extern uint8_t g_travel_method;
extern uint8_t g_travel_supply; /* supply merchant open (pre-travel shop) */
extern uint8_t g_travel_total;
extern uint8_t g_travel_left;
extern uint8_t g_travel_event;
extern uint8_t g_travel_choice;
extern uint8_t g_travel_msg;
extern uint8_t g_travel_seen;    /* an event rolled this journey (guarantee) */
extern uint8_t g_travel_in_ruins; /* suspended in ruins (1=unlooted, 2=already looted) */
extern uint8_t g_travel_amount;  /* camp result size (provisions/heal/steps saved) */
extern uint8_t g_travel_extra;   /* camp detour size (extra steps added) */
extern uint8_t g_travel_noprov;  /* consecutive starving days (no provisions) */
extern uint16_t g_travel_rng;    /* camp-action RNG stream (banked bodies) */
extern uint8_t g_travel_failed;  /* last choice failed its risk roll */
/* Active ambient weather (TRAVEL_WEATHER_* or NONE) + days left.
 * BSS zero boots CLEAR with no days left; the burn body rolls a fresh
 * condition whenever the counter hits zero, and travel_clear_state
 * resets to NONE at journey start, so the boot value never applies. */
extern uint8_t g_travel_weather;
extern uint8_t g_travel_weather_left;

/* Staged table rows (travel.c WRAM, copied from bank 14 on stage,
 * read by logic and render from WRAM -- never map a ROM window
 * around a render).  Non-static so the banked bodies (paint, camp
 * and burn logic) read them directly; fixed code keeps using the
 * row accessors below. */
extern TravelRoute s_route;
extern TravelEvent s_event;
extern TravelEventFail s_event_fail;

/* Current screen view for the renderer. */
uint8_t travel_view(void);

/* No-arg banked weather-bake & starve-pick bodies (emitted into
 * travel_content.c, ROM bank 14, next to the tables they read). */
void travel_wx_apply_banked(void);
void travel_starve_battle_banked(void);

/* Per-frame input/logic for the journey screen. */
void travel_update(struct Game *g);

/* No-arg banked paint body (travel_paint_banked.c, ROM bank 11). */
void travel_paint_banked(void);

/* Steps done (total - left), for the progress row. */
uint8_t travel_steps_done(void);

#endif /* TRAVEL_H */
