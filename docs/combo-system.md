# Card Combat Architecture — Detailed Future-Proof Plan

This plan locks down the architecture needed for **cards → hands → effects → buffs/debuffs**, while deliberately postponing implementation of the status system.

The key architectural rule is:

> **The combo system determines what hand the player made. The effect system determines what that hand does. Buffs/debuffs are effects, not combo mechanics.**

That separation is important because eventually a strong hand should be able to improve not only damage, but also poison chance, healing, stun duration, buff strength, etc.

---

# 0. Implementation status

**Phase A (cards → deck → selection → combo evaluator → attack/defend → HUD),
Phase B (generic effect layer), and the core of Phase C (status system) are
implemented.**  Status foundation (§12-§19 minus per-effect scaling):
`src/rpg/status.{h,c}` + bank-2 bodies (`status_content.c`) provide
StatusDefinition / StatusInstance / application with STACK+refresh rules
(POISON: tick 1/stack, max 5 stacks, duration 3); battle-scoped slots live
in the status module (0 = player, 1..n = enemies), reset at battle start.
Cards carry an on-hit rider as DATA (`CardDefinition.status_id/status_chance`,
copied into battle `Card`s); the deterministic RNG decides landing
(1/255 units, §17/§18) and poison ticks once per round at the
player-select boundary, with deaths resolving like combat deaths.
Telemetry: `STATUS_APPLIED` / `STATUS_TICKED` (payloads in
DEBUG_PROTOCOL.md §31.2).  Scenarios: `status_poison_apply` (apply + 3
ticks + expiry, payload-locked) and `status_poison_resist` (roll fails).

**Phase D (implemented)**: BURN (tick 2/round, max 3 stacks, duration 2)
and FREEZE (no tick, no stacking, duration 1 -- a frozen combatant skips
its next attack; battle consumes the frozen bitmask that apply + the
tick body maintain, emitting `TURN_SKIPPED`; player-side freeze skips
the whole offense).  Loot wiring: fire swords roll BURN riders and ice
swords FREEZE riders (docs/loot.md §34.1/§34.2).  Telemetry completed:
`STATUS_EXPIRED` fires per expiring tick and `STATUS_RESISTED` when a
rider roll fails (§19 fully implemented).  Scenarios: `status_burn_apply`,
`status_freeze_skip`, `status_freeze_player`.  Debug action
`DBG_ACT_APPLY_STATUS` applies any status to any combatant slot.
Poison's on-hit rider carries the **grey-out** rider (§16): applying
poison greys 2 random positions of the afflicted combatant's pool for
`POISON_GREY_TURNS` rounds (telemetry `CARDS_GREYED`, cleared by
`CARDS_UNGREYED`).  The gate lives in the battle telegraph: any pool
position covered by the victim's grey mask never plays -- for enemies the
mask ORs across ALL enemy slots against the party's single shared battle
deck, because every hostile encounter engages as a trio (three attackers
rotate through one draw position), so a greyed position must never be
drawn no matter which enemy is up.  A greyed draw emits `TURN_SKIPPED`
(cause `STATUS_POISON`) without advancing the draw position.  Scenarios:
`status_poison_apply` (apply + ticks + expiry + grey gate), `status_poison_reshuffle`
(5-stack cap, greyed cards recycled out of reshuffles, skips).  Still open
from Phase C/D: HEAL ALL; per-effect scaling refinements.

# 0.1 Hand table (implemented)

**Poker-hands-only scoring.** Only the cards forming the poker hand
count toward the effect magnitude -- every extra card deals/blocks 0.
HIGH CARD scores the single highest eligible card; pairs/kinds score
only the paired cards; five-card hands (straights, flushes, full houses
and up) score all five. Rings never add attack damage or block (0), but
count as wildcards for the tier: an attack ring converts the whole hand
to healing (scaled participating value, ring at its substituted value --
`SW1` + ring is a PAIR healing (1 + 1) x 120% = 2).

Strict poker sizing: pairs and kinds need >= 2 effective cards;
STRAIGHT, FLUSH, STRAIGHT_FLUSH and FIVE KIND require all five
effective cards (attack phase: every selected card; defend: SHIELDs
only -- other selected cards are inert for classification AND block);
the SHORT STRAIGHT needs a 3+ run at any staged count >= 3
(gem-gated, docs/short-straight.md).
Values classify order-independently.  Suited bonus: +25
percent when every effective card shares one symbol and a tier was
made.  Effective multiplier scales base_power into the effect amount,
and scales on-hit status rider chances (capped ~100%):

| Tier            | Mult | Tier           | Mult |
|-----------------|------|----------------|------|
| HIGH CARD       | 100  | FLUSH          | 240  |
| PAIR            | 120  | FULL HOUSE     | 260  |
| TWO PAIR        | 150  | FOUR KIND      | 280  |
| THREE KIND      | 180  | STRAIGHT FLUSH | 350  |
| STRAIGHT        | 210  | FIVE KIND      | 400  |
| SHORT STRAIGHT  | 130  |                |      |

The SHORT STRAIGHT (tier 10, docs/short-straight.md) needs a run of 3+
distinct sequential values at any staged count ≥ 3 and only classifies
while the STRAIGHT gem is equipped (GEM tab); it slots below THREE KIND,
above TWO PAIR.  Only the highest three run values count, one
best-eligible card each.

**Best-subset resolution.** Classification picks the ladder tier, but
resolution plays the highest-scoring subset of the staged cards: a
pair subset or a lone high card can beat the ladder's SHORT STRAIGHT
(`gem_combo_two_pair`, `gem_combo_singleton`), and a SHORT run can
beat the ladder's PAIR (`gem_combo_pair_run`).  Ringed hands convert
to healing, where best-subset evaluation and trial-amount tracking
maximize the resulting play.  The live COMBO-row preview shows the
winning play (`gem_combo_preview`, reads `SHORT STR`), so preview and
result agree.  Full rules live in docs/short-straight.md.

The POISON DAGGER (DA1) is a starter card: its own symbol, 1 physical
damage, base poison chance 50% scaled by the hand multiplier.  Poison
ticks a flat 1 HP per round after the afflicted actor's turn resolves;
re-applications refresh duration (stacks deepen for telemetry only).

Deviations from the plan's sketches, all
verified behavior-neutral by the harness (`combo_*`, `card_battle_*`,
`battle_sword_damage` scenarios assert `COMBO_RESOLVED` / `EFFECT_RESOLVED`
payloads):

* The hand table is **straights + same-type ("flush")** with suited bonuses
  (`s_straight_mults`, Baten-Kaitos-style number sequences), not PAIR/THREE
  KIND/FULL HOUSE.  §11's example values were illustrative; §26 leaves the
  exact table to balance.
* The effect vocabulary reuses the engine-generic `CardEffectType`
  (`rpg/cards.h`: DAMAGE_TARGET / BLOCK_DAMAGE / HEAL_HP) instead of new
  `EFFECT_*` names — same set, one enum.
* Every battle card carries its effect as data (`Card.effect`, copied from
  `CardDefinition.effect`): the definition owns the effect; combat consumes
  it.
* Combo evaluation and effect scaling share ONE banked dispatch
  (the banked combo dispatch → bank-3 body calling `effect_resolve_into`
  bank-locally), because the fixed bank is completely full (`make memmap`).
  The modules stay separate: the evaluator produces only quality
  (`ComboResult`: shape flags, multiplier, base_power, eff_count); the
  resolver produces only magnitude (`EffectResult`, readable via
  `effect_last()`).  A standalone fixed-entry `effect_resolve()` can be
  added when a second caller appears.
* `EffectContext` is deferred (§8 itself says don't fill speculative
  fields); the resolver is a pure function of (effect type, ComboResult).
  Application stays in battle flow: effects compute magnitudes, battle
  applies them to combatants.
* Per-effect combo scaling (§10 EffectScaling) is Phase C: every effect
  currently scales base_power identically (the former inline formula,
  ported verbatim into `effects_content.c`).

---

# 1. Final combat architecture

The intended combat pipeline is:

```text
                    PLAYER SELECTS CARDS
                            │
                            ▼
                    ┌───────────────┐
                    │ COMBO EVAL    │
                    └───────┬───────┘
                            │
                            ▼
                      ComboResult
                  ┌─────────┼─────────┐
                  │         │         │
                 Hand     Multiplier  Suited
                  │         │         │
                  └─────────┼─────────┘
                            ▼
                    EFFECT RESOLUTION
                            │
                ┌───────────┼───────────┐
                ▼           ▼           ▼
             DAMAGE       HEAL       STATUS
                                        │
                              ┌─────────┴─────────┐
                              ▼                   ▼
                            BUFF                DEBUFF
```

The systems have deliberately different responsibilities.

### Card system

Answers:

> What cards does the player own and what cards are in the deck?

### Combo system

Answers:

> What hand did these selected cards form?

### Effect system

Answers:

> What does this card do?

### Status system

Answers:

> What temporary effects are currently affecting this actor?

---

# 2. Do not implement Buffs/Debuffs yet

For the current implementation, **do not build**:

* status definitions;
* status instances;
* poison;
* burn;
* stun;
* buffs;
* debuffs;
* duration handling;
* stack handling;
* status resistance;
* status expiration.

But the card/effect architecture should not prevent them later.

The current implementation should therefore establish the interfaces that they will eventually plug into.

---

# 3. Card definitions

A card should ultimately describe an effect rather than simply being a number.

Conceptually:

```c
typedef struct {
    CardId id;

    const char *name;

    CardType type;

    uint8_t combo_type;
    uint8_t combo_value;

    uint8_t cost;
    uint8_t uses_per_battle;

    CardEffect effect;
} CardDefinition;
```

The important addition is:

```text
CardEffect
```

rather than hardcoding every card into combat logic.

---

# 4. Card effects

Plan for a generic effect representation.

Initially it can be very small:

```c
typedef enum {
    EFFECT_NONE = 0,
    EFFECT_DAMAGE,
    EFFECT_BLOCK,
    EFFECT_HEAL
} EffectType;
```

Later it expands:

```text
EFFECT_DAMAGE
EFFECT_BLOCK
EFFECT_HEAL
EFFECT_STATUS
EFFECT_DRAW
EFFECT_DISCARD
EFFECT_MODIFY_COST
EFFECT_MULTI
```

The exact list should remain open.

The important part is that **the card definition owns the effect**.

---

# 5. Do not put effects into the combo evaluator

This is the most important architectural constraint.

The combo evaluator must never contain:

```c
if (hand == HAND_THREE_KIND)
    poison_chance += 20;
```

or:

```c
if (hand == HAND_FULL_HOUSE)
    stun_duration += 1;
```

or:

```c
if (hand == HAND_FLUSH)
    heal += 5;
```

That would make the combo system responsible for every future mechanic.

Instead:

```text
Combo evaluator
       │
       ▼
ComboResult
       │
       ▼
Effect resolver
       │
       ├── damage
       ├── healing
       ├── block
       └── status
```

---

# 6. ComboResult

The combo system should return a generic result.

```c
typedef struct {
    HandId hand;
    uint16_t multiplier;
    bool suited;
} ComboResult;
```

Potentially also:

```c
uint8_t card_count;
```

if useful for future mechanics.

The result must not contain:

```text
poison chance
damage
heal
stun
burn
buff strength
```

Those belong to effect resolution.

---

# 7. Effect resolution

Create a separate subsystem:

```text
src/rpg/effects.h
src/rpg/effects.c
```

Its conceptual API:

```c
EffectResult effect_resolve(
    const CardDefinition *card,
    const ComboResult *combo,
    const EffectContext *context
);
```

The effect resolver receives:

1. the card's effect;
2. the resolved hand;
3. relevant combat context.

It then calculates the actual outcome.

For example:

```text
Card:
TOXIC BLADE

Base:
Damage = 4
Poison chance = 20%

Combo:
THREE KIND
Multiplier = 185%
```

The resolver could eventually produce:

```text
Damage = 7
Poison chance = 37%
```

The precise poison formula is deliberately **not part of this plan yet**.

---

# 8. EffectContext

Plan for a context object rather than passing dozens of parameters into every effect function.

Conceptually:

```c
typedef struct {
    ActorId source;
    ActorId target;

    const ComboResult *combo;

    uint8_t battle_type;
} EffectContext;
```

Eventually this could contain:

```text
source
target
selected cards
combo
current turn
battle state
existing statuses
random state
```

But do not fill it with speculative fields now.

The principle is:

> Effects receive context; effects don't reach arbitrarily into global game state.

---

# 9. How combo strength should influence effects

This is where the future poison mechanic belongs.

Do **not** think of the combo as:

```text
THREE KIND = +85% damage
```

Instead think:

```text
THREE KIND
    ↓
multiplier = 185
```

Then individual effects decide how to use that result.

For damage:

```text
base damage × combo multiplier
```

For healing:

```text
base heal × combo multiplier
```

For poison:

```text
base poison chance
        +
combo quality modifier
```

Those do not necessarily need to use the same mathematical formula.

That is important for balancing.

A 185% damage multiplier does not necessarily mean:

> "Poison chance becomes 185%."

Otherwise a 100% base chance could become nonsensical.

---

# 10. Introduce the concept of Effect Scaling

Eventually, effects should specify **how they respond to combo strength**.

For example:

```c
typedef enum {
    SCALE_NONE,
    SCALE_POWER,
    SCALE_CHANCE,
    SCALE_DURATION,
    SCALE_STACKS
} EffectScaling;
```

A future Toxic Blade might say:

```text
Damage:
    scales with combo power

Poison:
    scales with combo quality
```

Whereas a different card might have:

```text
Stun:
    fixed chance
    duration scales with combo
```

This prevents the combo system from deciding how every effect scales.

---

# 11. Separate multiplier from combo quality

This is worth planning now.

The existing hand table produces multipliers:

```text
PAIR        125
THREE KIND  160
FULL HOUSE  190
```

But later, a status may not want to use those raw values directly.

For example:

```text
ONE PAIR       → quality 1
TWO PAIR       → quality 2
THREE KIND     → quality 3
FULL HOUSE     → quality 4
FOUR KIND      → quality 5
```

The combo result could eventually expose both:

```c
typedef struct {
    HandId hand;
    uint16_t multiplier;
    uint8_t quality;
    bool suited;
} ComboResult;
```

This is preferable to forcing every future effect to reverse-engineer:

```text
190 → FULL HOUSE → quality 4
```

However, **don't necessarily implement `quality` yet**. Just keep the design open for it.

---

# 12. Future status architecture

When we eventually implement statuses, create:

```text
src/rpg/status.h
src/rpg/status.c
```

There should be three distinct concepts.

### StatusDefinition

What a status *is*.

```text
POISON
BURN
STUN
WEAKEN
REGEN
```

### StatusInstance

A status currently affecting an actor.

```text
POISON
stacks = 2
duration = 3
```

### StatusApplication

The event of trying to apply a status.

```text
source
target
status
chance
stacks
duration
```

This distinction will become extremely useful.

---

# 13. Status application pipeline

Eventually:

```text
TOXIC BLADE
      │
      ▼
EFFECT RESOLVER
      │
      ▼
STATUS APPLICATION
      │
      ├── status = POISON
      ├── chance = 37%
      ├── stacks = 1
      └── duration = 3
              │
              ▼
        RANDOM ROLL
          /       \
       success    fail
         │          │
         ▼          ▼
     apply status   nothing
```

The combo evaluator never sees Poison.

---

# 14. Buffs and debuffs should share the same status system

Do not create:

```text
BuffSystem
DebuffSystem
```

as two independent architectures.

Use:

```text
StatusSystem
```

with statuses that happen to be beneficial or harmful.

For example:

```text
POISON      harmful
BURN        harmful
WEAKEN      harmful

REGEN       beneficial
STRENGTH    beneficial
HASTE       beneficial
```

Potentially:

```c
typedef enum {
    STATUS_NEUTRAL,
    STATUS_BUFF,
    STATUS_DEBUFF
} StatusPolarity;
```

But this should primarily be metadata/UI information, not a separate execution path.

---

# 15. Status duration

Plan for duration from the beginning.

Eventually:

```text
POISON
Duration: 3 turns
Stacks: 2
```

The status system owns:

```text
turn start
turn end
expiration
```

The card/effect system only requests:

> Apply Poison for 3 turns.

It does not manually decrement the duration.

---

# 16. Status stacking

This should also be centralized.

Eventually each status definition can specify:

```text
STACK
REFRESH
REPLACE
IGNORE
```

For example:

```text
POISON
stack up to 5

STUN
does not stack
refreshes duration

REGEN
refreshes duration
```

Again, none of this belongs in the combo system.

---

# 17. Probability handling

A future status application should have a single place where probability is resolved.

Something like:

```c
bool status_roll_success(
    uint8_t chance,
    RngState *rng
);
```

The effect resolver computes:

```text
final chance
```

The status system performs:

```text
roll
```

This separation matters for deterministic testing.

For example:

```text
Effect resolver:
20% base
+ 15% combo bonus
= 35%

Status system:
RNG roll = 27
→ success
```

---

# 18. Deterministic RNG

Because this is a Game Boy game and the repository already emphasizes deterministic scenario testing, status probabilities should use the game's existing deterministic RNG rather than introducing ad-hoc randomness.

Tests should be able to establish:

```text
seed
+
card
+
hand
```

and reliably assert:

```text
POISON_APPLIED
```

or:

```text
POISON_RESISTED
```

This will become particularly important when debugging seemingly random combat behavior.

---

# 19. Telemetry plan

The existing combo telemetry should remain:

```text
COMBO_RESOLVED
```

with:

```text
hand
multiplier
```

Later add separate effect telemetry:

```text
EFFECT_RESOLVED
```

and:

```text
STATUS_ATTEMPTED
STATUS_APPLIED
STATUS_RESISTED
STATUS_EXPIRED
```

For example:

```text
COMBO_RESOLVED
    hand = THREE_KIND
    multiplier = 185

EFFECT_RESOLVED
    effect = TOXIC_BLADE
    damage = 7
    poison_chance = 37

STATUS_APPLIED
    status = POISON
    target = ENEMY_1
```

This makes debugging incredibly easier.

---

# 20. Do not put status state in CardState

This is another important boundary.

Persistent:

```text
CardState
├── collection
└── deck
```

Battle:

```text
BattleState
├── deck runtime
├── card uses
├── actors
└── statuses
```

A Poison status affecting an enemy should never appear in the player's card collection.

Likewise, a card's base Poison definition should not contain the enemy's current Poison stacks.

---

# 21. Do not make cards themselves stateful

Avoid:

```text
Potion.poison_chance = 37
```

or:

```text
ToxicBlade.current_uses
```

Card definitions should remain immutable.

Instead:

```text
CARD DEFINITION
    base poison = 20%

COMBAT
    combo = THREE KIND

EFFECT RESULT
    poison chance = 37%
```

This prevents battle-specific state leaking into persistent card data.

---

# 22. Example future card

A complete future card might conceptually be:

```text
TOXIC BLADE
────────────────
Type: ATTACK
Value: 4
Cost: 2

Effect:
  Deal 4 damage.
  20% chance to apply Poison.
```

Played as a sequence:

```text
TOXIC BLADE
TOXIC BLADE
```

Result:

```text
SEQUENCE
×120

Damage:
4 + 4 = 8
8 × 120% = 9

Poison:
base chance = 20%
combo-adjusted chance = future formula
```

Played as:

```text
TOXIC BLADE
TOXIC BLADE
TOXIC BLADE
```

Result:

```text
THREE KIND
×160
```

Now the same card effect becomes more potent because of the hand.

The card doesn't need to know what hand was played.

---

# 23. Defend works the same way

This architecture also makes defensive status effects possible later.

For example:

```text
IRON WALL

Block: 5
Buff:
  20% chance to gain FORTIFIED
```

The same pipeline works:

```text
cards
 ↓
combo
 ↓
effect resolution
 ↓
block
 ↓
status application
```

A good defensive hand could therefore eventually increase:

* block;
* Fortified chance;
* Fortified duration;
* stacks.

No special defend-specific status architecture is needed.

---

# 24. Future architecture in one diagram

```text
                         CARD DEFINITION
                               │
                               ▼
                       SELECTED CARDS
                               │
                               ▼
                     ┌──────────────────┐
                     │  COMBO EVALUATOR │
                     └────────┬─────────┘
                              │
                              ▼
                        ComboResult
                    ┌─────────┼─────────┐
                    │         │         │
                   Hand    Multiplier  Suited
                    │         │         │
                    └─────────┼─────────┘
                              │
                              ▼
                     ┌──────────────────┐
                     │ EFFECT RESOLVER  │
                     └────────┬─────────┘
                              │
             ┌────────────────┼────────────────┐
             │                │                │
             ▼                ▼                ▼
          DAMAGE             HEAL           STATUS
                                              │
                                              ▼
                                      ┌──────────────┐
                                      │STATUS SYSTEM │
                                      └──────┬───────┘
                                             │
                              ┌──────────────┼──────────────┐
                              ▼              ▼              ▼
                           POISON           BURN          REGEN
```

---

# 25. Implementation phases

### Phase A — Current work

Implement only:

1. Card definitions.
2. Card collection.
3. 20-card deck.
4. Card selection.
5. Combo evaluator.
6. Locked hand table.
7. Attack resolution.
8. Defend resolution.
9. Combo telemetry.
10. Combo HUD.

### Phase B — Architecture preparation

Before considering the system complete:

11. Introduce generic `CardEffect`.
12. Keep effects separate from combo evaluation.
13. Introduce `EffectContext`.
14. Introduce `EffectResult`.
15. Move damage/heal/block resolution behind the effect layer.

These can initially support only:

```text
DAMAGE
BLOCK
HEAL
```

### Phase C — Future

Only later:

16. `StatusDefinition`
17. `StatusInstance`
18. Status application
19. Buff/debuff duration
20. Stacking rules
21. Status probability
22. Combo-based effect scaling
23. Status telemetry
24. Status-specific cards.

---

# 26. What we should explicitly NOT decide yet

I would leave these unresolved until the actual status design pass:

* exact poison chance scaling;
* whether combo multiplier also scales status chance;
* whether status duration scales with hand;
* whether status stacks scale with hand;
* maximum status stacks;
* status resistance;
* status immunity;
* buff/debuff cleansing;
* whether statuses tick before or after actions;
* whether poison is based on percentage HP or flat damage;
* whether different statuses interact;
* whether statuses can crit;
* whether suited hands affect status chance differently from normal hands.

Those are **balance/design decisions**, not architecture decisions.

The architecture should merely make them possible.

---

# 27. Critical design rule

I would add this to the project specification verbatim:

> **Combos describe the quality of a card selection. They do not describe the effect produced by that selection.**
>
> A combo produces a `ComboResult`. Card effects consume that result when determining their final outcome. Statuses are one possible effect and are resolved independently by the status system.
>
> This allows future mechanics to scale with hand quality without introducing card-, status-, or effect-specific logic into the combo evaluator.

That gives you a very clean long-term system:

**Cards define actions.
Combos define quality.
Effects define consequences.
Statuses define persistent temporary conditions.**

And importantly, we can implement the first three quarters of the combat system now without prematurely committing ourselves to how Poison, buffs, debuffs, or their probability formulas ultimately work.
