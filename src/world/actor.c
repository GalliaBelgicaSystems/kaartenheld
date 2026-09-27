#include "actor.h"
#include "world.h"
#include "game_ids.h"
#include "banked.h"
#include <stddef.h>

/* ── Actor engine ──────────────────────────────────────────────────
 * Per-scene actor definitions live in bank GAME_ACTOR_BANK (4).
 * actor_load_scene() stages the bank-4 loader body
 * (src/world/actor_load_banked.c), which reads the registered tables
 * directly and spawns hostiles into World.actors runtime slots and
 * friendlies into g_static_actors[].  Gameplay lookups are pure WRAM. */

const WorldActorTable *g_actor_registry = NULL;
uint8_t g_actor_registry_count = 0;
/* BSS: actor_register_tables() (via game_content_init() at every boot)
 * sets the home bank before any table is read. */
static uint8_t g_actor_bank;

StaticActorDefinition g_static_actors[MAX_STATIC_ACTORS];
uint8_t g_static_actor_count = 0;

/* Per-slot display-name staging (fixed WRAM, always mapped).  The banked
 * loader copies each hostile's def literal here at spawn; encounter code
 * dereferences it with any ROM bank mapped.  Data-driven: every hostile
 * shows its level-JSON display_name in battle (no fixed-bank table). */
char s_actor_names[MAX_WORLD_ACTORS][12];

void actor_register_tables(const WorldActorTable *tables, uint8_t count, uint8_t bank)
{
    g_actor_registry = tables;
    g_actor_registry_count = count;
    g_actor_bank = bank;
}

uint8_t actor_find_hostile_slot(const World *world, uint8_t x, uint8_t y)
{
    uint8_t i;
    const WorldActorRuntime *a;
    if (!world) return NO_ACTOR_INDEX;
    for (i = 0; i < MAX_WORLD_ACTORS; i++) {
        a = &world->actors[i];
        if (!a->active) continue;
        if (a->x == x && a->y == y) return i;
        if (a->move_state && a->move_target_x == x && a->move_target_y == y) return i;
    }
    return NO_ACTOR_INDEX;
}

const StaticActorDefinition *actor_find_at(const World *world, uint8_t x, uint8_t y)
{
    uint8_t i;
    (void)world;
    for (i = 0; i < g_static_actor_count; i++) {
        if (g_static_actors[i].x == x && g_static_actors[i].y == y) {
            return &g_static_actors[i];
        }
    }
    return NULL;
}

ActorEngageResult actor_engage(const StaticActorDefinition *actor, DialogueState *dialogue)
{
    if (!actor) return ENGAGE_NONE;
    if (actor->flags & ACTOR_FLAG_HOSTILE) {
        return ENGAGE_BATTLE;
    }

    if (actor->interaction == INTERACTION_DIALOGUE) {
        dialogue_start_def(dialogue, actor->dialogue_id);
        return ENGAGE_DIALOGUE;
    }

    if (actor->interaction == INTERACTION_SHOP) {
        return ENGAGE_SHOP;
    }

    if (actor->interaction == INTERACTION_SAVE) {
        return ENGAGE_SAVE;
    }

    return ENGAGE_NONE;
}

void actor_load_scene(World *world, MapId map_id, const GameState *state)
{
    /* Body runs banked (src/world/actor_load_banked.c): the registered
     * tables live in the same ROM bank, so the body reads them directly
     * with no staging copies (AGENTS.md 52.11.1).  Hostile display names
     * are staged into s_actor_names there (fixed WRAM). */
    g_bk_call_bank = GAME_ACTOR_BANK;
    g_bk_call_target = (uint16_t)&actor_load_scene_banked;
    g_bk_ptr_a = (void *)world;
    g_bk_ptr_b = (void *)state;
    g_bk_byte_a = (uint8_t)map_id;
    banked_call_run();
}
