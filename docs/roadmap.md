# Game Boy RPG Foundation Roadmap

## Status

### DONE

- GameState (scene, party, inventory, flags, variables, currency, world actors, progression, equipment)
- World actors (data-driven definitions, persistent defeat lifecycle)
- Scenes & exits (data-driven)
- Dialogue (registered content)
- Scripted events (conditions + actions, registered content; ACTOR_DEFEATED is all-match)
- Inventory, items, equipment, shops (data-driven, per-shop stock)
- Quests as a registered engine module; data-driven QUEST menu
- Multiple quest archetypes (kill-counter, fetch/deliver/unlock)
- Game/content separation (`src/game/` registered against a generic engine)
- Deterministic debug harness (90 scenarios, assertions, telemetry, RNG)
- LLM-readable state/telemetry (snapshot, semantic dump, roundtrip)
- Complete vertical slice (town, quest, sword, boss, ending)
- Battery-backed SRAM save/load (versioned format, roundtrip scenario)
- Repository hardening (engine/game boundaries, `make memmap` budget)
- Foundation docs (`architecture`, `FOUNDATION_CONTRACT`, `game-vs-engine`,
  `save-format`, `memory-budget`, `testing`; graphics pipeline spec'd)

**Foundation 1.0 (non-graphical) reached** — the runtime is cleanly separated
from game content, the vertical slice is complete and deterministically
testable, save/load works, and the memory budget is understood.

### NEXT

- **Camera / scroll — B1 + B2 DONE**: scenes are parameterized, the
  overworld has a pixel camera (`World.camera_px_x/y`, `world_update_scroll`
  follows the player smoothly and clamps to scene bounds; exposed in the
  snapshot as `scroll_x/y` = camera_px/8).  FIELD is 32x18; covered by
  `large_map_scroll` + `field_east_scroll`.  B2 rendered the terrain as real
  GB tiles (`src/gfx/world_tiles.h`) into the 0x9800 tilemap.
- **Camera / scroll — B3 + B4 DONE**: the overworld background scrolls
  smoothly with SCX/SCY (set every frame from the pixel camera; the tile
  window is redrawn only when the camera crosses a tile boundary).  The
  viewport is full-screen (18 rows, no HUD overlay — the map occupies all
  144px of vertical display).  B4 made the camera player-centred
  (`camera_px = player_px - view/2`, clamped at the scene edges); covered by
  `large_map_scroll`, `field_east_scroll` and `camera_boundary_clamp`.
  Render verification: the overworld background uses a 32-column background ring
  (populated across all 32 columns x 18 rows during LCD-safe full map redraws,
  with unused columns in <32-wide maps filled with floor tiles). Camera scrolling
  only updates SCX/SCY (0 VRAM writes during movement). Tile commits execute
  exactly 2 VRAM writes inside VBlank (restore old tile, draw hero '@'). The
  debug build mirrors the ring into `g_tilemap_mirror` (WRAM) so the harness can
  assert the rendered map (mGBA cannot read VRAM); `scroll_render_alignment`
  verifies SCX/SCY-vs-ring alignment, and the snapshot exposes `camera_px_x/y`.
- Graphics pipeline (tile/sprite renderer, DMG+CGB, screen rewiring, asset
  converter + validation) — spec'd in `docs/graphics.md`; the `sprites`
  branch has landed the player OAM sprite + the `png2gb.py` PNG→tileset
  stage (`make gfx`), the tilemap/OAM/dedup and renderer rewiring remain
- Battle system expansion (per-enemy stats/AI, command menu, variance, etc.)
  - Card-combat architecture per `docs/combo-system.md`: Phase A + B DONE —
    combo evaluation (banked) produces hand quality only; a separate effect
    layer (`src/rpg/effects.{h,c}` + bank-2 body) scales it into
    damage/block/heal; every battle card carries its `CardEffectType` as
    data (`Card.effect` from `CardDefinition.effect`); `COMBO_RESOLVED` /
    `EFFECT_RESOLVED` telemetry plus the harness `event_arg` payload
    assertion pin the numbers.
  - Statuses (Phase C core) DONE: `src/rpg/status.{h,c}` + bank-2 bodies —
    StatusDefinition/Instance/application (STACK+refresh), battle-scoped
    slots reset at battle start, on-hit riders as card data
    (`status_id`/`status_chance`, 1/255 deterministic roll via the game
    RNG), per-round poison ticks with combat-death resolution, and
    `STATUS_APPLIED`/`STATUS_TICKED` telemetry.  Covered by
    `status_poison_apply` / `status_poison_resist`.
  - Enemy encounters rebalanced: ALL hostile engagements spawn as trios
    (clones of the struck actor; the final boss stands alone), regular
    enemy HP doubled (slimes 10-16, bats 8), boss tuned to 50 HP with a
    SET_ENEMY_HP scenario hook pinning it to 20 for deterministic tests.
  - Energy pool raised to 6/phase (three cost-2 cards now form a combo);
    rejected selections flash NO ENERGY! / OUT OF USES! on the battle HUD.
  - COMBO row shows the pending hand tier LIVE while selecting (blanks on
    resolution; executed results announce via the banner).  The Lost
    Amulet now renders in the CARDS tab as a non-deckable QUEST ITEM, and
    the Merchant sells a heavy BOW 10 (30g).
  - Poker hand tiers implemented (strict sizes; PAIR..FIVE KIND, FLUSH,
    STRAIGHT, FULL HOUSE, STRAIGHT FLUSH with tier multipliers + suited
    bonus), combo-scaled on-hit riders, and the DA1 POISON DAGGER starter
    card with flat 1-HP poison ticks.  Open: more statuses, finer-grained
    status telemetry.

### HARNESS REGRESSION POST-MORTEM (resolved 2026-08-26)

Three scenarios (`patrol_slime_cross`, `patrol_enemy_bumps_player`,
`battle_multi_enemy_cycle_kill`) regressed after the SDCC pointer-cache
hardening.  Root cause, established by clean-worktree A/B builds and an
mGBA watchpoint (NOT by inspecting a dirty working tree — stale mixed
objects mislead two earlier diagnoses, see AGENTS.md §52.2/§52.13):

* The SDCC 4.4.1 pointer-cache miscompile family is layout-sensitive:
  which instance fires depends on where the linker places neighboring
  banked bodies.  Each codegen knob flipped a different instance:
  - global `--max-allocs-per-node12000` + volatile `g_bk_*` staging
    globals: patrol stepping died entirely (zero movement);
  - function-local volatiles alone (battle/screen/UI): HP transition bug
    fixed BUT the volatile-parameter codegen in `ui_battle_content.c`
    shifted bank-3 layout and broke patrol step-interval persistence —
    watchpoint showed `ai_timer` being zeroed every frame from inside
    the patrol body's own window, so actors moved continuously instead
    of pausing between legs;
  - capping allocs in the UI unit instead re-exposed the HP corruption.
* Final configuration (all constraints satisfied simultaneously):
  function-local volatile guards everywhere (`volatile Battle *vb`,
  `volatile Game *vg`, `const volatile Battle *` render params), plain
  non-volatile `g_bk_*` globals, default optimization flags globally,
  plus per-file `-Wf--max-allocs-per-node500` overrides for
  `src/world/world.c` and `src/world/patrol_banked.c` (Makefile rules;
  both debug and release must stay in sync).
* Validation on that config: 144/144 harness, lint/memmap/verify-oam/
  ROM checks clean, full auto-play battle preserves hp=10 across 1500
  frames, patrol interval honored.
* Hardening kept: `patrol_enemy_bumps_player` wait 33->120 (sat exactly
  on the engagement boundary); `patrol_slime_cross` converted to
  ACTOR_STATE_CHANGE payload assertions (position-at-frame raced the
  first commit boundary even at baseline).
* Tooling note: `make test-scenario` returns rc=2 both for assertion
  failures and for missing scenario files; `python3 tools/dev.py scenario`
  now exits 2 (not-found) vs 1 (fail) and suggests close names.  Do not
  treat make's rc alone as evidence.

### DONE

- Card battle prototype (hand/deck/discard, draw, play, resolve; per-card
  energy costs with a 5-point pool refreshed each decision phase — see
  `docs/deck.md`)

### LATER

- `ACTOR_FLAG_BLOCKING` (src/world/actor.h) is emitted by the compiler and
  editor but never read by the ROM: `world_try_begin_move` blocks on any
  `g_static_actors` entry (`actor_find_at`) regardless of the flag, so a
  non-hostile compiled actor blocks even without `BLOCKING`.  The
  walkthrough planner mirrors that actual behavior (every non-hostile
  compiled actor is avoided) rather than the flag.  Decide later: honor
  `BLOCKING` in the engine (a gameplay change) or drop it from the flag
  vocabulary.
- Dialogue boxes: raise `MAX_DIALOGUE_LINES` beyond 8 for richer NPC /
  signpost text — constraints and deferred checklist in
  `docs/dialogue-boxes.md`.
- Extract a reusable template repo from this codebase
- Start the actual game repository on top of the foundation

The detailed plan below is the historical record of how the foundation was
built (every section marked DONE).  The forward-looking items live in the
status lists above; see the detailed sections for what each entails.

---

## 1. Objective — DONE

Implement a reusable, Game Boy-friendly RPG state layer that becomes the canonical source of persistent gameplay state.

The system should answer:

* Where is the player?
* Who is in the party?
* What items does the party possess?
* Which story/world flags have been set?
* What numeric progression variables exist?
* What persistent changes have happened to the world?

It should **not** yet implement quests, scripting, save hardware, sophisticated character progression, or the card system.

The guiding principle is:

> **Store state generically; keep game rules in systems that operate on that state.**

The resulting architecture should be usable by the current prototype and plausibly reusable by a future Game Boy RPG.

---

# 2. Architectural Target — DONE

The target architecture is:

```text
                    GameState
                       │
       ┌───────────────┼────────────────┐
       │               │                │
       ▼               ▼                ▼
 SceneState       PartyState       WorldState
       │               │                │
       │               ├── CharacterState[]
       │               │
       │               └── InventoryState
       │
       └──────────────────────────────────
                       │
                       ▼
              ProgressionState
                ├── Flags
                └── Variables
```

More concretely:

```text
GameState
├── SceneState
├── PartyState
├── InventoryState
├── FlagState
├── VariableState
└── WorldState
```

Keep these structures **plain C structs**.

Do not introduce classes, dynamic allocation, generic containers, reflection, scripting languages, or an ECS.

---

# 3. Establish the Core `GameState` — DONE

Create something along the lines of:

```c
typedef struct {
    SceneState scene;
    PartyState party;
    InventoryState inventory;
    FlagState flags;
    VariableState variables;
    WorldState world;
} GameState;
```

There should be exactly one authoritative game state for the running game.

The existing game systems should gradually stop owning their own duplicated representations of persistent state.

For example, avoid having:

```text
game.c        → current scene
world.c       → current scene
debug.c       → current scene
```

Instead:

```text
GameState.scene.scene_id
```

becomes authoritative.

---

# 4. Scene State — DONE

Create:

```c
typedef struct {
    SceneId scene_id;
    uint8_t player_x;
    uint8_t player_y;
    uint8_t player_facing;
} SceneState;
```

This represents **runtime state**, not the definition of the scene.

The distinction must remain:

```text
SceneDefinition
    static content
    map
    terrain
    actors
    exits
    music

SceneState
    mutable state
    current scene
    player position
    player facing
```

Do not put scene maps inside `GameState`.

Do not duplicate `SceneDefinition` data inside `SceneState`.

---

# 5. Party State — DONE

Introduce a small party model.

Initially:

```c
#define MAX_PARTY_MEMBERS 4

typedef struct {
    uint8_t id;
    uint8_t level;
    uint16_t experience;
    uint16_t hp;
    uint16_t max_hp;
} CharacterState;

typedef struct {
    CharacterState members[MAX_PARTY_MEMBERS];
    uint8_t count;
} PartyState;
```

The exact fields can be adjusted to match the current combat system.

### Important distinction

Keep character **definitions** separate from character **state**.

For example:

```text
CharacterDefinition
    name
    base stats
    visual
    abilities

CharacterState
    id
    level
    XP
    HP
```

Don't create `CharacterDefinition` yet if the existing prototype doesn't need it.

The important architectural rule is simply:

> Static identity/content must not be confused with mutable runtime state.

---

# 6. Inventory State — DONE

Introduce a minimal inventory representation.

```c
#define MAX_INVENTORY_ITEMS 32

typedef struct {
    ItemId item_id;
    uint8_t quantity;
} InventoryEntry;

typedef struct {
    InventoryEntry entries[MAX_INVENTORY_ITEMS];
    uint8_t count;
} InventoryState;
```

Provide operations such as:

```c
bool inventory_add(InventoryState *inventory,
                   ItemId item_id,
                   uint8_t quantity);

bool inventory_remove(InventoryState *inventory,
                      ItemId item_id,
                      uint8_t quantity);

uint8_t inventory_count(const InventoryState *inventory,
                        ItemId item_id);
```

### Important constraint

Do **not** build:

* equipment
* crafting
* durability
* item modifiers
* shops
* item scripting
* randomized item properties

Those are game features.

The foundation only needs to answer:

> Does the player possess this item, and how many?

---

# 7. Flags — DONE

This is one of the most important systems in the entire foundation.

Introduce a generic `FlagId`.

For example:

```c
typedef uint16_t FlagId;
```

Then a fixed-size storage representation appropriate for the Game Boy.

The public interface should conceptually provide:

```c
void game_flag_set(GameState *state, FlagId flag);
void game_flag_clear(GameState *state, FlagId flag);
bool game_flag_is_set(const GameState *state, FlagId flag);
```

Potential flags might eventually include:

```text
FLAG_MET_MAYOR
FLAG_DEFEATED_FIRST_SLIME
FLAG_CASTLE_GATE_OPEN
FLAG_INTRO_COMPLETE
```

But don't encode game-specific semantics into the flag system itself.

The state system knows:

```text
flag 37 = true
```

The game knows:

```text
flag 37 = FLAG_MET_MAYOR
```

---

# 8. Variables — DONE

Create a generic numeric variable system alongside flags.

For example:

```c
typedef uint16_t VariableId;
```

with:

```c
int16_t game_variable_get(
    const GameState *state,
    VariableId variable
);

void game_variable_set(
    GameState *state,
    VariableId variable,
    int16_t value
);

void game_variable_add(
    GameState *state,
    VariableId variable,
    int16_t amount
);
```

Potential uses:

```text
VAR_GOLD
VAR_CHAPTER
VAR_SLIMES_DEFEATED
VAR_MAYOR_DIALOGUE_STAGE
```

Again, the generic state system shouldn't care what these mean.

---

# 9. World State — DONE

This is the bridge between your new state system and the `WorldActor` architecture.

Create a small persistent-state representation for actors/world objects.

Conceptually:

```c
typedef struct {
    uint16_t actor_id;
    uint8_t state;
} PersistentActorState;
```

with a fixed-size collection.

For example:

```text
MAYOR
    state = NORMAL

SLIME_001
    state = DEFEATED

CHEST_003
    state = OPENED
```

The exact representation should be kept deliberately minimal.

## Critical distinction

Do not replace `WorldActorRuntime` with `PersistentActorState`.

Instead:

```text
WorldActorDefinition
        +
PersistentActorState
        ↓
WorldActorRuntime
```

The runtime actor is what the world needs to operate.

Persistent state is what survives scene reloads.

---

# 10. Actor IDs — DONE

This implementation will expose an important requirement:

**World actors need stable IDs.**

You need to distinguish:

```text
SLIME_001
SLIME_002
SLIME_003
```

rather than merely:

```text
SLIME
```

Otherwise you cannot eventually say:

```text
SLIME_002 = defeated
```

while:

```text
SLIME_003 = alive
```

Introduce a stable `ActorId`.

The ID should identify a particular persistent world actor, not merely its type.

Keep actor type/definition identity separate from instance identity.

---

# 11. State Initialization — DONE

Implement:

```c
void game_state_init(GameState *state);
void game_state_reset(GameState *state);
```

`game_state_init()` should establish a valid default starting state.

For example:

```text
scene = FIELD
player = starting position
party = initial party
inventory = initial inventory
flags = cleared
variables = default values
world = default world state
```

`game_state_reset()` should restore the canonical initial state.

Avoid having individual systems independently initialize pieces of the RPG state.

---

# 12. State Ownership Rules — DONE

This is important enough to document.

### `GameState` owns mutable persistent gameplay state. — DONE

### `SceneDefinition` owns static scene content. — DONE

### `WorldActorDefinition` owns static actor content. — DONE

### `WorldActorRuntime` owns temporary runtime information. — DONE

### `BattleState` owns temporary battle state. — DONE

For example:

```text
GameState
    FLAG_MET_MAYOR = true

SceneDefinition
    Mayor exists at (8,5)

WorldActorRuntime
    Mayor currently at (8,5)

BattleState
    temporary combat HP/turn/card state
```

This prevents the codebase from turning into one giant mutable structure.

---

# 13. Keep Battle State Separate — DONE

Do **not** put the entire battle system into `GameState`.

Eventually:

```c
typedef struct {
    ...
} BattleState;
```

will represent:

```text
current combatants
turn
temporary HP
battle phase
cards
damage
etc.
```

When combat ends, only the appropriate results should be written back to `GameState`.

Conceptually:

```text
GameState
    ↓
enter battle
    ↓
BattleState created
    ↓
combat
    ↓
results applied
    ↓
BattleState destroyed
```

This will be especially important when the card system arrives.

---

# 14. Integrate With the Existing World — DONE

Once the structures exist, migrate the existing runtime gradually.

Do **not** rewrite everything in one commit.

Recommended order:

### Phase A — DONE

Create the state structures and unit-style operations.

No gameplay behavior changes.

### Phase B — DONE

Make `SceneState` authoritative.

### Phase C — DONE

Make party state authoritative.

### Phase D — DONE

Add inventory state.

### Phase E — DONE

Add flags/variables.

### Phase F — DONE

Connect persistent actor state.

At every phase:

```text
make test-harness
make test
```

must remain green.

---

# 15. Integrate With the Debug Harness — DONE

This is where the project becomes particularly powerful.

Extend the scenario format so a scenario can specify initial RPG state.

Initially support:

```json
{
  "initial_state": {
    "scene": "TOWN"
  }
}
```

Then gradually:

```json
{
  "initial_state": {
    "scene": "TOWN",
    "flags": {
      "MET_MAYOR": true
    },
    "variables": {
      "GOLD": 100
    }
  }
}
```

Eventually:

```json
{
  "initial_state": {
    "scene": "TOWN",
    "player": {
      "x": 8,
      "y": 6
    },
    "flags": {
      "MET_MAYOR": true,
      "CASTLE_GATE_OPEN": false
    },
    "variables": {
      "GOLD": 100
    },
    "party": {
      "HERO": {
        "level": 3,
        "hp": 24
      }
    }
  }
}
```

Don't implement every field immediately.

Build the protocol incrementally.

---

# 16. Add State Telemetry — DONE

The LLM harness needs enough information to understand state.

Whenever a scenario starts, telemetry/debug output should be able to expose something like:

```text
STATE
scene=TOWN
player=(8,6)
flags=MET_MAYOR
gold=100
party_count=1
```

More importantly, state-changing operations should emit meaningful events.

For example:

```text
FLAG_SET MET_MAYOR
VARIABLE_SET GOLD 150
ITEM_ADD POTION 1
SCENE_CHANGE TOWN -> FOREST
ACTOR_STATE_CHANGE SLIME_001 DEFEATED
```

Don't dump the entire `GameState` every frame.

Telemetry should be **event-oriented**.

---

# 17. Add Assertions — DONE

The harness should eventually be able to express:

```text
assert scene == TOWN
assert flag MET_MAYOR == true
assert variable GOLD == 150
assert item POTION == 1
assert actor SLIME_001 == DEFEATED
```

This makes the RPG state system directly testable.

For the LLM, that's far better than:

> "Look at the screenshot and determine whether the Mayor seems to have disappeared."

---

# 18. Add State Snapshots — DONE

Your existing debug protocol should gain a canonical state snapshot.

The important property is:

> **The same state should be represented consistently everywhere.**

For example:

```text
GAME_STATE
scene=TOWN
player=8,6
facing=UP

PARTY
0:HERO level=3 hp=24/30 xp=120

INVENTORY
POTION=3
GOLD=150

FLAGS
MET_MAYOR
INTRO_COMPLETE

VARIABLES
CHAPTER=1

WORLD
SLIME_001=DEFEATED
```

This should be optimized for **LLM readability**, not human graphical debugging.

---

# 19. Add Regression Scenarios — DONE

I'd specifically create scenarios for:

### State initialization — DONE

```text
fresh game
→ expected default state
```

### Flag operations — DONE

```text
set flag
→ assert true
clear flag
→ assert false
```

### Variables — DONE

```text
set
add
read
```

### Inventory — DONE

```text
add item
remove item
quantity
```

### Party — DONE

```text
initial member
level
HP
XP
```

### Actor persistence — DONE

```text
defeat actor
leave scene
return
→ actor remains defeated
```

### Scene persistence — DONE

```text
move to scene
change state
leave
return
→ state preserved
```

### Complete state fixture — DONE

Create one scenario that initializes a nontrivial `GameState` and verifies the whole snapshot.

That scenario becomes extremely valuable as the architecture evolves.

---

# 20. Save/Load Preparation — But Not Implementation Yet — DONE

We should design `GameState` so that it can eventually be serialized.

Don't implement battery RAM/save slots yet.

But establish:

> **If a piece of state is part of `GameState`, it is potentially saveable.**

Conversely:

> **If something is temporary runtime state, it should not automatically become part of the save format.**

This gives us a clean future boundary:

```text
GameState
    ↓
Save serialization
    ↓
SRAM
```

The save system can come later.

---

# 21. Memory Constraints — DONE

Because this is Game Boy development, the implementation must remain deliberately fixed-size.

Avoid:

```c
malloc()
calloc()
realloc()
```

for the RPG state layer.

Prefer:

```c
#define MAX_PARTY_MEMBERS 4
#define MAX_INVENTORY_ITEMS 32
#define MAX_FLAGS ...
#define MAX_VARIABLES ...
#define MAX_PERSISTENT_ACTORS ...
```

The exact limits should be chosen based on actual ROM/RAM requirements.

The important thing is that memory usage is predictable.

We should also keep the state structures small enough that copying `GameState` is not accidentally expensive.

---

# 22. File Organization — DONE

I'd suggest something along these lines:

```text
src/rpg/
    state.c
    state.h
    party.c
    party.h
    inventory.c
    inventory.h
    progression.c
    progression.h
    world_state.c
    world_state.h
```

Potentially:

```text
src/rpg/state.h
```

owns the top-level `GameState`.

Don't split every tiny structure into its own file just for the sake of organization.

The objective is discoverability.

An agent should be able to answer:

> "Where is persistent RPG state defined?"

by opening:

```text
src/rpg/state.h
```

---

# 23. Update `AGENTS.md` — DONE

Once implemented, add explicit rules such as:

### Canonical state — DONE

> `GameState` is the authoritative source for persistent gameplay state. Do not create parallel persistent representations in individual systems.

### Definition/runtime separation — DONE

> Static definitions must not be used as mutable persistent state.

### Temporary state — DONE

> Battle/UI/input state must remain separate from persistent `GameState` unless it represents a gameplay result that survives the temporary state.

### Debugging — DONE

> New persistent gameplay state must be observable through the debug protocol and injectable through scenarios where practical.

This is particularly important given that the project is being developed by LLM agents.

---

# 24. Update `DEBUG_PROTOCOL.md` — DONE

Add sections for:

```text
STATE INITIALIZATION
STATE SNAPSHOT
FLAGS
VARIABLES
INVENTORY
PARTY
PERSISTENT ACTORS
STATE ASSERTIONS
```

The protocol should explain both:

1. how an agent requests/sets state;
2. how the game reports state back.

The protocol should remain **semantic**, not implementation-specific.

For example:

Good:

```text
FLAG_SET MET_MAYOR
```

Less useful:

```text
RAM[0xC042] = 0x01
```

The latter can exist as low-level diagnostics, but should not be the primary agent interface.

---

# 25. Definition of Done — DONE

I would consider RPG State Foundation v1 complete when:

* `GameState` is the canonical persistent state.
* Scene/player state is represented by `SceneState`.
* Party state exists independently of battle state.
* Inventory state exists.
* Generic flags exist.
* Generic variables exist.
* Persistent actor state has a defined representation.
* World actors have stable IDs.
* Battle state remains separate.
* State can be initialized deterministically.
* The debug harness can inject core state.
* The debug harness can inspect core state.
* State changes emit useful telemetry.
* State assertions work.
* At least ~10–15 regression scenarios exercise the system.
* `make test-harness` passes.
* `make test` passes.
* The release ROM still builds.
* mGBA remains the primary development/debug validation environment.
* Gambatte/SameBoy compatibility checks continue to work where relevant.

And critically:

> **No gameplay feature should have to know how the underlying state is stored.**

---

# What comes immediately after

Once this is complete, I'd resist jumping directly into cards.

The natural sequence becomes:

```text
                    CURRENT
                       │
                       ▼
              RPG State Foundation
                       │
          ┌────────────┼────────────┐
          ▼            ▼            ▼
       Items       Progression    Events
          │            │            │
          └────────────┼────────────┘
                       ▼
                  Save / Load
                       │
                       ▼
                Battle Expansion
                       │
                       ▼
                  Card System
```

That gives us a very strong RPG substrate before introducing the most game-specific part of your original idea.

---

# Detailed roadmap (per-area status)

## 1. RPG State Foundation v1 — DONE

Create the canonical `GameState` with scene, party, inventory, flags, variables, and persistent world state. Integrate it with the deterministic debug harness so an LLM can construct, inspect, and assert arbitrary RPG states.

## 2. Items & Inventory — DONE

Build the first genuinely reusable item system on top of `InventoryState`, starting with simple consumables and leaving equipment/crafting for later.

## 3. Progression — DONE

Add character XP, leveling, stats, and other progression primitives without baking a particular game's progression rules into the foundation.

## 4. Scripted Events — DONE

Build a lightweight event system using scenes, actors, flags, variables, dialogue, and transitions — enough to express RPG sequences without creating a general-purpose scripting language.

## 5. Foundation Reusability (content decoupling) — DONE

The vertical slice (Mayor quest, sword, boss, ending) is a complete RPG, so the
question became whether the architecture is reusable for a second game rather
than a pile of special cases.  All game content now lives in `src/game/` and is
registered with a generic engine:

* `src/game/game_ids.h` — named story-flag/variable/currency semantics (the
  engine stores generic slots; the game names them).
* `src/game/events.c`, `dialogue.c`, `actors.c`, `items.c` — the content tables,
  registered via `event_init` / `dialogue_register` / `actor_register_tables` /
  `item_register_defs`.
* `src/game/shops.{h,c}` — per-shop stock lists (`shop_id` on the actor def).
* `src/game/content.c` — `game_new_game` (initial state), `game_screen_after_victory`
  (the battle screen no longer knows about endings), and the moved
  `game_hero_attack` / `game_on_level_up` hooks.
* `state.c` is fully generic: `game_state_zero`; the starting state moved out.

Removed hard-coded game bits: the `actor_enemy_name` switch (enemy names now live
on the actor definitions as `display_name`), the shop's `ITEM_POTION` hardcode,
and the `ENDING_SHOWN` check in `battle_screen.c`.  Verified behavior-neutral:
82/82 scenarios, `make lint`, `make test`, and the save-boundary roundtrip.

## 6. Second Quest — Proving the Abstraction — DONE

The Lost Merchant quest (fetch/deliver/unlock) is structurally different from the
kill-counter: find the Amulet in the Forest, deliver it to the Merchant in Town,
receive a gold reward, and unlock his shop (which sells NUT).  Built entirely
from game data — event entries, dialogue, item defs, actor defs, a shop stock
list — with no game-specific branches in the engine.

The second quest forced three generic engine primitives, each expressed as data
and reusable by any game:

* `EVENT_COND_ITEM_COUNT` — an event condition on inventory count;
* `EVENT_ACTION_ADD_CURRENCY` / `EVENT_ACTION_REMOVE_ITEM` — reward/take actions;
* `shop_id` on `WorldActorDefinition` — per-actor shops.

New scenarios: `merchant_quest_start`, `merchant_pickup`, `merchant_deliver`,
`merchant_shop_open`, `merchant_repeat` (87 scenarios total).

## 7. Quest Abstraction — DONE (with known gaps)

Quests are a first-class registered engine module: the vocabulary
(`QuestDefinition`, `QuestStatus`) and the status derivation live in
`src/core/quest.{h,c}` and are registered via `quest_init()`, while the game
layer supplies its quest table (`src/game/quests.c`, `game_quest_register()`).
Quest behavior (starts, progress, completion) is expressed by the event table,
quest state lives in generic variables/items/flags, and the QUEST menu renders
the registry automatically.

Known quest-system gaps (reference — abstract further only when a new quest
archetype proves the need):

* linear 0/1/2 status only — no multi-stage / ordered-objective quests
  ("talk to A, then B, then return"); the menu cannot show "stage 2 of 3";
* one progress counter per quest — cannot express "collect 3 of A and 2 of B";
* no per-quest ACTIVE hint text (LOST AMULET shows "active", not "return it");
* the "key unlocks a location/exit" archetype is unexpressible — the event
  engine has no condition on scene transitions or locked tiles;
* soft coupling: a quest's `status_variable` must match the variable the
  events mutate; no validation catches a mismatch;
* no repeatable / cycling quests.

## 8. Save / Load — DONE

Battery-backed SRAM save/load of the canonical `GameState` in a versioned
format (`{magic, version, checksum, state}`), with the debug `save`/`load`
actions and the `save_load_roundtrip` harness scenario.  See
`docs/save-format.md`.  (Power-cycle persistence is emulator/hardware
behavior, verified manually.)

## 9. Battle Expansion — PARTIAL

Strengthen the existing battle system now that it has a proper persistent RPG state to interact with.
Missing:
* per-enemy attack/stats and AI (enemy attack is still hardcoded to 2; all enemies hit identically);
* damage variance / critical hits (the deterministic RNG is unused in battle);
* defense and stats beyond HP/attack;
* a battle command menu (attack/skill/item/flee) instead of the current A/B/START shortcuts;
* multiple enemies / groups and speed-based turn order;
* status effects;
* richer rewards (XP/leveling from battle, loot);
* flee chance / consequences.
* **data-driven battle HUD (Option B) — DONE:** `screens/battle/*.json`
  compiles to `BattleScreenDef`, and the ROM now reads it: `battle_start`
  dispatches a bank-4 body (`battle_hud_load_banked`, co-located with
  `g_battle_screens`) that stages positions + ticks + HUD rows into the
  WRAM `g_battle_hud` cache (`BattleHudCache`, `battle_data.h`), and the
  bank-3 renderer (`ui_battle_content.c`) draws every row/column from the
  cache.  Boss fights (`BATTLE_NONE`) select the `boss` screen, everything
  else `default`; `ambush`/`duo` remain compiled-but-unselected future
  variants.  All 4 battle JSONs are canonicalized to the ROM geometry and
  `make screens-check` pins generated C to source.  The editor preview
  still mirrors the same geometry with `hud_layout` reserved.  Validated:
  181/181 harness, `make test`, `make verify-oam`, `make memmap` OK
  (`make lint` has one pre-existing `rng_next` warning in untouched
  `battle.c` status code).
* **combat-art meta-tiles — DONE (ROM + compiler + editor):** enemies draw
  combat art from data, not hardcoded sets.  `screens/combat_art/*.json`
  declares named WxH sets (tiles, dims, palette, order;
  `combat_art.schema.json`); `battle_compile.py` emits blob offsets +
  dims into `battle_types.c` and the gfx `--tile-coords` order, replacing
  the hardcoded `ART_SETS`/Makefile coord literal (`battle_enemy_art.h`
  regenerates byte-identical).  The ROM loader stages per-slot geometry +
  cumulative VRAM bases (128-tile budget cap, text fallback past it) and
  the bank-3 stamper draws WxH from the cache with mult-free arithmetic
  (8-bit `*` would pull mult routines into fixed `_CODE`, §52.18).  Art
  resolves per battle through the game layer
  (`game_battle_enemy_type_id`, bank 4, same-bank call: fixed bank pays
  nothing).  The Combat Art Studio (editor toolbar) composes meta-tiles up
  to 6x4 from sheet tiles with frame-0/1, assigns combat art per enemy
  type, and the battle preview renders real meta-tiles per slot.  New sets
  append at the end (blob offsets stay stable).  Validated: 181/181
  harness, `make test`, `make verify-oam`, `make memmap` OK.
* **enemy overworld sprites — DONE (shared, type-owned):** the Enemies
  view (level dropdown) defines one transparent-background overworld
  sprite per enemy type (`screens/enemy_types`
  `overworld.cells/palette`, with optional `width`/`height` for multi-tile
  grids like the 2x2 boss), applied everywhere the type is placed.
  Pixels live in the shared `assets/enemy_sprites.png` (composed from
  `public/tiles/enemies/`); the gfx rule + compiler `--ow-coords` build the
  OAM blob (base 100, 28-tile budget, enemies-only: the hero uses its own
  `HERO_DESOLATE` sprite) and per-type base/frames/w/h/palette in
  `battle_types.c`.  Spawn resolves the type by the `ENTITY_ID_X`
  convention (explicit `enemy_type` wins) into a new actor `ow_type`
  field; the OAM writer reads the bank-4 row directly (`SPRITE_KIND_ENEMY`),
  writing a w*h grid of OAM entries for multi-tile sprites and falling back
  to ASCII.  Per-instance `overworld_sprite` names are ignored for typed
  enemies (Inspector says so).  Validated: 181/181 harness, `make test`,
  `make verify-oam` (boss asserted as a 2x2 OAM grid), `make memmap`,
  `screens-check` + `levels-check` OK.

### 9.1 Deck management UI — DONE

The START quick screen's CARDS tab manages collection vs deck through the
real deck mutators (paired rows whose membership glyph is the decked-copy
count, A adds one copy and clears every copy once the card is fully decked,
SELECT detail page, inline FILTER/SORT picker), and new games grant a
5-card starter deck so battles draw from the real system from the first
fight.  Spec: `docs/deck-management.md`; implementation deviations:
`docs/deck.md` §40.  Known gaps: picker LEFT/RIGHT both cycle forward
(relabelled hint; true bidirectional cycling is future polish), AMULET
hidden from the list, QUEST detail page is a placeholder.
Note: the fixed-bank budget is tight (~88 B headroom after the review-fix
round; `make memmap` enforces the invariant) — the next substantial
feature must plan banked placement (§52.11.1) up front.

## 10. Card System

Finally introduce the Baten Kaitos-inspired card mechanics on top of the stable RPG/battle foundation rather than allowing the card system to dictate the architecture.

**Guiding principle for the whole roadmap:** build only the abstractions that the current RPG actually proves it needs, while making every important state deterministic, observable, and testable by an LLM.

## 11. Content expansion (14-level demo)

The demo grew from 6 to 14 connected rooms on the current branch: the
forest branch (`forest_glade` → `forest_deep` → `forest_grove`,
`forest_shrine`), a desolate spur (`south_ridge`), and the castle chain
(`castle_entry` → `castle_hall` → `castle` → `throne_room`, with the
Lord of Slimes relocated to the throne room).  Reused enemy types with
per-placement HP/gold variants; advice is carried by signpost `?` NPCs;
no new entities/quests/assets.

Memory note (two waves):
1. The real scene table lives in bank 5 and the actor table in bank 2,
   both of which were nearly full.  To fit the new rooms, the release build
   moves the title-logo tiles + loader to bank 2 (spare after wave 2),
   while the harness build keeps them in bank 5 because moving them flips a
   layout-sensitive SDCC dialogue miscompile (AGENTS.md §52.19 — verified
   via `make verify-oam`).  The fixed title wrapper selects the matching
   bank at compile time.
2. Enemy wave: bank 2 (actor table) had only ~57 B left, so the **actor
   tables + loader moved to bank 4** (`GAME_ACTOR_BANK`, `--actors-bank 4`,
   `actor_load_banked.c #pragma bank 4`) — the debug build already linked
   the fixture actors in bank 4, so the harness is untouched.  Bank 2 then
   held ~3 KB spare, enough for the extra enemies while bank 4 keeps ~1 KB.
   The per-scene actor-table count is now **generated**
   (`g_actor_table_count`, read by the bank-4 loader) -- the previous
   hardcoded `GAME_ACTOR_TABLE_COUNT 6` silently dropped maps 8+ (the new
   rooms spawned no enemies).  `walk_sweep` now asserts each combat level
   spawns ≥1 hostile, so that regression cannot recur.
   Current release headroom: bank 2 ≈2.1 KB, bank 4 ≈1.1 KB, bank 5 ≈138 B.

## 12. new-levels merge (tunnels branch, Sep 2026)

`origin/new-levels` (one commit on a stale base: `castle_entry` redesign,
new `village_area` 17 + `desolate_field` 18) was cherry-picked onto
`tunnels` (linear history). Conflict in `castle_entry.json` resolved:
tunnels `neighbors` + enemies kept, new terrain/name/spawn taken, invalid
`castle.forest_plain_floor_1` (287 cells) fixed to `castle.castle_plain_floor`,
south gate opened at x=9,10, desolate `actor_id` 60→64 collision minted.

Bank-5 findings (release link is ground truth, `build/kaartenheld.map`
`l__CODE_5`): base headroom was 144 B; the two new levels add ~1576 B
(149+139 terrain blocks — dense single-cell detail vs 20–66 for existing
levels). No lossless fit exists: all other bank-5 residents are immovable
(asset/atlas/tiles/oam/scene-load, smallest mover exceeds every headroom)
and a merge post-pass saves 0 blocks (greedy output already maximal).

Landed instead:
1. Compiler elides interior default-ground blocks (`optimize_terrain`
   generalizes the dead `TILE_FLOOR` fossil to each level's own
   `level_default_const`, with a perimeter guard — ROM pre-fills interior
   default, perimeter pre-fills WALL, open rows appended after are
   untouched). -131 blocks / -655 B permanently, marginally faster loads.
2. `compile.py` emits NULL+0 actor tables for object-less scenes (SDCC
   rejects `{}`) — `village_area` exposed it.
3. `castle_entry` redesign only. Release bank 5 headroom 144 B → 384 B.
   `village_area`/`desolate_field` stay on `origin/new-levels` (intact)
   until the world bank grows.

Tunnel sweep after merge: zero point↔point one-way pairs (only the
pre-existing `tunnel_mountain_pass_south_field` + lone
`town→grassy_forest`, whose return is an edge — out of scope, tunnels
cover point exits only). No conversions made.

Recorded wiring for the follow-up (verified geometrically, reverted for
budget): `town.east ↔ village_area.west` reciprocal edge pair, gate
y=8,9 (town (19,8),(19,9) wall→floor; village (0,8),(0,9) already open).
Follow-up options for `village_area`/`desolate_field`: paged scene tables
(`terrain_bank` + WRAM staging in `scene_load_tiles_banked`, transition
time only) or author-side detail diet (~30 blocks/level at current density).

