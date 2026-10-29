#pragma bank 2

#include <stdint.h>
#include "banked.h"
#include "game.h"
#include "game_ids.h"

extern uint8_t g_oam_tilecache_valid;

/* Slime-trio spawn (game content, ROM bank 2).  Dispatched from
 * the event executor's EVENT_ACTION_SPAWN_SLIME_TRIO branch via the
 * WRAM trampoline (nothing staged: this body resolves its own ow byte
 * from the event row, keeping the fixed-bank dispatch to branch +
 * call only).
 *
 * The slime knight's armor breaks open on defeat: its runtime slot
 * becomes a slime-trio encounter in place (same tile, solo=0 so the
 * stock trio clones engage).  Finds the felled knight slot by entity
 * + inactive (the defeat path deactivates first); silent no-op if
 * none matches.  Reuses the slot, so no scene rows and no actor cap
 * are consumed.  The ow byte comes from the event row itself (same
 * bank: direct reads), pinned to the resolver by validate.py
 * --spawn-refs, never hand-synced.
 *
 * Dynamic spawn (actor_id 0, untracked): like all runtime actors it
 * rebuilds from definitions on scene load, so the trio despawns on
 * reload (the knight stays defeated).  Documented, not a bug: the
 * bonus trio is a one-time session reward for the kill, consistent
 * with non-persistent runtime state (AGENTS.md §53.1).  ai_type is
 * CHASE (not stationary): the trio surges toward the player, so the
 * walkthrough and any nearby hero reliably re-engage it wherever
 * the knight fell.
 *
 * Self-contained: same-bank reads (event table), WRAM
 * (g_game, s_actor_names), compile-time constants.  No fixed-bank
 * calls. */
extern char s_actor_names[MAX_WORLD_ACTORS][13];
extern const EventDefinition g_events[];

void game_slime_trio_spawn_banked(void)
{
    World *w = &g_game.world;
    uint8_t ow = 8;
    uint8_t i, j;
    uint8_t found;

    /* Resolve our ow byte from our own event row (first
     * SPAWN_SLIME_TRIO action in the table). */
    found = 0;
    for (i = 0; i < GAME_EVENT_COUNT; i++) {
        const EventDefinition *d = &g_events[i];
        for (j = 0; j < d->action_count && j < MAX_EVENT_ACTIONS; j++) {
            if (d->actions[j].type == EVENT_ACTION_SPAWN_SLIME_TRIO) {
                ow = d->actions[j].arg0;
                found = 1;
                break;
            }
        }
        if (found) break;
    }
    if (!found) return;

    for (i = 0; i < MAX_WORLD_ACTORS; i++) {
        WorldActorRuntime *a = &w->actors[i];
        if (a->id == ENTITY_ID_SLIME_KNIGHT && !a->active) {
            a->active = 1;
            a->id = (EntityId)ENTITY_ID_SLIME;
            a->hp = 10;
            a->max_hp = 10;
            a->flags = ACTOR_STATE_NONE;
            a->gold_reward = 5;
            a->reward_currency = CURRENCY_ID_GOLD;
            s_actor_names[i][0] = 'S';
            s_actor_names[i][1] = 'L';
            s_actor_names[i][2] = 'I';
            s_actor_names[i][3] = 'M';
            s_actor_names[i][4] = 'E';
            s_actor_names[i][5] = '\0';
            a->display_name = s_actor_names[i];
            a->visual = 'E';
            a->sprite_kind = SPRITE_KIND_ENEMY;
            a->ow_type = ow;
            a->spawn_x = a->x;
            a->spawn_y = a->y;
            a->ai_type = AI_CHASE;
            a->ai_step = 0;
            a->ai_timer = 0;
            a->move_state = 0;
            a->move_target_x = a->x;
            a->move_target_y = a->y;
            a->move_progress = 0;
            a->battle_type = BATTLE_SLIME;
            a->solo = 0;
            a->actor_id = 0;
            /* The morph rewrites resolve inputs (kind/visual/ow/ai) in
             * place: drop the OAM tile cache (WRAM, bank-independent)
             * so the next pass recomputes. */
            g_oam_tilecache_valid = 0;
            return;
        }
    }
}
