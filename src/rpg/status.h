#ifndef RPG_STATUS_H
#define RPG_STATUS_H

#include <stdint.h>
#include <stdbool.h>

/* ── Status system foundation (docs/combo-system.md Phase C, §12-§21) ──
 * Three distinct concepts, per plan §12:
 *   StatusDefinition -- what a status IS (bank-3 table, status_content.c)
 *   StatusInstance   -- a status currently affecting one combatant
 *                       (runtime Battle state, never GameState/save)
 *   Application      -- the event of trying to inflict one (battle flow
 *                       requests it; this module owns stacking/duration)
 *
 * The combo/effect pipeline never touches statuses directly: cards carry
 * a (status_id, chance) pair as DATA; battle rolls the die through the
 * deterministic RNG (§17/§18) and calls status_apply(). */

typedef enum {
    STATUS_NONE = 0,
    STATUS_POISON = 1,
    STATUS_BURN = 2,
    STATUS_FREEZE = 3
} StatusId;

/* Instances per combatant.  Small fixed budget: statuses are rare, and
 * Combatant embeds a StatusSlots (player + up to MAX_BATTLE_ENEMIES). */
#define MAX_STATUSES_PER_COMBATANT 2

/* One active affliction/blessing on one combatant. */
typedef struct {
    uint8_t id;        /* StatusId */
    uint8_t stacks;    /* 1..max_stacks (definition) */
    uint8_t duration;  /* turns remaining */
} StatusInstance;

typedef struct {
    StatusInstance slot[MAX_STATUSES_PER_COMBATANT];
    uint8_t count;
} StatusSlots;

/* Definition table geometry (rows live in src/rpg/status_content.c).
 * Kept minimal by design (plan §26 leaves balance open). */
#define STATUS_DEF_COUNT 4

typedef struct {
    uint8_t id;              /* StatusId */
    uint8_t tick;            /* FLAT damage per round (Phase C decision);
                              * stacks only refresh duration/telemetry */
    uint8_t max_stacks;      /* stacking cap (plan §16 STACK rule) */
    uint8_t duration;        /* default duration when applying */
} StatusDefinition;

/* Outcome of one tick, written to shared WRAM by the banked body and
 * consumed/emitted by the fixed wrapper. */
typedef struct {
    uint8_t damage;                    /* total tick damage this actor took */
    uint8_t expired_count;             /* instances removed this tick */
    uint8_t expired_ids[MAX_STATUSES_PER_COMBATANT];
} StatusTickResult;

extern StatusTickResult g_status_tick;

/* Frozen-combatant bitmask, maintained by the bank-3 tick body: bit N
 * set = slots[N] holds STATUS_FREEZE (bit 0 = player, 1..n = enemies).
 * Written at each end-of-round tick; battle consumes (tests and clears)
 * the bit when a frozen enemy's attack comes up -- one skipped swing per
 * application (docs/combo-system.md §12).  Kept as a flat WRAM byte so
 * the fixed-bank test costs one load/and -- no helper code. */
extern uint8_t g_status_frozen_mask;

/* What the last apply landed on the actor (STATUS_NONE when nothing did);
 * shared WRAM slot written by the banked body, read by the wrapper. */
extern StatusInstance g_status_applied;

/* Battle-scoped status storage, owned by this module (NOT by Game/Battle):
 * growing the battle structs would shift every later field past SDCC's
 * 8-bit struct-offset encodings and bloat every object touching them.
 * Slots: 0 = player, 1..n = enemy index.  Runtime battle state only --
 * never part of GameState/save (docs/combo-system.md §20); reset at
 * battle_start via status_reset_battle(). */
#define STATUS_ROUND_SLOTS 4

/* ── Poison grey-out (battle-scoped) ─────────────────────────────
 * When a combatant is poisoned, POISON_GREY_CARDS random cards in its
 * pool become unplayable for POISON_GREY_TURNS future player-decision
 * rounds.  For the player (slot 0) the pool is the 5-card battle hand
 * (position bit = hand idx); for an enemy (slot 1..n) it is that enemy's
 * deck position (bit = the shared battle deck's draw position).
 * Battle-scoped runtime state like the statuses themselves -- never
 * GameState/save; cleared at battle start via status_reset_battle(). */
#define POISON_GREY_CARDS 2
#define POISON_GREY_TURNS 2

/* Bit p set = pool position p is greyed for actor slot (0 = player,
 * 1..n = enemy).  Duration drains at each enter-select round boundary;
 * the stored counter is POISON_GREY_TURNS+1 at apply because the poison
 * lands mid-round, leaving exactly POISON_GREY_TURNS future rounds greyed
 * (0 = not greyed). */
extern uint8_t s_grey_mask[STATUS_ROUND_SLOTS];
extern uint8_t s_grey_dur[STATUS_ROUND_SLOTS];

/* Re-apply a poisoned victim's grey-out.  pool_size is the victim's card
 * pool (BATTLE_HAND_SIZE for the player, the enemy battle deck's count for
 * an enemy).  On a fresh grey the bank-3 body picks POISON_GREY_CARDS
 * distinct indices from the deterministic RNG; an already-active grey is
 * only refreshed (same cards, same duration).  Emits EVENT_CARDS_GREYED.
 * Fixed-bank wrapper around the ROM-bank-3 body; see banked.h. */
void status_grey_apply(uint8_t actor_slot, uint8_t pool_size);

/* Bank-3 body dispatched by status_grey_apply(). */
void status_grey_apply_banked(void);

/* One round boundary for a greyed combatant: decrement the duration and,
 * when it hits zero, clear the mask and emit EVENT_CARDS_UNGREYED.  Called
 * by the battle tick loop alongside status_tick().  Fixed wrapper around
 * the ROM-bank-3 body; see banked.h. */
void status_grey_tick(uint8_t actor_slot);

/* Bank-3 body dispatched by status_grey_tick(). */
void status_grey_tick_banked(void);

/* Decode the two set-bit positions of a grey mask into out-a/out-b
 * (0xFF when absent).  Pools are <= 5 positions, so only bits 0-4 are
 * ever set.  Debug-only: feeds status_grey_apply's telemetry tail. */
#ifdef DEBUG_BUILD
void status_grey_mask_indices(uint8_t mask, uint8_t *a, uint8_t *b);
#endif

void status_reset_battle(void);
StatusSlots *status_slots(uint8_t actor_slot);

/* Battle-scoped status storage (owned by this module, see above).  Exposed
 * so the bank-3 battle HUD (ui_battle_content.c) can color a combatant's
 * name by its active status without calling the fixed-bank status_slots()
 * helper.  slot 0 = player, 1..n = enemy. */
extern StatusSlots s_battle_status[STATUS_ROUND_SLOTS];

/* Apply a status to an actor's slots (STACK rule: cap at max_stacks,
 * refresh duration).  stacks==0 means 1; duration==0 means the
 * definition's default.  Both are packed 4-bit by the wrapper -- pass 0
 * rather than >15.  actor_slot is the combatant's status slot index
 * (0 = player, 1..n = enemy; used for the frozen bitmask).
 * Emits STATUS_APPLIED when a new instance or new stack lands; returns
 * true when the actor is now afflicted.
 *
 * Fixed-bank wrapper around the ROM-bank-3 body; see banked.h. */
bool status_apply(StatusSlots *slots, uint8_t actor_slot, uint8_t id,
                  uint8_t stacks, uint8_t duration);

/* Bank-3 body dispatched by status_apply(). */
void status_apply_banked(void);

/* Run one end-of-turn tick over an actor's slots: accumulate tick damage
 * (per stack), decrement durations, expire finished instances.  Returns
 * total damage taken (caller applies it to HP and checks deaths).
 * Emits STATUS_TICKED and one STATUS_EXPIRED per removed instance.
 * actor_slot: 0 = player, 1..n = enemy index (telemetry context only). */
uint8_t status_tick(StatusSlots *slots, uint8_t actor_slot);

/* Bank-3 body dispatched by status_tick(). */
void status_tick_banked(void);

#endif /* RPG_STATUS_H */
