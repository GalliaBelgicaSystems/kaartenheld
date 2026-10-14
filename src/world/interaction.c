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

    if (!g) return ENGAGE_NONE;

    actor = actor_find_at(&g->world, target_x, target_y);
    if (!actor) return ENGAGE_NONE;

    telemetry_emit(EVENT_ACTOR_INTERACTION, target_x, target_y,
                   (uint8_t)actor->id, (uint8_t)actor->interaction);

    /* No hostile branch: statics are never hostile (actor_load_banked
     * spawns every HOSTILE-flagged def into the runtime slots; only
     * non-hostiles reach g_static_actors).  Hostile bumps resolve as
     * movement encounters, never here. */
    result = event_engage_actor(g, actor);
    if (result != ENGAGE_NONE) {
        return result;
    }
    result = actor_engage(actor, &g->dialogue);
    if (result == ENGAGE_SHOP) {
        g->shop_id = actor->shop_id;
    }
    /* Lesson tile (Carl): facing-talk must sync the co-located
     * spar exactly like the bump path does, or closing the lesson
     * returns to the overworld with no fight.  The scan mirrors
     * actor_find_hostile_slot() and skips inactive slots, so post-victory
     * the remedial lesson just repeats.  Gated on dialogue.active: shop/
     * save/failed talks change nothing.
     *
     * Stale-index safety is structural, not checked: every gameplay
     * writer of encounter_actor_index stores NO or a live slot
     * (init/clears, the encounter branch, this sync -- all sources
     * skip inactive actors), dialogues are modal, and every battle
     * exit clears it, so dialogue_screen's != NO test cannot meet a
     * dead slot.
     *
     * Constraint: this block is skipped when event_engage_actor
     * handles the tile first (early return above).  Never put an
     * INTERACT event on a lesson tile -- its dialogue would open
     * with no synced spar and the auto-start would silently break. */
    if (result == ENGAGE_DIALOGUE && g->dialogue.active) {
        g->world.encounter_actor_index =
            actor_find_hostile_slot(&g->world, target_x, target_y);
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
