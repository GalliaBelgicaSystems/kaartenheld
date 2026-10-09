# Procedural Card Generation & Loot System Design

## 1. Purpose

This document defines the architecture for a **Borderlands-inspired procedural card loot system**.

Enemies do not primarily drop weapons, armor, potions, crafting materials, or loose currency.

They drop **cards**.

Some cards are useful in battle and can be added to the player's deck. Others exist primarily as valuable loot that can be sold for **gold**, the game's currency.

The goal is to make every loot drop potentially interesting:

> **What card did I get, and is it worth keeping?**

A card is therefore both:

* a gameplay object;
* a loot object.

The system must support large amounts of procedural variation without requiring every possible card to be manually authored.

---

# 2. Core Design Principles

## 2.1 Everything is a card

The player does not manage separate inventories for:

* weapons;
* potions;
* armor;
* loot;
* junk.

The player has a **card collection**.

Cards may have different purposes, but they all belong to the same overall collection.

```text
PLAYER COLLECTION
│
├── Combat Cards
│
├── Valuable Cards
│
└── Special Cards
```

---

## 2.2 Cards are instances, not just definitions

A major architectural distinction is required.

A card family defines what kind of card something is.

A generated card instance defines the actual card the player owns.

Example:

```text
ARCHETYPE
    PLASMA BLADE
```

Generated cards:

```text
VENOMOUS PLASMA BLADE
Damage: 6
Poison: 24%

HEAVY PLASMA BLADE
Damage: 9
Cost: 4

PRECISION PLASMA BLADE
Damage: 5
Combo Value: 5
```

These are different cards.

Therefore:

```text
CardArchetype
        ↓
Loot Generation
        ↓
CardInstance
        ↓
Player Collection
        ↓
Deck
```

The deck stores specific card instances.

---

## 2.3 Procedural does not mean random chaos

The generator must not simply randomize every property independently.

Bad generation:

```text
Damage: 18
Cost: 1
Poison: 60%
```

or:

```text
Damage: 1
Cost: 8
No secondary effect
```

Procedural generation must operate within controlled rules.

Every generated card should come from:

* an archetype;
* a rarity;
* a controlled stat range;
* a modifier pool;
* balance constraints;
* a power budget.

The result should feel unpredictable to the player but controlled by the game.

---

# 3. Card Categories

Every card has a primary purpose.

```c
typedef enum {
    CARD_PURPOSE_COMBAT,
    CARD_PURPOSE_VALUABLE,
    CARD_PURPOSE_SPECIAL
} CardPurpose;
```

---

## 3.1 Combat Cards

Combat cards can enter the player's deck.

They have gameplay properties such as:

```text
Effect
Power
Combo Type
Combo Value
Cost
Battle Use Rules
Modifiers
Rarity
Sell Value
```

Examples:

```text
VENOMOUS PLASMA BLADE

Damage: 6
Poison Chance: 24%
Combo Type: BLADE
Combo Value: 3
```

```text
REINFORCED BARRIER

Block: 8
Combo Type: SHIELD
Combo Value: 4
```

Combat cards can always be sold if the player does not want them.

---

## 3.2 Valuable Cards

Valuable cards exist primarily as loot.

They cannot be placed into the battle deck.

Their purpose is:

```text
DROP
  ↓
INSPECT
  ↓
SELL
  ↓
gold
```

Examples:

```text
DAMAGED NAVIGATION CORE
Value: 42 gold
```

```text
PRISTINE MILITARY AI CHIP
Value: 630 gold
```

```text
ANCIENT QUANTUM PROCESSOR
Value: 4,200 gold
```

These cards should still be procedurally generated and collectible.

The player should feel:

> "This isn't useful in battle, but this is an amazing drop."

---

## 3.3 Special Cards

Reserved for future systems.

Possible examples:

```text
Quest Cards
Access Cards
Story Cards
Boss Trophies
Unique Event Cards
```

These should not be mixed into normal procedural loot until their specific mechanics are designed.

---

# 4. Card Anatomy

A generated card is built from multiple layers.

```text
                    CARD INSTANCE
                         │
          ┌──────────────┼──────────────┐
          │              │              │
          ▼              ▼              ▼
       IDENTITY        GAMEPLAY       ECONOMY
          │              │              │
       Archetype       Effects        Sell Value
       Prefix          Stats          Rarity
       Suffix          Modifiers      Market Value
       Name
```

A combat card may look conceptually like:

```text
CARD INSTANCE
──────────────────────

Name:
VENOMOUS PLASMA BLADE OF THE VIPER

Purpose:
COMBAT

Archetype:
PLASMA BLADE

Rarity:
RARE

Prefix:
VENOMOUS

Suffix:
OF THE VIPER

Combat:
Damage: 7
Poison Chance: 28%

Combo:
Type: BLADE
Value: 4

Economy:
Sell Value: 145 gold
```

---

# 5. Card Archetypes

An archetype is the foundation from which a card is generated.

It defines the basic identity and legal modifier pools.

Examples:

```text
PLASMA BLADE
ARC BLASTER
KINETIC HAMMER
IRON WALL
ENERGY BARRIER
MEDICAL INJECTOR
REPAIR PROTOCOL
TOXIN MODULE
```

A future archetype definition could conceptually contain:

```c
typedef struct {
    CardArchetypeId id;

    CardPurpose purpose;

    CardType type;

    EffectTemplate base_effect;

    ComboType combo_type;

    uint8_t min_combo_value;
    uint8_t max_combo_value;

    AffixPool prefix_pool;
    AffixPool suffix_pool;
} CardArchetype;
```

The exact implementation can differ.

The important rule is:

> **An archetype constrains procedural generation.**

A Plasma Blade should not suddenly generate as a healing card unless a specifically designed modifier allows that.

---

# 6. Rarity

Rarity determines more than how large the numbers are.

The preferred model is:

> **Rarity controls complexity, modifier access, and power budget.**

Suggested rarity tiers:

```text
COMMON
UNCOMMON
RARE
EPIC
LEGENDARY
```

These names can change later.

---

## 6.1 Common

Typically:

```text
Base archetype
Minimal variation
0–1 minor modifier
```

Example:

```text
PLASMA BLADE

Damage: 5
Combo Value: 3
```

---

## 6.2 Uncommon

Typically:

```text
Base archetype
+ meaningful modifier
```

Example:

```text
HEAVY PLASMA BLADE

Damage: 8
Cost: Increased
```

---

## 6.3 Rare

Typically:

```text
Base archetype
+ major modifier
+ possible secondary effect
```

Example:

```text
VENOMOUS PLASMA BLADE

Damage: 6
Poison Chance: 20%
```

---

## 6.4 Epic

Typically:

```text
Multiple modifiers
Interesting tradeoffs
Strong specialization
```

Example:

```text
SURGICAL VENOMOUS PLASMA BLADE

Damage: 8
Poison Chance: 32%
Combo Value: 5
```

---

## 6.5 Legendary

Legendary cards should not simply be:

```text
Epic card
+ bigger numbers
```

They should introduce a special rule.

Example:

```text
THE LAST PROTOCOL

Damage: 5

If resolved as FOUR KIND:
Apply an additional special effect.
```

Legendary cards may eventually be:

* procedurally assembled from restricted high-tier components;
* partially handcrafted;
* entirely unique.

This should remain an open design decision.

---

# 7. Prefixes and Suffixes

Modifiers should contribute to card identity.

Example structure:

```text
PREFIX + ARCHETYPE + SUFFIX
```

Examples:

```text
VENOMOUS PLASMA BLADE
HEAVY KINETIC HAMMER
PRECISION ARC BLASTER

PLASMA BLADE OF THE VIPER
ARC BLASTER OF THE OVERLOAD
BARRIER OF THE BASTION
```

Combined:

```text
VENOMOUS PLASMA BLADE OF THE VIPER
```

The naming system should not be purely cosmetic.

Every name component should correspond to gameplay properties.

Example:

```text
VENOMOUS
    → Adds poison-related effect

HEAVY
    → Increases power
    → May increase cost

PRECISION
    → Improves combo properties

REINFORCED
    → Improves block

VOLATILE
    → Increases power
    → Introduces risk
```

The exact modifier catalogue should be designed later.

---

# 8. Procedural Generation Pipeline

The generator should follow a controlled sequence.

```text
ENEMY DEFEATED
       │
       ▼
DETERMINE DROP COUNT
       │
       ▼
ROLL LOOT CATEGORY
       │
       ├── Combat
       ├── Valuable
       └── Special
       │
       ▼
SELECT ARCHETYPE
       │
       ▼
ROLL RARITY
       │
       ▼
ALLOCATE POWER BUDGET
       │
       ▼
SELECT PREFIX
       │
       ▼
SELECT SUFFIX
       │
       ▼
GENERATE STATS
       │
       ▼
VALIDATE CARD
       │
       ▼
GENERATE SELL VALUE
       │
       ▼
CREATE CARD INSTANCE
```

Every step should use deterministic game RNG.

---

# 9. Power Budget System

The procedural generator requires a balance system.

Each rarity has a power budget.

Example only:

```text
COMMON      40
UNCOMMON    60
RARE        90
EPIC       130
LEGENDARY  Special
```

The generated card spends this budget.

Example:

```text
RARE CARD BUDGET: 90

Base Damage              35
Poison Effect            25
High Combo Value         20
Minor Modifier           10
────────────────────────────
TOTAL                    90
```

Another Rare card might be:

```text
RARE CARD BUDGET: 90

High Damage              65
Increased Cost Penalty  -15
Special Modifier         40
────────────────────────────
TOTAL                    90
```

Negative modifiers can compensate for powerful properties.

This allows interesting cards with tradeoffs.

---

# 10. Generated Stats

Stats must be generated within ranges defined by the archetype and rarity.

Example:

```text
PLASMA BLADE

COMMON:
Damage 4–6

UNCOMMON:
Damage 5–8

RARE:
Damage 6–10

EPIC:
Damage 8–14
```

The actual numbers should be balanced later.

The important architectural rule is:

> **The generator chooses values from constrained ranges rather than unrestricted random values.**

---

# 11. Modifiers and Tradeoffs

Modifiers should create meaningful choices.

Good procedural loot is not always:

```text
Higher rarity = strictly better.
```

Example:

```text
HEAVY PLASMA BLADE

Damage: 10
Cost: High
```

versus:

```text
LIGHT PLASMA BLADE

Damage: 6
Cost: Low
Combo Value: 5
```

Neither is automatically better.

One may be useful in:

```text
High-power decks
```

The other may be useful in:

```text
Combo-focused decks
```

This is important because the game has only limited deck space.

The player should be constantly asking:

> "Is this card actually better for my deck?"

not merely:

> "Is the rarity number higher?"

---

# 12. Combo Properties and Procedural Generation

Combat cards participate in the combo system.

Generated cards therefore require controlled combo properties.

Every combat card may have:

```text
Combo Type
Combo Value
```

Example:

```text
VENOMOUS PLASMA BLADE

Combo Type: BLADE
Combo Value: 3
```

Another:

```text
PRECISION PLASMA BLADE

Combo Type: BLADE
Combo Value: 5
```

A higher combo value is not automatically better.

For example:

```text
Value 2
```

may be useful because the player's deck already contains several `2` cards.

Procedural generation should therefore treat combo values as a balancing and deck-building dimension, not simply a power stat.

---

# 13. Effect Properties

Combat cards eventually contain one or more effects.

Initial effect families may include:

```text
DAMAGE
BLOCK
HEAL
```

Future effect families may include:

```text
POISON
BURN
STUN
WEAKEN
REGEN
DRAW
DISCARD
COST MODIFICATION
```

The generator must not directly implement status mechanics.

Instead:

```text
Generated Card
       │
       ▼
Effect Definition
       │
       ▼
Future Effect Resolver
```

For example:

```text
VENOMOUS

Modifier adds:

Status Effect:
POISON

Base Chance:
20–30%
```

The eventual effect resolver and combo system determine the final outcome during battle.

---

# 14. Future Combo Interaction

The procedural loot system should support cards whose effects respond differently to combo quality.

Example:

```text
TOXIC BLADE

Damage:
Scales with combo multiplier

Poison:
Future scaling mode
```

Potential future metadata:

```text
SCALE_NONE
SCALE_POWER
SCALE_CHANCE
SCALE_DURATION
SCALE_STACKS
```

The generator may eventually select effect scaling modes from legal archetype pools.

However:

> **The procedural generator creates card properties. The combo evaluator does not know what Poison, Healing, or other effects mean.**

---

# 15. Valuable Card Generation

Valuable cards should use a similar procedural system.

Example:

```text
PREFIX
DAMAGED

ORIGIN
MILITARY

OBJECT
NAVIGATION CORE
```

Generated result:

```text
DAMAGED MILITARY NAVIGATION CORE

Rarity: Common
Sell Value: 32 gold
```

Higher quality:

```text
PRISTINE PRE-WAR NAVIGATION CORE

Rarity: Rare
Sell Value: 870 gold
```

The identity can be generated from:

```text
Condition
Origin
Object Type
Technology Tier
Rarity
```

This makes economic loot interesting to inspect.

---

# 16. Sell Value

Sell value should be generated from controlled components.

Conceptually:

```text
BASE VALUE
× RARITY FACTOR
× QUALITY FACTOR
× SPECIAL MODIFIER
```

For combat cards:

```text
Base Archetype Value
+ Modifier Value
+ Rarity Value
= Sell Value
```

Example:

```text
PLASMA BLADE
Base: 40 gold

VENOMOUS:
+30

RARE:
×2

Final:
140 gold
```

The exact formula should be centralized.

Do not calculate sell values separately throughout the game.

---

# 17. Enemy Loot Profiles

Enemies should influence the kinds of cards they can generate.

Each enemy or enemy family should have a loot profile.

Example:

```text
ROBOT
│
├── Combat
│   ├── Plasma
│   ├── Arc
│   ├── Shield
│   └── Tech
│
└── Valuable
    ├── AI Core
    ├── Circuit
    ├── Capacitor
    └── Processor
```

Another:

```text
TOXIC CREATURE
│
├── Combat
│   ├── Poison
│   ├── Bleed
│   └── Organic attacks
│
└── Valuable
    ├── Biological samples
    ├── Rare glands
    └── Genetic material
```

This gives enemies a recognizable loot identity.

---

# 18. Loot Tables

Enemy loot should be generated from weighted pools.

Conceptually:

```text
Enemy Loot Profile
        │
        ├── 60% Combat
        ├── 35% Valuable
        └── 5% Special
```

Within Combat:

```text
50% Common
30% Uncommon
15% Rare
4% Epic
1% Legendary
```

These are placeholders.

Actual percentages should be tuned through testing.

The architecture should allow:

```text
Enemy difficulty
Boss status
Area
Player progression
Luck modifiers
```

to influence loot generation later.

---

# 19. Boss Loot

Bosses should not simply have:

```text
More random cards
```

Bosses should have access to:

* higher rarity chances;
* unique archetypes;
* special modifier pools;
* restricted valuable cards;
* possible guaranteed drops.

Example:

```text
BOSS DEFEATED
      │
      ├── Normal Procedural Drop
      │
      └── Boss-Specific Drop
```

This creates memorable rewards without abandoning procedural generation.

---

# 20. Card Instance Data

The player owns card instances.

Conceptually:

```c
typedef struct {
    CardInstanceId instance_id;

    CardArchetypeId archetype;

    CardPurpose purpose;

    Rarity rarity;

    AffixId prefix;
    AffixId suffix;

    GeneratedStats stats;

    uint32_t sell_value;
} CardInstance;
```

The actual storage format must be optimized for the Game Boy's memory constraints.

The conceptual model matters more than the final struct layout at this stage.

---

# 21. Persistent Collection

The player's collection contains card instances.

```text
PLAYER COLLECTION

Combat Cards
├── Card Instance #001
├── Card Instance #002
├── Card Instance #003
└── ...

Valuable Cards
├── Card Instance #004
├── Card Instance #005
└── ...
```

The deck references instances from the collection.

```text
COLLECTION
     │
     ▼
SELECT CARD INSTANCE
     │
     ▼
ADD TO DECK
```

The deck does not duplicate the card.

It references the card instance.

---

# 22. Deck Integration

The battle deck contains a fixed number of selected card instances.

For the current design:

```text
MAX DECK SIZE: 20
```

Example:

```text
DECK

1. Venomous Plasma Blade
2. Heavy Plasma Blade
3. Reinforced Barrier
4. Precision Arc Blaster
5. ...
20. Emergency Repair
```

The same archetype may appear multiple times.

However:

```text
Venomous Plasma Blade #001
```

and:

```text
Venomous Plasma Blade #002
```

are still separate instances.

This is important for future procedural differences.

---

# 23. Loot Collection Flow

After an enemy is defeated:

```text
ENEMY DEFEATED
       │
       ▼
GENERATE LOOT
       │
       ▼
DISPLAY CARD(S)
       │
       ▼
PLAYER CONTINUES
       │
       ▼
CARD ADDED TO COLLECTION
```

The player should see the generated card before it disappears into the collection.

The reveal moment is part of the reward.

---

# 24. Selling Flow

At a merchant or appropriate game location:

```text
PLAYER COLLECTION
       │
       ▼
SELECT CARD
       │
       ├── KEEP
       │
       ├── ADD TO DECK
       │
       └── SELL
              │
              ▼
          DESTROY CARD
              │
              ▼
          ADD gold
```

Selling a card permanently removes the card instance.

This creates a meaningful decision.

---

# 25. Duplicate Handling

Duplicates are allowed.

Procedural generation means two cards with the same archetype are not necessarily identical.

Example:

```text
PLASMA BLADE #1

Damage: 6
Combo Value: 3
```

```text
PLASMA BLADE #2

Damage: 8
Combo Value: 5
Cost: Higher
```

The player decides which one fits the deck.

This prevents the collection from becoming a simple count of predefined cards.

---

# 26. Deterministic Generation

Procedural generation must use deterministic game RNG.

The same:

```text
Seed
+
Enemy
+
Loot Profile
```

must generate reproducible results.

This is necessary for:

* debugging;
* automated tests;
* scenario testing;
* bug reproduction.

Tests should be able to assert:

```text
Given seed X
When enemy Y is defeated
Then generated card Z has:
    Archetype
    Rarity
    Prefix
    Stats
    Sell Value
```

---

# 27. Validation

Every generated card must pass validation before it is created.

The validation layer checks:

```text
Is this archetype valid?

Is this prefix legal for this archetype?

Is this suffix legal?

Are generated stats within allowed limits?

Is the power budget valid?

Are effects compatible?

Is the sell value valid?
```

If generation produces an invalid result:

```text
Discard generation attempt
        ↓
Regenerate
```

or preferably:

> Design generation constraints so invalid combinations are impossible.

The latter is preferable.

---

# 28. Future Affix Rules

Modifiers should eventually support:

```text
Stat Modifiers
Effect Additions
Effect Scaling
Tradeoffs
Conditional Effects
```

Examples:

```text
HEAVY
+ Damage
+ Cost
```

```text
VENOMOUS
+ Poison Effect
```

```text
FRAGILE
+ Power
- Limited Battle Uses
```

```text
PRECISION
+ Combo Value
```

```text
VOLATILE
+ Damage
+ Risk
```

The exact catalogue should be designed separately.

---

# 29. Relationship to the Combo System

The combo system remains independent.

```text
GENERATED CARD
       │
       ├── Combo Type
       └── Combo Value
                │
                ▼
          COMBO EVALUATOR
                │
                ▼
            ComboResult
```

The combo evaluator does not know:

* rarity;
* sell value;
* prefix;
* suffix;
* procedural generation history.

It only receives the properties required to evaluate a hand.

This keeps the combo system clean.

---

# 30. Relationship to the Future Effect System

The future pipeline is:

```text
CARD INSTANCE
      │
      ▼
SELECTED FOR BATTLE
      │
      ▼
COMBO EVALUATOR
      │
      ▼
ComboResult
      │
      ▼
EFFECT RESOLVER
      │
      ├── Damage
      ├── Block
      ├── Heal
      └── Future Status Effects
```

A procedurally generated modifier may add an effect, but the procedural system does not resolve that effect during battle.

---

# 31. Implementation Phases

## Phase 1 — Architecture Now

Design:

* `CardArchetype`;
* `CardInstance`;
* card purpose;
* rarity;
* archetype constraints;
* deck references to instances;
* collection storage model.

Do not yet implement complex procedural modifiers.

---

## Phase 2 — Basic Procedural Generation

Implement:

```text
Archetype selection
Rarity selection
Stat ranges
Sell value
Combat vs Valuable cards
Enemy loot profiles
```

Generated cards may initially have only:

```text
Base stats
Rarity
Sell value
```

---

## Phase 3 — Affixes

Add:

```text
Prefixes
Suffixes
Stat modifiers
Tradeoffs
```

At this point, cards begin to develop Borderlands-style identity.

---

## Phase 4 — Effect Generation

Once the effect system exists:

```text
Secondary effects
Poison
Buffs
Debuffs
Conditional effects
Effect scaling
```

---

## Phase 5 — Advanced Loot

Later:

```text
Boss-exclusive archetypes
Legendary rules
Area-specific loot
Faction loot
Progression scaling
Special drop tables
```

---

# 32. What Should Not Be Implemented Yet

The following should remain design placeholders:

* complete affix catalogue;
* exact rarity percentages;
* exact stat ranges;
* exact power-budget values;
* legendary card rules;
* poison mechanics;
* buff/debuff mechanics;
* market pricing balance;
* luck modifiers;
* player progression scaling.

The architecture should support them without locking them in.

---

# 33. Recommended Immediate Next Step

Before implementing the full battle loop, the next design task should be:

## **Define the Card Data Model**

Specifically:

```text
CardArchetype
CardInstance
GeneratedStats
Rarity
CardPurpose
Combo Properties
Effect Properties
Sell Value
Deck Reference
```

The reason is simple:

> The battle system needs to know what a card is, and the deck system needs to know whether it contains fixed card definitions or unique generated card instances.

For this project, the answer should be:

```text
THE PLAYER OWNS CARD INSTANCES.
THE DECK REFERENCES CARD INSTANCES.
```

Everything else builds naturally from that:

```text
ENEMY
  ↓
PROCEDURAL LOOT
  ↓
CARD INSTANCE
  ↓
COLLECTION
  ↓
DECK
  ↓
BATTLE
  ↓
EFFECTS
  ↓
REWARD
  ↓
NEW CARD INSTANCE
```

That loop gives the game a strong central identity:

> **Fight with cards, win cards, build better decks, sell valuable cards for gold, and constantly hunt for the next strange, powerful, or valuable procedural drop.**


---

# 34. Loot System — Authoritative Spec (derived-id model)

Supersedes the earlier instance-storage draft (removed).  Increment 1 and
Phase 2 shipped on branch `loot-instances`.

## 34.1 Identity: material x effect x weapon

Every loot card is fully defined by three axes; the assembled name is
"[material][effect?][space]weapon" — one letter per material/effect, two
letters for the weapon (effect omitted when plain):

```text
MATERIALS (power bonus)      EFFECTS                WEAPONS
wood   W   +0                plain (no rider, no code)   sword  SW
bronze B   +1                poison P                    bow    BO
iron   I   +2                fire   F                    dagger DA
mythril M   +3               ice    I                    shield SH
                                                         ring   RG (battle code HE)
```

Names look like `W SW` (plain wood sword), `M P DA` (mythril poison
dagger), `W F SW` (wood fire sword).

* Weapon base powers: sword 3, shield 2, bow 2, ring 2 (heals), dagger 1.
* Material bonus stacks on top: mythril sword = 6.
* Fire/ice rows are LIVE (Phase D): fire swords carry a BURN rider
  (2 dmg/round, 2 rounds) and ice swords a FREEZE rider (the target
  skips its next attack); chances tuned like DA1 in 1/255 units.
  Adding future effects later = filling in their table rows.
* Every combination maps to a **derived CardId** in the loot id range
  (`LOOT_ID_BASE`..0xFF): `id = BASE + (material*Neff + effect)*Nwpn + wpn`.
  `card_get_def()` synthesizes definitions for these ids from the tables --
  name (abbreviated), battle symbol, power, cost, effect/rider fields.

**Consequence**: the deck, collection, shops, CARDS tab, battle init, and
save format all treat loot cards as ordinary cards via derived ids.  No
instance layer, no save migration beyond the existing count-based
collection.  Owning two identical combos = two copies in the collection.

## 34.2 Effect legality per weapon

Rollable EFFECTS are the §34.1 axis rows (plain / poison / fire-resv /
ice-resv).  "Healing" below is NOT an effect axis: it is the weapon
ROLE (rings) whose combat resolution heals.

| Weapon | plain | poison | fire/ice | healing role |
|---|---|---|---|---|
| Sword  | Y | N | Y | N |
| Dagger | Y | Y | N | N |
| Bow    | Y | N | N | N |
| Shield | Y | N | N | N |
| Ring   | N | N | N | Y |

Rings are ALWAYS healing-class.  Plain shields never restore HP: a defense
over-block without a ring is clamped to zero (no heal) — see §34.4.
Illegal combinations are unrepresentable: the generator only rolls legal
(weapon, effect) pairs from the table.

## 34.3 Rings

* Rings have NO attack or block of their own -- they are the only healing
  role: plain shields never restore HP, so any defense over-block heal must
  come from a ring in the combo (see §34.4).
* Attack phase: a selected ring is a JOKER for the offense hand -- it
  substitutes as whatever value produces the best poker tier, deals 0
  attack damage (excluded from the attack sum), and heals its power when
  the combo resolves.  IMPLEMENTED.
* Defense phase: a selected ring is a wild-card SHIELD (value = its heal
  power), counting into the block sum and the hand classification --
  enabling shield kinds/straights/flushes.  IMPLEMENTED.
* MAX ONE RING PER SELECTION; a second attempt flashes "ONE RING!".
  IMPLEMENTED (selection gate, battle msg id 3).
* Rings are THE healing vector of the game: every HEAL-type card is a
  ring by construction, and the Shopkeeper sells a curated IRON RING
  (`I RG`; shop label **RG5**, 20g) as the early-game heal source.
  IMPLEMENTED.
* Future: HEAL ALL on high-tier rings; rings cure POISON (deferred).

## 34.4 Combat resolution rules

* Defense: hero always takes the NET --
  `net = attack - sum(shield block) - sum(ring/heal)`.  Positive net
  damages; negative net (over-block) is CLAMPED to zero — a plain-shield
  over-block never restores HP.  Over-block heals (restored by `-net`,
  capped at max HP) ONLY when a ring (healing shield) is in the defense
  combo (`EVENT_HEALED` cause d1=2).  IMPLEMENTED.
* Attack: rings count toward poker hands, deal 0 attack damage, and heal
  their value as the combo resolves.  IMPLEMENTED.
* Poison rider: poison-effect daggers carry the on-hit status rider
  (existing status system); chance tuned like DA1.  IMPLEMENTED.

## 34.5 Generation (Phase 2, implemented)

Every victory drops exactly one combat card (no rng gate — quality is
what varies, not presence):

```text
archetype weapon <- enemy profile bias (game layer, docs/loot.md §17)
material         <- weighted roll (wood-heavy)
effect           <- legal-for-weapon weighted roll (plain/poison-heavy)
```

Deterministic per seed (§26).  Telemetry: LOOT_CARD_ADDED
(d0=derived id, d1=count after) -- the id itself identifies the
material/effect/weapon roll via the §34.1 macros.

Implementation: the whole decision (profile pick + roll + encode)
is ONE bank-2 body (`src/game/loot_drop_banked.c`) behind the thin
`game_loot_drop()` wrapper; it steps the ISOLATED loot stream
(`g_loot_rng_state`, seeded from the main seed by `rng_set_seed`) so drops
never shift shared-RNG scenario outcomes.  The drop path must stay nearly
fixed-bank-free — overflowing `_CODE`/`_HOME` past 0x8000 corrupts bank 2
and hangs the guest (AGENTS.md §52.18).  Regression:
`tools/scenarios/tests/loot_drop_victory.json`.

The reveal is a three-line centered block on the otherwise-blank rows 10-12
of the battle victory banner: row 10 shows "YOU FOUND:", row 11 shows the
card's `[element][weapon]` icon tiles ahead of its identity name, and
row 12 shows the full English description (e.g. "WOOD POISON DAGGER").
All three lines share the card's effect color.  The icon mapping is the
same as the battle-hand cards and the CARDS-tab list.  Regression:
`tools/scenarios/tests/loot_reveal_icons.json`.

## 34.6 Economy (Merchant = Card Merchant)

The Lost Amulet merchant opens shop 2 as the CARD MERCHANT:

* BUY: the Shopkeeper (shop 1) sells a curated IRON RING (**RG5**, 20g) --
  rings are THE healing vector, so healing is purchasable but never
  free.  The Lost Amulet merchant (shop 2) stocks curated iron/mythril
  loot at formula prices (an iron sword and the mythril bow
  `M BO`).
* TRADE/SELL (implemented): pick an owned loot card in the CARDS tab ->
  its detail page shows `[A]SELL` while the player has engaged a buying
  shop (`ShopDefinition.buys`, cleared on scene change) -> A sells one
  copy for its synthesized `price` (§16 formula) and emits CARD_SOLD.
  A fully decked card cannot be sold (ownership must stay backed).

Wood gear needs no purchase: the hero starts with an all-wood deck
(sword/shield/dagger/bow/ring).

## 34.7 Implementation status

* DONE: identity tables + derived-id synthesis; joker rings (both phases,
  one-ring gate -- battle Cards carry a `ring` flag set at deck init from
  the derived id); net-damage defense; generation roll + profiles; wood
  starter; merchant buy stock; sell via CARDS-tab detail page (§34.6).
* FOLLOW-UP increments: affix catalogue growth; fire/ice statuses
  (DONE, see §34.1); HEAL ALL.
