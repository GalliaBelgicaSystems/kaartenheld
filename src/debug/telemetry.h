#ifndef TELEMETRY_H
#define TELEMETRY_H

#include <stdint.h>


#define MAX_TELEMETRY_EVENTS 32

/* Debug snapshot layout:
 *   bytes  0-19 : core game state (see telemetry.c debug_snapshot)
 *   bytes 20+   : scene actors as (id, x, y, facing) entries */
#define SNAPSHOT_BASE_SIZE      20

#define MAX_SNAPSHOT_ACTORS     4

#define SNAPSHOT_ACTOR_ENTRY_SIZE 4

/* Appended after the actor block (append-only wire contract; existing bytes
 * 0-35 never move).  Battle-runtime observability that is not part of
 * GameState lives here. */
#define SNAPSHOT_BATTLE_ENERGY_OFF (SNAPSHOT_BASE_SIZE + (MAX_SNAPSHOT_ACTORS * SNAPSHOT_ACTOR_ENTRY_SIZE))
#define SNAPSHOT_BATTLE_DRAW_OFF   (SNAPSHOT_BATTLE_ENERGY_OFF + 1)
#define SNAPSHOT_BATTLE_DISCARD_OFF (SNAPSHOT_BATTLE_DRAW_OFF + 1)

#define SNAPSHOT_TOTAL_SIZE     (SNAPSHOT_BATTLE_DISCARD_OFF + 1)

/* Extended RPG state snapshot (g_state_snap_buf) layout (version 0x06):
 *   byte  0            : version/validity (0x06)
 *   bytes 1..8         : FlagState.bytes[0..7]
 *   bytes 9..24        : variables as int16 LE
 *   byte  25           : currency count
 *   bytes 26..37       : up to 4 currency x {id, amt_lo, amt_hi}
 *   byte  38           : party count
 *   bytes 39..50       : up to 4 party members x {id, hp, max_hp}
 *   byte  51           : inventory count
 *   bytes 52..83       : up to 16 inventory entries x {item_id, quantity}
 *   byte  84           : world (persistent actor) count
 *   bytes 85..132      : up to 16 world entries x {actor_id_lo, actor_id_hi, state}
 *   byte  133          : progression count
 *   bytes 134..181     : up to 8 progression x {type, id_lo, id_hi, level, prog_lo, prog_hi}
 *   byte  182          : equipment weapon
 *   byte  183          : overworld camera scroll_x (tiles)
 *   byte  184          : overworld camera scroll_y (tiles)
 *   byte  185          : world width (scene tile columns)
 *   byte  186          : world height (scene tile rows)
 *   byte  187          : overworld camera pixel x (camera_px_x)
 *   byte  188          : overworld camera pixel y (camera_px_y)
 *   byte  189          : battle-deck card count
 *   bytes 190..209     : up to 20 deck entries x {card_id}
 */
#define STATE_SNAP_VERSION_BYTE    0x06
#define STATE_SNAP_FLAGS_OFFSET    1
#define STATE_SNAP_FLAGS_SIZE      8
#define STATE_SNAP_VARIABLES_OFFSET  9
#define STATE_SNAP_VARIABLES_SIZE    16
#define STATE_SNAP_CURRENCY_COUNT_OFF 25
#define STATE_SNAP_CURRENCY_ENTRY_OFF 26
#define STATE_SNAP_CURRENCY_ENTRY_SIZE 3
#define STATE_SNAP_PARTY_OFFSET      38
#define STATE_SNAP_PARTY_ENTRY_SIZE  3
#define STATE_SNAP_INVENTORY_OFFSET  51
#define STATE_SNAP_INVENTORY_ENTRY_SIZE 2
#define STATE_SNAP_WORLD_OFFSET      84
#define STATE_SNAP_WORLD_ENTRY_SIZE  3
#define STATE_SNAP_PROGRESSION_COUNT_OFF 133
#define STATE_SNAP_PROGRESSION_ENTRY_OFF 134
#define STATE_SNAP_PROGRESSION_ENTRY_SIZE 6
#define STATE_SNAP_EQUIPMENT_OFF 182
#define STATE_SNAP_SCROLL_X_OFF  183
#define STATE_SNAP_SCROLL_Y_OFF  184
#define STATE_SNAP_WORLD_WIDTH_OFF  185
#define STATE_SNAP_WORLD_HEIGHT_OFF 186
#define STATE_SNAP_CAMERA_PX_X_OFF  187
#define STATE_SNAP_CAMERA_PX_Y_OFF  188
#define STATE_SNAP_DECK_COUNT_OFF   189
#define STATE_SNAP_TOTAL_SIZE        210

/* Scenario initial-state descriptor (g_scen_state_buf) layout (version 0x02).
 * Written by the host STATE_LOAD command and applied by scenario_load_state().
 * Fixed offsets; variable-length sections carry a count and only the
 * listed entries are applied (unspecified sections keep their defaults). */
#define STATE_LOAD_DESC_VERSION           0x04
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
#define STATE_LOAD_DESC_WORLD_COUNT_OFF   126
#define STATE_LOAD_DESC_WORLD_ENTRY_OFF   127
#define STATE_LOAD_DESC_WORLD_ENTRY_SIZE  3
#define STATE_LOAD_DESC_PROGRESSION_COUNT_OFF 175
#define STATE_LOAD_DESC_PROGRESSION_ENTRY_OFF 176
#define STATE_LOAD_DESC_PROGRESSION_ENTRY_SIZE 6
#define STATE_LOAD_DESC_DIALOGUE_ID_OFF   224
#define STATE_LOAD_DESC_START_BATTLE_OFF  225
#define STATE_LOAD_DESC_GAME_OVER_CHOICE_OFF 226
#define STATE_LOAD_DESC_FONT_TEST_OFF     227
#define STATE_LOAD_DESC_EQUIPMENT_OFF     228
#define STATE_LOAD_DESC_DECK_PRESENT_OFF  229
#define STATE_LOAD_DESC_DECK_COUNT_OFF    230
#define STATE_LOAD_DESC_DECK_ENTRY_OFF    231
/* Descriptor layout (version 0x04): byte 229 = deck-present flag (1 = the
 * following deck section replaces the starter deck, count may be 0 for an
 * intentionally empty deck); byte 230 = deck card count; bytes 231..250 =
 * up to 20 deck entries x {card_id}.
 * Byte 251 = initial status: frozen bitmask (bit 0 = player, 1..3 = enemies). */
#define STATE_LOAD_DESC_STATUS_OFF        251
#define STATE_LOAD_DESC_SIZE              252

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
     * (docs/loot.md §34.4). */
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
    EVENT_COMBO_RESOLVED,   /* d0=multiplier d1=eff_count d2=flags(straight|suited<<1) d3=phase */
    EVENT_EFFECT_RESOLVED,  /* d0=amount d1=effect type d2=phase d3=target slot */
    /* Phase C statuses (docs/combo-system.md §12-§19). */
    EVENT_STATUS_APPLIED,   /* d0=status id d1=stacks d2=duration */
    EVENT_STATUS_TICKED,    /* d0=tick damage d1=actor slot (0=player) */
    EVENT_STATUS_EXPIRED,   /* d0=status id d1=actor slot (0=player) */
    /* Loot instances (docs/loot.md §34). */
    EVENT_LOOT_CARD_ADDED,  /* d0=derived id d1=count after */
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
    /* Poison grey-out (status.h): poison makes 2 random pool cards
     * unplayable for 2 rounds.  d0 = actor slot (0 = player, 1..n = enemy),
     * d1 = first greyed pool position, d2 = second = position, d3 = rounds. */
    EVENT_CARDS_GREYED,
    /* Cleared after POISON_GREY_TURNS: d0 = actor slot, d1/d2 = the
     * previously-greyed pool positions. */
    EVENT_CARDS_UNGREYED,
    /* Patrol step commit (world.c): d0 = EntityId, d1 = x, d2 = y,
     * d3 = facing.  Split out of EVENT_ACTOR_STATE_CHANGE, which carries
     * a different schema (actor_id_lo/hi + lifecycle state) — one event
     * id must mean one payload shape. */
    EVENT_ACTOR_MOVED
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

void telemetry_init(void);
void telemetry_emit(uint8_t type, uint8_t d0, uint8_t d1, uint8_t d2, uint8_t d3);
void telemetry_set_frame_ptr(const uint32_t *frame_ptr);
void debug_snapshot(void);
/* Bank-2 body (src/debug/snapshot_banked.c) dispatched by the wrapper. */
void debug_snapshot_banked(void);
void debug_state_snapshot(void);

/* Banked no-arg body of debug_state_snapshot() (src/debug/telemetry_snap.c,
 * ROM bank 2), run via the WRAM banked-call trampoline.  Self-contained:
 * reads g_game / g_state_snap_buf and its own static snap_copy. */
void debug_state_snapshot_banked(void);

#endif /* TELEMETRY_H */
