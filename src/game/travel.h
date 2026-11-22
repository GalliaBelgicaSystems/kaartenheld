#ifndef TRAVEL_H
#define TRAVEL_H

/* Travel-demo journey slice (travel-demo ROM only, TRAVEL_SRCS).
 * Data-driven random road events over a fixed-step route: each
 * [Continue] press advances one step and may roll a pool event
 * (ambush battle or help/ignore choice).  Helping pays gold but
 * adds a detour step; ignoring keeps the schedule.
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

#define TRAVEL_POOL_MAX 4
#define TRAVEL_TEXT_LEN 20
#define TRAVEL_NAME_LEN 11
#define TRAVEL_METHODS_MAX 2

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
#define TRAVEL_MSG_DEFEAT 4
#define TRAVEL_MSG_NOGOLD 5
#define TRAVEL_MSG_HUNTED 6
#define TRAVEL_MSG_RESTED 7
#define TRAVEL_MSG_STUDIED 8

/* Camp menu options (IDLE view, g_travel_choice order). */
#define TRAVEL_CAMP_CONTINUE 0
#define TRAVEL_CAMP_HUNT 1
#define TRAVEL_CAMP_REST 2
#define TRAVEL_CAMP_STUDY 3

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
    uint8_t dest_x;
    uint8_t dest_y;
    uint8_t food_price;  /* gold per ration (supply merchant) */
    uint8_t water_price; /* gold per water (supply merchant) */
    uint8_t food_per_day;   /* rations burned per travel day */
    uint8_t water_per_day;  /* water burned per travel day */
    uint8_t hunt_food_min;  /* forage yield range (food found) */
    uint8_t hunt_food_max;
    uint8_t hunt_delay_min; /* forage detour range (extra steps) */
    uint8_t hunt_delay_max;
    uint8_t rest_heal;   /* HP restored by resting */
    uint8_t rest_delay;  /* extra steps a rest day adds */
    uint8_t starve_food_hp;    /* HP lost without food ... */
    uint8_t starve_food_every; /* ... every N starving days */
    uint8_t starve_water_hp;   /* HP lost without water ... */
    uint8_t starve_water_every;
    uint8_t study_save_min; /* map-study shortcut range (steps saved) */
    uint8_t study_save_max;
    uint8_t method_count;
    TravelMethod methods[TRAVEL_METHODS_MAX];
} TravelRoute;

typedef struct {
    uint8_t kind;
    uint8_t battle;
    char enemy_name[TRAVEL_NAME_LEN + 1];
    uint8_t enemy_hp;
    int16_t reward_gold; /* victory_gold (battle) or help gold (choice) */
    uint8_t victory_days; /* days the fight costs (battle only) */
    uint8_t late_hp;      /* enemy_hp once DAY hits zero (battle only) */
    uint8_t delay;       /* help detour steps (choice only) */
    uint8_t ignore_days; /* days walking on costs (choice only) */
    char text[TRAVEL_TEXT_LEN + 1];
    char text2[TRAVEL_TEXT_LEN + 1];
    char text3[TRAVEL_TEXT_LEN + 1];
} TravelEvent;

extern const TravelRoute g_travel_routes[];
extern const uint8_t g_travel_route_count;
extern const TravelEvent g_travel_events[];
extern const uint8_t g_travel_event_count;

/* Transient travel state (WRAM BSS, travel.c).  Zero at boot covers
 * inactive (AGENTS.md 52.27: no nonzero initializer). */
extern uint8_t g_travel_active;
extern uint8_t g_travel_route;
extern uint8_t g_travel_method;
extern uint8_t g_travel_supply; /* supply merchant open (pre-travel shop) */
extern uint8_t g_travel_total;
extern uint8_t g_travel_left;
extern uint8_t g_travel_event;
extern uint8_t g_travel_fought;
extern uint8_t g_travel_choice;
extern uint8_t g_travel_msg;
extern uint8_t g_travel_seen;    /* an event rolled this journey (guarantee) */
extern uint8_t g_travel_in_ruins; /* suspended in the ruins map */
extern uint8_t g_travel_amount;  /* camp result size (food/heal/steps saved) */
extern uint8_t g_travel_extra;   /* camp detour size (extra steps added) */
extern uint8_t g_travel_nofood;  /* consecutive starving days (no food) */
extern uint8_t g_travel_nowater; /* consecutive starving days (no water) */

/* Staged table rows (bank-9 copies, valid while travelling). */
const TravelRoute *travel_route_row(void);
const TravelMethod *travel_method_row(void);
const TravelEvent *travel_event_row(void);

/* Current screen view for the renderer. */
uint8_t travel_view(void);

/* Per-frame input/logic for the journey screen. */
void travel_update(struct Game *g);

/* No-arg banked paint body (travel_paint_banked.c, ROM bank 9). */
void travel_paint_banked(void);

/* Steps done (total - left), for the progress row. */
uint8_t travel_steps_done(void);

#endif /* TRAVEL_H */
