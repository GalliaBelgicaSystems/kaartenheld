#pragma bank 7

#include "world.h"
#include "game.h"
#include "banked.h"

/* Banked body of world_on_battle_fled() (src/world/world.c).  Pure WRAM
 * reads/writes on the staged Game pointer; calls no functions at all, so
 * it is trivially self-contained (AGENTS.md 52.11.1).  Lives in bank 7
 * (not bank 3) to relieve the fixed-adjacent bank-3 budget. */

void world_on_battle_fled_banked(void)
{
    Game *g = (Game *)g_bk_ptr_a;
    World *w;
    uint8_t idx;

    if (!g) return;
    w = &g->world;

    idx = w->encounter_actor_index;
    w->encounter_actor_index = NO_ACTOR_INDEX;
    if (idx == NO_ACTOR_INDEX) return;

    if (w->actors[idx].active) {
        w->actors[idx].hp = g->battle.enemies[0].hp;
    }
}
