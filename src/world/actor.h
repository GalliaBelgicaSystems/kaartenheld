#ifndef ACTOR_H
#define ACTOR_H

#include <stdint.h>
#include <stdbool.h>
#include "entity.h"
#include "world.h"
#include "dialogue.h"
#include "rpg/state.h"

/* World Actor flags.  Hostility is a property of the actor: a hostile
 * actor starts combat when engaged, a non-hostile actor runs its
 * interaction (e.g. dialogue). */
#define ACTOR_FLAG_HOSTILE      0x01
#define ACTOR_FLAG_BLOCKING     0x02
#define ACTOR_FLAG_INTERACTABLE 0x04

/* Runtime actor state flags (placeholder for future persistence). */
#define ACTOR_STATE_NONE        0x00
/* future: ACTOR_STATE_DEFEATED, ACTOR_STATE_TALKED_TO, ACTOR_STATE_MOVED ... */

/* What happens when the player engages this actor. */
typedef enum {
    INTERACTION_NONE = 0,
    INTERACTION_DIALOGUE = 1,
    INTERACTION_COMBAT = 2,
    INTERACTION_SHOP = 3,
    INTERACTION_SAVE = 4
} InteractionId;

/* Which battle configuration a hostile actor starts.  The battle system
 * owns everything past this point. */
typedef enum {
    BATTLE_NONE = 0,
    BATTLE_SLIME = 1,
    BATTLE_BAT = 2,
    BATTLE_SLIME_TRIO = 3,
    BATTLE_MIMIC = 4,
    BATTLE_KOBOLD = 5,
    BATTLE_SPIDER = 6
} BattleId;

/* Overworld autonomous patrol/AI behavior type. */
typedef enum {
    AI_NONE          = 0,
    AI_PATROL_CIRCLE = 1,   /* Clockwise 2x2 circle around spawn (Bats) */
    AI_PATROL_CROSS  = 2,   /* + cross pattern around spawn (Slimes) */
    AI_CHASE         = 3,   /* Step toward the player every AI tick (Kobolds) */
    AI_PATROL_VERT   = 4    /* Bounce vertically up/down 3 tiles from spawn
                             * (castle Spiders); direction held in ai_step
                             * bit 0, flips at the bound or a blocked path */
} ActorAiType;

/* Vertical patrol amplitude: bounce up/down this many tiles from spawn. */
#define AI_PATROL_VERT_TILES 3

/* Static, scene-owned actor configuration.  No mutable gameplay state.
 * actor_id is the stable persistent instance id (ActorId) used to track
 * defeat/lifecycle in GameState.world; it must be unique across scenes. */
typedef struct {
    uint16_t actor_id;
    EntityId id;
    uint8_t x;
    uint8_t y;
    uint8_t facing;
    uint8_t flags;
    uint8_t visual;              /* ASCII prototype character */
    const char *display_name;    /* semantic name (battle enemy label, ...) */
    InteractionId interaction;
    uint8_t shop_id;             /* which shop this actor runs (0 = none) */
    DialogueId dialogue_id;
    BattleId battle_id;
    ActorAiType ai_type;
    uint8_t hp;                  /* hostile actors only */
    uint8_t max_hp;              /* hostile actors only */
    uint8_t gold_reward;         /* gold granted on defeat (0 = none) */
    CurrencyId reward_currency;  /* currency granted on defeat (0 = none) */
    VariableId spawn_variable;   /* spawn only when this variable == spawn_value (0 = always) */
    int16_t spawn_value;
    ActorSpriteKind sprite_kind; /* how the overworld renders this actor */
    uint8_t ow_type;             /* enemy-type OAM index for SPRITE_KIND_ENEMY
                                    (into g_enemy_types; 0xFF = ASCII fallback) */
    uint8_t solo;                /* nonzero: this hostile engages alone (no
                                    trio clones), even with an enemy deck.
                                    Appended last: keep initializers in sync. */
} WorldActorDefinition;

/* A per-map block of actor definitions.  The engine owns no scene content:
 * the game layer registers its tables via actor_register_tables(). */
typedef struct {
    MapId map_id;
    const WorldActorDefinition *defs;
    uint8_t count;
} WorldActorTable;

void actor_register_tables(const WorldActorTable *tables, uint8_t count, uint8_t bank);

/* Registry location (points into banked ROM; the bank-2 loader body
 * reads it directly while its bank is mapped). */
extern const WorldActorTable *g_actor_registry;
extern uint8_t g_actor_registry_count;

/* Bank-2 body dispatched by actor_load_scene(). */
void actor_load_scene_banked(void);

/* Result of engaging an actor. */
typedef enum {
    ENGAGE_NONE = 0,
    ENGAGE_DIALOGUE = 1,
    ENGAGE_BATTLE = 2,
    ENGAGE_SHOP = 3,
    ENGAGE_SAVE = 4
} ActorEngageResult;

/* Registered static actor table (WRAM copy, banked_copy pattern).  Read
 * directly by bank-2 snapshot code; gameplay lookups stay in actor.c.
 * Non-hostile sprite-kind statics (e.g. chests) also render as OAM
 * sprites in the slots after the hostile ones (see ui.c).  Sized for the
 * busiest hub scene.
 *
 * The table rows are a COMPACT StaticActorDefinition: the WRAM tail sits
 * inside the boot stack's blast zone (crt0 SP=0xE000 descends through
 * the s__BSS tail, AGENTS.md 52.14), so ten static rows must cost LESS
 * than the old seven full WorldActorDefinition rows (150 vs 175 B) —
 * statics never battle, never patrol, never drop loot, and only the
 * final boss uses conditional spawns, so the hostile-only payload is
 * dropped from the static copy.  Field names mirror
 * WorldActorDefinition so consumers read the same accessors. */
typedef struct StaticActorDefinition {
    ActorId actor_id;
    EntityId id;
    uint8_t x;
    uint8_t y;
    uint8_t facing;
    uint8_t flags;
    uint8_t visual;              /* ASCII prototype character */
    const char *display_name;    /* semantic name (battle label, ...) */
    InteractionId interaction;
    uint8_t shop_id;             /* which shop this actor runs (0 = none) */
    DialogueId dialogue_id;
    ActorSpriteKind sprite_kind; /* how the overworld renders this actor */
    uint8_t ow_type;             /* enemy-type OAM index for SPRITE_KIND_ENEMY
                                    (into g_enemy_types; 0xFF = ASCII fallback) */
} StaticActorDefinition;

/* Registered static actor table: 10 compact rows (see the comment above
 * the typedef).  The cap lives here so validate.py can parse it. */
#define MAX_STATIC_ACTORS 10

/* Find the actor definition at a world position on the current map.
 * Friendly actors use their static position; hostile actors resolve
 * against the spawned runtime actor slots. */
const StaticActorDefinition *actor_find_at(const World *world, uint8_t x, uint8_t y);

/* Return the runtime slot index of the active hostile actor at (x, y),
 * or NO_ACTOR_INDEX if none. */
uint8_t actor_find_hostile_slot(const World *world, uint8_t x, uint8_t y);

/* Single generic engagement entry point: hostile actors request combat,
 * everything else runs its interaction (dialogue for v1). */
ActorEngageResult actor_engage(const StaticActorDefinition *actor, DialogueState *dialogue);

/* Spawn all hostile actor definitions for the given map into
 * World.actors runtime slots.  Actors whose ActorId is marked DEFEATED in
 * state are not spawned (persistent defeat). */
void actor_load_scene(World *world, MapId map_id, const GameState *state);

extern StaticActorDefinition g_static_actors[MAX_STATIC_ACTORS];
extern uint8_t g_static_actor_count;


#endif /* ACTOR_H */
