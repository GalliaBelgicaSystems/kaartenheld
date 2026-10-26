#ifndef RPG_STATE_H
#define RPG_STATE_H

#include <stdint.h>
#include <stdbool.h>
#include "entity.h"
#include "screen.h"
#include "rpg/deck.h"

/* Fixed-size capacity limits for the Game Boy.  No dynamic allocation.
 * Sized to what the game actually uses (single hero, gold-only currency,
 * hero + weapon progression): every unused slot is WRAM + SRAM + checksum
 * time on every save.  Grow a cap when a mechanic needs it, never in
 * advance.  tools/walkthrough/state_reader.py mirrors these sizes. */
#define MAX_PARTY_MEMBERS     1
#define MAX_STATE_FLAGS       64
#define MAX_STATE_VARIABLES   16
#define MAX_PERSISTENT_ACTORS 16
#define MAX_CURRENCIES        1
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

typedef struct {
    ProgressionTarget target;
    ProgressionState state;
} ProgressionEntry;

typedef struct {
    uint8_t count;
    ProgressionEntry entries[MAX_PROGRESSION_TARGETS];
} ProgressionStore;

/* Persistent world actor state: survives scene reloads.  Keyed by the
 * stable ActorId of a particular spawned instance, not its type. */
typedef struct {
    ActorId actor_id;
    uint8_t state; /* ActorStateId */
} PersistentActorState;

typedef struct {
    PersistentActorState actors[MAX_PERSISTENT_ACTORS];
    uint8_t count;
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
