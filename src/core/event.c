#include "event.h"
#include "game.h"
#include "telemetry.h"
#include "screen.h"
#include "rpg/deck.h"
#include "rpg/currency.h"
#include "banked.h"

/* ── Event engine ──────────────────────────────────────────────────
 * The engine matches events against a table registered by the game layer
 * (event_init, called from src/game/events.c).  First match wins; for
 * INTERACT events more specific conditions must precede the fallback.
 *
 * The table may live in a banked ROM region (see game_ids.h
 * GAME_CONTENT_BANK): every row is copied into a WRAM scratch copy before
 * it is read, so callers never touch banked data and the bank register is
 * always restored to 0.  event_get_row() returns the scratch copy; a
 * returned pointer is only valid until the next row access. */
static const EventDefinition *g_events = NULL;
static uint8_t g_event_count = 0;
/* BSS: event_init() (via game_content_init() at every boot) sets the home
 * bank before any row is read. */
static uint8_t g_event_bank;
static EventDefinition g_event_scratch;

/* banked_copy() takes a uint8_t byte count; a larger row cannot be staged. */
typedef char event_def_fits_banked_copy[sizeof(EventDefinition) <= 255 ? 1 : -1];

void event_init(const EventDefinition *table, uint8_t count, uint8_t bank)
{
    g_events = table;
    g_event_count = count;
    g_event_bank = bank;
}

static const EventDefinition *event_get_row(uint8_t i)
{
    banked_copy(g_event_bank, &g_event_scratch, &g_events[i], sizeof(EventDefinition));
    return &g_event_scratch;
}

static bool event_condition_met(const GameState *state, const EventCond *cond)
{
    int16_t v;
    uint8_t t = cond->type;
    if (t == EVENT_COND_FLAG) {
        return game_flag_is_set(state, (FlagId)cond->id) == cond->flag_set;
    }
    if (t == EVENT_COND_VARIABLE) {
        v = game_variable_get(state, (VariableId)cond->id);
    } else if (t == EVENT_COND_ITEM_COUNT) {
        v = (int16_t)deck_collection_count(&state->cards, (CardId)cond->id);
    } else {
        return true;
    }
    return cond->at_least ? (v >= cond->value) : (v == cond->value);
}

static bool event_conds_met(const GameState *state, const EventDefinition *def)
{
    uint8_t i;
    for (i = 0; i < def->cond_count; i++) {
        if (!event_condition_met(state, &def->conds[i])) return false;
    }
    return true;
}

static void event_execute_actions(Game *g, const EventDefinition *def,
                                  bool *dialogue_started)
{
    uint8_t i;
    for (i = 0; i < def->action_count; i++) {
        const EventAction *a = &def->actions[i];
        uint8_t t = a->type;
        uint16_t a0 = a->arg0;
        int16_t a1 = a->arg1;
        if (t == EVENT_ACTION_DIALOGUE) {
            dialogue_start_def(&g->dialogue, (DialogueId)a0);
            if (dialogue_started) *dialogue_started = true;
        } else if (t == EVENT_ACTION_SET_FLAG) {
            game_flag_set(&g->state, (FlagId)a0);
        } else if (t == EVENT_ACTION_CLEAR_FLAG) {
            game_flag_clear(&g->state, (FlagId)a0);
        } else if (t == EVENT_ACTION_SET_VARIABLE) {
            game_variable_set(&g->state, (VariableId)a0, a1);
        } else if (t == EVENT_ACTION_ADD_VARIABLE) {
            game_variable_add(&g->state, (VariableId)a0, a1);
        } else if (t == EVENT_ACTION_SCENE_CHANGE) {
            scene_load(g, (SceneId)a0, (uint8_t)a1, (uint8_t)a->arg2);
        } else if (t == EVENT_ACTION_ADD_ITEM) {
            if (deck_collection_add(&g->state.cards, (CardId)a0, (uint8_t)a1)) {
                telemetry_emit(EVENT_CARD_ADDED_TO_COLLECTION, (uint8_t)a0, (uint8_t)a1, 0, 0);
            }
        } else if (t == EVENT_ACTION_ADD_CURRENCY) {
            currency_add(&g->state, (CurrencyId)a0, a1);
        } else if (t == EVENT_ACTION_REMOVE_ITEM) {
            if (deck_collection_remove(&g->state.cards, (CardId)a0, (uint8_t)a1)) {
                telemetry_emit(EVENT_CARD_REMOVED_FROM_COLLECTION, (uint8_t)a0, (uint8_t)a1, 0, 0);
            }
        }
    }
}

static const EventDefinition *event_first_match(Game *g, EventTriggerType trigger,
                                                EntityId actor, MapId map)
{
    uint8_t i;
    MapId match_map;
    if (!g_events) return NULL;
    /* Frozen harness-test fixtures run on TEST_* maps (6..11) while the
     * single event table is written against the real maps (0..5): alias
     * test maps to their real counterparts so one table drives both
     * worlds (no duplicated event content to drift).  Release builds
     * never see the TEST block (guarded at every lookup), so this is a
     * no-op there. */
    match_map = map;
    if (map != EVENT_MAP_ANY && map >= MAP_TEST_FIELD
        && map < (MapId)(MAP_TEST_FIELD + MAP_TEST_COUNT)) {
        match_map = (MapId)(map - MAP_TEST_FIELD);
    }
    for (i = 0; i < g_event_count; i++) {
        const EventDefinition *def = event_get_row(i);
        if (def->trigger != trigger) continue;
        if (actor != ENTITY_ID_NONE && def->actor != ENTITY_ID_NONE && def->actor != actor) continue;
        if (map != EVENT_MAP_ANY && def->map != EVENT_MAP_ANY && def->map != match_map) continue;
        if (!event_conds_met(&g->state, def)) continue;
        return def;
    }
    return NULL;
}

ActorEngageResult event_engage_actor(Game *g, const StaticActorDefinition *actor)
{
    bool dialogue_started = false;
    const EventDefinition *def;

    if (!g || !actor) return ENGAGE_NONE;

    def = event_first_match(g, EVENT_TRIGGER_INTERACT, actor->id, EVENT_MAP_ANY);
    if (def) {
        telemetry_emit(EVENT_SCRIPT_TRIGGERED, (uint8_t)def->id, 0, 0, 0);
        event_execute_actions(g, def, &dialogue_started);
        return dialogue_started ? ENGAGE_DIALOGUE : ENGAGE_NONE;
    }
    return ENGAGE_NONE;
}

void event_resolve_map_enter(Game *g, MapId to_map)
{
    bool dialogue_started = false;
    const EventDefinition *def;

    if (!g) return;

    def = event_first_match(g, EVENT_TRIGGER_MAP_ENTER, ENTITY_ID_NONE, to_map);
    if (def) {
        telemetry_emit(EVENT_SCRIPT_TRIGGERED, (uint8_t)def->id, 0, 0, 0);
        event_execute_actions(g, def, &dialogue_started);
    }
}

void event_resolve_actor_defeated(Game *g, ActorId actor_id, EntityId entity_id)
{
    uint8_t i;
    bool dialogue_started = false;

    /* Runs for EVERY hostile defeat, including non-persistent actors
     * (actor_id 0), so their kills count toward quest progress.  Unlike
     * INTERACT and MAP_ENTER (first-match), ACTOR_DEFEATED events are
     * all-match: a specific defeat event (e.g. the boss) does NOT suppress
     * the generic fallback counter. */
    if (!g || !g_events) return;
    (void)actor_id;

    for (i = 0; i < g_event_count; i++) {
        const EventDefinition *def = event_get_row(i);
        if (def->trigger != EVENT_TRIGGER_ACTOR_DEFEATED) continue;
        if (def->actor != ENTITY_ID_NONE && def->actor != entity_id) continue;
        if (!event_conds_met(&g->state, def)) continue;

        telemetry_emit(EVENT_SCRIPT_TRIGGERED, (uint8_t)def->id, 0, 0, 0);
        event_execute_actions(g, def, &dialogue_started);
    }
}
