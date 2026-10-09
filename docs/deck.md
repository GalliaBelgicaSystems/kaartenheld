# Card System & Deck Implementation Plan

## 1. Goal

Replace the current RPG item/inventory/equipment model with a unified **card system**.

The player owns cards. Some of those cards are placed into a 20-card combat deck.

There are no separate:

* Items
* Inventory
* Equipment
* Weapons
* Potions

A potion is simply a **HEAL card**.

Cards are never permanently consumed during combat. Instead, individual cards may have a **uses-per-battle** limit.

Example:

```text
POTION
Type: HEAL
Power: 5
Cost: 1
Uses/Battle: 3
```

The player owns the Potion permanently.

During a battle:

```text
3/3 → play → 2/3 → play → 1/3 → play → 0/3
```

At the beginning of the next battle:

```text
0/3 → 3/3
```

The deck itself remains unchanged.

---

# 2. Target Architecture

The current repository has separate inventory and equipment state in `GameState`, while item definitions are registered through the game layer.

The target state should instead be:

```text
GameState
├── scene
├── party
├── cards
│   ├── collection
│   └── deck
├── flags
├── variables
├── currency
├── world
└── progression
```

Remove:

```text
InventoryState
EquipmentState
ItemDefinition
item_use()
item_equip()
inventory_add()
inventory_remove()
```

as gameplay concepts.

The card system becomes the only loadout system.

---

# 3. Phase 1 — Define the Card Model

Create a generic card subsystem in:

```text
src/rpg/cards.h
src/rpg/cards.c
```

and:

```text
src/rpg/deck.h
src/rpg/deck.c
```

## 3.1 Card ID

Introduce:

```c
typedef uint8_t CardId;

#define CARD_NONE       0
#define CARD_FIRST_GAME 0x80
```

Follow the existing ID convention used by `ItemId`.

Game-specific IDs belong in:

```text
src/game/game_ids.h
```

For example:

```c
#define CARD_POTION      (CARD_FIRST_GAME + 0)
#define CARD_FIRE_BOLT   (CARD_FIRST_GAME + 1)
#define CARD_SHIELD      (CARD_FIRST_GAME + 2)
```

---

# 4. Phase 2 — Define Card Types

Start with a small set of types.

```c
typedef enum {
    CARD_TYPE_NONE = 0,
    CARD_TYPE_ATTACK,
    CARD_TYPE_DEFENSE,
    CARD_TYPE_HEAL,
    CARD_TYPE_STATUS,
    CARD_TYPE_UTILITY
} CardType;
```

Do not create separate systems for each type.

The type is primarily data used by:

* card effects;
* filtering;
* sorting;
* UI;
* balancing.

---

# 5. Phase 3 — Define Card Properties

Create a `CardDefinition`.

Conceptually:

```c
typedef struct {
    CardId id;
    const char *name;

    uint8_t type;
    uint8_t cost;
    uint8_t power;

    uint8_t uses_per_battle;
    uint8_t max_copies;

    uint8_t effect;
} CardDefinition;
```

Important meaning:

### `uses_per_battle`

`0` means unlimited.

Otherwise it is the maximum number of times that card may be played during one battle.

Examples:

```text
SLASH
uses_per_battle = 0

POTION
uses_per_battle = 3

METEOR
uses_per_battle = 1
```

### `max_copies`

Maximum number of copies allowed in one deck.

Example:

```text
POTION
max_copies = 3
```

The player could own 20 Potion cards, but could only put 3 into a deck.

---

# 6. Phase 4 — Replace Inventory State

Remove the card-related dependence on:

```c
InventoryState
```

from `GameState`.

Create:

```c
#define MAX_CARD_COLLECTION 64
#define MAX_DECK_CARDS 20
```

Then:

```c
typedef struct {
    CardId card_id;
    uint8_t quantity;
} CardCollectionEntry;

typedef struct {
    CardCollectionEntry entries[MAX_CARD_COLLECTION];
    uint8_t count;
} CardCollectionState;
```

And:

```c
typedef struct {
    CardId cards[MAX_DECK_CARDS];
    uint8_t count;
} DeckState;
```

Then:

```c
typedef struct {
    CardCollectionState collection;
    DeckState deck;
} CardState;
```

`GameState` contains:

```c
CardState cards;
```

---

# 7. Phase 5 — Card Collection API

Implement generic operations:

```c
bool card_collection_add(
    CardCollectionState *collection,
    CardId card_id,
    uint8_t quantity
);

bool card_collection_remove(
    CardCollectionState *collection,
    CardId card_id,
    uint8_t quantity
);

uint8_t card_collection_count(
    const CardCollectionState *collection,
    CardId card_id
);
```

Also:

```c
bool card_is_owned(
    const CardCollectionState *collection,
    CardId card_id
);
```

These replace:

```text
inventory_add()
inventory_remove()
inventory_count()
```

The collection is persistent.

Playing a card in combat does **not** call `card_collection_remove()`.

---

# 8. Phase 6 — Deck API

Implement:

```c
bool deck_add(
    DeckState *deck,
    const CardCollectionState *collection,
    CardId card_id
);
```

This must validate:

1. Card exists.
2. Player owns the card.
3. Deck has fewer than 20 cards.
4. Card copy limit has not been exceeded.

Implement:

```c
bool deck_remove(
    DeckState *deck,
    CardId card_id
);
```

Also:

```c
uint8_t deck_count(const DeckState *deck);
uint8_t deck_card_count(const DeckState *deck, CardId card_id);
bool deck_contains(const DeckState *deck, CardId card_id);
bool deck_validate(const DeckState *deck, const CardCollectionState *collection);
```

---

# 9. Phase 7 — Preserve Deck Order

The deck should explicitly store its cards in order:

```c
CardId cards[MAX_DECK_CARDS];
```

Do not store only quantities.

For example:

```text
[ FIRE_BOLT,
  SHIELD,
  POTION,
  FIRE_BOLT,
  METEOR,
  ... ]
```

This makes the deck a real ordered combat object.

Collection sorting must never modify this order.

---

# 10. Phase 8 — Card Definitions as Game Content

Move card definitions into:

```text
src/game/cards_content.c
```

and registration into:

```text
src/game/cards.c
```

following the repository's existing game-content registration pattern.

Initial test cards should include at least:

```text
FIRE BOLT
Type: ATTACK
Power: 3
Cost: 1
Uses: unlimited

SLASH
Type: ATTACK
Power: 5
Cost: 2
Uses: unlimited

SHIELD
Type: DEFENSE
Power: 4
Cost: 1
Uses: unlimited

POTION
Type: HEAL
Power: 5
Cost: 1
Uses: 3

METEOR
Type: ATTACK
Power: 20
Cost: 6
Uses: 1
```

These values are placeholders for testing the system, not final balance.

---

# 11. Phase 9 — Battle Runtime Card State ✅ DONE

Do not store remaining uses inside the persistent `CardDefinition`.

`uses_per_battle` is immutable card data.

The current remaining uses are **battle state**.

Add something conceptually like:

```c
typedef struct {
    CardId card_id;
    uint8_t uses_remaining;
} BattleCardState;
```

The battle state contains the temporary usage information for cards currently in the deck.

At battle start:

```text
for every card in deck:
    remaining_uses = definition.uses_per_battle
```

For unlimited cards:

```text
uses_per_battle == 0
```

means:

```text
unlimited
```

No counter needs to be maintained.

---

# 12. Phase 10 — Card Play ✅ DONE

> **Implemented flow** (differs from the original single-call sketch below —
> see "Deviations from spec" at the end of this document): cards are played by
> building a combo, not one at a time. Affordability is checked at
> *selection* time and the cost is paid at *resolve* time.
>
> 1. `battle_card_select()` (`src/battle/battle.c`) adds the cursor card to
>    the combo if `hand_card_playable()` passes: uses remaining AND
>    `cost <= energy - combo_reserved_cost()` (the sum of costs already in
>    the pending combo). The cursor then auto-advances to the next playable,
>    unselected card.
> 2. Confirming the combo (or the turn timer expiring) runs
>    `battle_execute_combo()`, which evaluates and resolves the effect.
> 3. `battle_resolve_hand_discard()` pays the summed combo cost (saturating
>    at 0), decrements limited uses, emits `CARD_PLAYED` per card, discards
>    them and refills those hand slots from the deck.
> 4. `energy` is a per-turn pool (the `ENERGY_POOL` variable, default
>    2 = `BATTLE_ENERGY_PER_TURN`, staged at battle entry by the bank-4
>    HUD body), refreshed to full at every decision-phase entry (attack
>    AND defend).
>
> For a Potion:

```text
remaining = 3
       ↓
play
       ↓
heal
       ↓
remaining = 2
```

For unlimited cards:

```text
remaining = unlimited
       ↓
play
       ↓
still unlimited
```

---

# 13. Phase 11 — Card Availability ✅ DONE

A card should become unavailable when:

```text
uses_per_battle > 0
AND
uses_remaining == 0
```

The card remains in the deck.

It simply cannot currently be played.

This distinction is important.

The deck does not shrink during combat.

---

# 14. Phase 12 — Battle Reset ✅ DONE

At the beginning of every battle:

```text
initialize_card_uses(deck)
```

Every limited card gets:

```text
uses_remaining = definition.uses_per_battle
```

At battle end, the temporary battle state is discarded.

The persistent deck is untouched.

This guarantees:

```text
Battle 1:
Potion 3/3 → 0/3

Battle ends

Battle 2:
Potion 3/3
```

No player interaction is required between battles.

---

# 15. Phase 13 — Deck-Building Screen ✅ DONE

Create:

```text
src/screens/deck.c
src/screens/deck.h
```

The screen replaces the existing item/inventory UI as the player's primary loadout interface.

The top-level screen should have two modes:

```text
DECK
CARDS
```

Where:

### DECK

Shows the 20 cards currently selected.

### CARDS

Shows every card owned by the player.

---

# 16. Phase 14 — Deck UI ✅ DONE

The deck view should show:

```text
DECK 17/20

> FIRE BOLT     P3 C1
  SHIELD        P4 C1
  POTION        P5 C1
  METEOR       P20 C6
```

Selecting a card shows:

```text
POTION

HEAL
POWER 5
COST 1
USES 3/BATTLE

Owned: 2
In Deck: 1/3

A REMOVE
B BACK
```

---

# 17. Phase 15 — Collection UI ✅ DONE

> Note: implemented as the CARDS tab in `src/screens/item_screen.c`. Selecting a
> card shows a single inline PWR/COST/U row (row 14) rather than a full detail
> screen; `A` adds the selected card to the deck.

The collection view should show:

```text
CARDS 42

> FIRE BOLT     P3 C1
  SHIELD        P4 C1
  POTION        P5 C1
  METEOR       P20 C6
```

Selecting a card:

```text
POTION

HEAL
POWER 5
COST 1
USES 3/BATTLE

Owned: 2
In Deck: 1/3

A ADD TO DECK
B BACK
```

---

# 18. Phase 16 — Filtering ✅ DONE

> Note: the type filter is implemented (SELECT cycles ALL→ATTACK→DEFENSE→HEAL→
> STATUS→UTILITY). Cost, power, and deck-status filters are deferred (see the
> roadmap); they are not yet composable.

Implement filters in this order:

### 1. Type

```text
ALL
ATTACK
DEFENSE
HEAL
STATUS
UTILITY
```

### 2. Cost

```text
ALL
0
1
2
3
4+
```

### 3. Power

```text
ALL
1-2
3-4
5-7
8+
```

### 4. Deck status

```text
ALL
IN DECK
NOT IN DECK
```

Filters should be composable.

Example:

```text
TYPE: ATTACK
COST: 0-2
NOT IN DECK
```

---

# 19. Phase 17 — Sorting ✅ DONE

> Note: START cycles sort mode: OFF→TYPE→POWER↑→COST↑→POWER↓→COST↓. NAME and
> USES sorts are deferred.

Implement:

```text
NAME
TYPE
POWER
COST
USES
```

Support ascending/descending where appropriate.

Examples:

```text
POWER ↓
```

finds the strongest cards.

```text
COST ↑
```

finds cheap cards.

```text
USES ↑
```

finds cards with high battle availability.

---

# 20. Phase 18 — Filter/Sort Implementation ✅ DONE

> Note: `s_view_indices[20]` (static in item_screen.c) maps view position to
> source index; a stable insertion sort (`sort_view`) reorders only the index
> buffer. Collection/deck order is never mutated. Scrolling (8-row window) keeps
> the cursor visible, including on wraparound.

Do not modify the underlying collection.

Build a temporary view of card IDs/indices:

```text
collection
    ↓
filter
    ↓
sort
    ↓
render
```

For the Game Boy, a small fixed-size index buffer and insertion sort are sufficient.

The important invariant is:

> **Sorting is a UI operation, never a state mutation.**

---

# 21. Phase 19 — Deck Summary ✅ DONE

> Note: the DECK tab shows a single compact summary row (`ATK N DEF N HEA N`)
> with per-type counts. Average cost and STATUS/UTILITY counts were deferred to
> keep the fixed-bank _CODE budget under control.

Add a compact summary to the deck screen:

```text
DECK 20/20

ATK   8
DEF   5
HEAL  4
UTIL  3

AVG COST 1.6
```

This is informational only.

Do not make deck validation overly complicated initially.

---

# 22. Phase 20 — Remove Legacy Systems ✅ DONE

Once the card system works, remove obsolete gameplay concepts.

Delete or refactor:

```text
src/rpg/inventory.*
InventoryState
InventoryEntry
src/rpg/items.*
ItemDefinition
item_use()
item_equip()
EquipmentState
```

Remove game content such as:

```text
ITEM_POTION
ITEM_BOMB
ITEM_ETHER
ITEM_SWORD
ITEM_AMULET
ITEM_NUT
```

and replace them with cards.

For example:

```text
ITEM_POTION
```

becomes:

```text
CARD_POTION
```

The important distinction is that this isn't merely a rename.

The old Potion was an inventory object.

The new Potion is a card with:

```text
type
power
cost
uses_per_battle
max_copies
effect
```

---

# 23. Phase 21 — Remove Equipment ✅ DONE

There should be no:

```text
weapon
armor
amulet
equipment slot
```

A powerful weapon-like effect is represented by a card.

For example:

```text
SWORD STRIKE
ATTACK
POWER 7
COST 2
```

If the game eventually needs persistent character upgrades, those should be a separate progression system rather than an equipment inventory.

---

# 24. Phase 22 — Save Data ✅ DONE

> Note: `save.c` copies the whole `GameState` (including `cards.collection` and
> `cards.deck`) to SRAM. Remaining uses are battle-runtime state (`Battle`), never
> part of `GameState`, so they are never persisted — satisfying this phase's
> requirement by construction.

The persistent save state should contain:

```text
card collection
deck
```

It should **not** contain:

```text
remaining card uses
```

because uses reset between battles.

Example:

```text
Persistent:
Potion owned = 2
Potion in deck = 1

Transient:
Potion remaining uses = 3
```

After using it twice:

```text
Persistent:
Potion owned = 2
Potion in deck = 1

Transient:
Potion remaining uses = 1
```

After battle:

```text
Transient state discarded.
```

---

# 25. Phase 23 — Debug / Test Harness ✅ DONE

> Note: card-specific scenarios live in `tools/scenarios/tests/` (card_*, deck_*,
> battle_*). Semantic state is exposed via the extended snapshot; card telemetry
> covers collection/deck/battle transitions.

The repository already has deterministic scenario infrastructure and semantic state inspection.

Add card-specific scenario fixtures.

Minimum scenarios:

### Collection

```text
card_collection_add
card_collection_remove
card_collection_quantity
```

### Deck

```text
deck_add
deck_remove
deck_20_card_limit
deck_copy_limit
deck_requires_owned_card
deck_validation
```

### Battle usage

```text
unlimited_card_can_repeat
limited_card_decrements
limited_card_becomes_unavailable
limited_card_resets_next_battle
```

### Example

```text
Scenario:
    deck = [POTION]
    potion uses = 3

Action:
    play potion

Assert:
    potion uses = 2
    potion still in deck
    collection unchanged
```

Then:

```text
play potion
play potion
```

Assert:

```text
potion uses = 0
potion still in deck
fourth play rejected
```

Then start another battle:

```text
potion uses = 3
```

---

# 26. Phase 24 — Telemetry ✅ DONE

> Note: legacy ITEM_* events were renamed in place to CARD_ADDED_TO_COLLECTION,
> CARD_REMOVED_FROM_COLLECTION, CARD_ADDED_TO_DECK, CARD_REMOVED_FROM_DECK, and
> CARD_PLAYED (new events 44-46). CARD_DEPLETED was dropped — CARD_PLAYED carries
> the post-play `uses_remaining`. DECK_VALIDATED was not implemented (no
> `deck_validate()` exists).

Add explicit card events.

For example:

```text
CARD_ADDED_TO_COLLECTION
CARD_REMOVED_FROM_COLLECTION
CARD_ADDED_TO_DECK
CARD_REMOVED_FROM_DECK
CARD_PLAYED
CARD_DEPLETED
DECK_VALIDATED
```

For `CARD_PLAYED`, telemetry should include enough information to diagnose the action.

Conceptually:

```text
card_id
deck_index
uses_remaining
```

This is particularly useful because the repository is designed around semantic observability for automated development and testing.

---

# 27. Phase 25 — Initial Card Combat Integration ✅ DONE

> Note: the vertical slice is live — `battle_start()` bridges the persistent
> `DeckState` into the battle `Deck`, cards are playable as combos, limited-use
> cards decrement and reset between battles. Enemy decks have also landed:
> each enemy draws from its own registered card deck
> (`src/battle/enemy_deck_content.c`, `EVENT_ENEMY_CARD_PLAYED`); the flat
> attack value remains only as a fallback for enemies without a deck.

Do not build the complete card combat system at the same time as the deck UI.

First establish this vertical slice:

```text
Acquire cards
      ↓
Build 20-card deck
      ↓
Start battle
      ↓
Draw card
      ↓
Play card
      ↓
Effect occurs
      ↓
Limited-use card decrements
      ↓
Battle ends
      ↓
Start another battle
      ↓
Limited-use card resets
```

Once this works deterministically, expand the card mechanics.

---

# 28. Recommended Implementation Order

The actual coding sequence should be:

```text
1. Card IDs
        ↓
2. CardDefinition
        ↓
3. Card collection state
        ↓
4. Deck state
        ↓
5. Collection API
        ↓
6. Deck API
        ↓
7. Game card definitions
        ↓
8. Deck validation
        ↓
9. Battle card runtime state
        ↓
10. Card play/use system
        ↓
11. Per-battle use reset
        ↓
12. Basic DECK screen
        ↓
13. Basic CARDS screen
        ↓
14. Card detail screen
        ↓
15. Filtering
        ↓
16. Sorting
        ↓
17. Deck summary
        ↓
18. Save/load
        ↓
19. Debug scenarios
        ↓
20. Remove legacy inventory/equipment
        ↓
21. Full card combat integration
```

Do not start with the visual UI.

The data model and battle semantics need to be stable first.

---

# 29. Definition of Done

The implementation is complete when:

* There is no player-facing Inventory screen.
* There is no player-facing Equipment screen.
* There are no non-card combat items.
* Every combat effect is represented by a card.
* The player owns a persistent card collection.
* The player has a maximum 20-card deck.
* Cards can have a per-battle usage limit.
* Limited-use cards never disappear from the player's collection.
* Limited-use cards never require deck maintenance after combat.
* Limited-use cards reset automatically at the beginning of the next battle.
* Unlimited cards can be played normally.
* Deck copy limits are enforced.
* Deck size is enforced.
* Collection sorting does not alter deck order.
* Collection filtering does not alter persistent state.
* The player can quickly filter by type, power, cost, and deck status.
* Card details expose power, cost, and uses/battle.
* Save data persists collection and deck but not temporary battle-use counters.
* Automated scenarios verify the complete card lifecycle.

---

# 30. Design Principle

The implementation should preserve this distinction throughout the codebase:

```text
                 PERSISTENT
                     │
           ┌─────────┴─────────┐
           │                   │
       COLLECTION             DECK
       "I own it"       "I bring it"
           │                   │
           └─────────┬─────────┘
                     │
                   BATTLE
                     │
              TEMPORARY STATE
                     │
             uses_remaining
                     │
              battle ends
                     │
                     ▼
                   RESET
```

The most important rule is:

> **Combat consumes availability, not ownership.**

A Potion with `3 uses/battle` is therefore not an inventory item and not a consumable item. It is a permanent card whose **combat availability is limited**.

This should be the foundation for the entire card system.

---

# 40. Deviations from spec

The implementation follows the spec's semantics (collection vs deck split,
per-battle availability, deterministic reset) with these deliberate
divergences, driven by Game Boy memory limits and the engine/game layer split
(AGENTS.md §55):

- **Sizes**: `MAX_CARD_COLLECTION` is 12 (not 64); the battle deck is 20
  (`MAX_DECK_SIZE`, `src/battle/deck.h`). Card ids start at
  `CARD_FIRST_GAME = 0x40` (`src/rpg/cards.h`), not 0x80 — the card range
  shares the item-id space convention of the game layer.
- **API shape**: there is no single `battle_play_card()`. Cards are selected
  into combos (`battle_card_select`) and resolved together
  (`battle_execute_combo`), which is what makes combos/energy meaningful.
  Persistent-side API: `deck_collection_add(cs, id, qty)`,
  `deck_add_card(cs, id)`, `deck_remove_card(cs, id)`; no
  `deck_validate`/`deck_contains` (validation happens inside the mutators,
  which return `bool`). The `CARD_DEPLETED` / `DECK_VALIDATED` telemetry
  events do not exist; depletion is observable via `CARD_PLAYED` payloads and
  hand state.
- **CardDefinition extras**: `battle_type` (maps a collection card to its
  combat `BattleCardType`), `price` (shop integration), and an inline
  `name[9]` for display.
- **`max_copies` caps both collection and deck** (spec said the collection is
  uncapped). Enforced in `src/rpg/deck.c`; capping the collection too keeps
  the fixed-size entry array honest at 12 slots.
- **Energy model**: per-turn pool from the `ENERGY_POOL` variable
  (default 2 = `BATTLE_ENERGY_PER_TURN`), staged at battle entry and
  refreshed to full at every decision-phase entry rather than a
  per-combat budget. Story
  events raise the variable with `ADD_VARIABLE` (no cap); battles stage the
  pool at entry and reuse it for the whole fight. Affordability is checked
  at selection time against `energy - combo_reserved_cost()` and paid at
  resolve time. The small default pool means combo scenarios select
  cost-0 (or at most two cost-1) cards so the hand still fits the pool.
- **Deck-size ceiling unreachable**: the 20-card deck limit cannot be hit by
  current content — total deckable copies across the catalog are 17
  (IRON_SWORD ×4 + WOODEN_SHIELD ×3 + WOOD_RING ×3 + FIRE_SWORD ×3 +
  POISON_DAGGER ×3 + BOW_9 ×1; AMULET is SPECIAL and not deckable). Recorded as a known
  gap; no synthetic content was added just to test it.
- **Starter deck granted at new game**: a new game grants the collection
  IRON_SWORD ×4 + WOODEN_SHIELD ×3 + FIRE_SWORD ×3 + POISON_DAGGER ×2 (and decks all
  twelve, deal order SW SW SH SH SW first so the opening hand is SW3 SW3 SH2 SH2 SW4),
  so the first battle already draws from the real deck system. The engine's
  fallback battle deck (`deck_init_default`) mirrors the same ten cards so
  legacy and empty states behave identically. The grant is silent by design:
  `deck_add_card` emits no telemetry; callers do.
- **Test coverage** (`tools/scenarios/tests/`): `card_cost_energy` (pool
  gating + reservation), `unlimited_card_can_repeat` (uses 0xFF never
  depletes), `limited_card_resets_next_battle` (per-battle reset; also pins
  that fleeing does not write battle HP back to the party),
  `deck_copy_limit` (max_copies enforcement via real `deck_add_card`),
  `deck_requires_owned_card` (ownership gate),
  `reshuffle_preserves_limits` (a full slime-trio fight needs more plays
  than the 5-card deck holds, exercising the discard-to-draw reshuffle path
  end to end).
