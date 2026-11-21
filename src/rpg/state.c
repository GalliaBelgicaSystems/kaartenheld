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

static bool actor_id_valid(ActorId actor_id)
{
    return (actor_id >= 1 && actor_id <= ZONE_DEFEAT_BITS);
}

static void defeat_bit(ActorId actor_id, uint8_t *out_byte, uint8_t *out_mask)
{
    *out_byte = ZONE_DEFEAT_BYTE(actor_id);
    *out_mask = (uint8_t)(1u << ZONE_DEFEAT_BIT(actor_id));
}

bool game_world_actor_is_defeated(const GameState *state, ActorId actor_id)
{
    uint8_t byte, mask;
    if (!state || !actor_id_valid(actor_id)) return false;
    defeat_bit(actor_id, &byte, &mask);
    return (state->world.bits[byte] & mask) != 0;
}

void game_world_set_actor_state(GameState *state, ActorId actor_id,
                                ActorStateId actor_state)
{
    uint8_t byte, mask;
    if (!state || !actor_id_valid(actor_id)) return;
    defeat_bit(actor_id, &byte, &mask);
    if (actor_state == (uint8_t)ACTOR_STATE_DEFEATED) {
        if (state->world.bits[byte] & mask) return;
        state->world.bits[byte] |= mask;
    } else {
        if (!(state->world.bits[byte] & mask)) return;
        state->world.bits[byte] &= (uint8_t)~mask;
    }
    telemetry_emit(EVENT_ACTOR_STATE_CHANGE,
                   (uint8_t)(actor_id & 0xFF),
                   (uint8_t)((actor_id >> 8) & 0xFF),
                   (uint8_t)actor_state, 0);
}
