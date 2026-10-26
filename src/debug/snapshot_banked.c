#pragma bank 2

/* Compiled empty in the release ROM (DEBUG_ONLY_SRCS); this declaration
 * keeps the ISO "empty translation unit" lint clean for that pass. */
typedef int snap_banked_translation_unit_not_empty;

#ifdef DEBUG_BUILD

#include "telemetry.h"
#include "game.h"
#include "audio.h"
#include "actor.h"
#include "banked.h"

/* ── Banked core snapshot (DEBUG-only) ─────────────────────────────
 * Body of debug_snapshot() (src/debug/telemetry.c), relocated to ROM
 * bank 2 so it does not consume the fixed-bank budget.  Reads g_game
 * (WRAM) and its own bank-local helpers; writes g_snap_buf (WRAM).
 * Calls debug_state_snapshot_banked() DIRECTLY -- same bank, legal;
 * going through the fixed wrapper would re-enter the trampoline, which
 * is non-reentrant (src/core/banked.h). */

extern Game g_game;

static uint8_t snap_screen_broad(ScreenId s)
{
    if (s == SCREEN_BATTLE) return 1;
    if (s == SCREEN_GAME_OVER) return 2;
    if (s == SCREEN_THANKS) return 3;
    return 0;
}

static uint8_t snap_write_actors(const World *world, uint8_t *out,
                                 uint8_t max_actors)
{
    uint8_t i, n = 0, slot;
    uint8_t *p = out;

    if (!world || !out || max_actors == 0) return 0;

    for (slot = 0; slot < MAX_WORLD_ACTORS && n < max_actors; slot++) {
        if (world->actors[slot].active) {
            *p++ = (uint8_t)world->actors[slot].id;
            *p++ = world->actors[slot].x;
            *p++ = world->actors[slot].y;
            *p++ = (uint8_t)world->actors[slot].facing;
            n++;
        }
    }

    /* Clamped like actor_find_at (trashed count must not dump garbage
     * rows into the snapshot). */
    for (i = 0; i < g_static_actor_count && i < MAX_STATIC_ACTORS && n < max_actors; i++) {
        *p++ = (uint8_t)g_static_actors[i].id;
        *p++ = g_static_actors[i].x;
        *p++ = g_static_actors[i].y;
        *p++ = g_static_actors[i].facing;
        n++;
    }

    while (n < max_actors) {
        *p++ = 0; *p++ = 0; *p++ = 0; *p++ = 0;
        n++;
    }

    return n;
}

void debug_snapshot_banked(void)
{
    const Game *g = &g_game;
    uint8_t first_hp = 0;
    uint8_t first_active = 0;
    uint8_t i;

    for (i = 0; i < MAX_WORLD_ACTORS; i++) {
        if (g->world.actors[i].active) {
            first_hp = g->world.actors[i].hp;
            first_active = 1;
            break;
        }
    }

    g_snap_buf[0] = snap_screen_broad(g->screen);
    g_snap_buf[1] = g->world.player.position.x;
    g_snap_buf[2] = g->world.player.position.y;
    g_snap_buf[3] = g->world.player.hp;
    g_snap_buf[4] = first_hp;
    g_snap_buf[5] = first_active;
    g_snap_buf[6] = (uint8_t)g_audio_current_track;
    g_snap_buf[7] = (uint8_t)g->battle.turn;
    g_snap_buf[8] = (uint8_t)g->battle.result;
    g_snap_buf[9] = g->battle.player.hp;
    g_snap_buf[10] = g->battle.enemies[g->battle.target_idx].hp;
    g_snap_buf[11] = (uint8_t)g->world.map_id;
    g_snap_buf[12] = g->state.flags.bytes[0];
    g_snap_buf[13] = g->dialogue.active ? 1 : 0;
    g_snap_buf[14] = g->dialogue.current_line;
    g_snap_buf[15] = (uint8_t)g->dialogue.id;
    g_snap_buf[16] = (uint8_t)g->world.player.facing;
    g_snap_buf[17] = g->game_over_choice;
    g_snap_buf[18] = (uint8_t)g->screen;
    g_snap_buf[19] = (uint8_t)g->state.scene.scene_id;

    snap_write_actors(&g->world, &g_snap_buf[SNAPSHOT_BASE_SIZE],
                      MAX_SNAPSHOT_ACTORS);

    g_snap_buf[SNAPSHOT_BATTLE_ENERGY_OFF] = g->battle.energy;
    g_snap_buf[SNAPSHOT_BATTLE_DRAW_OFF] =
        (uint8_t)(g->battle.deck.count - g->battle.deck.draw_idx);
    g_snap_buf[SNAPSHOT_BATTLE_DISCARD_OFF] = g->battle.deck.discard_count;

    /* Same-bank direct call: the extended snapshot builder lives in this
     * bank too (telemetry_snap.c). */
    debug_state_snapshot_banked();
}

#endif /* DEBUG_BUILD */
