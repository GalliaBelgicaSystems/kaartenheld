#pragma bank 8

#include <stdint.h>
#include <gb/gb.h>
#include "banked.h"
#include "game.h"
#include "game_ids.h"
#include "actor.h"
#include "rpg/state.h"
#include "rng.h"
#include "rpg/loot.h"
#include "game/gamble.h"
#include "ap_tiles_generated.h"

/* Card-menu material OAM icon ids/slots + canonical OBJ restore bytes
 * (palette shade maps per inventory ramp, OBJ slots from the obj
 * aliases).  Ramp bytes + tile art live with the bank-2 menu body
 * that streams them (same-bank rule); this side only restores. */
#define MAT_NEED_RESTORE_BYTES
#include "material_tiles_generated.h"

/* OAM map (hero idle base, blob base) for the overworld-entry restore.
 * Header-only (macros + declarations, no code), safe in this bank. */
#include "ui/ui.h"

/* AP tile bytes (png2gb --ap-coords, same order as AP_TILE_IDS). */
#include "gfx/ap_tiles.h"

/* Battle rider icon bytes (png2gb --rider-coords, BURN/FREEZE/POISON order
 * matching RIDER_TILE_*).  Same-bank data for the battle stream below. */
#include "rider_tiles_generated.h"
#include "gfx/rider_tiles.h"

/* Menu AP pip BG bytes (ap_pips_bg.png, cost order 1-5).  Same-bank data
 * for the ITEM-tick stream below (bank 8 overflow bank, room). */
#include "gfx/ap_pip_tiles.h"

/* Hero overworld bytes (png2gb whole-sheet, walk frames 1 & 2 then idle
 * frames 1 & 2).  Bank-8 home for both the boot stream (ui_sprite_init
 * reads through banked_copy) and the overworld-entry restore below, so
 * the fixed bank carries no hero art (AGENTS.md 52.18). */
#include "gfx/hero_desolate_sprite_tile.h"

/* Shared overworld blob bytes (enemy_ow_content.c, same bank: extern
 * read for the restore -- never include the header here, it would
 * duplicate the 416-byte array in this bank). */
extern const uint8_t enemy_ow_tiles[];

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
static const uint8_t s_ap_vram_ids[7] = { AP_TILE_1, AP_TILE_2, AP_TILE_3, AP_TILE_4, AP_TILE_5, EXCLAIM_TILE, QUESTION_TILE };
/* Fixed OAM ids for the battle rider icons (must match RIDER_TILE_*:
 * time-share blob-tail ids 123-125, restored on overworld entry). */
static const uint8_t s_rider_vram_ids[3] = { RIDER_TILE_BURN, RIDER_TILE_FREEZE, RIDER_TILE_POISON };
static uint8_t s_mark_tiles_loaded;
static uint8_t s_battle_tiles_loaded;
static uint8_t s_ap_pip_loaded;

/* Program one OBJ palette slot from 8 CRAM bytes, LCD-safely (same
 * bounded Mode-wait pattern as the tile streamer above; the timer ISR
 * can eclipse a bare wait->store window while the LCD is on). */
static void ow_obj_program(uint8_t slot, const uint8_t *src)
{
    uint8_t c;
    OCPS_REG = (uint8_t)(0x80 | (slot << 3));
    for (c = 0; c < 8; c++) {
        BANKED_VRAM_WRITE(&OCPD_REG, src[c]);
    }
}

/* Stream one OAM tile (16 bytes) LCD-safely.  Single copy shared by the
 * mark/battle loaders and the overworld-entry restore below (bank 8 stays
 * lean: three inlined loops would triple this). */
static void ow_oam_stream_cell(uint8_t oam_id, const uint8_t *src)
{
    uint8_t j;
    volatile uint8_t *dst;
    dst = (volatile uint8_t *)(0x8000u + ((uint16_t)oam_id << 4));
    for (j = 0; j < 16; j++) {
        BANKED_VRAM_WRITE(dst, (uint8_t)src[j]);
        dst++;
    }
}
static uint8_t s_last_map_init;
static uint8_t s_last_map;

/* Alert sting sequencing, derived from the running alert_ttl countdown
 * (150 when a chaser spots the hero -- on the first overworld tick if one
 * spawns within sight -- drained per frame): the scene track KEEPS PLAYING
 * underneath and the SFX_ALERT sting plays on top of it at ttl 120 (~30 f
 * after the wake-up, once the chaser is awake and moving).  Coexistence
 * rides the normal SFX path: audio_play_sfx() mutes the tracker's CH2
 * voice for the 55-tick sting, then unmutes it, so the song continues on
 * the other voices and resumes fully after -- never a pause, never a
 * restart, no MUSIC_CHANGED (a sting is not a track change). */
#define ALERT_STING_TTL 120

/* Overworld HUD marker state (WRAM BSS globals, NOT World fields: growing
 * World shifts every later Game field and trips the layout-sensitive
 * miscompile family, AGENTS.md 52.19.  Appending globals only costs BSS
 * tail).  g_alert_ttl counts down the pursuit alert; g_actor_marks /
 * g_static_marks carry per-actor marks for the bank-4 OAM pass (fixed
 * entries 30+).  Zero-init: no marks, no alert until a chaser spots the
 * hero (sight scan in marks_refresh). */
uint8_t g_alert_ttl;
uint8_t g_actor_marks[MAX_WORLD_ACTORS];
uint8_t g_static_marks[10];

/* Retired music-pause flag (WRAM BSS): kept as a 1-byte placeholder so
 * later BSS globals keep their addresses (AGENTS.md 52.19).  The pursuit
 * alert sting plays over the running track via the normal SFX mute path,
 * so nothing ever reads or writes this byte. */
uint8_t g_music_paused;

/* Jingle edge bits (jingle_full_banked.c, same bank): bit 7 is the
 * seeded flag.  load_game_slot clears the whole byte on a real load;
 * the re-arm check above reads the pre-seed value (marks runs first). */
extern uint8_t g_jingle_done;

#define MARKS_FLAG_SET(id) \
    ((g_game.state.flags.bytes[((id) - 1) >> 3] & (uint8_t)(1u << (((id) - 1) & 7))) != 0)

static uint8_t marks_has_card(uint8_t id)
{
    uint8_t i;
    for (i = 0; i < g_game.state.cards.collection.count; i++) {
        if (g_game.state.cards.collection.entries[i].id == id &&
            g_game.state.cards.collection.entries[i].count != 0) {
            return 1;
        }
    }
    return 0;
}

static uint8_t marks_carl_defeated(void)
{
    uint8_t i;
    for (i = 0; i < g_game.state.world.count; i++) {
        if (g_game.state.world.actors[i].actor_id == ACTOR_ID_CARL_TUTOR &&
            g_game.state.world.actors[i].state == (uint8_t)ACTOR_STATE_DEFEATED) {
            return 1;
        }
    }
    return 0;
}

/* ── Carl's "leaving without the lesson" offer ─────────────────────
 * Lives in src/game/carl_offer_banked.c (ROM bank 10: bank 8 sits hard
 * against 16 KB).  Dispatched from the per-frame tick below through the
 * WRAM trampoline; see that file for the contract. */

/* Stream the overworld HUD mark glyphs (!/?) to OAM 126-127 on the first
 * tick (the OAM passes need them resident before any marker draws; LCD
 * may be on, so every write waits for an accessible VRAM window like the
 * anim swap does).  These ids are free on every screen, so this runs
 * ungated like before. */
static void marks_load_mark_tiles(void)
{
    uint8_t i;

    if (s_mark_tiles_loaded) return;
    s_mark_tiles_loaded = 1;
    for (i = 5; i < 7; i++) {
        ow_oam_stream_cell(s_ap_vram_ids[i], ap_tiles + ((uint16_t)i << 4));
    }
}

/* Stream the battle OAM tiles on the first battle/item tick (same LCD-safe
 * pattern).  AP pips (ap_tiles 0-4) plus the rider icons (rider_tiles,
 * over blob-tail ids 123-125): 8 cells, one loop, one flag.
 * Battle/item-only: in the overworld OAM 96-99 hold the standing-hero
 * idle/walk frames and 122-125 the blob tail, so streaming here would
 * clobber live overworld art (overworld entry restores both and re-arms
 * this). */
static void marks_load_battle_tiles(void)
{
    uint8_t i;

    if (s_battle_tiles_loaded) return;
    s_battle_tiles_loaded = 1;
    for (i = 0; i < 5; i++) {
        ow_oam_stream_cell(s_ap_vram_ids[i], ap_tiles + ((uint16_t)i << 4));
    }
    for (i = 0; i < 3; i++) {
        ow_oam_stream_cell(s_rider_vram_ids[i], rider_tiles + ((uint16_t)i << 4));
    }
}

/* Re-arm the battle tile stream after overworld entry restores the
 * time-shared OAM ids (AP pips 1-4 over hero idle/walk at 96-99, pip 5
 * plus riders over the blob tail at 122-125, material icons over blob
 * offsets 17-21 at 117-121).
 * No-arg trampoline target for the fixed-bank overworld-entry path. */
void marks_battle_tiles_invalidate_banked(void)
{
    /* Re-arm both streams: AP pips/riders time-share OAM ids with
     * overworld art (96-99 hero, 122-125 blob tail), and the !/? marks
     * (126-127) are streamed once per boot.  The material icons (streamed
     * by the bank-2 menu render, restored with the rider tail below)
     * need no flag: renders always precede their stamps.  screen_change()
     * calls this on every BATTLE/ITEM/SHOP entry so the next tick
     * restreams before the first render -- even if the flag powered up
     * set (harness skips CRT0, so BSS is not cleared) or VRAM was
     * clobbered by a path that did not clear the flag, the pips would
     * otherwise show hero pixels with correct OAM ids (all id-only
     * asserts green). */
    s_battle_tiles_loaded = 0;
    s_mark_tiles_loaded = 0;
    s_ap_pip_loaded = 0;
}

/* Overworld-entry OAM restore (same bank: direct reads; LCD-safe writes).
 * Battles time-share OAM ids with overworld art, so coming home restreams
 * the overworld side: hero idle frames 1 & 2 to 96-97 and walk frames 1 &
 * 2 to 98-99 (battles stream AP pips 1-4 over them) and the blob tail
 * (offset 22 plus offsets 23-25: chest-open + kobold/knight idle) to
 * 122-125 (battles stream AP pip 5 over 122 and rider icons over
 * 123-125).  Every write waits for an accessible VRAM window (same
 * pattern as the battle loader above), so this is safe with the LCD on
 * and under the harness.  No-arg trampoline target behind
 * ui_restore_ow_oam(). */
void ow_oam_restore_banked(void)
{
    uint8_t i;

    for (i = 0; i < 2; i++) {
        ow_oam_stream_cell((uint8_t)(HERO_IDLE_SPRITE_TILE_ID + i),
                           hero_desolate_sprite_tile + 32 + ((uint16_t)i << 4));
    }
    for (i = 0; i < 2; i++) {
        ow_oam_stream_cell((uint8_t)(HERO_DESOLATE_SPRITE_TILE_ID + i),
                           hero_desolate_sprite_tile + ((uint16_t)i << 4));
    }
    /* AP pip 5 time-shares blob offset 22 (OAM 122): restore it from the
     * blob like the rider tail below.  AP_TILE_5 == RIDER_TILE_BURN - 1
     * only by adjacency, so spell out the tile and offset plainly. */
    ow_oam_stream_cell(AP_TILE_5, enemy_ow_tiles + ((uint16_t)(AP_TILE_5 - ENEMY_OW_BASE) << 4));
    for (i = 0; i < 3; i++) {
        ow_oam_stream_cell((uint8_t)(RIDER_TILE_BURN + i),
                           enemy_ow_tiles + (((uint16_t)(RIDER_TILE_BURN - ENEMY_OW_BASE + i)) << 4));
    }
    /* Card-menu material icons time-share blob offsets 17-21 (OAM
     * 117-121): restore them from the blob the same way.  Overworld
     * actors using those cells were hidden while the menu was open
     * (screen_change hides all OAM off-overworld), so the restore is
     * invisible. */
    for (i = 0; i < 5; i++) {
        ow_oam_stream_cell((uint8_t)(MAT_TILE_STEEL + i),
                           enemy_ow_tiles + ((uint16_t)(17 + i) << 4));
    }
    /* The menu also borrows OBJ slots 0/1/5/6/7 for the inventory ramps:
     * restore the canonical overworld ramps so heroes, mimics, wizards,
     * merchants and mayors read their own colors again.  Battle entry
     * reprograms its own scratch slots itself, and riders (slots 2-4)
     * are never touched by the menu. */
    if (g_is_cgb) {
        for (i = 0; i < 5; i++) {
            ow_obj_program(mat_obj_restore_slots[i],
                           mat_obj_restore_bytes + ((uint16_t)i << 3));
        }
    }
    s_battle_tiles_loaded = 0;
    s_ap_pip_loaded = 0;
}

/* Recompute every overworld HUD marker from game state (see ACTOR_MARK_*
 * in actor.h, drawn at fixed OAM entries 30+ by the bank-4 sprite pass):
 * - Quest "?": an NPC whose talk advances or turns in a quest.
 *   Mayor: quest NOT_STARTED, or ACTIVE with 5+ monsters slain
 *   (turn-in ready).  Merchant: quest NOT_STARTED, or ACTIVE while
 *   holding the shop keys (delivery ready).  Heal Mage: owns the Iron
 *   Ring and hasn't heard the ring-tactics lesson yet.  Carl: always,
 *   until the
 *   tutorial spar is won.  Village dog: until it hands over the
 *   Old Key for the skeleton's bone.
 *   Hidden while its quest is in progress and once its reward is
 *   claimed.  The mimic
 *   grants AP but is a hostile, never a quest NPC: no mark by design.
 * - Pursuer "!": awake (alerted) AI_CHASE/AI_CHASE_FAST hostiles from
 *   the sting edge (ttl 120) until the alert runs out.  The bubble
 *   debuts with the alert, never before it.  The TTL starts when the
 *   hero first comes within a chaser's line of sight (sight scan below;
 *   the fixed bank has no room for the scan); the sting fires once the
 *   chaser is awake and moving, over the still-playing scene track
 *   (no byte codes, no MUSIC_CHANGED -- a sting is not a track change).
 * MONSTERS_DEFEATED threshold 3 mirrors the QUEST_COMPLETE event row
 * (events_content.c); keep in sync if that row ever changes. */
static void marks_refresh(void)
{
    uint8_t i;

    g_bk_byte_c = 0;
    g_bk_byte_d = 0;
    /* Mark glyphs (!/? at 126-127) stream on the first tick of any screen
     * (free ids everywhere).  Pips + riders stream only on
     * battle/item/shop ticks: in the overworld those ids hold hero
     * idle/walk + blob-tail art, so a shop row painted a rider OAM id
     * without this shows blob art (e.g. a guard sprite for poison).
     * Same-tick guarantee as before (this runs in game_update
     * before the render), so the first battle/item/shop frame already
     * draws resident art.  Shop exit needs no restore: overworld entry
     * restreams the blob side (ui_restore_ow_oam). */
    marks_load_mark_tiles();
    if (g_game.screen == SCREEN_BATTLE || g_game.screen == SCREEN_ITEM ||
        g_game.screen == SCREEN_SHOP) {
        marks_load_battle_tiles();
    }
    /* Menu AP pip BG tiles (236-240) stream on ITEM ticks while the DECK
     * tab is active (item_menu_tab 0; tabs share the screen id).  Same
     * LCD-safe pattern as the OAM loader above (BANKED_VRAM_WRITE per
     * byte) and same-tick guarantee: the tick runs before the render,
     * so the first DECK frame already stamps resident art.  One-shot
     * per menu stay (s_ap_pip_loaded); overworld entry wipes the window
     * via the world tileset reload and re-arms through
     * ow_oam_restore_banked, ITEM entry through the invalidate above. */
    if (g_game.screen == SCREEN_ITEM && g_game.item_menu_tab == 0 &&
        !s_ap_pip_loaded) {
        uint8_t d, j;
        volatile uint8_t *dst;
        s_ap_pip_loaded = 1;
        VBK_REG = 0;
        for (d = 0; d < 5; d++) {
            dst = (volatile uint8_t *)(0x8000u + (((uint16_t)UI_TILE_AP_PIP_BASE + d) << 4));
            for (j = 0; j < 16; j++) {
                BANKED_VRAM_WRITE(dst, ap_pip_tiles[((uint16_t)d << 4) + j]);
                dst++;
            }
        }
    }

    /* Fresh-load re-arm: load_game_slot clears g_jingle_done's seeded
     * bit (bit 7); the jingle body re-seeds from the live mask later
     * this same tick (marks runs first), so an unseeded jingle here
     * means a real load landed since the last tick -- same-map reloads
     * included.  Browsing SAVE_LOAD without loading leaves the seed
     * intact, so returning from the menu stays silent.  TITLE and
     * GAME_OVER keep the always-re-arm poll below: leaving them always
     * means a reload by construction.  Battle/ending screens are
     * excluded: the pursuit alert's pause must never cut their music
     * (loads always land on the overworld anyway). */
    if (g_game.screen != SCREEN_BATTLE &&
        g_game.screen != SCREEN_GAME_OVER &&
        g_game.screen != SCREEN_ENDING &&
        !(g_jingle_done & 0x80)) {
        s_last_map = 0xFF;
    }

    /* Same-map reloads (death -> continue, title -> continue) never
     * change map_id, so the entry edge below would miss them and the
     * alert would stay silent.  Re-arm while on any screen that can
     * originate a load: the next overworld tick then scans as a fresh
     * entry. */
    if (g_game.screen == SCREEN_TITLE ||
        g_game.screen == SCREEN_GAME_OVER) {
        s_last_map = 0xFF;
    } else if (!s_last_map_init) {
        /* Map entry edge.  s_last_map starts 0, which is a valid map id
         * (MAP_FIELD), so the first tick arms explicitly -- a boot
         * directly onto a pursuer map must scan like an entry. */
        s_last_map_init = 1;
        s_last_map = 0xFF;
    } else if (g_game.world.map_id != s_last_map) {
        /* Map change: no alert yet.  The sight scan below arms it the
         * moment a chaser actually notices the hero (spawn actors start
         * sleeping, so entering a map full of distant chasers is
         * silent).  Cancels any alert from the previous map. */
        s_last_map = g_game.world.map_id;
        g_alert_ttl = 0;
    } else if (g_alert_ttl != 0) {
        g_alert_ttl--;
        /* Sting edge at 120 (~30 f after the wake-up: the chaser is
         * awake and moving) -- never inside a battle / game-over / ending
         * that owns the music.  The sting plays on top of the running
         * scene track via the normal SFX mute path (CH2 ducks, other
         * voices keep playing); same screen rule as before (battle exit
         * restores the area track itself; the jingle path owns the
         * title).  Menus, dialogue and shops are overworld-attached:
         * the sting plays under them. */
        if (g_alert_ttl == ALERT_STING_TTL &&
            g_game.screen != SCREEN_BATTLE &&
            g_game.screen != SCREEN_GAME_OVER &&
            g_game.screen != SCREEN_ENDING) {
            g_bk_byte_c = 2;
            g_bk_byte_d = SFX_ALERT;
        }
    }

    /* Line of sight (AI_CHASE / AI_CHASE_FAST, actor.h AI_CHASE_SIGHT):
     * the hero coming within range wakes a sleeping chaser for the rest
     * of the scene (ACTOR_STATE_ALERTED, gating the chase in
     * world_patrol_slot_banked) and arms the alert above: the sting fires
     * at ttl 120 over the still-playing scene track (never a restart,
     * never a MUSIC_CHANGED).
     * Overworld only -- menus and dialogue pause the world, and a battle
     * must never arm the alert (the sting would cut into battle music).
     * Scene loads respawn actors with the bit clear, so every visit
     * starts asleep. */
    if (g_game.screen == SCREEN_OVERWORLD) {
        uint8_t px = g_game.world.player.position.x;
        uint8_t py = g_game.world.player.position.y;
        if (g_game.world.map_id == MAP_THRONE_ROOM && py <= 11 &&
            !MARKS_FLAG_SET(STORY_FLAG_ID_SLIME_LORD_INTRO)) {
            g_game.state.flags.bytes[(STORY_FLAG_ID_SLIME_LORD_INTRO - 1) >> 3] |=
                (uint8_t)(1u << ((STORY_FLAG_ID_SLIME_LORD_INTRO - 1) & 7));
            g_bk_dlg_id = (uint8_t)DIALOGUE_ID_SLIME_LORD_INTRO;
        }
        for (i = 0; i < MAX_WORLD_ACTORS; i++) {
            WorldActorRuntime *a = &g_game.world.actors[i];
            uint8_t dx;
            uint8_t dy;
            if (!a->active) continue;
            if (a->ai_type != AI_CHASE && a->ai_type != AI_CHASE_FAST) continue;
            if (a->flags & ACTOR_STATE_ALERTED) continue;
            dx = (px >= a->x) ? (uint8_t)(px - a->x) : (uint8_t)(a->x - px);
            dy = (py >= a->y) ? (uint8_t)(py - a->y) : (uint8_t)(a->y - py);
            if (dx > AI_CHASE_SIGHT || dy > AI_CHASE_SIGHT) continue;
            a->flags |= ACTOR_STATE_ALERTED;
            if (g_alert_ttl == 0) {
                g_alert_ttl = ALERT_TTL_FRAMES;
            }
        }
    }

    /* Overworld HUD markers (g_actor_marks / g_static_marks) are only
     * consumed on Overworld and Dialogue-over-Overworld; skip the loops
     * and collection/actor scans on Battle, Menu, Shop, and Title frames. */
    if (g_game.screen != SCREEN_OVERWORLD &&
        g_game.screen != SCREEN_DIALOGUE) {
        return;
    }

    /* Hostiles: pursuer "!" over the chasers that spotted the hero, once
     * the alert sting has fired (ttl at or below the sting edge) until
     * the alert runs out.  The bubble debuts with the alert, never before
     * it; a sleeping chaser never shows one. */
    for (i = 0; i < MAX_WORLD_ACTORS; i++) {
        const WorldActorRuntime *a = &g_game.world.actors[i];
        uint8_t m = ACTOR_MARK_NONE;
        if (a->active && (a->flags & ACTOR_STATE_ALERTED) &&
            g_alert_ttl != 0 && g_alert_ttl <= ALERT_STING_TTL) {
            m = ACTOR_MARK_ALERT;
        }
        g_actor_marks[i] = m;
    }

    /* Statics: quest "?" from quest state, evaluated lazily only when
     * the relevant NPC is actually present on the current map. */
    for (i = 0; i < MAX_STATIC_ACTORS; i++) {
        uint8_t m = ACTOR_MARK_NONE;
        if (i < g_static_actor_count) {
            uint8_t id = g_static_actors[i].id;
            if (id == ENTITY_ID_MAYOR) {
                int16_t q = g_game.state.variables.values[VARIABLE_ID_QUEST_MONSTER_HUNT - 1];
                if (q == 0 ||
                    (q == 1 && g_game.state.variables.values[VARIABLE_ID_MONSTERS_DEFEATED - 1] >= 3)) {
                    m = ACTOR_MARK_QUEST;
                }
            } else if (id == ENTITY_ID_CARL) {
                if (!marks_carl_defeated()) {
                    m = ACTOR_MARK_QUEST;
                }
            } else if (id == ENTITY_ID_VILLAGE_DOG) {
                if (!MARKS_FLAG_SET(STORY_FLAG_ID_DOG_KEY)) {
                    m = ACTOR_MARK_QUEST;
                }
            } else if (id == ENTITY_ID_MERCHANT) {
                int16_t mq = g_game.state.variables.values[VARIABLE_ID_MERCHANT_QUEST - 1];
                if (mq == 0 || (mq == 1 && marks_has_card(CARD_SHOPKEYS))) {
                    m = ACTOR_MARK_QUEST;
                }
            } else if (id == ENTITY_ID_HEAL_MAGE) {
                if (!MARKS_FLAG_SET(STORY_FLAG_ID_RING_LESSON) &&
                    marks_has_card(CARD_IRON_RING)) {
                    m = ACTOR_MARK_QUEST;
                }
            }
        }
        g_static_marks[i] = m;
    }
}

/* BSS: one-shot flag for the input-timed RNG re-mix. */
static uint8_t s_rng_mixed;
extern volatile uint8_t g_harness_mode;

extern void jingle_full_banked(void);

void game_frame_tick_banked(void)
{
    /* RNG priming (AGENTS.md 52.x: the custom CRT0 clears WRAM but never
     * copies _INITIALIZER, so g_rng_state/g_loot_rng_state boot as zero
     * -- the xorshift fixed point, which would make every "random" roll
     * in the release ROM constant, e.g. eternal chuck-a-luck [1,1,1]).
     * Prime both from the free-running divider on the first tick with a
     * nonzero stream (same derivation as rng_set_seed_banked); the
     * harness pre-seeds nonzero, so this never fires there and scenario
     * determinism is untouched.  Zero fixed-bank cost: this dispatch
     * already runs every frame. */
    if (!g_rng_state) {
        uint16_t seed;
        seed = (uint16_t)(((uint16_t)LY_REG << 8) | (uint16_t)DIV_REG);
        if (!seed) seed = 0xACE1;
        g_rng_state = seed;
        seed = (uint16_t)(seed ^ 0x1B3C);
        if (!seed) seed = 1;
        g_loot_rng_state = seed;
    }
    /* One-shot re-mix on the first real input.  The first-tick seed above
     * comes from DIV/LY at a fixed boot offset, i.e. it is the SAME every
     * session -- a player repeating the same actions would see the same
     * dice.  A button press lands at an arbitrary DIV offset, so folding
     * it in buys genuine entropy.  Deliberately skipped under the harness:
     * scenarios pre-seed and injected presses must not perturb their
     * determinism (g_harness_mode is 1 there). */
    if (!s_rng_mixed && !g_harness_mode && g_inp_pad != 0) {
        uint16_t entropy;
        s_rng_mixed = 1;
        entropy = (uint16_t)(((uint16_t)LY_REG << 8) | (uint16_t)DIV_REG);
        if (!entropy) entropy = 0x5A5A;
        /* Both streams: the loot stream is seeded as seed ^ 0x1B3C, so
         * mixing only g_rng_state would leave loot rolls repeatable per
         * cold boot. */
        g_rng_state ^= entropy;
        if (!g_rng_state) g_rng_state = 0xACE1;
        g_loot_rng_state ^= (uint16_t)(entropy ^ 0x1B3C);
        if (!g_loot_rng_state) g_loot_rng_state = 1;
    }
    /* Marks first: the re-arm edge below observes the jingle's unseeded
     * state (g_jingle_done without bit 7), which a real load clears via
     * load_game_slot.  The jingle seeds from the live mask later this
     * same tick, so running it first would hide the edge.  The two
     * bodies touch disjoint staging bytes (marks: c/d, jingle: b) and
     * the fixed side reads all three after the return, so the order is
     * otherwise irrelevant.  (Carl's leave-the-field offer runs as its
     * own bank-10 dispatch from the fixed wrapper above -- a banked body
     * can never nest another trampoline call.) */
    marks_refresh();
    jingle_full_banked();
    /* Chuck-a-luck: the table's whole frame body (entry paint, input,
     * dice, gold, OAM) plus its audio request live in bank 8 too, so
     * both are plain same-bank calls.  The gamble frame runs after the
     * jingle body so its win-edge can override the restore code, and
     * the audio tick runs last so it owns the final g_bk_byte_b value
     * for this frame.  Skipped entirely off-screen: zero cost on every
     * other map/screen. */
    if (g_gamble_enter) {
        gamble_enter_screen();
    }
    if (g_game.screen == SCREEN_GAMBLE) {
        gamble_frame_banked();
    }
    gamble_audio_tick();
}
