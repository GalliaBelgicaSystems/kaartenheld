#ifndef TELEMETRY_H
#define TELEMETRY_H

#include <stdint.h>


#define MAX_TELEMETRY_EVENTS 32

/* Debug snapshot layout:
 *   bytes  0-19 : core game state (see telemetry.c debug_snapshot)
 *   bytes 20+   : scene actors as (id, x, y, facing) entries */
#define SNAPSHOT_BASE_SIZE      20

/* Only the first this many ACTIVE hostiles (then statics) are reported;
 * deliberately decoupled from MAX_WORLD_ACTORS (world.h): crowded
 * scenes still fit the harness wire format, and fixtures never exceed
 * 4 actives, so later hostiles are invisible to snapshot asserts (their
 * defeat/victory still emits telemetry).  Bumping this moves every
 * offset below -- treat as a wire-contract change (AGENTS.md 53.4). */
#define MAX_SNAPSHOT_ACTORS     4

#define SNAPSHOT_ACTOR_ENTRY_SIZE 4

/* Appended after the actor block (append-only wire contract; existing bytes
 * 0-35 never move).  Battle-runtime observability that is not part of
 * GameState lives here. */
#define SNAPSHOT_BATTLE_ENERGY_OFF (SNAPSHOT_BASE_SIZE + (MAX_SNAPSHOT_ACTORS * SNAPSHOT_ACTOR_ENTRY_SIZE))
#define SNAPSHOT_BATTLE_DRAW_OFF   (SNAPSHOT_BATTLE_ENERGY_OFF + 1)
#define SNAPSHOT_BATTLE_DISCARD_OFF (SNAPSHOT_BATTLE_DRAW_OFF + 1)

#define SNAPSHOT_TOTAL_SIZE     (SNAPSHOT_BATTLE_DISCARD_OFF + 1)

/* Extended RPG state snapshot (g_state_snap_buf) layout (version 0x09):
 *   byte  0            : version/validity (0x09)
 *   bytes 1..8         : FlagState.bytes[0..7]
 *   bytes 9..40        : variables as int16 LE (all MAX_STATE_VARIABLES;
 *                        grew 9..24 -> 9..40 for the 9th variable, Nov 2026:
 *                        the builder always copied 16 vars while the layout
 *                        only reserved 8, so vars 9+ aliased the currency
 *                        section and read back currency bytes)
 *   byte  41           : currency count
 *   bytes 42..53       : up to 4 currency x {id, amt_lo, amt_hi}
 *   byte  54           : party count
 *   bytes 55..66       : up to 4 party members x {id, hp, max_hp}
 *   byte  67           : inventory count
 *   bytes 68..167      : up to 50 inventory entries x {item_id, quantity}
 *   byte  168          : world (persistent actor) count
 *   bytes 169..216     : up to 16 world entries x {actor_id_lo, actor_id_hi, state}
 *   byte  217          : progression count
 *   bytes 218..265     : up to 8 progression x {type, id_lo, id_hi, level, prog_lo, prog_hi}
 *   byte  266          : equipment weapon
 *   byte  267          : overworld camera scroll_x (tiles)
 *   byte  268          : overworld camera scroll_y (tiles)
 *   byte  269          : world width (scene tile columns)
 *   byte  270          : world height (scene tile rows)
 *   byte  271          : overworld camera pixel x (camera_px_x)
 *   byte  272          : overworld camera pixel y (camera_px_y)
 *   byte  273          : battle-deck card count
 *   bytes 274..293     : up to 20 deck entries x {card_id}
 *   byte  294          : market index (1/32 units, docs/economy.md §6)
 *   bytes 295..296     : gold_in LE (loot gold only, §6.1)
 *   bytes 297..298     : gold_out LE (shop spend)
 *   byte  299          : battles-since-update
 */
#define STATE_SNAP_VERSION_BYTE    0x09
#define STATE_SNAP_FLAGS_OFFSET    1
#define STATE_SNAP_FLAGS_SIZE      8
#define STATE_SNAP_VARIABLES_OFFSET  9
#define STATE_SNAP_VARIABLES_SIZE    32
#define STATE_SNAP_CURRENCY_COUNT_OFF 41
#define STATE_SNAP_CURRENCY_ENTRY_OFF 42
#define STATE_SNAP_CURRENCY_ENTRY_SIZE 3
#define STATE_SNAP_PARTY_OFFSET      54
#define STATE_SNAP_PARTY_ENTRY_SIZE  3
#define STATE_SNAP_INVENTORY_OFFSET  67
#define STATE_SNAP_INVENTORY_ENTRY_SIZE 2
#define STATE_SNAP_WORLD_OFFSET      168
#define STATE_SNAP_WORLD_ENTRY_SIZE  3
#define STATE_SNAP_PROGRESSION_COUNT_OFF 217
#define STATE_SNAP_PROGRESSION_ENTRY_OFF 218
#define STATE_SNAP_PROGRESSION_ENTRY_SIZE 6
#define STATE_SNAP_EQUIPMENT_OFF 266
#define STATE_SNAP_SCROLL_X_OFF  267
#define STATE_SNAP_SCROLL_Y_OFF  268
#define STATE_SNAP_WORLD_WIDTH_OFF  269
#define STATE_SNAP_WORLD_HEIGHT_OFF 270
#define STATE_SNAP_CAMERA_PX_X_OFF  271
#define STATE_SNAP_CAMERA_PX_Y_OFF  272
#define STATE_SNAP_DECK_COUNT_OFF   273
/* Economy tail (docs/economy.md §6.3, appended -- never inserted):
 * market index (1/32 units), gold_in LE, gold_out LE,
 * battles-since-update. */
#define STATE_SNAP_MARKET_INDEX_OFF 294
#define STATE_SNAP_MARKET_GOLD_IN_OFF 295
#define STATE_SNAP_MARKET_GOLD_OUT_OFF 297
#define STATE_SNAP_MARKET_BATTLES_OFF 299
#define STATE_SNAP_TOTAL_SIZE        300

/* Scenario initial-state descriptor (g_scen_state_buf) layout (version 0x06).
 * Written by the host STATE_LOAD command and applied by scenario_load_state().
 * Fixed offsets; variable-length sections carry a count and only the
 * listed entries are applied (unspecified sections keep their defaults). */
#define STATE_LOAD_DESC_VERSION           0x06
#define STATE_LOAD_DESC_SCREEN_OFF        1
#define STATE_LOAD_DESC_SCENE_OFF         2
#define STATE_LOAD_DESC_PLAYER_X_OFF      3
#define STATE_LOAD_DESC_PLAYER_Y_OFF      4
#define STATE_LOAD_DESC_PLAYER_FACING_OFF 5
#define STATE_LOAD_DESC_SEED_OFF          6
#define STATE_LOAD_DESC_FLAGS_OFF         10
#define STATE_LOAD_DESC_FLAGS_SIZE        8
#define STATE_LOAD_DESC_VARIABLES_COUNT_OFF 18
#define STATE_LOAD_DESC_VARIABLES_ENTRY_OFF 19
#define STATE_LOAD_DESC_VARIABLES_ENTRY_SIZE 3
#define STATE_LOAD_DESC_CURRENCY_COUNT_OFF 67
#define STATE_LOAD_DESC_CURRENCY_ENTRY_OFF 68
#define STATE_LOAD_DESC_CURRENCY_ENTRY_SIZE 3
#define STATE_LOAD_DESC_PARTY_COUNT_OFF   80
#define STATE_LOAD_DESC_PARTY_ENTRY_OFF   81
#define STATE_LOAD_DESC_PARTY_ENTRY_SIZE  3
#define STATE_LOAD_DESC_INVENTORY_COUNT_OFF 93
#define STATE_LOAD_DESC_INVENTORY_ENTRY_OFF 94
#define STATE_LOAD_DESC_INVENTORY_ENTRY_SIZE 2
#define STATE_LOAD_DESC_WORLD_COUNT_OFF   194
#define STATE_LOAD_DESC_WORLD_ENTRY_OFF   195
#define STATE_LOAD_DESC_WORLD_ENTRY_SIZE  3
#define STATE_LOAD_DESC_PROGRESSION_COUNT_OFF 243
#define STATE_LOAD_DESC_PROGRESSION_ENTRY_OFF 244
#define STATE_LOAD_DESC_PROGRESSION_ENTRY_SIZE 6
#define STATE_LOAD_DESC_DIALOGUE_ID_OFF   292
#define STATE_LOAD_DESC_START_BATTLE_OFF  293
#define STATE_LOAD_DESC_GAME_OVER_CHOICE_OFF 294
#define STATE_LOAD_DESC_FONT_TEST_OFF     295
#define STATE_LOAD_DESC_EQUIPMENT_OFF     296
#define STATE_LOAD_DESC_DECK_PRESENT_OFF  297
#define STATE_LOAD_DESC_DECK_COUNT_OFF    298
#define STATE_LOAD_DESC_DECK_ENTRY_OFF    299
/* Descriptor layout (version 0x04): byte 229 = deck-present flag (1 = the
 * following deck section replaces the starter deck, count may be 0 for an
 * intentionally empty deck); byte 230 = deck card count; bytes 231..250 =
 * up to 20 deck entries x {card_id}.
 * Byte 319 = initial status: frozen bitmask (bit 0 = player, 1..3 = enemies).
 * Economy tail (version 0x06, docs/economy.md §6.3, appended -- never
 * inserted): byte 320 = market index (1/32 units, out-of-band clamps to
 * neutral 32 on apply), bytes 321..322 = gold_in LE, 323..324 = gold_out
 * LE, byte 325 = battles-since-update. */
#define STATE_LOAD_DESC_STATUS_OFF        319
#define STATE_LOAD_DESC_MARKET_INDEX_OFF  320
#define STATE_LOAD_DESC_MARKET_GOLD_IN_OFF 321
#define STATE_LOAD_DESC_MARKET_GOLD_OUT_OFF 323
#define STATE_LOAD_DESC_MARKET_BATTLES_OFF 325
#define STATE_LOAD_DESC_SIZE              326

typedef enum {
    EVENT_PLAYER_MOVED,
    EVENT_COLLISION,
    EVENT_ENCOUNTER_STARTED,
    EVENT_BATTLE_STARTED,
    EVENT_BATTLE_ACTION,
    EVENT_DAMAGE_DEALT,
    EVENT_DAMAGE_RECEIVED,
    EVENT_ENTITY_DEFEATED,
    EVENT_BATTLE_WON,
    EVENT_BATTLE_LOST,
    EVENT_GAME_STATE_CHANGED,
    EVENT_MUSIC_CHANGED,
    EVENT_MAP_CHANGED,
    EVENT_STORY_FLAG_SET,
    EVENT_STORY_FLAG_CLEARED,
    EVENT_DIALOGUE_STARTED,
    EVENT_DIALOGUE_NEXT,
    EVENT_DIALOGUE_ENDED,
    EVENT_INTERACTION_ATTEMPT,
    EVENT_RENDER_SCREEN,
    EVENT_RENDER_DIALOGUE,
    EVENT_SCREEN_CHANGED,
    EVENT_SCENE_CHANGED,
    EVENT_ACTOR_COLLISION,
    EVENT_ACTOR_INTERACTION,
    EVENT_ACTOR_COMBAT_START,
    EVENT_VARIABLE_SET,
    EVENT_CARD_ADDED_TO_DECK,
    EVENT_CARD_REMOVED_FROM_DECK,
    /* Persistent lifecycle (state.c game_world_set_actor_state):
     * d0 = ActorId lo, d1 = ActorId hi, d2 = ActorStateId, d3 = 0.
     * Patrol movement uses EVENT_ACTOR_MOVED (different schema). */
    EVENT_ACTOR_STATE_CHANGE,
    EVENT_SCRIPT_TRIGGERED,
    /* d0 = HP restored; d1 = cause: 0 = heal card, 1 = ring resolving
     * with an offense hand (docs/loot.md §34.3), 2 = defense net
     * over-block WITH a ring (healing shield) in the defense combo
     * (docs/loot.md §34.4), 3 = NPC full restore (Heal Mage event). */
    EVENT_HEALED,
    EVENT_ITEM_USED,
    EVENT_ITEM_USE_FAILED,
    EVENT_CARD_PURCHASED,
    EVENT_CARD_PURCHASE_FAILED,
    EVENT_CURRENCY_ADDED,
    EVENT_CURRENCY_SPENT,
    EVENT_PROGRESSION_GAINED,
    EVENT_LEVEL_UP,
    EVENT_ITEM_EQUIPPED,
    EVENT_BATTLE_FLED,
    EVENT_GAME_SAVED,
    EVENT_GAME_LOADED,
    EVENT_CARD_ADDED_TO_COLLECTION,
    EVENT_CARD_REMOVED_FROM_COLLECTION,
    EVENT_CARD_PLAYED,
    EVENT_ENEMY_CARD_PLAYED,
    EVENT_DECK_RESHUFFLED,
    /* Append-only: these byte values are a wire contract with
     * tools/emulator.py EVENT_TYPE_MAP (AGENTS.md §53.4). */
    EVENT_COMBO_RESOLVED,   /* d0=tier d1=eff_count d2=suited d3=phase */
    EVENT_EFFECT_RESOLVED,  /* d0=amount d1=effect type d2=phase d3=target slot */
    /* Phase C statuses (docs/combo-system.md §12-§19). */
    EVENT_STATUS_APPLIED,   /* d0=status id d1=stacks d2=duration */
    EVENT_STATUS_TICKED,    /* d0=tick damage d1=actor slot (0=player) */
    EVENT_STATUS_EXPIRED,   /* d0=status id d1=actor slot (0=player) */
    /* Loot instances (docs/loot.md §34, docs/enemies_ranking.md §4.3). */
    EVENT_LOOT_CARD_ADDED,  /* d0=derived id d1=count after d2=enemy rank R0-R6 */
    EVENT_LOOT_GENERATED,   /* reserved (kept for wire stability) */
    EVENT_CARD_SOLD,        /* d0=CardId d1=gold received d2=count after */
    EVENT_TURN_SKIPPED,     /* d0=enemy slot (1..n) d1=cause StatusId */
    EVENT_STATUS_RESISTED,  /* d0=status id d1=target slot (roll failed) */
    /* Title / presentation (append-only wire contract). */
    EVENT_NEW_GAME_STARTED, /* NEW GAME chosen on the title menu */
    EVENT_GAME_CONTINUED,   /* CONTINUE chosen on the title menu */
    EVENT_SOUND_TOGGLED,    /* d0 = g_sound_enabled (1 = ON, 0 = OFF) */
    EVENT_TARGET_CHANGED,   /* d0 = old enemy slot (0..n-1) d1 = new enemy slot
                               (battle target caret moved or auto-advanced) */
    /* Ice grey-out (status.h): ice makes 2 random pool cards
     * unplayable for 2 rounds.  d0 = actor slot (0 = player, 1..n = enemy),
     * d1 = first greyed pool position, d2 = second = position, d3 = rounds. */
    EVENT_CARDS_GREYED,
    /* Cleared after ICE_GREY_TURNS: d0 = actor slot, d1/d2 = the
     * previously-greyed pool positions. */
    EVENT_CARDS_UNGREYED,
    /* Patrol step commit (world.c): d0 = EntityId, d1 = x, d2 = y,
     * d3 = facing.  Split out of EVENT_ACTOR_STATE_CHANGE, which carries
     * a different schema (actor_id_lo/hi + lifecycle state) — one event
     * id must mean one payload shape. */
    EVENT_ACTOR_MOVED,
    /* Dialogue YES/NO answer (dialogue_screen.c): d0 = DialogueId,
     * d1 = choice index (0 = YES, 1 = NO), d2/d3 = 0. */
    EVENT_DIALOGUE_CHOICE,
    /* Locked equip rejection (docs/enemies_ranking.md §4.8):
     * d0 = card id, d1 = required AP, d2/d3 = 0.  No
     * CARD_ADDED_TO_DECK fires with it. */
    EVENT_CARD_EQUIP_LOCKED,
    /* Global market update (docs/economy.md §6.3, append-only wire
     * contract): d0 = old index (1/32 units), d1 = new index,
     * d2 = direction (1 = up, 2 = down, 3 = toward-100/stay).
     * Emitted on every non-skipped shop-boundary update. */
    EVENT_MARKET_UPDATED,
} GameEventType;

typedef struct {
    uint32_t seq;
    uint32_t frame;
    uint8_t type;
    uint8_t data[4];
} GameEvent;

extern uint8_t g_snap_buf[SNAPSHOT_TOTAL_SIZE];
extern uint8_t g_state_snap_buf[STATE_SNAP_TOTAL_SIZE];
extern uint8_t g_scen_state_buf[STATE_LOAD_DESC_SIZE];
extern GameEvent g_telemetry_buffer[MAX_TELEMETRY_EVENTS];
extern uint8_t g_telemetry_count;
extern uint8_t g_telemetry_head;
/* Telemetry-append staging + sequence state (telemetry.c, WRAM): the
 * bank-2 append body's inputs.  Not part of the host contract (the host
 * reads only the ring + count + head). */
extern uint8_t g_telemetry_emit_args[5];
extern uint32_t g_telemetry_seq;
extern const uint32_t *g_telemetry_frame_ptr;

#ifdef DEBUG_BUILD

void telemetry_init(void);
void telemetry_emit(uint8_t type, uint8_t d0, uint8_t d1, uint8_t d2, uint8_t d3);
void telemetry_set_frame_ptr(const uint32_t *frame_ptr);
void debug_snapshot(void);

#else /* release ROM: telemetry is debug-only (no ring buffer, no per-event
 * CPU).  Calls compile out entirely; the one call site with a
 * side-effecting argument (world_on_battle_end's LOOT_CARD_ADDED count)
 * is #ifdef'd at the site, so no gameplay behavior changes. */

#define telemetry_init() ((void)0)
#define telemetry_emit(type, d0, d1, d2, d3) ((void)0)
#define telemetry_set_frame_ptr(frame_ptr) ((void)0)
#define debug_snapshot() ((void)0)

#endif /* DEBUG_BUILD */
/* Bank-11 append body (src/debug/telemetry_emit_banked.c) dispatched by the
 * telemetry_emit() wrapper. */
void telemetry_emit_banked(void);
/* Bank-11 body (src/debug/snapshot_banked.c) dispatched by the wrapper. */
void debug_snapshot_banked(void);

/* Bank-11 extended state snapshot helper (src/debug/telemetry_snap.c),
 * called directly by debug_snapshot_banked() in the same bank. */
void debug_state_snapshot_banked(void);

#endif /* TELEMETRY_H */
