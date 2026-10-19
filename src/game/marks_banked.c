#pragma bank 8

#include <stdint.h>
#include "banked.h"
#include "game.h"
#include "game_ids.h"
#include "actor.h"
#include "rpg/state.h"
#include "ap_tiles_generated.h"

/* AP tile bytes (png2gb --ap-coords, same order as AP_TILE_IDS). */
#include "gfx/ap_tiles.h"

extern void jingle_full_banked(void);

/* ── Per-frame game tick (bank-8 body) ──────────────────────────────
 * Single trampoline target replacing game_update()'s old standalone
 * jingle dispatch: runs the chest-jingle body, then the overworld HUD
 * marker refresh below.  One dispatch instead of two (fixed-bank
 * budget, AGENTS.md 52.18).  Never calls fixed-bank code (the bank is
 * switched away while it runs). */

/* Carl's tutor hostile actor id (persistent defeat key). */
#define ACTOR_ID_CARL_TUTOR 1

/* static_marks[] in World must track MAX_STATIC_ACTORS (actor.h):
 * world.h cannot include actor.h (circular), so the size is literal
 * there -- fail loudly here instead of silently truncating. */
#if MAX_STATIC_ACTORS != 10
#error "g_static_marks sizing drifted from MAX_STATIC_ACTORS; update the array"
#endif

/* Fixed OAM ids for the HUD AP tiles (must match AP_TILE_* above). */
static const uint8_t s_ap_vram_ids[4] = { AP_TILE_1, AP_TILE_2, EXCLAIM_TILE, QUESTION_TILE };
static uint8_t s_ap_loaded;
static uint8_t s_last_map_init;
static uint8_t s_last_map;

/* Overworld HUD marker state (WRAM BSS globals, NOT World fields: growing
 * World shifts every later Game field and trips the layout-sensitive
 * miscompile family, AGENTS.md 52.19.  Appending globals only costs BSS
 * tail).  g_alert_ttl counts down the pursuit-entry alert; g_actor_marks /
 * g_static_marks carry per-actor marks for the bank-4 OAM pass (fixed
 * entries 30+).  Zero-init: no marks, no alert until the first map-entry
 * scan fires. */
uint8_t g_alert_ttl;
uint8_t g_actor_marks[MAX_WORLD_ACTORS];
uint8_t g_static_marks[10];

static uint8_t marks_flag_set(const GameState *st, uint8_t flag)
{
    uint8_t f = (uint8_t)(flag - 1u);
    return (uint8_t)((st->flags.bytes[(uint8_t)(f >> 3)] >> (uint8_t)(f & 7)) & 1u);
}

static uint8_t marks_has_amulet(const GameState *st)
{
    uint8_t i;
    for (i = 0; i < st->cards.collection.count; i++) {
        if (st->cards.collection.entries[i].id == CARD_AMULET &&
            st->cards.collection.entries[i].count != 0) {
            return 1;
        }
    }
    return 0;
}

static uint8_t marks_carl_defeated(const GameState *st)
{
    uint8_t i;
    for (i = 0; i < st->world.count; i++) {
        if (st->world.actors[i].actor_id == ACTOR_ID_CARL_TUTOR &&
            st->world.actors[i].state == (uint8_t)ACTOR_STATE_DEFEATED) {
            return 1;
        }
    }
    return 0;
}

/* Stream the AP HUD tiles to OAM on the first tick (the OAM passes need
 * them resident before any marker draws; LCD may be on, so every write
 * waits for an accessible VRAM window like the anim swap does). */
static void marks_load_ap_tiles(void)
{
    uint8_t i, j;
    volatile uint8_t *dst;
    const uint8_t *src;

    if (s_ap_loaded) return;
    s_ap_loaded = 1;
    for (i = 0; i < 4; i++) {
        dst = (volatile uint8_t *)(0x8000u + ((uint16_t)s_ap_vram_ids[i] << 4));
        src = ap_tiles + ((uint16_t)i << 4);
        for (j = 0; j < 16; j++) {
            BANKED_VRAM_WRITE(dst, src[j]);
            dst++;
            src++;
        }
    }
}

/* Recompute every overworld HUD marker from game state (see ACTOR_MARK_*
 * in actor.h, drawn at fixed OAM entries 30+ by the bank-4 sprite pass):
 * - Quest "?": an NPC whose talk advances or turns in a quest.
 *   Mayor: quest NOT_STARTED, or ACTIVE with 5+ monsters slain
 *   (turn-in ready).  Merchant: quest NOT_STARTED, or ACTIVE while
 *   holding the amulet (delivery ready).  Carl: the tutorial lesson
 *   is available (field chest open, spar not yet won).  Hidden while a
 *   quest is in progress and once its reward is claimed.  The mimic
 *   grants AP but is a hostile, never a quest NPC: no mark by design.
 * - Pursuer "!": live AI_CHASE/AI_CHASE_FAST hostiles while
 *   alert_ttl runs.  The TTL starts on the first tick after a map entry
 *   with live chasers (self-detected below; the fixed bank has no room
 *   for the scan) -- the fixed wrapper plays SFX_ALERT on that tick.
 * MONSTERS_DEFEATED threshold 5 mirrors the QUEST_COMPLETE event row
 * (events_content.c); keep in sync if that row ever changes. */
static void marks_refresh(void)
{
    uint8_t i;
    uint8_t k;
    uint8_t pursuers;
    int16_t quest_monster_hunt;
    int16_t monsters_defeated;
    int16_t merchant_quest;
    uint8_t has_amulet;
    uint8_t carl_done;
    uint8_t field_chest;

    g_bk_byte_c = 0;
    marks_load_ap_tiles();

    /* Map entry edge: scan once for live pursuers.  s_last_map starts
     * 0, which is a valid map id (MAP_FIELD), so the first tick arms
     * explicitly -- a boot directly onto a pursuer map must alert. */
    if (!s_last_map_init) {
        s_last_map_init = 1;
        s_last_map = 0xFF;
    }
    if (g_game.world.map_id != s_last_map) {
        s_last_map = g_game.world.map_id;
        pursuers = 0;
        for (k = 0; k < MAX_WORLD_ACTORS; k++) {
            const WorldActorRuntime *a = &g_game.world.actors[k];
            if (a->active &&
                (a->ai_type == AI_CHASE || a->ai_type == AI_CHASE_FAST)) {
                pursuers = 1;
                break;
            }
        }
        g_alert_ttl = pursuers ? ALERT_TTL_FRAMES : 0;
        if (pursuers) {
            g_bk_byte_c = 1;
        }
    } else if (g_alert_ttl != 0) {
        g_alert_ttl--;
    }

    quest_monster_hunt =
        g_game.state.variables.values[VARIABLE_ID_QUEST_MONSTER_HUNT - 1];
    monsters_defeated =
        g_game.state.variables.values[VARIABLE_ID_MONSTERS_DEFEATED - 1];
    merchant_quest =
        g_game.state.variables.values[VARIABLE_ID_MERCHANT_QUEST - 1];
    has_amulet = marks_has_amulet(&g_game.state);
    carl_done = marks_carl_defeated(&g_game.state);
    field_chest = marks_flag_set(&g_game.state, STORY_FLAG_ID_FIELD_CHEST);

    /* Hostiles: pursuer "!" while the entry alert runs. */
    for (i = 0; i < MAX_WORLD_ACTORS; i++) {
        const WorldActorRuntime *a = &g_game.world.actors[i];
        uint8_t m = ACTOR_MARK_NONE;
        if (a->active && g_alert_ttl != 0 &&
            (a->ai_type == AI_CHASE || a->ai_type == AI_CHASE_FAST)) {
            m = ACTOR_MARK_ALERT;
        }
        g_actor_marks[i] = m;
    }

    /* Statics: quest "?" from quest state. */
    for (i = 0; i < MAX_STATIC_ACTORS; i++) {
        uint8_t m = ACTOR_MARK_NONE;
        if (i < g_static_actor_count) {
            const StaticActorDefinition *d = &g_static_actors[i];
            if (d->id == ENTITY_ID_MAYOR) {
                if (quest_monster_hunt == 0 ||
                    (quest_monster_hunt == 1 && monsters_defeated >= 5)) {
                    m = ACTOR_MARK_QUEST;
                }
            } else if (d->id == ENTITY_ID_CARL) {
                if (field_chest && !carl_done) {
                    m = ACTOR_MARK_QUEST;
                }
            } else if (d->id == ENTITY_ID_MERCHANT) {
                if (merchant_quest == 0 ||
                    (merchant_quest == 1 && has_amulet)) {
                    m = ACTOR_MARK_QUEST;
                }
            }
        }
        g_static_marks[i] = m;
    }
}

extern void jingle_full_banked(void);

void game_frame_tick_banked(void)
{
    jingle_full_banked();
    marks_refresh();
}
