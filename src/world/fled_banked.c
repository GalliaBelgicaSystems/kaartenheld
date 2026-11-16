#pragma bank 12

#include "world.h"
#include "banked.h"
#include "game.h"

/* Bank-12 fled body (cold path: only on battle flee). Moved out of the
 * fixed bank to free _HOME headroom for the diagonal wall-hug slide
 * (AGENTS.md 52.18: debug fixed bank had 2B headroom). Pure WRAM
 * reads/writes, no telemetry, no fixed calls, no di/ei. Self-contained:
 * reads Game* from g_bk_ptr_a, never calls fixed-bank code while the
 * content bank is mapped (52.11.1). */
void world_on_battle_fled_banked(void)
{
    World *w;
    WorldActorRuntime *act;
    uint8_t idx;
    uint8_t e;
    Game *g = (Game *)g_bk_ptr_a;
    if (!g) return;
    w = &g->world;

    idx = w->encounter_actor_index;
    w->encounter_actor_index = NO_ACTOR_INDEX;
    w->encounter_cooldown = WORLD_FLEE_GRACE_FRAMES;
    if (idx == NO_ACTOR_INDEX) return;

    act = &w->actors[idx];
    if (act->active) {
        act->hp = 0;
        for (e = 0; e < g->battle.enemy_count; e++) {
            if (g->battle.enemies[e].hp > act->hp) {
                act->hp = g->battle.enemies[e].hp;
            }
        }
    }
}
