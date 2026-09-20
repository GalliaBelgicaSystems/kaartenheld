# Debug Protocol

This document defines the machine-readable development and debugging interface for the Game Boy RPG.

The protocol exists to make the game **fully testable and inspectable by an LLM** without requiring a human to play through the game or interpret screenshots.

The player-facing Game Boy interface is optimized for gameplay.

The debug interface is optimized for deterministic automation, inspection, testing, and diagnosis.

---

# 1. Goals

The debug protocol must allow an external development agent to:

1. Start the game in a known state.
2. Load a predefined scenario.
3. Inspect the complete relevant game state.
4. Control player input.
5. Advance the game by deterministic numbers of frames.
6. Inspect recent gameplay events.
7. Inspect the world and entities.
8. Inspect story flags.
9. Inspect battle state.
10. Inspect audio state.
11. Control and inspect RNG.
12. Assert expected conditions.
13. Detect state transitions.
14. Reproduce bugs deterministically.
15. Produce machine-readable PASS/FAIL results.

The protocol must not depend on the visual appearance of the game.

---

# 2. Design Principles

## 2.1 Semantic State Over Pixels

The debug interface is authoritative for gameplay state.

A screenshot may show:

```text
@     E
```

The debug interface must instead identify:

```text
PLAYER:
  id: player
  position: (8,5)

ENEMY:
  id: slime_01
  type: enemy
  position: (14,5)
```

The fact that the enemy is currently rendered as `E` is irrelevant to the protocol.

---

## 2.2 Deterministic By Default

Given:

* the same ROM;
* the same scenario;
* the same RNG seed;
* the same input sequence;
* the same frame advancement;

the game should produce the same observable result.

---

## 2.3 The Harness Must Test Real Gameplay

Scenarios may establish state.

They must not bypass the behavior being tested.

For example, a town-event scenario may teleport the player into town and establish story flags.

It must then allow normal collision/event/gameplay logic to trigger the event.

---

## 2.4 Stable Semantic Names

Names exposed through the protocol are part of the development API.

Prefer:

```text
BATTLE_STARTED
PLAYER_MOVED
TOWN_ATTACK_STARTED
slime_01
town_guard_01
```

Avoid names based on implementation details or visual representations.

---

# 3. Protocol Layers

The development system consists of four layers:

```text
┌──────────────────────────────┐
│            LLM               │
│     Agent / Coding Tool      │
└──────────────┬───────────────┘
               │
        Host Debug API
               │
┌──────────────▼───────────────┐
│       tools/dev.py           │
│       Test Runner            │
└──────────────┬───────────────┘
               │
        Emulator Transport
               │
┌──────────────▼───────────────┐
│         SameBoy               │
│       Debug ROM               │
└──────────────┬───────────────┘
               │
┌──────────────▼───────────────┐
│        Game Systems           │
│ world / battle / story / etc. │
└──────────────────────────────┘
```

Game systems must not depend on the LLM or host-side Python tooling.

---

# 4. Debug Build

The protocol is available only in the debug ROM.

Build with:

```bash
make debug
```

Result:

```text
build/kaartenheld_debug.gb
```

The release ROM:

```bash
make release
```

must not require the debug protocol.

Debug functionality must be conditionally compiled or otherwise excluded from the release build where appropriate.

---

# 5. Transport

The exact transport implementation is emulator-specific.

The protocol must therefore distinguish between:

### Transport

How bytes/messages physically move between host and emulator.

### Protocol

What those messages mean.

The gameplay code must not contain SameBoy-specific assumptions.

Conceptually:

```text
DebugProtocol
      │
      ▼
DebugTransport
      │
      ├── SameBoy
      └── future implementation
```

The initial implementation targets SameBoy.

The exact SameBoy integration must be verified against the version available through the Nix development environment.

---

# 6. Command Model

The host may send commands to the debug ROM.

The initial command vocabulary is:

```text
HELP
RESET
LOAD_SCENARIO
PRESS
RELEASE
WAIT
STEP
INSPECT
INSPECT_AREA
SNAPSHOT
EVENTS
EVENTS_SINCE
SET_RNG
GET_RNG
SET_FLAG
CLEAR_FLAG
TELEPORT
SET_HP
ASSERT
```

Commands must return a structured response.

Every response must contain enough information to determine:

* whether the command succeeded;
* what changed;
* what the current relevant state is;
* whether an error occurred.

---

# 7. Command Responses

Successful commands should use a structure conceptually equivalent to:

```json
{
  "ok": true,
  "command": "INSPECT",
  "result": {}
}
```

Errors:

```json
{
  "ok": false,
  "command": "LOAD_SCENARIO",
  "error": {
    "code": "SCENARIO_NOT_FOUND",
    "message": "Unknown scenario: town_event_99"
  }
}
```

Error codes must be stable.

Do not rely on parsing human prose to determine whether a command succeeded.

---

# 8. HELP

Command:

```text
HELP
```

Returns the available commands and concise descriptions.

Example:

```json
{
  "ok": true,
  "command": "HELP",
  "commands": [
    "RESET",
    "LOAD_SCENARIO",
    "PRESS",
    "WAIT",
    "STEP",
    "INSPECT",
    "INSPECT_AREA",
    "SNAPSHOT",
    "EVENTS",
    "EVENTS_SINCE",
    "SET_RNG",
    "GET_RNG"
  ]
}
```

---

# 9. RESET

Command:

```text
RESET
```

Returns the game to its normal initial state.

It should clear:

* gameplay state;
* story flags;
* entities;
* battle state;
* telemetry;
* debug state.

It should establish a deterministic default RNG seed.

The default seed should be documented and stable.

---

# 10. LOAD_SCENARIO

Command:

```text
LOAD_SCENARIO <name>
```

Example:

```text
LOAD_SCENARIO first_encounter
```

The scenario establishes a known state.

Loading a scenario must:

1. reset relevant game state;
2. apply scenario configuration;
3. set the RNG seed;
4. initialize entities;
5. initialize story state;
6. initialize battle state if applicable;
7. initialize audio state if specified;
8. clear old telemetry;
9. emit a `SCENARIO_LOADED` event.

The scenario must then run through normal gameplay logic.

### Declarative loading (`initial_state`)

Scenarios specify their starting state declaratively in the JSON
`initial_state` field.  The host serializes it into a fixed-size byte
descriptor in `g_scen_state_buf` (layout documented in
`src/debug/telemetry.h`, `STATE_LOAD_DESC_*`), sets `g_scen_load_state`,
and the ROM applies it through a single general loader
(`scenario_load_state()`) that constructs the canonical `GameState`,
loads the world through the normal `world_init`/`world_load_map` paths
(so persistent actor defeats are respected), and starts the game in the
requested screen.

Supported `initial_state` fields:

```json
{
  "scene": "TOWN",
  "player": { "x": 8, "y": 6, "facing": "DOWN" },
  "seed": 42,
  "flags": { "ARRIVED_TOWN": true, "MET_MAYOR": false },
  "variables": { "GOLD": 150, "CHAPTER": 2 },
  "party": { "HERO": { "level": 3, "hp": 24, "max_hp": 30 } },
  "inventory": { "POTION": 2, "BOMB": 1 },
  "deck": { "IRON_SWORD": 2, "WOODEN_SHIELD": 2, "FIRE_SWORD": 1 },
  "world": { "SLIME_FIELD": "DEFEATED" },
  "screen": "BATTLE",
  "dialogue": "MAYOR_GREETING",
  "start_battle": true,
  "game_over_choice": 1,
  "font_test": true
}
```

Rules:

* Each variable-length section carries a count; only the listed entries
  are applied, so unspecified sections keep the game's default state
  (e.g. leaving `variables` out keeps `CHAPTER == 1`).
* `inventory` applies additively on top of the new-game starter grants
  (`inventory_initial` relies on this coexistence); the roundtrip rebuild
  therefore emits only the delta over those grants.  That delta assumes
  NEW_GAME-origin snapshots — rebuilding from any other origin would
  under-report inventory by the starter amounts.
* `deck` is the exception: when the key is present it *replaces* the
  starter deck entirely (descriptor v4 carries a deck-present flag), and
  an explicit `"deck": {}` constructs an intentionally empty deck — the
  only way for scenarios to reach `battle_start`'s packed fallback-deck
  path now that `DECK_MIN_CARDS` blocks emptying through gameplay.
  `{name: copies}` expands to one descriptor byte per copy.
* A `DEFEATED` actor in `world` is honoured by `actor_load_scene()`: the
  actor is not spawned into the scene.
* Scenario setup must never emit gameplay telemetry; all descriptor state
  is written directly into `GameState`.
* `screen` starts a non-overworld screen directly (DIALOGUE/BATTLE/
  GAME_OVER/THANKS); `dialogue` + `start_battle` configure the active
  runtime screens.

### Semantic state dump (LLM-facing)

The byte buffers (`g_snap_buf`, `g_state_snap_buf`) are internal transports.
The LLM-facing representation is semantic text rendered host-side from the
parsed snapshot:

```text
SCENE=FOREST
PLAYER=(10,8) FACING=RIGHT
FLAGS: ARRIVED_TOWN MET_MAYOR
VARIABLES: CHAPTER=1 GOLD=150
PARTY[0]: HERO lvl=3 24/30
INVENTORY: POTION x2
WORLD: SLIME_FOREST=DEFEATED
```

It is opt-in via `dev.py`:

```text
python3 tools/dev.py scenario <name> --state   # run + dump semantic state
python3 tools/dev.py state <name>              # run + dump semantic state
python3 tools/dev.py test --state              # every scenario
```

### Save/load boundary check

`GameState` is the potential save unit; `Battle`, `DialogueState`,
`RenderCache`, input, and `World.actors` HP/facing are runtime state and are
excluded.  The roundtrip check proves the boundary is lossless:

```text
python3 tools/dev.py roundtrip <scenario>
```

It loads an `initial_state`, dumps the canonical state, rebuilds a descriptor
from the dump, reloads, and asserts the observed state is unchanged.

### Scripted events

Gameplay sequences (dialogue selection, story flags, scene transitions) are
declarative `EventDefinition` records in `src/core/event.c`, not hard-coded
into `game.c`.  An event has a trigger (`INTERACT` with a specific actor, or
`MAP_ENTER` on a scene), an optional flag condition, and a fixed action list
(start dialogue, set/clear flag, set/add variable, scene change).  First match
in table order wins.  Actions call the normal state APIs, so they emit their
own existing telemetry (`DIALOGUE_STARTED`, `STORY_FLAG_SET`,
`VARIABLE_SET`, ...).

Every fired event emits:

```text
SCRIPT_TRIGGERED
  event: MAYOR_INTRO
```

(`EVENT_SCRIPT_TRIGGERED`, type 30; `data[0]` is the stable `EventId`).
The host maps the id to its name via `EVENT_ID_MAP`.

Example event chain tested by scenarios: `TOWN_ARRIVAL` (sets `ARRIVED_TOWN`
on first entry), `MAYOR_INTRO` (first interaction with the Mayor → intro
dialogue + sets `MET_MAYOR`), `MAYOR_GREETING` (already met),
`GUARD_AFTER_MAYOR` / `GUARD_GREETING` (Guard reacts differently once
`MET_MAYOR`).

### Battle setup helpers

`set_enemy_hp` (`DBG_ACT_SET_ENEMY_HP`) pins an enemy combatant's current HP
in the active battle: `{"type": "set_enemy_hp", "index": 0, "hp": 20}`.
Direct state write, no telemetry -- scenario setup semantics (deterministic
fight lengths, e.g. the boss fight pins the Lord of Slimes to 20 HP).

`apply_status` (`DBG_ACT_APPLY_STATUS`) applies a status through the real
mechanic (`status_apply`, `STATUS_APPLIED` telemetry included):
`{"type": "apply_status", "slot": 0, "status": 3, "duration": 1}` -- slot
0 = player, 1..n = enemy index.  Reaches targets no card rider can hit
today (e.g. freezing the PLAYER; see `status_freeze_player`).

`set_hand_card_ring` (`DBG_ACT_SET_HAND_RING`) marks an injected hand card
as a loot RING (docs/loot.md §34.3): joker classification, defense shield
value, one-ring selection gate.  See `ring_one_ring_gate` /
`ring_joker_heal`.

`collection_add` (`DBG_ACT_ADD_ITEM`) grants cards to the collection by
RAW id: `{"type": "collection_add", "card_id": 139, "quantity": 1}` --
raw ids let scenarios reach derived loot-range CardIds that have no name
in the host maps (docs/loot.md §34).

### Items, money, progression & the shop

* Item effects are generic primitives (`src/rpg/items.{h,c}`):
  `ITEM_EFFECT_HEAL_HP` (`POTION` heals party member 0 by 5, capped at
  `max_hp`, costs 20G).  `item_use` consumes only if the use succeeds;
  `item_purchase` is atomic (failed purchases leave state unchanged).
* Money is the generalized `currency` state (`src/rpg/currency.{h,c}`),
  dense slots keyed by `CurrencyId` (`GOLD`).  Defeated hostile actors grant
  their `gold_reward` (slime 5G, bat 8G) on victory; shops spend it.
* The shop (`SCREEN_SHOP`) buys via `item_purchase`; the item menu
  (`SCREEN_ITEM`, opened with SELECT in overworld and battle) uses items.
* Progression (`src/rpg/progression.{h,c}`) is a generic engine over
  arbitrary targets (HERO_1, IRON_SWORD, ...).  `progression_add` crosses
  static thresholds and emits `PROGRESSION_GAINED` + `LEVEL_UP`; the
  game-specific consequence (`game_on_level_up`) is applied by the caller.
* Semantic harness actions exercise the real mechanics without the UI:
  `add_item`, `remove_item`, `add_currency`, `add_progress`, `buy_item`,
  `use_item_direct`, `deck_add`, `deck_remove` (via the ROM
  `g_debug_action` channel).  The button-driven `use_item` action still
  tests the item menu UI.  `deck_add`/`deck_remove` call the real
  `deck_add_card`/`deck_remove_card`, so every validation (ownership,
  per-card caps, `DECK_MIN_CARDS`) applies; they emit
  `CARD_ADDED_TO_DECK` / `CARD_REMOVED_FROM_DECK` on success only, so
  scenarios assert rejection via event counts.
* Save/load: the `save` and `load` scenario actions (via the `g_debug_action`
  channel, codes 8 and 9) call `save_game`/`load_game`
  (`src/rpg/save.{h,c}`) — SRAM battery persistence of `GameState`.  After a
  `load`, the world copy is rebuilt from the restored state.  See
  `docs/save-format.md`.
* Assertions: `currency`, `progression_level`, `progression_progress`;
  `party_level` reads the `HERO_1` progression target.
* Equipment: `GameState.equipment.weapon`; `item_equip` emits `ITEM_EQUIPPED`;
  hero attack is derived (`game_hero_attack` = 3 + weapon bonus, SWORD +3).
  Assertions: `attack`; semantic dump shows `EQUIPMENT`/`ATTACK`.

### Quests (event-driven, no quest engine)

Quest state and objectives are generic variables owned by the event table in
`src/core/event.c`:

* `QUEST_MONSTER_HUNT` = 0 (NOT_STARTED), 1 (ACTIVE), 2 (COMPLETE).
* `MONSTERS_DEFEATED` — a **global** counter incremented on every hostile
  defeat (no quest gating), so kills before the quest starts still count.
* `ENDING_SHOWN` — set when the final boss (Lord of Slimes) is defeated;
  the battle victory then goes to the `SCREEN_ENDING` instead of the
  overworld.  A on the ending restarts the game.

The event engine supports generic conditions (flag is/not set, variable
`>=`/`==`) and actions (dialogue, set/clear flag, set/add variable, give
item, scene change).  The Mayor quest is expressed entirely as event data:

* `QUEST_START` (interact MAYOR, quest NOT_STARTED) → MAYOR_INTRO dialogue,
  set MET_MAYOR, quest=ACTIVE.
* `QUEST_COMPLETE` (ACTIVE && MONSTERS_DEFEATED >= 3) → reward dialogue +
  give SWORD + quest=COMPLETE (given exactly once).
* `QUEST_ACTIVE` (ACTIVE) → "still working" dialogue.
* `QUEST_DONE` (COMPLETE) → already-rewarded dialogue.

Scenario actions: `equip_item` (mechanic) and the quick-screen UI (START →
SELECT tab-focus → arrows → A) are both testable.

### Quick screen (tabbed)

`SCREEN_ITEM` is the quick screen: tabs ITEM | EQUIP | QUEST | STATUS.
START opens it in both the overworld and battle (player turn); SELECT in the
overworld does nothing.  SELECT (or LEFT/RIGHT) cycles tabs directly with a
`^` marking the active tab; B closes.  Hero HP/gold appear only on the
STATUS tab; the QUEST tab lists ongoing quests and their progress.

### Menu screens (`MenuFrame`)

All menu screens (quick screen, shop, future menus) are drawn through the
shared `MenuFrame` (`src/ui/menu.{h,c}`): a centered title plus a bounded
content area.  Harness-relevant details:

* Assert menu content with `screen_row` (row = absolute screen row).  The
  quick screen layout is: row 0 centered title, row 2 tab labels, row 3 `^`
  marker, content from row 5; the shop uses row 0 title, content from row 3.
* Titles and tab labels are direct literals (never a const pointer table),
  so asserting row 0 / row 2 text proves the title/labels actually render.
* Scenario actions for menus: `START` opens, `SELECT` cycles the tab,
  `B` closes; `use_item`/`equip_item` drive the underlying mechanics.

---

# 11. Scenario Format

Scenario files live in:

```text
tools/scenarios/
```

They use JSON.

Example:

```json
{
  "name": "first_encounter",
  "description": "Player walks into the first enemy.",
  "rng_seed": 12345,

  "world": {
    "map": "field",
    "player": {
      "x": 8,
      "y": 5,
      "facing": "EAST"
    }
  },

  "entities": [
    {
      "id": "slime_01",
      "type": "enemy",
      "x": 10,
      "y": 5
    }
  ],

  "story": {
    "flags": {}
  },

  "audio": {
    "track": "FIELD"
  }
}
```

The scenario format may evolve.

New fields must remain backwards compatible where practical.

---

# 12. Scenario Philosophy

A scenario establishes **preconditions**, not outcomes.

Correct:

```json
{
  "world": {
    "map": "town",
    "player": {
      "x": 12,
      "y": 8
    }
  }
}
```

Then the test walks into the relevant NPC or trigger.

Incorrect:

```json
{
  "game_state": "TOWN_EVENT_COMPLETE"
}
```

when the scenario is supposed to test whether the town event triggers.

The latter tests state injection rather than gameplay.

---

# 13. PRESS

Command:

```text
PRESS <button>
```

Supported buttons:

```text
UP
DOWN
LEFT
RIGHT
A
B
START
SELECT
```

A press represents a normal gameplay button action.

The input should pass through the same input system used by the player.

---

# 14. RELEASE

Command:

```text
RELEASE <button>
```

Releases a previously held button.

This is primarily useful for future systems requiring held input.

The initial game may treat `PRESS` as a single logical button action.

---

# 15. WAIT

Command:

```text
WAIT <frames>
```

Advances the game by the specified number of frames.

Example:

```text
WAIT 60
```

represents approximately one second at 60 Hz.

The exact timing must use the Game Boy/emulator frame rate rather than host wall-clock timing.

---

# 16. STEP

Command:

```text
STEP <frames>
```

`STEP` is equivalent to deterministic frame advancement but is intended primarily for debugging.

Example:

```text
STEP 1
```

advances exactly one frame.

This is useful for investigating:

* input timing;
* collisions;
* state transitions;
* animations;
* battle logic;
* VBlank behavior;
* audio transitions.

---

# 17. INSPECT

Command:

```text
INSPECT
```

Returns the current semantic game state.

The response must be concise enough for an LLM to consume.

Minimum fields:

```text
game
player
map
entities
story
battle
audio
rng
frame
```

Example:

```json
{
  "ok": true,
  "result": {
    "game": {
      "state": "OVERWORLD"
    },

    "player": {
      "id": "player",
      "map": "field",
      "x": 8,
      "y": 5,
      "facing": "EAST",
      "hp": 20,
      "max_hp": 20
    },

    "entities": [
      {
        "id": "slime_01",
        "type": "enemy",
        "x": 10,
        "y": 5,
        "hp": 10,
        "max_hp": 10
      }
    ],

    "story": {
      "flags": {}
    },

    "battle": null,

    "audio": {
      "track": "FIELD",
      "playing": true
    },

    "rng": {
      "seed": 12345
    },

    "frame": 1832
  }
}
```

---

# 18. INSPECT_AREA

Command:

```text
INSPECT_AREA <radius>
```

Returns the semantic and spatial information around the player.

Example:

```text
INSPECT_AREA 4
```

The response should include a compact ASCII representation where useful.

Example:

```text
012345678
.........
....@....
......E..
.........
```

It must additionally provide semantic entities.

Example:

```json
{
  "entities": [
    {
      "id": "slime_01",
      "type": "enemy",
      "x": 10,
      "y": 5
    }
  ]
}
```

ASCII is convenience information.

Entity data is authoritative.

---

# 19. SNAPSHOT

Command:

```text
SNAPSHOT
```

Returns a complete machine-readable state snapshot.

Unlike `INSPECT`, which may be optimized for concise repeated inspection, `SNAPSHOT` should include all state relevant to debugging.

Potential sections:

```text
game
world
player
party
entities
story
inventory
battle
audio
rng
input
frame
telemetry
debug
```

Do not include raw memory dumps by default.

---

# 20. EVENTS

Command:

```text
EVENTS
```

Returns the recent telemetry events in the ring buffer.

Example:

```json
{
  "events": [
    {
      "sequence": 120,
      "frame": 1001,
      "type": "PLAYER_MOVED",
      "data": {
        "from": [8, 5],
        "to": [9, 5]
      }
    },
    {
      "sequence": 121,
      "frame": 1002,
      "type": "COLLISION",
      "data": {
        "entity_a": "player",
        "entity_b": "slime_01"
      }
    },
    {
      "sequence": 122,
      "frame": 1002,
      "type": "ENCOUNTER_STARTED",
      "data": {
        "enemy": "slime_01"
      }
    }
  ]
}
```

---

# 21. EVENTS_SINCE

Command:

```text
EVENTS_SINCE <sequence>
```

Example:

```text
EVENTS_SINCE 120
```

Returns all available events after sequence `120`.

This is the preferred mechanism for incremental inspection.

An agent should not need to repeatedly download the entire telemetry buffer.

---

# 22. Telemetry Sequence Numbers

Every event receives a monotonically increasing sequence number.

Example:

```text
120
121
122
123
```

Sequence numbers allow the host to determine exactly what happened between two inspections.

If the ring buffer overwrites old events, the response must explicitly indicate that events were lost.

Example:

```json
{
  "events_lost": true,
  "oldest_available_sequence": 150
}
```

The harness must never silently pretend that missing events did not occur.

---

# 23. Telemetry Event Schema

Every event contains:

```json
{
  "sequence": 123,
  "frame": 1002,
  "type": "PLAYER_MOVED",
  "data": {}
}
```

Required fields:

```text
sequence  (uint32_t, little-endian)
frame     (uint32_t, little-endian)
type      (uint8_t, mapped via EVENT_TYPE_MAP)
data      (uint8_t[4])
```

Binary Memory ABI Layout (`GameEvent` = 13 bytes):
- Offset 0..3: `uint32_t seq`
- Offset 4..7: `uint32_t frame`
- Offset 8:    `uint8_t type`
- Offset 9..12: `uint8_t data[4]`

Event types must use stable uppercase identifiers.

---

# 24. Required Initial Event Types

The initial protocol defines:

```text
GAME_STARTED
GAME_STATE_CHANGED

SCENARIO_LOADED

PLAYER_MOVED
PLAYER_FACING_CHANGED
COLLISION

ACTOR_COLLISION
ACTOR_INTERACTION
ACTOR_COMBAT_START
ACTOR_STATE_CHANGE
ACTOR_MOVED

ENCOUNTER_STARTED
BATTLE_STARTED
BATTLE_ENDED

DAMAGE_DEALT
HEALING_APPLIED
ENTITY_DEFEATED

COMBO_RESOLVED
EFFECT_RESOLVED
TARGET_CHANGED

TURN_STARTED
TURN_ENDED

STORY_FLAG_SET
STORY_FLAG_CLEARED
STORY_EVENT_STARTED
STORY_EVENT_ENDED

MUSIC_CHANGED

RNG_SEEDED
RNG_USED
```

## World Actors

**World Actor** is the canonical term for any character-like entity that
exists in an overworld scene (Mayor, Guard, Shopkeeper, Slime, Bat, Boss,
Villager, Monster). Friendly NPCs and hostile enemies share one data-driven
structure; hostility, interaction type, dialogue ID, and battle ID decide
what happens when the player engages an actor.

Observe actors with:

```text
ACTOR_COLLISION      actor collided with the player (id, x, y)
ACTOR_INTERACTION    actor engaged by the player (id, interaction)
ACTOR_COMBAT_START   hostile actor engagement started combat (id)
ACTOR_STATE_CHANGE   persistent actor lifecycle changed (actor id lo, id hi, new state)
ACTOR_MOVED          patrol step committed (entity id, x, y, facing); split
                     from ACTOR_STATE_CHANGE so one event id means one
                     payload shape
```

The SNAPSHOT `actors` section lists the active scene's actors with their
semantic `id`, `position`, `facing`, `visual`, `hostile`, `interaction`,
`dialogue`, and `battle` fields. Do not maintain separate NPC/enemy debug
formats: every overworld character is a World Actor.

Not every game system needs every event immediately.

As systems are implemented, their important transitions must become observable.

---

# 25. GAME_STATE_CHANGED

Example:

```json
{
  "type": "GAME_STATE_CHANGED",
  "data": {
    "from": "OVERWORLD",
    "to": "BATTLE"
  }
}
```

Initial game states may include:

```text
BOOT
TITLE
OVERWORLD
BATTLE
MENU
DIALOGUE
GAME_OVER
```

The enum may expand as the game grows.

---

# 26. PLAYER_MOVED

Example:

```json
{
  "type": "PLAYER_MOVED",
  "data": {
    "from": [8, 5],
    "to": [9, 5]
  }
}
```

Blocked movement should also be observable when useful.

Example:

```json
{
  "type": "PLAYER_MOVEMENT_BLOCKED",
  "data": {
    "position": [9, 5],
    "direction": "EAST",
    "reason": "WALL"
  }
}
```

---

# 27. COLLISION

Example:

```json
{
  "type": "COLLISION",
  "data": {
    "entity_a": "player",
    "entity_b": "slime_01"
  }
}
```

Collision must be emitted before encounter/battle transitions where applicable.

This allows the harness to distinguish:

```text
movement
→ collision
→ encounter
→ battle
```

---

# 28. ENCOUNTER_STARTED

Example:

```json
{
  "type": "ENCOUNTER_STARTED",
  "data": {
    "enemy": "slime_01"
  }
}
```

This is distinct from `BATTLE_STARTED`.

This distinction allows future encounter systems to support:

* surprise attacks;
* scripted encounters;
* escape opportunities;
* pre-battle dialogue;
* encounter animations.

---

# 29. BATTLE_STARTED

Example:

```json
{
  "type": "BATTLE_STARTED",
  "data": {
    "enemy_ids": [
      "slime_01"
    ]
  }
}
```

Battle state must become inspectable immediately after this event.

---

# 30. BATTLE_ENDED

Example:

```json
{
  "type": "BATTLE_ENDED",
  "data": {
    "result": "VICTORY"
  }
}
```

Possible results:

```text
VICTORY
DEFEAT
ESCAPE
SCRIPTED
```

---

# 31. DAMAGE_DEALT

Example:

```json
{
  "type": "DAMAGE_DEALT",
  "data": {
    "source": "player",
    "target": "slime_01",
    "amount": 4,
    "remaining_hp": 6
  }
}
```

The event should expose the final applied damage, not merely the pre-randomized damage value.

---

# 31.1 COMBO_RESOLVED / EFFECT_RESOLVED (card combat)

Emitted by the battle system whenever a played hand is resolved
(`docs/combo-system.md` §19).  `COMBO_RESOLVED` announces the QUALITY of the
selection; `EFFECT_RESOLVED` announces the consequence computed from it.
Both fire before the matching `DAMAGE_DEALT` / `HEALED` / `DAMAGE_RECEIVED`
event.

Hand tiers follow strict poker sizing (`docs/combo-system.md` hand table):
pairs/kinds need >= 2 effective cards; STRAIGHT, FLUSH, STRAIGHT_FLUSH and
FIVE KIND require all five.  The scored multiplier is the tier percent plus
25 when every effective card shares one symbol:

```text
HIGH CARD 100   STRAIGHT 210      FOUR KIND 280      STRAIGHT FLUSH 350
PAIR      120   FLUSH     240     FULL HOUSE 260     FIVE KIND      400
TWO PAIR  150   THREE KIND 180
```

```text
COMBO_RESOLVED
  data[0] = HandTier id (0 NONE .. 9 FIVE KIND, combo.h)
  data[1] = effective card count (attack: all cards; defend: shields only)
  data[2] = suited flag (all effective cards share one symbol)
  data[3] = phase (0 = attack, 1 = defend)

EFFECT_RESOLVED
  data[0] = amount (base_power x multiplier / 100)
  data[1] = effect type (CardEffectType: 1 damage, 2 block, 3 heal)
  data[2] = phase (0 = attack, 1 = defend)
  data[3] = target slot (0 = player; otherwise enemy index)
```

On-hit status riders scale with the same multiplier:
`effective_chance = min(255, base_chance x multiplier / 100)` (1/255 units).

---

# 31.2 STATUS_APPLIED / STATUS_TICKED / STATUS_EXPIRED / STATUS_RESISTED

Phase C/D statuses (`docs/combo-system.md` §12-§19).  `STATUS_APPLIED`
fires when an on-hit rider lands (after the deterministic RNG roll passes)
or a debug `apply_status` action succeeds; `STATUS_TICKED` fires once per
round per afflicted combatant at the transition back into the player
select phase; `STATUS_EXPIRED` accompanies each tick that removed an
instance; `STATUS_RESISTED` fires when an on-hit roll fails (§19).

```text
STATUS_APPLIED
  data[0] = status id (1 = POISON, 2 = BURN, 3 = FREEZE)
  data[1] = stacks after application (capped by the definition)
  data[2] = duration in turns

STATUS_TICKED
  data[0] = flat tick damage (POISON 1, BURN 2, FREEZE 0 per round
            regardless of stacks; extra applications refresh duration
            and deepen stacks for telemetry only)
  data[1] = actor slot (0 = player; otherwise enemy index)
  data[2] = instances expired by this tick
  data[3] = first expired status id (valid when data[2] != 0)

STATUS_EXPIRED
  data[0] = status id of the first instance removed by this tick
  data[1] = actor slot

STATUS_RESISTED
  data[0] = status id whose roll failed
  data[1] = target slot

TURN_SKIPPED (docs/combo-system.md §12, Phase D)
  data[0] = combatant slot whose turn was skipped (0 = player,
            1..n = enemy index)
  data[1] = cause StatusId (STATUS_FREEZE for a frozen attacker/player,
            STATUS_POISON for a greyed-draw skip)
```

Poison grey-out events (Phase D, docs/combo-system.md §12/§16):

```text
CARDS_GREYED
  data[0] = actor slot (0 = player, 1..n = enemy); for an enemy the
            grey applies to the party's SHARED battle deck
  data[1] = first greyed pool position (deck position / hand index)
  data[2] = second greyed pool position
  data[3] = grey duration in rounds (POISON_GREY_TURNS)

CARDS_UNGREYED
  data[0] = actor slot
  data[1] = first previously-greyed pool position
  data[2] = second previously-greyed pool position
```

The enemy grey gate tests the OR of every enemy slot's mask against the
shared deck's current draw position (every encounter engages as a trio,
so the deck rotates through all three attackers; a greyed position never
plays no matter which enemy is up).  The skip emits `TURN_SKIPPED` with
`data[1] = STATUS_POISON` and leaves the draw position unadvanced.

---

# 31.3 TARGET_CHANGED (battle target selection)

Fires whenever the battle's attacker/card target caret moves (UP/DOWN during
the player select or defend phase, `battle_target_move`) and when the target
auto-advances to the next living enemy after its current target is defeated
(`battle_target_auto_advance`).  The target persists across turns — this
event only fires when the *selection itself* changes, never on the reset of
a new turn.  Single-enemy battles do not emit it (the caret is trapped on
the only slot), and a wrap-back press that lands on the same living enemy
(e.g. DOWN cycling past only dead slots in a multi-enemy battle reduced to
one survivor) emits nothing either.

```text
TARGET_CHANGED
  data[0] = previous enemy slot (0..enemy_count-1)
  data[1] = new enemy slot (0..enemy_count-1)
```

---

# 31.4 LOOT (docs/loot.md §34)

```text
LOOT_CARD_ADDED
  data[0] = derived CardId (0x80..0xFF; decode via the §34.1 macros:
            material = (id-0x80)>>5, effect = (id>>3)&3, weapon = id&7)
  data[1] = collection count after the add

CARD_SOLD (docs/loot.md §34.6)
  data[0] = CardId sold
  data[1] = gold received (the def's price -- the centralized sell
            value for loot ids)
  data[2] = collection count after the sale
```

---

# 32. STORY FLAGS

Story flags are semantic boolean state.

Example:

```text
MET_MAYOR
TOWN_ATTACK_STARTED
TOWN_ATTACK_COMPLETE
BOSS_DEFEATED
```

When a flag changes:

```json
{
  "type": "STORY_FLAG_SET",
  "data": {
    "flag": "TOWN_ATTACK_STARTED"
  }
}
```

Story flags must be inspectable.

---

# 33. SET_FLAG

Debug command:

```text
SET_FLAG <flag>
```

Example:

```text
SET_FLAG MET_MAYOR
```

This is a state-construction tool.

It must emit an appropriate debug event.

Scenarios should prefer setting initial flags through scenario configuration rather than issuing a sequence of debug commands.

---

# 34. CLEAR_FLAG

Command:

```text
CLEAR_FLAG <flag>
```

Example:

```text
CLEAR_FLAG TOWN_ATTACK_STARTED
```

---

# 35. TELEPORT

Command:

```text
TELEPORT <map> <x> <y>
```

Example:

```text
TELEPORT town 12 8
```

Teleportation is a debug-only state-construction operation.

It must not simulate player movement.

Therefore it must not emit `PLAYER_MOVED`.

Instead, it should emit a debug-specific event such as:

```text
DEBUG_TELEPORTED
```

This distinction prevents tests from confusing state setup with actual gameplay.

---

# 36. SET_HP

Command:

```text
SET_HP <entity> <value>
```

Example:

```text
SET_HP player 1
```

This is primarily a scenario/debug setup tool.

It must not emit `DAMAGE_DEALT` or `HEALING_APPLIED`, because no actual combat action occurred.

---

# 37. RNG

All gameplay randomness must pass through the game's RNG abstraction.

The debug protocol provides:

```text
SET_RNG <seed>
GET_RNG
```

Example:

```text
SET_RNG 12345
```

Inspect:

```text
GET_RNG
```

Result:

```json
{
  "seed": 12345
}
```

If random consumption is important for debugging, the harness may emit:

```text
RNG_USED
```

with enough information to reproduce the relevant result.

---

# 38. SET_RNG

`SET_RNG` must reset the deterministic RNG state.

Loading a scenario with:

```json
{
  "rng_seed": 12345
}
```

must have the same effect as:

```text
RESET
SET_RNG 12345
```

before the scenario state is established.

---

# 39. Audio State

The current audio state must be inspectable.

Example:

```json
{
  "audio": {
    "track": "BATTLE",
    "playing": true
  }
}
```

Track identifiers must be semantic:

```text
TITLE
FIELD
TOWN
BATTLE
VICTORY
DEFEAT
```

The actual musical implementation is irrelevant to the debug protocol.

---

# 40. MUSIC_CHANGED

When the music changes:

```json
{
  "type": "MUSIC_CHANGED",
  "data": {
    "from": "FIELD",
    "to": "BATTLE"
  }
}
```

This allows a scenario to verify that an encounter correctly changes the music without analyzing audio output.

---

# 41. Assertions

The protocol supports machine-readable assertions.

Examples:

```text
game.state == "BATTLE"
player.hp == 20
player.x == 12
player.y == 8
story.TOWN_ATTACK_STARTED == true
audio.track == "BATTLE"
```

The host-side test runner should perform most assertions rather than requiring complex expression evaluation inside the Game Boy.

---

# 42. Assertion Types

The initial assertion system should support:

```text
equals
not_equals
greater_than
less_than
greater_or_equal
less_or_equal
contains
exists
event_occurred
event_not_occurred
event_arg
```

`event_arg` asserts a telemetry payload byte: the scenario passes when at
least one emitted event of the given type carries `data[index] == value`.

Example:

```json
{
  "type": "event_arg",
  "event": "COMBO_RESOLVED",
  "index": 0,
  "value": 175
}
```

Failure output lists the observed payloads of every matching event, e.g.
`no payload match among 1 event(s): data=[100, 1, 0, 0]`.

Example:

```json
{
  "assert": {
    "path": "game.state",
    "equals": "BATTLE"
  }
}
```

---

# 43. Scenario Actions

Scenario files should support an action sequence.

Example:

```json
{
  "actions": [
    {
      "press": "RIGHT"
    },
    {
      "wait": 5
    },
    {
      "inspect": true
    }
  ]
}
```

The exact schema may evolve.

Actions should be deterministic.

---

# 44. Scenario Assertions

Example:

```json
{
  "assertions": [
    {
      "path": "game.state",
      "equals": "BATTLE"
    },
    {
      "path": "audio.track",
      "equals": "BATTLE"
    },
    {
      "event_occurred": "ENCOUNTER_STARTED"
    },
    {
      "event_occurred": "BATTLE_STARTED"
    }
  ]
}
```

---

# 45. Scenario Test Result

A completed scenario returns:

```json
{
  "scenario": "first_encounter",
  "status": "PASS"
}
```

A failure must include diagnostics:

```json
{
  "scenario": "first_encounter",
  "status": "FAIL",

  "failure": {
    "assertion": "game.state == BATTLE",
    "expected": "BATTLE",
    "actual": "OVERWORLD"
  },

  "snapshot": {},
  "recent_events": []
}
```

---

# 46. LLM-Friendly Failure Output

The human-readable test runner should produce output such as:

```text
SCENARIO: first_encounter
STATUS: FAIL

ASSERTION:
  game.state == BATTLE

EXPECTED:
  BATTLE

ACTUAL:
  OVERWORLD

PLAYER:
  map: field
  position: (8,5)
  facing: EAST

ENEMY:
  slime_01
  position: (9,5)

RECENT EVENTS:
  120 PLAYER_MOVED
  121 PLAYER_MOVEMENT_BLOCKED

MISSING EVENTS:
  COLLISION
  ENCOUNTER_STARTED
  BATTLE_STARTED
```

An LLM should be able to diagnose the likely area of failure from this information.

---

# 47. Example: First Encounter Scenario

Scenario:

```json
{
  "name": "first_encounter",

  "rng_seed": 12345,

  "world": {
    "map": "field",
    "player": {
      "x": 8,
      "y": 5,
      "facing": "EAST"
    }
  },

  "entities": [
    {
      "id": "slime_01",
      "type": "enemy",
      "x": 9,
      "y": 5
    }
  ],

  "audio": {
    "track": "FIELD"
  },

  "actions": [
    {
      "press": "RIGHT"
    },
    {
      "wait": 10
    }
  ],

  "assertions": [
    {
      "path": "game.state",
      "equals": "BATTLE"
    },
    {
      "event_occurred": "COLLISION"
    },
    {
      "event_occurred": "ENCOUNTER_STARTED"
    },
    {
      "event_occurred": "BATTLE_STARTED"
    },
    {
      "path": "audio.track",
      "equals": "BATTLE"
    }
  ]
}
```

This verifies the entire chain:

```text
RIGHT
  ↓
movement
  ↓
collision
  ↓
encounter
  ↓
battle
  ↓
battle music
```

---

# 48. Example: Town Event Scenario

The scenario should place the player immediately before the trigger.

Example:

```json
{
  "name": "town_event_01",

  "rng_seed": 1001,

  "world": {
    "map": "town",
    "player": {
      "x": 12,
      "y": 8,
      "facing": "EAST"
    }
  },

  "story": {
    "flags": {
      "MET_MAYOR": true,
      "TOWN_ATTACK_STARTED": false
    }
  },

  "actions": [
    {
      "press": "RIGHT"
    },
    {
      "wait": 20
    }
  ],

  "assertions": [
    {
      "path": "story.TOWN_ATTACK_STARTED",
      "equals": true
    },
    {
      "event_occurred": "STORY_EVENT_STARTED"
    }
  ]
}
```

The test does not directly start the event.

It creates the conditions under which normal game logic must start it.

---

# 49. Event Ordering

Event ordering is significant.

For an encounter, the expected ordering might be:

```text
PLAYER_MOVED
COLLISION
ENCOUNTER_STARTED
GAME_STATE_CHANGED
BATTLE_STARTED
MUSIC_CHANGED
```

The exact order may evolve as implementation changes, but important ordering dependencies must be documented and tested.

The host harness must preserve event sequence numbers.

---

# 50. State vs Events

The harness must distinguish between:

### State

What is true now.

Example:

```text
game.state = BATTLE
```

### Event

What happened.

Example:

```text
BATTLE_STARTED
```

A current state does not prove how the game got there.

Events provide historical evidence.

Tests should use both where useful.

---

# 51. Debug-Only Events

Debug operations may emit events prefixed with:

```text
DEBUG_
```

Examples:

```text
DEBUG_TELEPORTED
DEBUG_HP_CHANGED
DEBUG_FLAG_CHANGED
DEBUG_SCENARIO_LOADED
```

These must never be confused with real gameplay events.

For example:

```text
DEBUG_TELEPORTED
```

does not mean:

```text
PLAYER_MOVED
```

---

# 52. Telemetry Ring Buffer

The Game Boy debug implementation must use a bounded ring buffer.

The initial implementation should target approximately 32 recent events.

Each event should be compact.

The ring buffer must never allocate memory dynamically for every event.

When old events are overwritten, the protocol must expose that information.

---

# 53. Telemetry Performance

Telemetry must not materially interfere with gameplay timing.

Avoid:

* expensive string formatting every frame;
* large allocations;
* full state serialization every frame;
* writing giant debug messages continuously.

Emit events at meaningful state boundaries.

For example:

Good:

```text
PLAYER_MOVED
```

Bad:

```text
PLAYER_POSITION_CHECKED
```

every frame.

---

# 54. Debug State Must Not Change Gameplay Semantics

The debug harness may:

* inspect;
* seed RNG;
* inject input;
* construct state;
* advance frames.

It must not silently alter normal gameplay rules.

For example, loading a scenario should use the same battle initialization code as a naturally occurring encounter.

---

# 55. Input Recording

The host runner should eventually support recording:

```text
scenario
rng seed
input sequence
frame timing
```

This creates a reproducible bug report.

Example:

```text
REPLAY:

ROM: kaartenheld_debug
SCENARIO: first_encounter
RNG: 12345

1. RIGHT
2. STEP 1
3. RIGHT
4. WAIT 20
```

A future replay system should be able to reproduce the same execution.

---

# 56. Future Save-State Integration

The protocol should be designed so that future emulator save-state support can be added without changing the semantic protocol.

Potential future commands:

```text
SAVE_STATE <name>
LOAD_STATE <name>
```

These should be considered transport/debug features, not gameplay features.

Scenario loading remains the preferred portable mechanism.

---

# 57. Future Card-System Integration

When the card battle system is implemented, the same protocol must expose:

```text
deck
hand
discard
draw_pile
turn
active_card
combo
damage
enemy_intent
```

Example:

```json
{
  "battle": {
    "turn": 3,
    "player_hp": 18,
    "enemy_hp": 12,

    "hand": [
      "fire_slash",
      "water_guard",
      "heal"
    ],

    "deck_count": 14,
    "discard_count": 6
  }
}
```

Card events should include:

```text
CARD_DRAWN
CARD_SELECTED
CARD_PLAYED
CARD_DISCARDED
COMBO_STARTED
COMBO_RESOLVED
```

The card system must remain testable without screenshot interpretation.

---

# 58. Future Story/Cutscene Integration

Future story systems should expose:

```text
dialogue_id
speaker
dialogue_state
cutscene_id
cutscene_step
story_flags
```

Events may include:

```text
DIALOGUE_STARTED
DIALOGUE_ADVANCED
DIALOGUE_ENDED

CUTSCENE_STARTED
CUTSCENE_STEP
CUTSCENE_ENDED
```

This allows an agent to test long narrative sequences without manually watching them.

---

# 59. Future NPC/AI Integration

NPC state should eventually expose:

```text
id
type
position
facing
behavior
current_target
state
```

AI transitions may emit:

```text
NPC_STATE_CHANGED
NPC_TARGET_CHANGED
NPC_MOVED
```

This allows bugs in NPC behavior to be tested deterministically.

---

# 60. Protocol Compatibility

Once a field, command, event name, or semantic identifier is used by automated scenarios, changing it may break the development API.

Prefer additive changes.

For breaking changes:

1. update the protocol version;
2. update affected scenarios;
3. update the host runner;
4. update `AGENTS.md`;
5. document the migration.

---

# 61. Protocol Version

The debug protocol should expose a version.

Example:

```json
{
  "protocol": {
    "name": "gameboy-rpg-debug",
    "version": 1
  }
}
```

The host should verify compatibility when connecting.

---

# 62. Connection Handshake

On connection, the debug target should identify itself.

Conceptual response:

```json
{
  "protocol": {
    "name": "gameboy-rpg-debug",
    "version": 1
  },

  "game": {
    "name": "kaartenheld",
    "build": "debug"
  }
}
```

This prevents the host from accidentally communicating with an incompatible ROM.

---

# 63. Error Codes

Errors must use stable identifiers.

Initial error codes:

```text
UNKNOWN_COMMAND
INVALID_ARGUMENT
SCENARIO_NOT_FOUND
SCENARIO_INVALID
ENTITY_NOT_FOUND
MAP_NOT_FOUND
INVALID_STATE
INVALID_BUTTON
INVALID_FRAME_COUNT
ASSERTION_FAILED
PROTOCOL_VERSION_MISMATCH
TRANSPORT_ERROR
TIMEOUT
```

Human-readable messages may accompany them.

The host should branch on error codes, not message strings.

---

# 64. Timeouts

The host runner must never wait indefinitely for the ROM.

Commands must have a timeout.

On timeout, report:

```text
TIMEOUT

COMMAND:
  INSPECT

LAST KNOWN FRAME:
  1832

LAST KNOWN STATE:
  OVERWORLD
```

The harness should preserve enough diagnostic information to determine whether the emulator, transport, or game stopped responding.

---

# 65. PASS/FAIL Contract

The canonical test result is:

```text
PASS
```

or:

```text
FAIL
```

The process exit code must be:

```text
0 = all tests passed
non-zero = at least one test failed
```

This allows:

```bash
make test-harness
```

to be used by both humans and automated coding agents.

---

# 66. Definition of Done

The debug protocol is considered implemented when an LLM can execute the following workflow:

```text
BUILD DEBUG ROM
      ↓
START EMULATOR
      ↓
LOAD SCENARIO
      ↓
INSPECT
      ↓
PRESS INPUT
      ↓
STEP / WAIT
      ↓
INSPECT
      ↓
EVENTS_SINCE
      ↓
ASSERT
      ↓
PASS / FAIL
```

For the first encounter:

```text
LOAD_SCENARIO first_encounter
        ↓
PRESS RIGHT
        ↓
WAIT
        ↓
INSPECT
        ↓
ENCOUNTER_STARTED
        ↓
BATTLE_STARTED
        ↓
MUSIC_CHANGED
        ↓
ASSERT
        ↓
PASS
```

For a future town event:

```text
LOAD_SCENARIO town_event_01
        ↓
PRESS movement
        ↓
WAIT
        ↓
INSPECT
        ↓
STORY_FLAG_SET
        ↓
STORY_EVENT_STARTED
        ↓
ASSERT
        ↓
PASS
```

---

# 67. Golden Rule

The development harness must always answer these five questions:

```text
WHERE AM I?
WHAT STATE IS THE GAME IN?
WHAT JUST HAPPENED?
WHAT HAPPENS NEXT?
DID THE EXPECTED BEHAVIOR OCCUR?
```

If an LLM cannot answer those questions from the harness, the harness is not sufficiently observable.

The long-term objective is therefore:

> **Every important gameplay system must expose enough deterministic semantic state and telemetry that an LLM can operate, test, diagnose, and reproduce the game without relying on human gameplay or visual interpretation.**
