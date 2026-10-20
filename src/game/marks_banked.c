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

/* Alert-first music sequencing, derived from the running alert_ttl
 * countdown (150 at entry on pursuer maps, drained per frame): the
 * track holds at entry, the sting fires at ttl 120 (~30 f later, once
 * the level is loaded and pursuers start moving), and the track resumes
 * at ttl 60 (60 f after the sting; ALERT is 55 tracker ticks ≈ 0.86 s).
 * Zero new WRAM: added BSS bytes shift 256 globals and flip
 * layout-sensitive codegen (AGENTS.md 52.19/52.24, defend_warn_block). */
#define ALERT_STING_TTL 120
#define ALERT_MUSIC_TTL 60

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
            BANKED_VRAM_WRITE(dst, (uint8_t)src[j]);
            dst++;
        }
    }
}

/* Recompute every overworld HUD marker from game state (see ACTOR_MARK_*
 * in actor.h, drawn at fixed OAM entries 30+ by the bank-4 sprite pass):
 * - Quest "?": an NPC whose talk advances or turns in a quest.
 *   Mayor: quest NOT_STARTED, or ACTIVE with 5+ monsters slain
 *   (turn-in ready).  Merchant: quest NOT_STARTED, or ACTIVE while
 *   holding the amulet (delivery ready).  Carl: always, until the
 *   tutorial spar is won (talking early just points at the chest).
 *   Hidden while its quest is in progress and once its reward is
 *   claimed.  The mimic
 *   grants AP but is a hostile, never a quest NPC: no mark by design.
 * - Pursuer "!": live AI_CHASE/AI_CHASE_FAST hostiles from the sting
 *   edge (ttl 120) until the entry alert runs out.  The bubble debuts
 *   with the alert, never before it.  The TTL starts on the first tick
 *   after a map entry with live chasers (self-detected below; the
 *   fixed bank has no room for the scan); the track holds at entry,
 *   the sting fires once the level is loaded and pursuers move, and
 *   the scene track resumes
 *   after it (fixed wrapper: byte_c codes + byte_d replay).
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

    g_bk_byte_c = 0;
    g_bk_byte_d = 0;
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
            /* Alert-first: hold the scene track at once (the fixed
             * wrapper stops it; the sting + resume below ride the
             * running ttl).  Cancelled by any map change. */
            g_bk_byte_c = 1;
        }
    } else if (g_alert_ttl != 0) {
        g_alert_ttl--;
        /* Sting edge at 120 (~30 f after entry: level loaded, pursuers
         * moving) -- never inside a battle / game-over / ending that
         * owns the music (the entry hold already silenced the track).
         * Music resumes at 60, same screen rule (battle exit restores
         * the area track itself; the jingle path owns the title).
         * Menus, dialogue and shops are overworld-attached: the sting
         * and the track resume under them. */
        if (g_alert_ttl == ALERT_STING_TTL &&
            g_game.screen != SCREEN_BATTLE &&
            g_game.screen != SCREEN_GAME_OVER &&
            g_game.screen != SCREEN_ENDING) {
            g_bk_byte_c = 2;
        } else if (g_alert_ttl == ALERT_MUSIC_TTL &&
            g_game.screen != SCREEN_BATTLE &&
            g_game.screen != SCREEN_GAME_OVER &&
            g_game.screen != SCREEN_ENDING) {
            g_bk_byte_d = 1;
        }
    }

    quest_monster_hunt =
        g_game.state.variables.values[VARIABLE_ID_QUEST_MONSTER_HUNT - 1];
    monsters_defeated =
        g_game.state.variables.values[VARIABLE_ID_MONSTERS_DEFEATED - 1];
    merchant_quest =
        g_game.state.variables.values[VARIABLE_ID_MERCHANT_QUEST - 1];
    has_amulet = marks_has_amulet(&g_game.state);
    carl_done = marks_carl_defeated(&g_game.state);

    /* Hostiles: pursuer "!" once the entry sting has fired (ttl at or
     * below the sting edge) until the entry alert runs out.  The bubble
     * debuts with the alert, never before it. */
    for (i = 0; i < MAX_WORLD_ACTORS; i++) {
        const WorldActorRuntime *a = &g_game.world.actors[i];
        uint8_t m = ACTOR_MARK_NONE;
        if (a->active && g_alert_ttl != 0 &&
            g_alert_ttl <= ALERT_STING_TTL &&
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
                /* Tutorial lesson available until the spar is won:
                 * always shown, even before the field chest is opened
                 * (talking early just points at the chest). */
                if (!carl_done) {
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
