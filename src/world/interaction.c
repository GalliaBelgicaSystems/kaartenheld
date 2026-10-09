#include "interaction.h"
#include "game.h"
#include "event.h"
#include "telemetry.h"
#include "story.h"
#include <stddef.h>

ActorEngageResult interaction_try_at(Game *g, uint8_t target_x, uint8_t target_y)
{
    const StaticActorDefinition *actor;
    ActorEngageResult result;
    uint8_t slot;

    if (!g) return ENGAGE_NONE;

    actor = actor_find_at(&g->world, target_x, target_y);
    if (!actor) return ENGAGE_NONE;

    telemetry_emit(EVENT_ACTOR_INTERACTION, target_x, target_y,
                   (uint8_t)actor->id, (uint8_t)actor->interaction);

    if (actor->flags & ACTOR_FLAG_HOSTILE) {
        slot = actor_find_hostile_slot(&g->world, target_x, target_y);
        g->world.encounter_actor_index = slot;
        return ENGAGE_BATTLE;
    }

    result = event_engage_actor(g, actor);
    if (result != ENGAGE_NONE) {
        return result;
    }
    result = actor_engage(actor, &g->dialogue);
    if (result == ENGAGE_SHOP) {
        g->shop_id = actor->shop_id;
    }
    return result;
}

ActorEngageResult interaction_try_facing(Game *g)
{
    const StaticActorDefinition *actor;
    uint8_t tx, ty;

    if (!g) return ENGAGE_NONE;

    tx = g->world.player.position.x;
    ty = g->world.player.position.y;
    if (g->world.player.facing == DIRECTION_UP) ty--;
    else if (g->world.player.facing == DIRECTION_DOWN) ty++;
    else if (g->world.player.facing == DIRECTION_LEFT) tx--;
    else if (g->world.player.facing == DIRECTION_RIGHT) tx++;

    actor = actor_find_at(&g->world, tx, ty);
    telemetry_emit(EVENT_INTERACTION_ATTEMPT, tx, ty,
                   (uint8_t)g->world.player.facing,
                   actor ? (uint8_t)actor->id : 0);

    return interaction_try_at(g, tx, ty);
}

ActorEngageResult interaction_try_bump(Game *g, int8_t dx, int8_t dy)
{
    if (!g || (dx == 0 && dy == 0)) return ENGAGE_NONE;
    return interaction_try_at(g, (uint8_t)(g->world.player.position.x + dx),
                                 (uint8_t)(g->world.player.position.y + dy));
}

void interaction_on_dialogue_end(DialogueState *dialogue, GameState *state)
{
    if (dialogue && state && dialogue->completion_flag != 0) {
        story_set_flag(state, (FlagId)dialogue->completion_flag);
    }
}
