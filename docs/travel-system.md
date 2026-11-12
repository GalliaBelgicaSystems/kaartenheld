# Journey / Oregon Trail System — Detailed Implementation Plan

# 0. Game Design Summary

## Premise
The hero begins as a cocky adventurer who challenges the Lord of Slime, is defeated, and wakes up in a distant biome. To return and defeat the Lord, they must travel across several biomes, grow stronger, and make difficult choices.

## The Countdown
- A cosmic event called the **Convergence** will occur in approximately 180 days.
- The Lord of Slime grows stronger as the event approaches, changing the world and its inhabitants.
- The player must balance developing their strength, helping others, disrupting the Lord's plans, and travelling quickly.
- Arriving late changes the world but never causes an automatic game over.

## Travel and Choices
- Travel advances a global day counter.
- Events offer choices with different time costs and consequences.
- Helping people may provide allies, shortcuts, or future opportunities.
- Ignoring events can save time but may close opportunities or create different story paths.
- Choices influence relationships, routes, factions, and endings.

## Card Materials
Cards are made from progressively stronger materials:

**Wood → Bronze → Iron → Steel → Mythril**

Mythril cards are exceptionally powerful: the strongest material, and
accordingly rare. Their deeper temporal connection is a sequel hook,
not a Kaartenheld 1 mechanic.

## Time Is Final

- There is no rewinding. Spent days are gone.
- Helping a village for three days costs three days — but that village
  may help you later.
- Missed opportunities become different storylines, not erased mistakes.
- The Lord's progression never reverses.
- This is what gives every choice weight without making the game cruel.

## Design Principles
- Time is valuable, but not every activity should be a waste of time.
- You can't undo yesterday: choices stand, and missed opportunities
  become different stories rather than dead ends.
- Missed opportunities should often lead to alternative stories rather than dead ends.
- Choices should create different adventures, not simply good or bad outcomes.
- The hero's journey is about learning that strength requires preparation, responsibility, and sacrifice.

## Core Loop (locked)

1. **Prepare** — cards, equipment, supplies, and what you know about the route.
2. **Travel** — a fixed-duration route; each day advances the world calendar.
3. **Make choices** — encounters, detours, fights, or press onward.
4. **Arrive changed** — decisions affect people, locations, future
   opportunities, and endings.

---

## 1. Purpose

Add a data-driven **journey system** to Kaartenheld that handles travel between distant RPG areas.

The existing top-down RPG remains the primary gameplay for exploring towns, dungeons, and other areas.

The journey system becomes the strategic layer connecting those areas:

```text
RPG Area
   ↓
Travel Organizer NPC
   ↓
Choose destination
   ↓
Confirm journey (fixed duration shown)
   ↓
Journey simulation
   ↓
Events / choices / battles
   ↓
Arrival
   ↓
New RPG Area
```

The journey system must be driven by JSON content, in the same general spirit as the existing `levels/*.json` workflow.

The engine should provide generic mechanics; JSON should define the actual game design.

---

# 2. Travel Organizer NPCs

The player should not start journeys from a generic system menu.

Instead, special NPCs in the world organize travel.

For example:

```text
Town
 └── Travel Organizer
       ├── Mountain Pass
       ├── Desert City
       └── Northern Castle
```

The NPC presents available destinations. There is no transport menu:
every route has one fixed duration, and the journey itself is the
adventure.

Example dialogue:

```text
TRAVEL ORGANIZER

"Where are you headed?"

[Mountain Pass]
[Desert City]
[Northern Castle]
[Cancel]
```

After selecting a destination:

```text
TRAVEL ORGANIZER

"Road to the Mountains."

5 days, mountain roads.

[Travel]
[Back]
```

The duration and the known route notes come from the route JSON —
the organizer never hard-codes them.

---

# 3. NPC → Journey Architecture

The NPC should only be responsible for presenting travel and starting it.

Conceptually:

```text
Travel NPC
    ↓
select route
    ↓
journey_start(route)
    ↓
Journey System
```

The NPC should not contain journey simulation logic.

It should not know:

* how many days travel takes
* how much food is consumed
* what events occur
* what happens when parts run out
* how battles work

All of that belongs to the journey data/runtime.

This keeps NPCs reusable.

---

# 4. Journey Content Structure

Add:

```text
journeys/
├── routes/
│   ├── town_to_mountains.json
│   ├── town_to_desert.json
│   └── ...
│
└── events/
    ├── bandit_attack.json
    ├── broken_wagon.json
    ├── find_food.json
    └── ...
```

Routes define journeys.

Events define encounters.

This avoids putting every possible event directly into every route.

---

# 5. Route Definition

A route defines:

* starting area
* destination
* fixed travel duration (`travel_days`)
* resource consumption
* event pools
* environmental characteristics

One route, one duration — there is no per-route transport menu. Travel
is an adventure with sidetracks, not a transportation choice. The
player's decisions along the way determine what the journey costs, not
a plan picked up front.

Example:

```json
{
  "id": "town_to_mountains",
  "name": "Road to the Mountains",

  "start": "TOWN",
  "destination": "MOUNTAIN_PASS",

  "travel_days": 5,

  "resources": {
    "food_per_day": 2,
    "parts_per_day": 0
  },

  "events": {
    "frequency": 35,
    "pool": [
      "bandit_attack",
      "lost_traveler",
      "mysterious_shrine"
    ]
  },

  "environment": {
    "terrain": "mountain"
  }
}
```

The exact schema can evolve during implementation.

Travel time vs detour time (replaces the old plan/clock split): the
route's `travel_days` is the fixed baseline under normal conditions —
the engine advances the journey and the global `DAY` clock (§35) by it.
Anything *extra* comes from detours: event choices, delays, and
recovery days, which extend the journey through the same shared
time-advancement path (`journey delay` effects, defeat recovery §18,
biome clears §35b). A 5-day route played as travel-2, help-2, travel-3
arrives on day 8 with an ally — never failed, just not the journey as
planned. Detour days count *as* journey days (progress + clock advance
together), never silently stacked on top, so time cannot double-count.

The important rule is:

> **The journey engine should not contain knowledge that belongs in this JSON.**

---

# 6. Travel Time vs Detour Time

Every route has one fixed baseline duration (`travel_days`). There is
no caravan/wagon/foot menu and no fare — the tradeoff is not money for
speed, it is focus against engagement:

```text
Stay on the road
    ↓
Arrive as planned
```

vs:

```text
Stop, help, investigate
    ↓
Arrive later, changed world
```

Not every interesting event costs time. Content follows one rule:
time is a resource the player manages, not a penalty on every encounter.
An event states its cost up front, and the choice is explicit:

* Continue immediately, schedule preserved.
* Spend a day helping, gain a future ally.
* Risk a shortcut that may save a day or cause trouble.
* Investigate a site tied to the Convergence.

Detour UX decision: the extra days pass *after* the detour plays out,
and the cost is shown *before* committing. A story-heavy detour runs
as a small self-contained adventure (dialogue, battle, reward) first;
only on its resolution are the stated days charged through the shared
time path. The player always knows the price before paying it.

This is what fits Kaartenheld: you never fail a journey; you fail to
have the journey you intended.

---

# 7. Travel Organizer Configuration

Decision: organizers are ordinary world actors with a new interaction
type, following the `shop_id` pattern (`src/world/actor.h`) — not a
parallel NPC system.

* `InteractionId`: add `INTERACTION_TRAVEL = 6`; `ActorEngageResult`:
  add `ENGAGE_TRAVEL = 5`.
* `WorldActorDefinition`: add `travel_id` (uint8_t, 0 = none), mirroring
  `shop_id`. The compact `StaticActorDefinition` WRAM copy gains the
  same field (watch the `MAX_STATIC_ACTORS` WRAM comment in `actor.h`).
* A compiled organizer table maps `travel_id` → route-id list:

```json
{
  "id": "TRAVEL_ORGANIZER_TOWN",
  "routes": [
    "town_to_mountains",
    "town_to_desert",
    "town_to_castle"
  ]
}
```

* Actors are placed in `levels/*.json` (`travel` property), validated by
  `validate.py --travel-refs`, and compiled by `compile.py` like shops.
* The NPC system asks the journey system for the organizer's routes;
  adding a destination is: create route JSON → add its ID to the
  organizer → build the destination area. No engine changes.

---

# 8. Resources

Journey resources are **currencies**, not a parallel storage system.

Decision: `FOOD` and `PARTS` become new `CurrencyId` slots
(`CURRENCY_ID_FOOD = 2`, `CURRENCY_ID_PARTS = 3` in
`src/game/game_ids.h`). `MAX_CURRENCIES` is already 4
(`src/rpg/state.h`), so no struct growth is needed.

Consequences:

* Consumption and rewards go through the existing `currency_add` /
  `currency_get` (`src/rpg/currency.{h,c}`) — the single writer for
  `GameState.currency` (AGENTS.md §53.1/§54.1).
* `CURRENCY_ADDED` / `CURRENCY_SPENT` telemetry, the `currency`
  scenario assertions, the semantic state dump, the scenario
  `initial_state` currency section, and SRAM save/load all work
  unchanged (the snapshot/descriptor currency sections are generic).
* Starter grants live in `game_new_game` (`src/game/content.c`)
  alongside gold.

### Food

Represents food and water together. Consumed during travel.

### Parts

Represents journey repair/maintenance supplies. Consumed by rough
terrain and breakdown events.

Do not hard-code final consequences for depletion (see §10).

---

# 9. Resource Consumption

Each journey day consumes resources according to the route.

For example:

```json
{
  "resources": {
    "food_per_day": 2,
    "parts_per_day": 1
  }
}
```

The journey runtime simply applies the configured consumption.

---

# 10. Resource Depletion

Depletion fires as an **edge**, not a level: `RESOURCE_DEPLETED(food)`
is emitted once on the transition to zero, not every day spent at
zero. The compiler requires **some** depletion response by Phase 1 —
even if the first content is only HP damage — because victory-only
battle resume (§18) and open-ended depletion both leave "what happens
next?" undefined, and "never a game over" (§0) needs a data-defined
answer from the start.

Depletion resolves through the normal outcome machinery (§16):

```text
Food reaches zero
       ↓
RESOURCE_DEPLETED(food)   ← edge: once per transition to zero
       ↓
Data-defined response (outcomes + effects, §16–17)
```

Minimal Phase 1 schema (compiler-rejected if absent):

```json
{
  "on_depleted": {
    "food": { "outcomes": [ { "weight": 100, "effects": [
      { "type": "hp", "amount": -2 }
    ] } ] },
    "parts": { "outcomes": [ { "weight": 100, "effects": [
      { "type": "journey", "id": "delay", "days": 1 }
    ] } ] }
  }
}
```

Richer content can say:

```text
Food depleted
→ HP damage
```

or:

```text
Food depleted
→ Poison
→ Journey delay
```

or:

```text
Food depleted
→ Arrive in poor condition
```

or something completely different.

Likewise for parts.

The engine should not make the design decision.

---

# 11. Journey Runtime State

The active journey is **persistent state** inside `GameState`
(AGENTS.md §53.1/§54.1), so mid-journey save/load works. This bumps
`SAVE_VERSION` 2 → 3 (`src/rpg/save.h`, `docs/save-format.md`) and is
covered by the save roundtrip scenario.

```c
typedef struct {
    uint8_t active;         /* 0 = none, 1 = travelling */

    uint8_t route_id;       /* game-range id (>= 0x80 convention) */

    uint8_t total_days;     /* fixed baseline (travel_days) + detours taken */
    uint8_t days_remaining;

    /* Abandon target: where the journey started. */
    uint8_t origin_scene;   /* SceneId */
    uint8_t origin_x;
    uint8_t origin_y;
    uint8_t origin_facing;

    uint8_t event_pending;  /* journey-event id awaiting choice, 0 = none */

    /* Added before the SAVE_VERSION bump: Phase 3 depends on both. */
    uint8_t return_to_journey; /* 1 = a road battle is in progress, resume
                                * the journey on battle end */
    uint8_t pending_delay;     /* extra days appended by delay effects */
} JourneyState;
```

Rules:

* IDs are `uint8_t` following the `EventId` / `EntityId` / `DialogueId`
  game-range convention — never bare `uint16_t`.
* Food/parts live in `GameState.currency`, **not** in this struct (§8).
* No large stack copies of `GameState` (AGENTS.md §52.14, SP = 0xFFFE
  under the harness). The struct itself is ~11 bytes; WRAM/SRAM cost is
  tracked in `make memmap` + `docs/save-format.md`.

The distinction remains:

```text
JSON
= journey rules/content

JourneyState (+ currency)
= current journey
```

---

# 12. Journey Simulation

The journey proceeds in discrete days over the fixed baseline plus any
detours taken.

Basic flow:

```text
Start Journey
      ↓
Set duration = travel_days
      ↓
DAY START (advance travel progress + global DAY clock)
      ↓
Consume resources
      ↓
Process depletion conditions
      ↓
Check for event
      ↓
Event?
 ┌────┴─────┐
 No         Yes
 │           │
 │       Show Event (+ stated time cost, if any)
 │           │
 │       Player Choice
 │           │
 │       Play out detour (if any), then charge its days
 │           │
 └─────┬─────┘
       ↓
  Next Day
       ↓
Days remaining?
 ├── yes → DAY START
 └── no  → ARRIVAL
```

Detour days count *as* journey days: when an event's choice carries a
time cost, the detour plays out first (dialogue, battle, reward) and
its days are charged on resolution — travel progress and the global
clock advance together, never double-counted. Not every event costs
time (§6).

---

# 13. Journey Events

Events are separate JSON files.

Example:

```json
{
  "id": "broken_wagon",
  "text": "A wheel has broken.",

  "choices": [
    {
      "id": "repair",
      "text": "Repair it",

      "requirements": [
        {
          "type": "currency",
          "id": "parts",
          "minimum": 2
        }
      ],

      "effects": [
        {
          "type": "currency",
          "id": "parts",
          "amount": -2
        }
      ]
    },

    {
      "id": "continue",
      "text": "Keep going",

      "effects": [
        {
          "type": "journey",
          "id": "delay",
          "days": 2
        }
      ]
    }
  ]
}
```

Events should support both:

* automatic events
* player-choice events

---

# 14. Event Selection

Routes provide event pools.

For example:

```json
{
  "events": {
    "frequency": 35,
    "pool": [
      "bandit_attack",
      "broken_wagon",
      "lost_traveler",
      "find_food"
    ]
  }
}
```

Later, pools can use weights:

```json
{
  "id": "bandit_attack",
  "weight": 10
}
```

and:

```json
{
  "id": "find_food",
  "weight": 30
}
```

The engine should select according to the configured weights.
Weights are `uint8_t` (0–255); the pool sum must fit `uint16_t`
(compiler-rejected above 65535; in practice keep pools small enough to
audit). The weighted pick runs in the banked journey body, where `%`
is allowed — the fixed-bank wrapper never rolls (AGENTS.md §52.18).

---

# 15. Conditions

Decision: extend the existing event condition/action vocabulary
(`src/core/event.h`: `EventCond FLAG/VARIABLE/ITEM_COUNT`,
`EventAction ...`) — never a second parallel journey-only language
(AGENTS.md §55.1).

Journey content reuses the same structs in a `JourneyEventDefinition`
table (separate table so overworld `INTERACT`/`MAP_ENTER` first-match
order semantics stay untouched). New variants:

```json
{
  "type": "currency",
  "id": "food",
  "minimum": 5
}
```

(`EVENT_COND_CURRENCY`, mirroring `ITEM_COUNT`.) The existing `flag` /
`variable` / `status` shapes apply unchanged:

```json
{
  "type": "flag",
  "id": "helped_traveler"
}
```

The condition system should be generic enough to grow later.

---

# 16. Outcomes

Events should support multiple possible outcomes.

For example:

```json
{
  "outcomes": [
    {
      "weight": 70,
      "effects": [
        {
          "type": "currency",
          "id": "parts",
          "amount": -2
        }
      ]
    },
    {
      "weight": 20,
      "effects": [
        {
          "type": "currency",
          "id": "parts",
          "amount": -5
        }
      ]
    },
    {
      "weight": 10,
      "effects": [
        {
          "type": "journey",
          "id": "delay",
          "days": 2
        }
      ]
    }
  ]
}
```

The engine performs:

```text
evaluate conditions
      ↓
select outcome
      ↓
execute effects
```

It does not need to know whether an outcome is "good" or "bad."

---

# 17. Effects

Effects reuse the extended `EventAction` vocabulary (§15). Resource
effects are `ADD_CURRENCY` on the food/parts slots (§8); HP effects go
through the existing party/battle paths; battle effects call the
existing `battle_start` (which never knows the battle came from a
journey); scene effects reuse `SCENE_CHANGE`. New action:
`EVENT_ACTION_JOURNEY_START` (organizer confirm path).

Initial effect types (`EventActionType` extensions):

```text
currency   (food / parts / gold — replaces "resource" and "money")
hp
status
journey    (delay / days modifiers)
battle
```

Potential future effects (existing or planned actions, not new systems):

```text
item
flag
quest
heal       (via hp)
```

Example:

```json
{
  "type": "hp",
  "amount": -5
}
```

```json
{
  "type": "status",
  "id": "poison"
}
```

```json
{
  "type": "battle",
  "id": "BANDIT_AMBUSH",
  "on_victory": { "effects": [ { "type": "currency", "id": "gold", "amount": 20 } ] },
  "on_defeat": { "effects": [
    { "type": "journey", "id": "delay", "days": 1 },
    { "type": "hp", "id": "floor", "amount": 1 }
  ] }
}
```

Defeat is lost opportunity, not game over (the "time is the currency"
principle). A road-battle effect routes the battle result into the
same outcome machinery as §16: `on_victory` grants whatever the event
promises (loot, flags, shortcuts); `on_defeat` grants nothing lootable
and instead applies a data-defined recovery — normally a `journey
delay` of +1 day (time is the scarce resource, so repeated defeat still
hurts) plus an HP floor (`hp floor 1`) so the party cannot chain one
defeat straight into the next at 0 HP. For boss fights, defeat
additionally leaves the boss alive (no `ACTOR_STATE_DEFEATED`, no
`clear_days` skip from §35b) — the retry itself is the cost. Both
branches are required schema: the compiler rejects a `battle` effect
with neither, and scenarios pin victory-loot vs defeat-no-loot-plus-delay.

```json
{
  "type": "journey",
  "id": "delay",
  "days": 2
}
```

The journey system should use existing game systems for effects where possible instead of creating duplicate implementations.

---

# 18. Battle Integration

Journey events should be able to launch existing card battles, with
**both results data-defined** — victory-only resume leaves defeat
semantics undefined, which "never a game over" (§0) cannot tolerate.

Example:

```json
{
  "id": "bandit_attack",

  "choices": [
    {
      "id": "fight",
      "text": "Fight",
      "effects": [
        {
          "type": "battle",
          "id": "BANDIT_AMBUSH",
          "on_victory": { "effects": [ { "type": "currency", "id": "gold", "amount": 20 } ] },
          "on_defeat": { "effects": [
            { "type": "journey", "id": "delay", "days": 1 },
            { "type": "hp", "id": "floor", "amount": 1 }
          ] }
        }
      ]
    }
  ]
}
```

Flow:

```text
Journey
   ↓
Bandit Event
   ↓
Fight (sets JourneyState.return_to_journey = 1)
   ↓
Existing Battle System
   ↓
Battle Result → on_victory | on_defeat
   ↓
Resume Journey (return flag cleared)
```

The existing battle system does not need to know that the battle originated from a journey.

---

# 19. Existing RPG State

The journey system should be able to affect existing player state through generic effects.

Initially this could include:

```text
HP
Poison
```

AP should **not** be permanently modified by the journey engine unless a future design explicitly adds a safe recovery mechanism.

The architecture should nevertheless allow an effect to modify AP later if desired.

For example:

```json
{
  "type": "ap",
  "amount": -1
}
```

would be a data possibility, but does not need to be used initially.

This keeps the design flexible without committing to potentially soft-locking mechanics.

---

# 20. Terrain and Environment

Routes can define an environment:

```json
{
  "environment": {
    "terrain": "mountain"
  }
}
```

Terrain can influence data-driven event pools and modifiers.

Examples:

```text
Mountain
→ breakdowns
→ snow
→ slower travel over passes

Desert
→ heat
→ water/food pressure
→ fewer safe stops

Forest
→ getting lost
→ wildlife
```

Avoid scattering terrain-specific rules throughout C code.

The goal is eventually:

```text
Terrain data
→ modifiers/events
```

rather than:

```c
if (terrain == DESERT) ...
```

everywhere.

---

# 21. Journey Screen

The journey screen should be deliberately simple.

Example:

```text
ROAD TO THE MOUNTAINS

Day 4 of 7 (clock 4 / 180)

Food       18
Parts       4

████████████░░░░░

[Continue]
```

(The §21 mock's `Weather: Clear` row is cut from the slice — weather
exists nowhere in the schema; see §32. The `Day` line shows simulated
progress plus the global clock against the `CONVERGENCE_DAYS` content
constant, never a hard-coded 180.)

When an event happens, the screen becomes an event/choice screen:

```text
A group of travelers approaches.

[Talk]
[Ignore]
[Prepare for trouble]
```

After resolving the event, the player returns to the journey screen.

The existing UI framework should be reused rather than creating an entirely separate UI system.

---

# 22. Travel Organizer UI Flow

The complete player experience should be:

```text
TOP-DOWN RPG
     ↓
Talk to Travel Organizer
     ↓
Destination Selection
     ↓
Confirm Journey
     ↓
Journey Begins
```

Example:

```text
TRAVEL ORGANIZER

Where do you want to go?

> Mountain Pass
  Desert City
  Castle
```

Then:

```text
TRAVEL ORGANIZER

Mountain Pass.

Known about the route:
5 days, mountain roads.

[Travel]
[Back]
```

The organizer should retrieve this information from the route definition rather than duplicating it.

---

# 23. Starting a Journey

The runtime API should conceptually expose:

```c
journey_start(route_id);
```

It should:

1. Validate the route.
2. Confirm the player is at the route's start.
3. Record `JourneyState.origin_*` from the current `GameState.scene`.
4. Set the duration to the route's fixed `travel_days` (no roll —
   determinism is structural, so the banked body never needs `%`,
   AGENTS.md §52.18).
5. Initialize journey state (`active = 1`, `return_to_journey = 0`,
   `pending_delay = 0`).
6. Switch to the journey screen.

The Travel Organizer (`INTERACTION_TRAVEL`, §7) should call this
function after the player confirms. A wrong start scene is a transient
menu message, never a silent no-op.

---

# 24. Completing a Journey

When the journey finishes:

```c
journey_complete();
```

should:

1. Apply any final journey effects.
2. Clear active journey state (`active = 0`).
3. Resolve arrival conditions.
4. Move the player to the destination scene (`scene_load`, so
   `MAP_ENTER` events fire on arrival).
5. Resume normal top-down gameplay.

The destination is already an existing world scene.

The journey system simply provides the connection between areas.

## 24b. Abandoning a Journey

Decision: the player may abandon the voyage at any time from the
journey screen:

```c
journey_abandon();
```

* Returns to `JourneyState.origin_*` via `scene_load` (exact
  pre-voyage position).
* Consumed food/parts and HP/status effects **stay** (the penalty).
  No full-`GameState` checkpoint buffer is kept — this keeps the cost
  to the ~11-byte `JourneyState` instead of an 800B second copy in WRAM.
* Emits `JOURNEY_ABANDONED` telemetry and is scenario-tested
  (abandon → origin scene/position restored, consumed effects kept).

---

# 25. JSON Compilation

The Game Boy should not parse arbitrary JSON at runtime.

The JSON should be compiled into generated C data, following the
existing content pipeline (`tools/screen_compiler/*_compile.py` +
`tools/level_compiler/compile.py`, with `make *-check` gates — never a
parallel build mechanism).

Conceptually:

```text
journeys/routes/*.json
journeys/events/*.json
        ↓
Journey Compiler (tools/screen_compiler/journey_compile.py)
        ↓
src/game/journeys_content.c
src/game/journeys_content.h
```

The JSON remains the source of truth.

Generated files should not be manually edited.

---

# 26. Journey Compiler

Add `tools/screen_compiler/journey_compile.py` (+ `--check`), wired as
`make journeys` / `make journeys-check` and into the `release`/`debug`
builds exactly like `screens`, `dialogues`, and `shops` (see `Makefile`).

The compiler should:

* load route JSON
* load event JSON
* validate schemas
* resolve IDs against the real registries: scenes against
  `levels/registry.json`, battles against `BattleId`
  (`src/world/actor.h`), currencies against `game_ids.h`
* validate references (organizer → route → event → battle/currency)
* produce compact generated C tables into a **banked** ROM bank
  (fixed `_CODE` headroom is ~88 B; confirm the bank with
  `make memmap` before locking — banks 2/4/5 are nearly full, see
  `docs/roadmap.md` §11)

Fat journey logic lives in a banked body behind a thin `g_bk_call_*`
staging wrapper (the `combo_evaluate` / `scene_spawn` pattern,
AGENTS.md §52.11.1), dispatched by `switch` (no function pointers,
AGENTS.md §52.1). No `%` / `/` / 8-bit `*` in the fixed-bank wrapper (AGENTS.md §52.18).

---

# 27. Validation

The compiler should reject invalid content.

### Routes

Check:

* unique route IDs
* valid start scene (against `levels/registry.json`)
* valid destination scene (against `levels/registry.json`)
* valid fixed `travel_days` (positive integer)
* valid currency references (food / parts / gold)
* valid event references

### Travel Organizers

Check:

* unique organizer IDs
* valid route references
* valid scene/actor references (`validate.py --travel-refs`,
  alongside `--shop-refs` / `--dialogue-refs`)

### Events

Check:

* unique event IDs
* valid effect types (extended `EventActionType`)
* valid battle IDs (against `BattleId`)
* valid currency IDs
* valid status IDs
* valid conditions

### Cross-references

For example:

```text
Travel Organizer
    ↓
Route
    ↓
Event
    ↓
Battle
```

Every reference should resolve at compile time.

---

# 28. Testing

The journey system follows the harness contract (AGENTS.md §6–10,
AGENTS.md §53): semantic telemetry + snapshots + deterministic RNG + fixture
scenarios. New scenarios use fixture levels
(`tools/scenarios/fixtures/levels/test_*.json`, `TEST_*` scenes, bank
4) — never real `levels/` scenes (AGENTS.md §42.1). Event
frequency and weighted picks go through the game RNG (`SET_RNG`,
inspectable) — the baseline duration is fixed, so no duration roll
exists. New telemetry is append-only (`telemetry.h` +
`EVENT_TYPE_MAP`); scenario setup writes direct-to-`GameState`
(no gameplay telemetry, AGENTS.md §53.3) with loader ordering per AGENTS.md §52.12.

Test at minimum:

```text
Start journey
→ correct route, fixed travel_days

Daily update
→ resources consumed correctly

Detour choice with a time cost
→ journey extended, clock advanced together

Free event (no time cost)
→ schedule preserved

Event selection
→ correct event pool used

Weighted events
→ correct selection behavior

Event choice
→ correct effects applied

Battle event victory
→ loot granted, journey resumes

Battle event defeat
→ no loot, +1 day delay, HP floor, journey resumes

Journey completion
→ correct destination

Abandon journey
→ origin scene/position restored

Save/load mid-journey
→ roundtrip preserves journey + currency state

Invalid JSON
→ compiler rejects it

Invalid reference
→ compiler rejects it
```

Randomness (event frequency, weighted pools/outcomes) goes
through the game RNG with a deterministic/testable seed.

---

# 29. Initial Vertical Slice

Do not build the complete journey system for every destination immediately.

Build one complete route:

```text
Town
  ↓
Travel Organizer
  ↓
Mountain Pass
```

With a fixed duration (e.g. 5 days):

Two resources:

```text
Food
Parts
```

Four events:

```text
Find Food
Broken Wagon
Traveler
Bandit Attack
```

And basic effects:

```text
currency (food / parts via ADD_CURRENCY)
HP
status
delay (journey action)
battle
```

This proves the entire architecture:

```text
NPC
 ↓
Route JSON (fixed travel_days)
 ↓
Journey (+ detours)
 ↓
Resources
 ↓
Event
 ↓
Choice
 ↓
Effect
 ↓
Existing battle
 ↓
Destination
```

---

# 30. Expansion After the Vertical Slice

Once the first route works, adding content should primarily involve JSON.

For example, adding:

```text
Town → Desert City
```

should require:

```text
journeys/routes/town_to_desert.json
```

plus any new events.

Adding a new event should require:

```text
journeys/events/desert_storm.json
```

rather than C engine changes.

Adding another travel organizer should mostly be a content change.

---

# 31. Architectural Principle

The final architecture should be:

```text
                 ┌─────────────────────┐
                 │ Travel Organizer NPC │
                 └──────────┬──────────┘
                            │
                     select destination
                            │
                            ▼
                    ┌──────────────┐
                    │ Route JSON   │
                    └──────┬───────┘
                           │
                     fixed travel_days
                           │
                           ▼
                    ┌──────────────┐
                    │ Journey      │
                    │ Runtime      │
                    └──────┬───────┘
                           │
              ┌────────────┼────────────┐
              │            │            │
           resources      events      time
              │            │            │
              │            ▼            │
              │       conditions       │
              │            │            │
              │         outcomes       │
              │            │            │
              └────────── effects ─────┘
                           │
              ┌────────────┼────────────┐
              │            │            │
             HP          status       battle
              │                         │
              │                         ▼
              │                    Card Battle
              │                         │
              └────────────┬────────────┘
                           │
                           ▼
                      Destination
                           │
                           ▼
                     Existing RPG
```

The core rule is:

> **The engine implements the journey mechanics. JSON defines what the journey means.**

That means depletion responses and battle-defeat consequences are content
decisions that the data model expresses through §10/§16–§18 — the engine
only routes `RESOURCE_DEPLETED` and `on_victory`/`on_defeat` into outcomes.

---

# 32. Repo Integration (normative)

This section binds the plan to the repository's invariants. It is
part of the plan, not an appendix.

* **Engine vs game layer** (AGENTS.md §55.1–55.2): engine
  (`src/world/journey.{h,c}` + banked body) is generic and owns no
  content; game content (`journeys/*.json` → `journeys_content.c`,
  organizer table, `CURRENCY_ID_FOOD/PARTS`, `game_journey_register()`)
  lives in `src/game/`. Engine files never include game headers or
  branch on game ids.
* **Screen contract** (AGENTS.md §54.2/§54.6/§55.6, AGENTS.md §36): new `SCREEN_JOURNEY`
  (next `ScreenId`, currently 13 in `src/screens/screen.h`); screens
  hold no persistent state (transient UI in `Game`, reset on exit);
  draw through `MenuFrame` with direct-literal titles; targeted
  redraws for Continue/day-step; declare the VRAM block-1 / CGB
  palette footprint and the `ui_invalidate_tileset()` return path;
  timer-ISR music (AGENTS.md §35) is never stalled by transitions.
* **Journey screen weather**: cut from the slice (§21 mock no longer
  shows it). If weather ever returns, it is route `environment` data
  (§20) — no hardcoded weather engine either way.
* **Telemetry** (AGENTS.md §55.4, `telemetry.h` append-only):
  `JOURNEY_STARTED`, `JOURNEY_DAY_ADVANCED`, `JOURNEY_EVENT_SHOWN`,
  `JOURNEY_CHOICE`, `JOURNEY_COMPLETED`, `JOURNEY_ABANDONED`,
  `RESOURCE_CONSUMED`, `RESOURCE_DEPLETED`, `LORD_POWER_UP`,
  `ARRIVAL`
  (plus reused `CURRENCY_*`, `DAMAGE/HEAL`, `BATTLE_*`,
  `SCRIPT_TRIGGERED`, `MUSIC_CHANGED`). Payloads follow the
  `data[4]` convention; host `EVENT_TYPE_MAP` stays in sync.
* **Snapshot / descriptor** (AGENTS.md §53.4–53.5): extend `g_state_snap_buf`
  (journey fields) and `g_scen_state_buf` (journey setup) with version
  bumps; keep the append-only byte contract; expose semantic
  assertions only (`journey_day`, `resource`, `event_occurred`, … —
  never byte offsets, AGENTS.md §53.7).
* **Fixed baseline, no zero-day shortcut** (§5/§12): the route's
  `travel_days` always run — duration is structural, never a roll, so
  no choice of transport can buy immunity from the Convergence clock.
  Speed comes only from declining detours.
* **Validation gates**: `make clean && make debug && make release`
  (headers have no deps, AGENTS.md §52.2), `make test-harness` (incl. sentinels
  `patrol_slime_cross`, `patrol_enemy_bumps_player`,
  `battle_multi_enemy_cycle_kill`, AGENTS.md §52.19), `make test`,
  `make memmap` (fails on `_HOME ≥ 0x8000`), `make lint`,
  `make verify-oam`; add a walkthrough milestone for the journey
  screen (AGENTS.md §56.3) when the slice lands.

---

# 33. Phased Implementation Plan (normative)

Additive only: every phase adds new files, a new `ScreenId`, and new
table rows — never changes to existing screen flows, existing struct
layouts beyond appends, or existing bank placement. Each phase ends
green on `make test-harness`, `make test`, `make memmap`, `make lint`,
`make verify-oam`. Full `make clean` rebuild after any header change
(AGENTS.md §52.2).

ASCII-only until the graphics seam (Phase 5): journey screens use
`menu_draw_frame` + `ui_draw_text_line` into `g_ui_screen_buf` (the
`shop_screen.c` pattern: frame + bank-2 content body, bank-2 caret
repaint, no full redraws per AGENTS.md §36). No new tile ids, palette writes,
OAM entries, or VRAM block claims before Phase 5, so `verify-oam`
stays unaffected and all tests assert `screen_row` text + semantic
state (never tile data — the harness cannot see tile data anyway,
AGENTS.md §52.22).

## Phase 0 — IDs + currencies (no behavior)

* `src/game/game_ids.h`: `CURRENCY_ID_FOOD = 2`,
  `CURRENCY_ID_PARTS = 3`; starter grants in `game_new_game`.
  `VARIABLE_ID_DAY` + `VARIABLE_ID_LORD_POWER` named variables;
  `CONVERGENCE_DAYS` content constant (default 180).
* Why safe: `MAX_CURRENCIES` is already 4; currency/variable snapshot /
  descriptor / telemetry sections are generic, so no wire change.

## Phase 1 — Data + compiler + registration (no UI)

* New: `journeys/routes/town_to_mountains.json`, four event JSONs,
  `tools/screen_compiler/journey_compile.py` + `make journeys` /
  `make journeys-check` (wired into `release`/`debug` like `screens` /
  `dialogues` / `shops`), `src/game/journeys_content.c` in a banked
  ROM bank (bank chosen via `make memmap`; banks 2/4/5 are nearly
  full), `game_journey_register()` mirroring `event_init` /
  `dialogue_register` (AGENTS.md §55.2). Schema requires
  fixed `travel_days`, `on_victory`/`on_defeat` on every `battle`
  effect, and `on_depleted` responses (§10) — rejected if absent.
* Engine skeleton: `src/world/journey.{h,c}` + banked body via the
  `g_bk_call_*` trampoline (`combo_evaluate` / `scene_spawn` pattern,
  AGENTS.md §52.11.1), `switch` dispatch only (no function pointers, AGENTS.md §52.1), no
  `%` / `/` / 8-bit `*` in the fixed-bank wrapper (AGENTS.md §52.18). Pure
  logic only: validate, set the fixed baseline duration,
  consumption math, clock charge. Callable
  from harness actions; no screen yet.

## Phase 2 — Organizer + start / abandon / arrive (ASCII menus)

* `actor.h`: `INTERACTION_TRAVEL = 6`, `ENGAGE_TRAVEL = 5`,
  `travel_id` on `WorldActorDefinition` (mirrors `shop_id`) plus the
  compact `StaticActorDefinition` copy (watch the `MAX_STATIC_ACTORS`
  WRAM comment); organizer table; `levels` actor `travel` property +
  `validate.py --travel-refs`.
* `SCREEN_JOURNEY` (next id, currently 13): destination →
  confirm menu. `journey_start` records `origin_*`;
  `journey_abandon` → `scene_load(origin)` with
  consumed food/parts + HP/status effects kept (§24b);
  `journey_complete` → `scene_load(dest)` so `MAP_ENTER` fires.
  Wrong start scene is a transient menu message,
  never a silent no-op.

## Phase 3 — Day loop + road events + battle resume (still ASCII)

* Day advance on `[Continue]` (one press + `WAIT` per edge-trigger,
  AGENTS.md §52.10): consume → depletion edge check (§10) → frequency roll → pool pick
  (weighted, banked body) → choice screen (same MenuFrame pattern) →
  `EventAction` effects (`ADD_CURRENCY` / hp / status / `battle` with
  `on_victory`/`on_defeat` / `journey`). Detour days extend the
  journey and the clock together (§6); free events preserve the
  schedule. Start/arrival telemetry always fires (§32).
* Battle resume: road `battle` effects call the existing
  `battle_start` (which never knows the origin); victory/defeat
  writeback per AGENTS.md §54.1, then resume the journey via
  `JourneyState.return_to_journey`. Defeat grants no loot, applies the
  `on_defeat` delay + HP floor (§18); boss defeat leaves the boss alive
  (no `clear_days` skip, §35b).
* Biome clear: boss-defeat event applies `clear_days` (§35b) —
  permanent, Lord ticks for the full skip.

## Phase 4 — Observability lock-in

* Telemetry, snapshot/descriptor deltas, semantic dump, fixture level
  (`test_journey*`, bank 4, `TEST_*` scene per AGENTS.md §42.1), and the
  walkthrough milestone (AGENTS.md §56.3). See §34 for the per-phase audit
  surface.

## Phase 6 — Cut: no rewind in Kaartenheld 1

Phase 6 was the rewind phase. It is deleted with the mechanic (§38):
Phases 0–5 are the whole plan, ending at the graphics seam. The
sequel restores a rewind phase on top of the shipped observability
contract when it needs one.

## Phase 5 — Graphics seam (future, no logic change)

Screens never hardcode pixels: layout (rows/cols/labels) comes from
compiled journey/skin data and all drawing goes through
`ui_draw_text_line` / `menu_draw_frame` — the same seam
`battle_hud.json` / `cards_skin.json` use (`battle_compile.py` →
banked loader → WRAM cache → bank-3 renderer). Graphics work is then
only: a `journeys/skin.json` (frame tiles, route icon, palette slots
from free `UI_COLOR_*` / VRAM ids in `ui.h`), a banked tile loader
(`ui_card_tiles_load_banked` pattern: LCD-off, `VBK_REG = 0` reset,
signed-addressing AGENTS.md §52.22), and per-row `ui_color_span` calls. Text
rows and assertions stay identical.

Acceptance for "ASCII done, graphics easy": all Phase 1–4 scenarios
assert only `screen_row` text + semantic state; the only files a
graphics pass touches are the new skin JSON, its loader, and color
spans.

---

# 34. LLM Audit Contract (normative)

Semantic state is authoritative; pixels never gate (AGENTS.md
§7/§53.5/§55.4). Each phase ships the telemetry + snapshot +
scenarios it needs — no "trust the screen" steps.

## Per-phase observable surface

* **P0**: `currency` assertions on `FOOD` / `PARTS`
  (`dev.py state <scenario> --state` shows the `CURRENCY:` lines);
  existing `CURRENCY_ADDED` / `SPENT` events.
* **P1**: compiler `--check` rejects (dup id, bad scene vs
  `registry.json`, bad `BattleId` / currency, dangling
  organizer → route → event); logic scenarios assert fixed-duration,
  consumption math via `event_arg` payloads —
  all seeded (`SET_RNG`), deterministic.
* **P2**: `screen_row` text asserts (row 0 `TRAVEL`,
  destination rows + duration line — titles are direct
  literals per AGENTS.md §54.6, so row text proves render);
  `event_occurred: JOURNEY_STARTED / ARRIVAL / JOURNEY_ABANDONED`;
  abandon asserts origin scene + position restored with consumed
  effects kept; `event_not_occurred` guards setup-telemetry leaks (AGENTS.md §53.3).
* **P3**: `JOURNEY_DAY_ADVANCED` (day-counter args),
  `JOURNEY_EVENT_SHOWN` / `JOURNEY_CHOICE`,
  `RESOURCE_CONSUMED` / `DEPLETED`, `BATTLE_STARTED` → `BATTLE_ENDED`
  → journey resume, `SCRIPT_TRIGGERED` for road events; mid-journey
  `roundtrip` proves the save boundary lossless.
* **P4**: full semantic dump
  (`SCENE` / `PLAYER` / `FLAGS` / `VARIABLES` / `CURRENCY` / `PARTY` /
  `JOURNEY` / `WORLD`) + `EVENTS SINCE` incremental inspection;
  walkthrough milestone text-verified (`bg_text`), PNGs review-only
  (AGENTS.md §56.4).

## Audit commands (same every phase)

```bash
make test-scenario SCENARIO=journey_<name>   # smallest repro
python3 tools/dev.py state <scenario>        # semantic dump
python3 tools/dev.py roundtrip <scenario>    # save-boundary probe (P3+)
make test-harness JOBS=4                     # full suite + sentinels
```

Rules baked in: scenarios stage `TEST_*` fixture scenes only (AGENTS.md §42.1,
never real `levels/`); new telemetry is append-only (`telemetry.h` +
`EVENT_TYPE_MAP` in sync); snapshot/descriptor bumps carry version
bytes; every new check gets a negative test (restore-the-bug → must
FAIL, AGENTS.md §52.16). A phase is done only when an LLM can state expected
vs actual from `PASS` / `FAIL` output alone — no human "looks right
on screen" step, including the future graphics phase (which asserts
identical `screen_row` text plus a new `GRAPHICS:` skin block per
`docs/graphics.md`, never pixel hunts).

---

# 35. Global Day Counter & Convergence Clock (technical)

§0 fixes the design: **~180 days to the Convergence; travel advances a
global day counter; arriving late changes the world, never game-overs.**

Technical mapping (no new persistent store):

* `VARIABLE_ID_DAY` (new named variable, `game_ids.h`): days elapsed
  since waking in the distant biome, `0..CONVERGENCE_DAYS+` (`int16`,
  fits the existing `VariableState.values[]`). `MAX_STATE_VARIABLES`
  is 16 with 8 used, so this consumes one free slot — no struct
  growth, no SRAM growth, no save-version bump for the slot itself.
* `CONVERGENCE_DAYS` is a **compiled content constant** (default 180),
  not a hard-coded `180` in C or screen text — "approximately 180"
  means it may move during tuning. The journey screen shows
  `Day N of M (clock N / M)` from the constant; content may reference
  fractions of it for tier thresholds.
* Increment points (closed list, all through `game_variable_add` so
  `VARIABLE_SET` telemetry fires):
  1. the journey day loop (`DAY START`, §12, advancing the fixed
  baseline plus detours);
  2. the biome-clear skip (§35b, charging `clear_days` at once);
  3. defeat-recovery delays (`on_defeat` → `journey delay`, §18);
  4. any future wait/rest/training action. Overworld wandering,
  dialogue, and victorious battles do **not** tick the clock. Biome
  exploration before the boss is therefore free by default — an
  explicit choice ("preparation is free, commitment costs"), not an
  oversight. If tuning wants preparation to cost something, charge
  part of the biome's `clear_days` on biome **entry** and the rest on
  the kill; the schema already supports splitting it because
  `clear_days` is data, not a C constant.
* Display: journey screen `Day N of M` row (§21) plus an overworld
  HUD readout once the clock exists. Both read the same variable;
  neither stores a copy (screens hold no persistent state, AGENTS.md
  §54.2).
* Observability: `VARIABLE_SET` (day), `JOURNEY_DAY_ADVANCED`
  (day-counter args), semantic `VARIABLES: DAY=N` dump, `variable`
  scenario assertions. Deterministic under `SET_RNG` like everything
  else — the clock itself never rolls RNG, it only counts ticks.

# 35b. Biome Clear = Fixed `clear_days` Skip (technical)

Killing a biome's boss stands in for all the exploration the game does
not simulate: a fixed day skip, data per biome, charged at once.

* Schema: the boss-defeat event carries `clear_days` (e.g. 10):
  `ACTOR_DEFEATED` → `SET_VARIABLE` progress + `ADD_VARIABLE DAY` +
  Lord ticks for the full skip. Different biomes may deserve 8 or 15 —
  it is a per-biome number, never a C constant. Budget check: ~10
  days/biome plus fixed route durations means 180 days cover roughly
  9–14 biomes plus the return — decide the biome count first, then
  tune the per-biome values against it.
* No ten `DAY START`s run: consumption, depletion checks, and random
  events do **not** fire inside the skip (it is exploration abstracted
  away, not travel). What **does** apply is the deterministic part —
  the Lord ticks for all `clear_days` (looped `ADD_VARIABLE` in the
  banked body, or a tick-as-function-of-elapsed-days; either way the
  fixed-bank wrapper emits no `*`/`/`, AGENTS.md §52.18).
* Permanence: a boss clear cannot be undone — there is no rewind
  (§38), so the days are gone and the clear stands. The clear event
  sets a persistent `CLEARED_<biome>` flag (an ordinary flag, not a
  rewind-surviving special case) so the skip cannot re-trigger.

# 36. Lord of Slime Progression (technical)

§0: the Lord grows stronger as the Convergence approaches, changing the
world and its inhabitants.

Technical mapping (derived + one stored variable, no new engine):

* `VARIABLE_ID_LORD_POWER` (new named variable, one more free slot):
  the Lord's persistent strength. Advanced by the day tick (e.g. +1
  per N days via the same `DAY START` path) and **reduced** by
  disruption events (`ADD_VARIABLE` negative amount through the normal
  journey choice/outcome effects, §15–17). It never reverses: there is
  no rewind (§38), so the countdown's urgency is structural.
* Scaling consumers (all data-driven, no `if (day > X)` in C):
  - Battle: `battle_init` reads the tier (`LORD_POWER` threshold table
    in game content, banked like other tuning tables) and applies the
    existing bonus paths — enemy HP/damage bonus, loot AP-row shift
    (`LOOT_AP_*` in `src/rpg/loot.h`), status-rider chance. Same
    functions battles already use; the tier table is content.
  - Events: route `events.frequency` / pool `weight` entries gain
    `variable` conditions on `DAY` / `LORD_POWER`, so late-game pools
    skew toward harder ambushes and Convergence-themed events through
    the existing first-match / weighted-pick machinery (§14–15).
  - Dialogue/world: `MAP_ENTER` / `INTERACT` events branch on the same
    conditions (changed greetings, fortified patrols, closed shortcuts)
    — again existing `EventCond` vocabulary, not a parallel system.
* "Never a game over": enforced by content rule, not by engine
  special-case — no route, event, or scaling tier sets a fail flag or
  blocks arrival. Late arrival only resolves harder pools/tiers. A
  `quest_gate`-style scenario pins this (late-day arrival still
  reaches `ARRIVAL`).

# 37. Card Materials Wood → Mythril (technical)

§0: cards run **Wood → Bronze → Iron → Steel → Mythril**; Mythril is
exceptionally powerful and manipulates time.

There is **no new material system to build**. The five tiers already
exist as the loot material axis (`MAT_WOOD..MAT_MYTHRIL`,
`src/rpg/loot.h`):

* Power bands: `LOOT_POWER(mat, wpn)` — wood 1, bronze 2–3, iron 4–5,
  steel 6–7, mythril 8–9 (sword/bow high end, shield/dagger/ring low
  end). Curated cards (`src/game/cards_content.c`) follow the same
  bands so a material name always implies the same range.
* Drop gating: `LOOT_AP_BRONZE/IR/STEEL` thresholds on
  `VARIABLE_ID_ENERGY_POOL` — stronger biomes raise the pool, which
  unlocks higher material drops through the existing weighted roll
  (`loot_drop_banked.c`). Biome progression **is** material
  progression; no journey-specific drop code.
* Ring charges: `LOOT_RING_USES(mat) = mat + 1` (mythril 5/battle).
* No time manipulation in Kaartenheld 1. Mythril needs nothing more
  than being the strongest band (power 8–9); its temporal connection
  is story foreshadowing for the sequel, not a mechanic here.

# 38. Time Travel Is a Sequel Mechanic (deferred)

Kaartenheld 1 has no rewinding: no checkpoint ring, no burn accounting,
no restore, no paradox rules. The complexity — undoing story choices,
restoring inventory, tracking world states, closing rewind exploits —
would overshadow the journey/choices/consequences core, so it is cut
deliberately, not postponed vaguely:

* The Convergence countdown and its urgency stay (§35) — only the
  escape hatch goes.
* Mythril stays as the strongest material (§37); its temporal
  connection is sequel foreshadowing.
* Nothing in Phases 0–5 pays rewind's costs: no hidden SRAM ring, no
  checkpoint headers/checksums, no `SAVE_VERSION` bump beyond the
  already-planned `JourneyState`, no burn-ordering or Lord-rebate
  rules, no rewind-surviving set.
* The sequel picks this section back up: dawn checkpoints, burn
  accounting, `lord = max(checkpoint, running − rebate)`, integrity
  checksums, and the full audit contract drafted here previously all
  transfer directly. This stub reserves the number so existing
  cross-references (§35b, §37, §40) keep resolving.

# 39. Choices, Relationships, Routes, Factions, Endings (technical)

§0: events cost time and have consequences; helping opens
allies/shortcuts/futures; ignoring closes some doors and opens others;
everything feeds relationships, routes, factions, endings.

No new relationship/faction engine. All of it is the existing
flag/variable/event vocabulary:

* Helping vs ignoring: a journey choice's effects are `SET_FLAG` /
  `ADD_VARIABLE` / `ADD_CURRENCY` / `SCENE_CHANGE` rows (§17). "Ally"
  = an ally flag (e.g. `HELPED_TRAVELER`); "shortcut" = an organizer route
  with a `requires: flag` condition (§15) that the flag now
  satisfies; "closed opportunity" = a first-match `INTERACT` /
  `MAP_ENTER` event whose condition the flag now fails, falling
  through to the alternative-story event listed after it.
* Routes: organizer route lists (§7) gain optional per-route `requires`
  conditions (flag set / variable threshold / currency minimum),
  validated by `journey_compile.py --check` against the same
  registries. A new destination, ally shortcut, or faction-locked pass
  is route JSON + organizer entry — never an engine change.
* Factions/endings: each faction is one generic variable (standing,
  e.g. 0–100) and each ending branch is a variable threshold checked
  by endgame `MAP_ENTER` events setting `VARIABLE_ID_ENDING_SHOWN`.
  The QUEST menu pattern (AGENTS.md §54.6) already proves generic-variable
  quests render without per-quest screen code; endings reuse it.
* Design-principles enforcement stays in content review: every
  significant choice ships **both** branches as events (no dead-end
  `CONTINUE` with no effect row); "different adventures, not
  good/bad" means reviewers check that each branch grants *something*
  (ally, gold, shortcut, lore flag) rather than help = reward /
  ignore = punishment.

# 40. Save, Memory, and Harness Cost of §35–§37 + §39 (normative)

* **WRAM/SRAM:** two generic variable slots (`DAY`,
  `LORD_POWER`) and nothing else. No hidden checkpoint ring, no
  surviving set, no extra SRAM words. No `GameState` struct growth
  except the already-planned `JourneyState` (~11 bytes with
  `return_to_journey` + `pending_delay`, §11). Tracked in
  `make memmap` + `docs/save-format.md` + `docs/roadmap.md` §11;
  no `SAVE_VERSION` bump beyond the already-planned `JourneyState`.
* **Banks:** clock/progression math lives in the banked journey body
  (§26/§33 Phase 1) via the `g_bk_call_*` trampoline pattern
  (AGENTS.md §52.11.1), `switch` dispatch only (AGENTS.md §52.1), no
  `%`/`/`/8-bit-`*` in fixed wrappers (AGENTS.md §52.18). Content
  tables (tier thresholds, route `requires`) are compiled JSON in the
  journey content bank — fixed `_CODE` stays untouched.
* **Telemetry/snapshot (append-only):** `JOURNEY_DAY_ADVANCED`,
  `LORD_POWER_UP`, `ROUTE_UNLOCKED` as needed; `VARIABLES: DAY /
  LORD_POWER` lines in the semantic dump; `journey_day`, `variable`,
  `flag`, `event_occurred / event_not_occurred` assertions only —
  never byte offsets (AGENTS.md §53.7). Descriptor/snapshot version
  bytes bump with the format change (AGENTS.md §53.4).
* **Phasing:** §35–§37 ride the existing Phase 1–3 (variables exist
  at P0, tiers/pools at P1–P3; §35b `clear_days` lands with the
  boss-defeat event in P3); §39 faction/ending content is pure JSON
  once §35–§36 exist and needs no phase of its own. Each phase still
  ends green on `make test-harness` + sentinels, `make test`,
  `make memmap`, `make lint`, `make verify-oam` (§32).

