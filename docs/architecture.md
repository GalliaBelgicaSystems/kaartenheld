# Architecture

This repository is a small, reusable Game Boy RPG runtime with a deliberately
thin game/content layer.  The goal is that a different RPG can be built on the
same runtime by supplying new content, without editing the engine.

For operational rules (build commands, harness protocol, GBDK/CRT0 gotchas,
state ownership details) see `AGENTS.md` and `docs/DEBUG_PROTOCOL.md`.

## Module layout

```text
engine (generic runtime)                 game (this RPG's content)
-----------------------------------      ---------------------------
src/core/   boot glue, events,           src/game/
            dialogue, story, quests        game_ids.h    named ids
src/rpg/    GameState, items, inventory    content.c     new-game state, hooks
            currency, party, progression   events.c      scripted-event table
src/world/  world, scenes, actors,         dialogue.c    dialogue lines
            entities, interaction          actors.c      per-map actor definitions
src/battle/ combatants, battle flow        items.c       item catalog
src/screens/overworld, battle, dialogue,   quests.c      quest registry
            item, shop, ending, ...        shops.c       per-shop stock lists
src/ui/     shared menu frame, drawing
src/input/  joypad abstraction
src/audio/  music (timer-driven ISR)
src/debug/  telemetry, scenarios, rng, assertions
```

## Dependency direction

```text
src/core  src/rpg  src/world  src/battle  src/ui  src/input  src/audio
        \           |          |          /
         \          |          |         /
          generic APIs (engine headers)
         /          |          |         \
        /           |          |          \
   src/screens (game-facing presentation)   src/debug (harness)
                          ^
                          | game-layer hooks
                       src/game
```

Three boundary tiers:

1. **Strict** — `core/`, `rpg/`, `world/`, `battle/`, `input/`, `audio/` must
   never include game-layer headers (`content.h`, `game_ids.h`, `shops.h`) or
   branch on game ids.  Verified clean.  The two documented exceptions:
   * the **ID vocabulary** — the engine headers define only the ID *types*
     (`EntityId`/`EventId`/`DialogueId`/`ItemId` are `uint8_t`) and engine
     sentinels plus a per-game content range base (`*_FIRST_GAME = 0x80`).
     Game-specific values (`ENTITY_ID_MAYOR`, `ITEM_SWORD`, ...) are
     `#define`s in `src/game/game_ids.h` relative to those bases, so a
     second game defines its own ids there without touching the engine
     headers.  The generic ids (`FlagId`, `VariableId`, `CurrencyId`) are
     plain integers in `src/rpg/state.h`.
   * the composition root — `main.c` calls `game_content_init()` before
     `game_init()`, and `src/core/game.c` calls the `game_new_game` hook at
     boot (engine → game hook, the intended dependency inversion).
2. **Presentation** — `screens/` are game-facing: they call game-layer hooks
   (`game_hero_attack`, `game_screen_after_victory`, `game_shop_for_id`,
   `quest_at`/`quest_status`) and may read the game's named semantics for
   display (e.g. `CURRENCY_ID_GOLD` in the shop/status HUD).  They must not
   contain game *logic* (quest state machines, shop rules, post-battle
   decisions) — those live in `src/game` or are driven by data.
3. **Harness** — `debug/` observes and controls the game through real
   mechanics; scenario setup writes state directly into `GameState` without
   emitting gameplay telemetry.

## State ownership

| State | Owner | Mutators | Persistent | Serialized |
|---|---|---|---|---|
| Player HP/max | `state.party.members[0]` | battle, item_use, level-up | yes | yes |
| Current scene/pos | `state.scene` | scene_load / scene_sync_from_world | yes | yes |
| Inventory | `state.inventory` | inventory_add/remove, item_use, events | yes | yes |
| Currency | `state.currency` | currency_add, item_purchase | yes | yes |
| Equipment | `state.equipment` | item_equip | yes | yes |
| Flags | `state.flags` | game_flag_set/clear, events | yes | yes |
| Variables | `state.variables` | game_variable_set/add, events | yes | yes |
| Persistent actor defeats | `state.world` | world_on_battle_end | yes | yes |
| Progression | `state.progression` | progression_add | yes | yes |
| Quest status | derived from variables by `quest_status` | events | yes | (as variables) |
| World (runtime copy) | `g_game.world` | world_* | no | no |
| Battle | `g_game.battle` | battle_* | no | no |
| Dialogue | `g_game.dialogue` | dialogue_* | no | no |
| Screen/transient UI | `g_game.screen`, `item_menu_*`, `shop_*` | screens | no | no |

`GameState` is the single source of truth for anything a save would contain;
the save format serializes it wholesale (see `docs/save-format.md`).

## Content registration pattern

Every content system is an engine provider plus a game-layer `game_*_register()`
called from `game_content_init()`:

| Content | Engine | Game data |
|---|---|---|
| events | `event_init` (`src/core/event.c`) | `src/game/events.c` |
| dialogue | `dialogue_register` (`src/core/dialogue.c`) | `src/game/dialogue.c` |
| actors | `actor_register_tables` (`src/world/actor.c`) | `src/game/actors.c` |
| items | `item_register_defs` (`src/rpg/items.c`) | `src/game/items.c` |
| quests | `quest_init` (`src/core/quest.c`) | `src/game/quests.c` |
| shops | read via `game_shop_for_id` | `src/game/shops.c` |

Adding a new enemy, item, quest, shop, dialogue line, or scene content is a
`src/game/` change only.

## Events

The event engine is generic.  `INTERACT` and `MAP_ENTER` events resolve
first-match in table order; `ACTOR_DEFEATED` events run **every** matching
event so a specific defeat handler never suppresses generic defeat
bookkeeping.  Conditions: flag is/not set, variable == / >=, item count == / >=.
Actions: dialogue, set/clear flag, set/add variable, scene change, add/remove
item, add currency.

## Quests

A quest is a row in the quest registry (`src/game/quests.c`) plus event-table
entries.  Status is one variable encoded `0 = not started, 1 = active,
2 = complete` (thresholds declared per quest); the menu renders the registry
generically.  Known gaps (multi-stage, key-locked locations, hints,
repeatables) are logged in `docs/roadmap.md` — do not add machinery until a
real quest needs it.

## Battle boundary

The battle system operates on `Combatant` structs through
`battle_start` / `battle_execute_action` / `battle_update`; it knows nothing
about how the player's command was chosen (the future card system plugs in at
the screen layer, which feeds `battle_execute_action`).  Results flow back to
`GameState` through `world_on_battle_end` / `world_on_battle_fled`.  Enemy
stats are hardcoded (`enemy.attack = 2`); per-enemy stats are a roadmap item.

## Second-implementation test (verdict)

Every supposedly generic system has at least one content-only second example:

* **Quests** — Monster Hunt (kill counter) and Lost Merchant (fetch/deliver/
  unlock) share the event engine + quest registry, zero engine changes.
* **Actors** — enemies (slime/bat/boss) and NPCs are data; the boss is a
  conditional-spawn content row.
* **Items / shops** — potions/NUT/sword/shop-keys and two per-shop stock lists
  are data.
* **Scenes / events / battles** — exits, dialogues, defeat counters are data.

No further abstraction is justified until a new game demonstrates otherwise.

## Debug harness

The debug ROM is deterministic: scenarios construct `GameState` from a
host-written descriptor, inputs are injected through the same joypad
abstraction, frames step synchronously, and telemetry + snapshots expose
semantic state.  Screenshots are a secondary check; semantic state is
authoritative.  See `docs/DEBUG_PROTOCOL.md` and `docs/testing.md`.
