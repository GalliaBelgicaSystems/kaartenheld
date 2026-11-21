# Game vs Engine

How to decide where a piece of functionality belongs.

## The two layers

* **Engine** (`src/core`, `src/rpg`, `src/world`, `src/battle`, `src/ui`,
  `src/screens`, `src/input`, `src/audio`, `src/debug`): generic Game Boy
  runtime + RPG systems.  It owns *how* things work and the shared ID
  vocabulary.
* **Game** (`src/game`): this RPG's content — tables, named ids, initial
  state, stat hooks, per-shop stock.  It owns *what exists*.

`GameState` (`src/rpg/state.h`) is the shared boundary: the engine defines
the storage, the game defines the semantics (which variable id means
"MERCHANT_QUEST", which flag means "MET_MAYOR").

## Questions to ask

For any new feature:

1. **Could a different RPG use this unchanged?** → engine.
2. **Is it specific to this game's story/world/content?** → `src/game`.
3. **Does the engine need to *know* a specific game id to do it?** → stop;
   that's a boundary crossing.  Express it as a hook the game provides
   (e.g. `game_screen_after_victory`, `game_hero_attack`) or as data the
   game registers (events, quests, shops, actors, items).

## Concrete boundaries

| Feature | Belongs in | Evidence |
|---|---|---|
| New enemy / NPC | `src/game/actors.c` | boss = conditional-spawn data row |
| New item | `src/game/items.c` | shop-keys/NUT added with no engine change |
| New shop / stock | `src/game/shops.c` | second shop via `shop_id` |
| New quest | `src/game/quests.c` + events | Lost Merchant, zero engine edits |
| New dialogue | `src/game/dialogue.c` | lines are data |
| New scene content | `src/game/` + scene data | exits/terrain per-map |
| Defeat bookkeeping | event table | `ACTOR_DEFEATED` all-match |
| Post-battle screen choice | `game_screen_after_victory` | battle screen stays generic |
| Hero stat derivation | `game_hero_attack` | engine never knows about SWORD |

## Anti-patterns

* A `switch` over `ENTITY_ID_MAYOR` / `ITEM_SWORD` / `QUEST_MONSTER_HUNT` in
  an engine file.
* An engine file including `content.h` / `game_ids.h` / `shops.h` (the only
  exception is the composition root, `main.c`).
* A quest/shop/menu hardcoding per-quest or per-shop strings/logic instead of
  iterating registered data.
* Silent coupling between a quest's registry `status_variable` and the
  variable its events mutate.

## How to add content (never touch the engine)

1. Define any new ids in `src/game/game_ids.h` as `#define`s relative to the
   engine's `*_FIRST_GAME` bases (`entity.h`/`event.h`/`dialogue.h`/`state.h`
   define only the sentinels and the range base).
2. Add the data row(s) in `src/game/`.
3. Register via `game_content_init()` if the content system needs it.
4. Add a scenario + assertions; run `make test-harness`.
