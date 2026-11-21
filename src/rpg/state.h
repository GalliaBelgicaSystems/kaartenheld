#ifndef RPG_STATE_H
#define RPG_STATE_H

#include <stdint.h>
#include <stdbool.h>
#include "entity.h"
#include "screen.h"
#include "rpg/deck.h"

/* Fixed-size capacity limits for the Game Boy.  No dynamic allocation.
 * Sized to what the game actually uses (single hero, gold + arrows
 * currencies, hero + weapon progression): every unused slot is WRAM +
 * SRAM + checksum time on every save.  Grow a cap when a mechanic needs
 * it, never in advance.  tools/walkthrough/state_reader.py mirrors
 * these sizes. */
#define MAX_PARTY_MEMBERS     1
#define MAX_STATE_FLAGS       64
#define MAX_STATE_VARIABLES   16
/* Zone-scoped defeat store (see WorldState below): bit (N-1) records
 * ActorId N defeated.  ActorIds must stay 1..128 (validate.py rejects
 * wider ids; actor_id 0 stays untracked runtime). */
#define ZONE_DEFEAT_BITS 128
#define ZONE_DEFEAT_BYTES (ZONE_DEFEAT_BITS / 8)

/* Zone defeat addressing shared by fixed and banked readers (pure
 * arithmetic, no calls -- safe to expand in banked bodies, AGENTS.md
 * §52.11.1).  Bit (N-1) records ActorId N.  Banked bodies pair the byte
 * macro with a same-bank 8-entry mask table (a `1u << n` runtime shift
 * compiles to an sla loop); fixed-bank readers use plain shifts. */
#define ZONE_DEFEAT_BYTE(aid) ((uint8_t)(((aid) - 1) >> 3))
#define ZONE_DEFEAT_BIT(aid) ((uint8_t)(((aid) - 1) & 7))
#define MAX_CURRENCIES        2
#define MAX_PROGRESSION_TARGETS 2

/* Generic, saveable identifiers.  The game layer decides what a given
 * ID means; the state layer only stores and retrieves it. */
typedef uint16_t FlagId;
typedef uint16_t VariableId;
typedef uint16_t ActorId;
typedef uint16_t CurrencyId;

/* Item identity.  Retained for event-system compatibility; the card system
 * (deck.h) provides the replacement persistent collection. */

/* Party member identity. */
typedef enum {
    CHARACTER_NONE = 0,
    CHARACTER_HERO = 1
} CharacterId;

/* Persistent actor lifecycle state. */
typedef enum {
    ACTOR_STATE_ALIVE = 0,
    ACTOR_STATE_DEFEATED = 1
} ActorStateId;

/* ── SceneState: mutable persistent scene/position state.
 *    Runtime scene content lives in SceneDefinition; this struct only
 *    records where the player is. */
typedef struct {
    SceneId scene_id;
    uint8_t player_x;
    uint8_t player_y;
    uint8_t player_facing; /* Direction */
} SceneState;

typedef struct {
    CharacterId id;
    uint8_t hp;
    uint8_t max_hp;
} CharacterState;

typedef struct {
    CharacterState members[MAX_PARTY_MEMBERS];
    uint8_t count;
} PartyState;

/* Bit-packed flag storage; flag N lives in byte (N-1)/8, bit (N-1)%8. */
typedef struct {
    uint8_t bytes[MAX_STATE_FLAGS / 8];
} FlagState;

typedef struct {
    int16_t values[MAX_STATE_VARIABLES];
} VariableState;

/* Dense currency storage indexed by CurrencyId - 1.  Only currencies the
 * game actually uses occupy slots. */
typedef struct {
    int16_t amount[MAX_CURRENCIES];
} CurrencyState;

/* Progression: a target (anything in the game that can progress) with its
 * mutable level/progress.  The engine is generic; target types only tag the
 * owner (hero, weapon, card, ...) and are resolved to static definitions. */
typedef enum {
    PROG_TYPE_NONE = 0,
    PROG_TYPE_HERO = 1,
    PROG_TYPE_WEAPON = 2,
    PROG_TYPE_COMPANION = 3
} ProgressionTargetType;

typedef struct {
    uint8_t type;     /* ProgressionTargetType */
    uint16_t id;
} ProgressionTarget;

typedef struct {
    uint8_t level;
    uint16_t progress;
} ProgressionState;

/* Persistent economy state (docs/economy.md §6): one global market index
 * shared by all shops plus the transaction counters for the next update.
 * Appended last: keep initializers in sync.  market_index is in 1/32
 * units (32 = neutral 100%, range 24-48 = 75%-150%); gold_in counts
 * enemy-loot gold only (sales excluded, §6.1); counters saturate. */
typedef struct {
    uint8_t market_index;
    uint16_t gold_in;
    uint16_t gold_out;
    uint8_t battles;
} EconomyState;

typedef struct {
    ProgressionTarget target;
    ProgressionState state;
} ProgressionEntry;

typedef struct {
    uint8_t count;
    ProgressionEntry entries[MAX_PROGRESSION_TARGETS];
} ProgressionStore;

/* Persistent world actor state: zone-scoped defeat bitmask.  Leaving a
 * zone respawns its enemies, so only the CURRENT zone's defeats are
 * stored: bit (N-1) records ActorId N defeated (ActorIds unique within
 * a zone, enforced by validate.py).  128 bits cover a whole zone; the
 * old flat list (16 entries game-wide) silently dropped the 17th kill
 * while keeping its loot.  Zone 0 means unsettled (fresh boot/new game):
 * the actor loader settles it to the loaded table's zone without
 * clearing. */
typedef struct {
    uint8_t zone; /* story act of the recorded defeats (level JSON act key) */
    uint8_t bits[ZONE_DEFEAT_BYTES];
} WorldState;

/* The canonical, potentially-saveable persistent game state.  Gameplay
 * systems must not keep their own parallel persistent representations. */
typedef struct {
    SceneState scene;
    PartyState party;
    CardState cards;
    FlagState flags;
    VariableState variables;
    CurrencyState currency;
    WorldState world;
    ProgressionStore progression;
    EconomyState economy;
} GameState;

void game_state_zero(GameState *state);

/* ── Flags ───────────────────────────────────────────────────────── */
bool game_flag_is_set(const GameState *state, FlagId flag);
void game_flag_set(GameState *state, FlagId flag);
void game_flag_clear(GameState *state, FlagId flag);

/* ── Variables ───────────────────────────────────────────────────── */
int16_t game_variable_get(const GameState *state, VariableId variable);
void game_variable_set(GameState *state, VariableId variable, int16_t value);
void game_variable_add(GameState *state, VariableId variable, int16_t amount);

/* ── Persistent actor state ──────────────────────────────────────── */
bool game_world_actor_is_defeated(const GameState *state, ActorId actor_id);
void game_world_set_actor_state(GameState *state, ActorId actor_id,
                                ActorStateId actor_state);

#endif /* RPG_STATE_H */
