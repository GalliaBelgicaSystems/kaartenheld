#include "rpg/state.h"
#include "telemetry.h"
#include <stddef.h>

static bool flag_id_valid(FlagId flag)
{
    return (flag >= 1 && flag <= MAX_STATE_FLAGS);
}

static bool variable_id_valid(VariableId variable)
{
    return (variable >= 1 && variable <= MAX_STATE_VARIABLES);
}

void game_state_zero(GameState *state)
{
    uint8_t *p = (uint8_t *)state;
    size_t n = sizeof(GameState);
    if (!state) return;
    while (n--) *p++ = 0;
}

static uint8_t flag_byte_bit(FlagId flag, uint8_t *out_mask)
{
    uint8_t f = (uint8_t)(flag - 1);
    *out_mask = (uint8_t)(1U << (f & 7));
    return (uint8_t)(f >> 3);
}

bool game_flag_is_set(const GameState *state, FlagId flag)
{
    uint8_t mask, byte;
    if (!state || !flag_id_valid(flag)) return false;
    byte = flag_byte_bit(flag, &mask);
    return (state->flags.bytes[byte] & mask) != 0;
}

void game_flag_set(GameState *state, FlagId flag)
{
    uint8_t mask, byte;
    if (!state || !flag_id_valid(flag)) return;
    byte = flag_byte_bit(flag, &mask);
    if (!(state->flags.bytes[byte] & mask)) {
        state->flags.bytes[byte] |= mask;
        telemetry_emit(EVENT_STORY_FLAG_SET, (uint8_t)flag, 0, 0, 0);
    }
}

void game_flag_clear(GameState *state, FlagId flag)
{
    uint8_t mask, byte;
    if (!state || !flag_id_valid(flag)) return;
    byte = flag_byte_bit(flag, &mask);
    if (state->flags.bytes[byte] & mask) {
        state->flags.bytes[byte] &= (uint8_t)~mask;
        telemetry_emit(EVENT_STORY_FLAG_CLEARED, (uint8_t)flag, 0, 0, 0);
    }
}

int16_t game_variable_get(const GameState *state, VariableId variable)
{
    if (!state || !variable_id_valid(variable)) return 0;
    return state->variables.values[variable - 1];
}

void game_variable_set(GameState *state, VariableId variable, int16_t value)
{
    if (!state || !variable_id_valid(variable)) return;
    state->variables.values[variable - 1] = value;
    telemetry_emit(EVENT_VARIABLE_SET, (uint8_t)variable,
                   (uint8_t)(value & 0xFF), (uint8_t)((value >> 8) & 0xFF), 0);
}

void game_variable_add(GameState *state, VariableId variable, int16_t amount)
{
    if (!state || !variable_id_valid(variable)) return;
    game_variable_set(state, variable, (int16_t)(state->variables.values[variable - 1] + amount));
}

static PersistentActorState *find_actor_state(GameState *state, ActorId actor_id)
{
    uint8_t i;
    if (!state || actor_id == 0) return 0;
    for (i = 0; i < state->world.count; i++) {
        if (state->world.actors[i].actor_id == actor_id) return &state->world.actors[i];
    }
    return 0;
}

bool game_world_actor_is_defeated(const GameState *state, ActorId actor_id)
{
    const PersistentActorState *a = find_actor_state((GameState *)state, actor_id);
    return a ? (a->state == (uint8_t)ACTOR_STATE_DEFEATED) : false;
}

void game_world_set_actor_state(GameState *state, ActorId actor_id,
                                ActorStateId actor_state)
{
    PersistentActorState *a;
    if (!state || actor_id == 0) return;

    a = find_actor_state(state, actor_id);
    if (a) {
        if (a->state == (uint8_t)actor_state) return;
    } else {
        if (state->world.count >= MAX_PERSISTENT_ACTORS) return;
        a = &state->world.actors[state->world.count++];
        a->actor_id = actor_id;
    }
    a->state = (uint8_t)actor_state;
    telemetry_emit(EVENT_ACTOR_STATE_CHANGE,
                   (uint8_t)(actor_id & 0xFF),
                   (uint8_t)((actor_id >> 8) & 0xFF),
                   (uint8_t)actor_state, 0);
}
