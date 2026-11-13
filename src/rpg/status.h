#ifndef RPG_STATUS_H
#define RPG_STATUS_H

#include <stdint.h>
#include <stdbool.h>

/* ── Status system foundation (docs/combo-system.md Phase C, §12-§21) ──
 * Three distinct concepts, per plan §12:
 *   StatusDefinition -- what a status IS (bank-10 table, status_content.c)
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
    /* Named FREEZE for historic id stability (wire/telemetry/save
     * compatible); it means ICE -- greys out cards, never skips turns
     * (docs/loot.md §34.6).  Do not add skip semantics here. */
    STATUS_FREEZE = 3
} StatusId;

/* Instances per combatant.  Small fixed budget: statuses are rare, and
 * Combatant embeds a StatusSlots (player + up to MAX_BATTLE_ENEMIES). */
#define MAX_STATUSES_PER_COMBATANT 2

/* Shared poison definition constants (used by both the bank-10 status
 * table in status_content.c and the bank-11 post-swing counter rider
 * in battle_rider_banked.c so the two banks cannot drift). */
#define STATUS_POISON_MAX_STACKS   5
#define STATUS_POISON_DURATION     2

/* One active affliction/blessing on one combatant. */
typedef struct {
    uint8_t id;        /* StatusId */
    uint8_t stacks;    /* 1..max_stacks (definition) */
    uint8_t duration;  /* turns remaining */
    uint8_t tick;      /* per-round damage for THIS instance.  Burn scales:
                        * 10% of the inflicting hit's damage (min 1),
                        * staged per application (see status_apply); poison
                        * and freeze stay 0. */
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
                              * stacks only refresh duration/telemetry.
                              * Burn ignores this: its per-instance tick is
                              * scaled at apply time (10% of the inflicting
                              * hit, min 1 -- see StatusInstance.tick). */
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

/* Frozen-combatant bitmask (UNUSED since the freeze-skip removal --
 * ice now greys out instead of skipping turns, and poison confuses
 * instead of greying; see the grey mask below).  Kept as a flat WRAM
 * byte (and its descriptor byte) so the WRAM layout and the host
 * transport never shift -- do not remove, do not read. */
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

/* ── Ice grey-out (battle-scoped) ────────────────────────────────
 * When a combatant is iced, ICE_GREY_CARDS random cards in its pool
 * become unplayable for ICE_GREY_TURNS future player-decision rounds.
 * For the player (slot 0) the pool is the 5-card battle hand
 * (position bit = hand idx); for an enemy (slot 1..n) it is that enemy's
 * deck position (bit = the shared battle deck's draw position).
 * Battle-scoped runtime state like the statuses themselves -- never
 * GameState/save; cleared at battle start via status_reset_battle(). */
#define ICE_GREY_CARDS 2
#define ICE_GREY_TURNS 2

/* Bit p set = pool position p is greyed for actor slot (0 = player,
 * 1..n = enemy).  Duration drains at each enter-select round boundary
 * in lockstep with the STATUS_FREEZE instance, so tint and grey-out
 * expire together after ICE_GREY_TURNS rounds (0 = not greyed).
 * Effective coverage for a mid-round application: the victim's current
 * round is still ahead for an enemy (same-round swing fizzles) and its
 * defend select is still ahead for the hero, so both sides feel two
 * affected rounds (enemy swings N/N+1, hero defends N/N+1 plus select
 * N+1) -- a single shared counter can never cover more without
 * outliving the tint (the old +1 desync). */
extern uint8_t s_grey_mask[STATUS_ROUND_SLOTS];
extern uint8_t s_grey_dur[STATUS_ROUND_SLOTS];

/* Re-apply an iced victim's grey-out.  pool_size is the victim's card
 * pool (BATTLE_HAND_SIZE for the player, the enemy battle deck's count for
 * an enemy).  On a fresh grey the bank-10 body picks ICE_GREY_CARDS
 * distinct indices from the deterministic RNG; an already-active grey is
 * only refreshed (same cards, same duration).  Emits EVENT_CARDS_GREYED.
 * Fixed-bank wrapper around the ROM-bank-10 body; see banked.h. */
void status_grey_apply(uint8_t actor_slot, uint8_t pool_size);

/* Bank-10 body dispatched by status_grey_apply(). */
void status_grey_apply_banked(void);

/* One round boundary for a greyed combatant: decrement the duration and,
 * when it hits zero, clear the mask and emit EVENT_CARDS_UNGREYED.  Called
 * by the battle tick loop alongside status_tick().  Fixed wrapper around
 * the ROM-bank-10 body; see banked.h. */
void status_grey_tick(uint8_t actor_slot);

/* Bank-10 body dispatched by status_grey_tick(). */
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
 * Burn scaling: the banked body reads the basis (inflicting hit's
 * damage, 10% per round, min 1) from g_effect_last.amount -- the combo
 * dispatch every hit runs leaves it there, so gameplay rider paths
 * (attack strike, dagger counter) always see their own hit.  Debug and
 * organic applies see whatever the last dispatch left (bounded below by
 * the min-1 floor; no shipped scenario depends on more) -- in
 * particular, a riderless or resisted hit leaves a stale amount behind
 * (no apply runs to consume-clear it), so a later debug burn apply in
 * the same battle would scale off it.  No shipped scenario stages that
 * order; a future one must apply burn before the first hit.  Poison and
 * freeze ignore the basis.
 * Emits STATUS_APPLIED when a new instance or new stack lands; returns
 * true when the actor is now afflicted.
 *
 * Fixed-bank wrapper around the ROM-bank-10 body; see banked.h. */
bool status_apply(StatusSlots *slots, uint8_t actor_slot, uint8_t id,
                  uint8_t stacks, uint8_t duration);

/* Bank-10 body dispatched by status_apply(). */
void status_apply_banked(void);

/* Run one end-of-turn tick over an actor's slots: accumulate tick damage
 * (per instance), decrement durations, expire finished instances.  Returns
 * total damage taken (caller applies it to HP and checks deaths).
 * Emits STATUS_TICKED and one STATUS_EXPIRED per removed instance.
 * actor_slot: 0 = player, 1..n = enemy index (telemetry context only). */
uint8_t status_tick(StatusSlots *slots, uint8_t actor_slot);

/* Bank-10 body dispatched by status_tick(). */
void status_tick_banked(void);

#endif /* RPG_STATUS_H */
