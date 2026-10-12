#pragma bank 3

#include "world.h"
#include "game.h"
#include "banked.h"

/* Banked body of the lesson-sync in interaction_try_at()
 * (src/world/interaction.c, DEBUG builds only).  Syncs
 * encounter_actor_index from the talked-to tile: the live hostile
 * there, or NO_ACTOR_INDEX.
 *
 * DEBUG ONLY (see DEBUG_ONLY_SRCS in the Makefile): the debug fixed
 * bank has 17 B of headroom, so even the scan cannot live there;
 * debug bank 3 has room.  Release builds run the same sync inline
 * (release fixed bank is roomy); both derive the tile from facing
 * and mirror actor_find_hostile_slot(), so the semantics match.
 *
 * The tile is derived from the player's facing, NOT staged: every
 * interaction caller (overworld A-key facing path, BLOCKED bump path)
 * talks to the facing tile -- world_try_begin_move syncs facing from
 * (dx,dy) on entry, so bump and facing agree by construction.  A
 * future caller passing a non-facing tile must stage x/y explicitly.
 *
 * The wrapper runs on every talk; only a freshly opened dialogue
 * needs a synced spar, hence the dialogue.active gate.  Shop/save/
 * failed talks leave dialogue inactive and change nothing.  The scan
 * skips inactive slots, so post-victory the remedial lesson just
 * repeats.
 *
 * Self-contained scan -- a banked target must not call fixed-bank
 * functions while a content bank is selected (AGENTS.md 52.11.1). */

void lesson_sync_banked(void)
{
    Game *g = (Game *)g_bk_ptr_a;
    World *w;
    uint8_t x;
    uint8_t y;
    uint8_t i;
    WorldActorRuntime *a;

    if (!g) return;
    if (!g->dialogue.active) return;
    w = &g->world;
    x = w->player.position.x;
    y = w->player.position.y;
    if (w->player.facing == DIRECTION_UP) y--;
    else if (w->player.facing == DIRECTION_DOWN) y++;
    else if (w->player.facing == DIRECTION_LEFT) x--;
    else if (w->player.facing == DIRECTION_RIGHT) x++;

    w->encounter_actor_index = NO_ACTOR_INDEX;
    for (i = 0; i < MAX_WORLD_ACTORS; i++) {
        a = &w->actors[i];
        if (!a->active) continue;
        if (a->x == x && a->y == y) {
            w->encounter_actor_index = i;
            return;
        }
        if (a->move_state && a->move_target_x == x && a->move_target_y == y) {
            w->encounter_actor_index = i;
            return;
        }
    }
}
