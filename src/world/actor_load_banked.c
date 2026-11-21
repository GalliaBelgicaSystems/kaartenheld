/* Test fixtures keep their actor tables in bank 9 (GAME_TEST_ACTOR_BANK):
 * debug bank 4 is full (battle art + actor tables + sprite bodies share
 * it), while the loader body must share its tables' bank (direct reads,
 * AGENTS.md §52.11.1).  Mirrors scene_load.c's TEST_LEVELS bank split. */
#ifdef TEST_LEVELS
#pragma bank 9
#else
#pragma bank 4
#endif

#include "actor.h"
#include "world.h"
#include "rpg/state.h"
#include "banked.h"
#include "telemetry.h"
/* Ring writer (debug): a banked body cannot re-enter the trampoline to
 * reach telemetry_emit_banked (non-reentrant), so the zone wipe writes
 * the ring directly -- same precedent as the game-owned grant bodies
 * (carl_grant_banked.c).  Fixed-bank code keeps using telemetry_emit(). */
#include "telemetry_ring.h"

/* Fixed-WRAM per-slot display-name staging (defined in actor.c, always
 * mapped).  actor_spawn copies the bank-2 def literal here; fixed-bank
 * encounter code then dereferences it with any ROM bank mapped. */
extern char s_actor_names[MAX_WORLD_ACTORS][13];

/* ── Scene actor loader, bank-2 body ────────────────────────────────
 * Dispatched by actor_load_scene() (src/world/actor.c) through the WRAM
 * trampoline.  The registered definition tables live in THIS bank, so
 * the body reads them directly -- the old fixed-bank implementation's
 * per-row banked_copy staging disappears entirely.
 *
 * Self-contained: reads only its own bank-local data, the staged
 * pointers/bytes, and WRAM (always mapped).  Never calls fixed-bank
 * code: the tiny GameState lookups below are deliberate local replicas
 * of game_variable_get() (src/rpg/state.c) -- KEEP IN SYNC with that if
 * its storage layout ever changes.  Defeat checks read the zone
 * bitmask directly (same layout as rpg/state.h WorldState).
 *
 * The loader owns zone settling: when the table's zone differs from the
 * recorded one, the old zone's bits clear (leaving a zone respawns its
 * enemies) and the new zone records.  Zone 0 means unsettled (fresh
 * boot/new game or staged scenario bits): it settles without clearing
 * so staged defeats survive the first load. */

extern StaticActorDefinition g_static_actors[MAX_STATIC_ACTORS];
extern uint8_t g_static_actor_count;
extern const uint8_t g_actor_table_count;   /* generated, bank-local */
extern uint8_t g_oam_tilecache_valid;   /* OAM tile cache (ui bank); WRAM */

static void actor_spawn(WorldActorRuntime *r, const WorldActorDefinition *def)
{
    /* NOTE: display_name bytes are staged into fixed WRAM (s_actor_names)
     * here, where bank 2 (the def literals) is mapped.  Fixed-bank code
     * dereferences the staged pointer with any bank mapped -- hence no
     * fixed-bank name table (fixed _HOME budget, AGENTS.md 55.5). */
    r->actor_id = def->actor_id;
    r->id = def->id;
    r->active = 1;
    r->x = def->x;
    r->y = def->y;
    r->facing = def->facing;
    r->hp = def->hp;
    r->max_hp = def->max_hp;
    r->flags = ACTOR_STATE_NONE;
    r->gold_reward = def->gold_reward;
    r->reward_currency = def->reward_currency;
    /* display_name bytes are staged by the caller into s_actor_names
     * (fixed WRAM); the pointer is assigned there. */
    r->visual = def->visual;
    r->sprite_kind = def->sprite_kind;
    r->ow_type = def->ow_type;
    r->spawn_x = def->x;
    r->spawn_y = def->y;
    r->ai_type = def->ai_type;
    r->ai_step = 0;
    r->ai_timer = PATROL_STEP_INTERVAL;
    r->move_state = 0;
    r->move_target_x = def->x;
    r->move_target_y = def->y;
    r->move_progress = 0;
    r->battle_type = (uint8_t)def->battle_id;
    r->solo = def->solo;
}

/* Zone settle + defeat test live in tiny static helpers, not inline:
 * inlining their live values into actor_load_scene_banked spills the
 * static-copy address math below into ~700 extra bytes (bank-4/9
 * headroom, review).  Same-bank direct calls need no trampoline
 * (actor_spawn precedent); the loader stays the single settle funnel. */
static void zone_sync_banked(GameState *state, uint8_t act)
{
    uint8_t i;
    if (!state || state->world.zone == act) return;
    if (state->world.zone != 0) {
        for (i = 0; i < ZONE_DEFEAT_BYTES; i++) {
            state->world.bits[i] = 0;
        }
        /* Visible wipe (debug builds; the release macro compiles it
         * out): leaving a zone respawns its enemies, and the harness
         * can assert on ZONE_CHANGED instead of inferring it.  Silent
         * settles (zone 0) stay quiet -- every boot/load would spam. */
        telemetry_ring_emit((uint8_t)EVENT_ZONE_CHANGED,
                            state->world.zone, act, 0, 0);
    }
    state->world.zone = act;
}

static uint8_t defeat_test_banked(const GameState *state, uint16_t actor_id)
{
    uint8_t aid;
    if (!state || actor_id == 0 || actor_id > ZONE_DEFEAT_BITS) return 0;
    aid = (uint8_t)actor_id;
    return (uint8_t)((state->world.bits[ZONE_DEFEAT_BYTE(aid)] &
                      (uint8_t)(1u << ZONE_DEFEAT_BIT(aid))) != 0);
}

void actor_load_scene_banked(void)
{
    World *world = (World *)g_bk_ptr_a;
    GameState *state = (GameState *)g_bk_ptr_b;
    MapId map_id = (MapId)g_bk_byte_a;
    uint8_t i, d, slot;

    if (!world) return;

    for (slot = 0; slot < MAX_WORLD_ACTORS; slot++) {
        world->actors[slot].active = 0;
    }
    g_static_actor_count = 0;

    if (!g_actor_registry) return;

    /* The generated count is bank-local (emitted next to g_actor_tables by
     * compile.py), so it is read directly here; g_actor_registry_count is
     * the fixed-bank registration hint only (AGENTS.md 52.2). */
    for (i = 0; i < g_actor_table_count; i++) {
        const WorldActorTable *tbl = &g_actor_registry[i];
        if (tbl->map_id != map_id) continue;

        /* Zone settling (rpg/state.h WorldState): a new zone clears the
         * old zone's defeat bits -- leaving a zone respawns its enemies.
         * Zone 0 settles silently (fresh boot/new game, staged scenario
         * bits): no clear, so setup survives the first load. */
        zone_sync_banked(state, tbl->act);

        slot = 0;
        for (d = 0; d < tbl->count; d++) {
            const WorldActorDefinition *def = &tbl->defs[d];

            /* Bit-test replica of game_world_actor_is_defeated() (helper
             * above): bit (N-1) records ActorId N.  Id 0 is untracked
             * runtime (slime-trio morph, co-located tutor statics); ids
             * past ZONE_DEFEAT_BITS never persist (validate.py rejects
             * them, belt and braces). */
            if (defeat_test_banked(state, def->actor_id)) {
                continue;
            }

            /* Replica of game_variable_get() (src/rpg/state.c):
             * variables.values[] is 1-indexed by VariableId. */
            if (def->spawn_variable != 0 &&
                (!(def->spawn_variable >= 1 &&
                   def->spawn_variable <= MAX_STATE_VARIABLES) ||
                 state->variables.values[def->spawn_variable - 1] !=
                     def->spawn_value)) {
                continue;
            }

            if (def->flags & ACTOR_FLAG_HOSTILE) {
                if (slot < MAX_WORLD_ACTORS) {
                    uint8_t j;
                    char *dst = s_actor_names[slot];
                    const char *src = def->display_name ?
                        def->display_name : "Enemy";
                    actor_spawn(&world->actors[slot], def);
                    /* Stage the def name literal (bank 2 mapped here) into
                     * fixed WRAM, capped like Combatant.name (12+NUL, same
                     * idiom as battle_start in battle.c). */
                    for (j = 0; j < 12 && src[j]; j++) {
                        dst[j] = src[j];
                    }
                    dst[j] = 0;
                    world->actors[slot].display_name = dst;
                    slot++;
                }
            } else if (g_static_actor_count < MAX_STATIC_ACTORS) {
                /* Field-wise copy into the compact StaticActorDefinition
                 * (hostile-only payload dropped): struct assignment
                 * lowers to __memcpy, which lives in the fixed bank and
                 * is unreachable while bank 2 is mapped (see
                 * status_content.c for the identical pattern). */
                g_static_actors[g_static_actor_count].id = def->id;
                g_static_actors[g_static_actor_count].x = def->x;
                g_static_actors[g_static_actor_count].y = def->y;
                g_static_actors[g_static_actor_count].facing = def->facing;
                g_static_actors[g_static_actor_count].visual = def->visual;
                g_static_actors[g_static_actor_count].sprite_kind =
                    def->sprite_kind;
                g_static_actors[g_static_actor_count].ow_type =
                    def->ow_type;
                g_static_actors[g_static_actor_count].interaction =
                    def->interaction;
                g_static_actors[g_static_actor_count].shop_id = def->shop_id;
                g_static_actors[g_static_actor_count].dialogue_id =
                    def->dialogue_id;
                g_static_actor_count++;
            }
        }
        break;
    }
    /* Respawns rewrite resolve inputs: drop the OAM tile cache (WRAM,
     * bank-independent) so the next pass recomputes. */
    g_oam_tilecache_valid = 0;
}
