## Phase 0 — Establish the contract

**Goal:** Define the format that everything else will depend on.

Create:

```text
levels/
  schema/
    level.schema.json
    tileset.schema.json

  forest.json

tools/
  level_compiler/
    README.md
    validate.py
    compile.py
```

### Deliverables

Define the canonical level format:

```text
Level
├── id
├── name
├── map
│   ├── width
│   ├── height
│   └── tileset
├── layers
│   └── terrain
├── collision
├── player
├── exits
├── objects
└── regions
```

Also define the tileset format:

```text
Tileset
├── id
├── graphics
└── tiles
    ├── semantic ID
    ├── GB tile constant
    ├── walkable
    └── ...
```

**Important:** don't worry about the web editor yet.

### Success criterion

You can manually write:

```text
levels/forest.json
```

and validate it successfully.

---

# Phase 1 — Build the compiler first

This is the most important phase.

Take:

```text
levels/forest.json
```

and produce the existing:

```text
src/game/scenes_content.c
```

The compiler should understand the existing structures rather than changing the game engine.

Your pipeline:

```text
forest.json
     ↓
parse
     ↓
validate
     ↓
normalize
     ↓
optimize
     ↓
emit C
```

### Implement

```python
load_level()
validate_level()
resolve_tiles()
derive_collision()
optimize_terrain()
emit_scene_definition()
emit_exits()
```

For example:

```json
{
  "x": 4,
  "y": 2,
  "width": 2,
  "height": 1,
  "tile": "forest.tree"
}
```

should ultimately produce something equivalent to:

```c
{ 4, 2, 2, 1, TILE_WALL }
```

because that is the representation the current game already uses.

### Success criterion

Run:

```bash
python tools/level_compiler/compile.py levels/forest.json
```

and get valid C.

Then:

```bash
make
```

produces a working ROM.

**Do not proceed until this works.**

---

# Phase 2 — Make the compiler safe

Now add validation.

Things like:

```text
ERROR: Unknown tileset
ERROR: Unknown tile
ERROR: Map dimensions don't match tile data
ERROR: Player spawn outside map
ERROR: Player spawn is blocked
ERROR: Exit target doesn't exist
ERROR: Exit outside map
ERROR: Duplicate object ID
WARNING: unreachable object
```

I'd make validation usable independently:

```bash
python tools/level_compiler/validate.py levels/forest.json
```

Output:

```text
forest.json

✓ Schema valid
✓ Tiles valid
✓ Collision valid
✓ Player spawn valid
✓ Exits valid
✓ Objects valid

LEVEL VALID
```

### Success criterion

A deliberately broken level produces useful errors rather than a compiler crash.

---

# Phase 3 — Migrate one existing level

Now take **one existing map from the repository** and reproduce it in JSON.

Don't attempt the whole game.

Pick the simplest scene.

Convert:

```text
existing C scene
        ↓
   equivalent JSON
        ↓
      compiler
        ↓
   generated C
```

Then compare the resulting game behavior.

This is an extremely important test because it proves:

> **JSON → compiler → existing engine**

doesn't change the game.

### Success criterion

The migrated level behaves identically to the original.

---

# Phase 4 — Extract the tileset definitions

Now formalize what `tiles_content.c` currently knows.

The editor needs a machine-readable description of the tiles.

Create:

```text
tools/level_editor/tilesets/
    forest.json
    exterior.json
    interior.json
```

Something conceptually like:

```json
{
  "id": "forest",

  "tiles": [
    {
      "id": "floor",
      "label": "Floor",
      "gb_constant": "TILE_FLOOR",
      "walkable": true
    },
    {
      "id": "tree",
      "label": "Tree",
      "gb_constant": "TILE_WALL",
      "walkable": false
    },
    {
      "id": "gate",
      "label": "Gate",
      "gb_constant": "TILE_GATE",
      "walkable": true
    }
  ]
}
```

The important thing is that **the editor now knows nothing about C**.

It knows:

```text
forest.tree
forest.floor
forest.gate
```

The compiler knows how those map to C.

---

# Phase 5 — Build the tiny web editor

Now we finally build the UI.

I'd use:

```text
React
TypeScript
Vite
HTML Canvas
```

Something like:

```text
tools/level_editor/
├── package.json
├── vite.config.ts
└── src/
    ├── App.tsx
    ├── MapCanvas.tsx
    ├── TilesetPalette.tsx
    ├── Toolbar.tsx
    ├── LayerPanel.tsx
    ├── Inspector.tsx
    │
    ├── model/
    │   ├── Level.ts
    │   ├── Tileset.ts
    │   └── Objects.ts
    │
    └── io/
        ├── loadLevel.ts
        └── saveLevel.ts
```

### MVP UI

```text
┌───────────────────────────────────────────┐
│ Forest       [Save] [Load] [Validate]     │
├──────────────┬────────────────────────────┤
│ TILESET      │                            │
│              │                            │
│ 🟩 🟩 🌲 🌲  │        MAP                 │
│ 🟫 🟫 🚪 🪨  │                            │
│              │                            │
│              │                            │
├──────────────┴────────────────────────────┤
│ Layer: Terrain   Grid: ✓   Zoom: 200%    │
└───────────────────────────────────────────┘
```

And that's **all**.

No NPC editor.

No fancy AI.

No animation.

No multiplayer.

Just:

> select tile → paint map → save JSON.

---

# Phase 6 — Make editing feel good

Once basic painting works:

### Tools

* pencil
* eraser
* rectangle
* fill
* eyedropper
* undo
* redo

### Navigation

* zoom
* pan
* grid toggle
* map boundaries

### Keyboard shortcuts

```text
B  brush
E  erase
G  fill
I  eyedropper
Ctrl+Z
Ctrl+Shift+Z
```

### Success criterion

You can recreate the migrated level faster in the editor than manually editing JSON.

That's the point where the editor becomes genuinely useful.

---

# Phase 7 — Add collision visualization

Now add the derived collision system.

A button:

```text
[Terrain] [Collision] [Objects]
```

Selecting collision shows:

```text
░░░░████████░░░░
░░░░████████░░░░
░░░░████████░░░░
░░░░░░░░░░░░░░░░
```

where blocked cells are visually obvious.

But don't make the user paint collision by default.

Instead:

```text
tile
 ↓
walkable property
 ↓
collision
```

Collision is derived, never stored: there is no `collision.overrides`
section in the level format (removed — it had no compiler/ROM consumer).
Walkability comes solely from the tileset manifest's `walkable` flag.

---

# Phase 8 — Add exits

Create an editor tool:

```text
[Exit]
```

Click a map edge.

Inspector:

```text
EXIT

Position
X: 12
Y: 0

Target Scene
[ mountain_pass ▼ ]

Target Position
X: 12
Y: 10

Direction
[ East ▼ ]

[Apply]
```

This directly corresponds to the existing `SceneExit` concept.

Now the editor can visually display:

```text
                ↑ mountain_pass
                │
┌────────────────────────┐
│                        │
│          MAP           │
│                        │
│                        │
└────────────────────────┘
                │
                ↓ field
```

---

# Phase 9 — Add objects

Now introduce the semantic object layer.

Palette:

```text
OBJECTS

Player Spawn
NPC
Enemy
Chest
Door
Warp
Trigger
Item
```

Click:

```text
NPC
```

then click the map.

Inspector:

```text
NPC

ID:
forest_hermit

Character:
hermit

Position:
X 8
Y 6
```

JSON:

```json
{
  "id": "forest_hermit",
  "type": "npc",
  "position": {
    "x": 8,
    "y": 6
  },
  "properties": {
    "character": "hermit"
  }
}
```

---

# Phase 10 — Add regions + descriptions

This is where the editor starts becoming **LLM-native**.

Allow the designer to draw a rectangle and say:

```text
Region: North Clearing

Description:
A quiet clearing where the player meets the hermit.

Gameplay purpose:
Exploration

Difficulty:
1
```

JSON:

```json
{
  "id": "north_clearing",

  "bounds": {
    "x": 3,
    "y": 2,
    "width": 8,
    "height": 6
  },

  "description": "A quiet clearing where the player meets the hermit.",

  "gameplay": {
    "purpose": "exploration",
    "difficulty": 1
  }
}
```

This information costs essentially nothing but makes the resulting level vastly easier for an AI to understand.

---

# Phase 11 — Create the LLM representation

Don't make the LLM consume the raw optimized JSON.

Build:

```text
level.json
    ↓
describe_level.py
    ↓
level_description.json
```

Potential output:

```json
{
  "level": "forest",

  "size": "20x18",

  "connections": [
    "north → mountain_pass",
    "south → field"
  ],

  "player_start": [12, 10],

  "regions": [
    {
      "name": "north_clearing",
      "purpose": "exploration"
    }
  ],

  "objects": [
    {
      "type": "npc",
      "id": "forest_hermit",
      "position": [8, 6]
    }
  ]
}
```

Or even generate an LLM-friendly Markdown document:

```text
# FOREST

20×18 forest level.

## Connections

North → Mountain Pass
South → Field

## Important Objects

Hermit
Position: (8, 6)

## Design Intent

A quiet exploration area...
```

Now the AI gets **semantic information instead of implementation details**.

---

# Phase 12 — Add an LLM command layer

Only after the editor works should you expose operations to the AI.

Start tiny.

```text
create_level
paint_rectangle
place_object
move_object
delete_object
create_exit
set_spawn
describe_region
validate_level
```

For example:

```json
{
  "operation": "paint_rectangle",
  "tile": "forest.tree",
  "x": 3,
  "y": 2,
  "width": 5,
  "height": 4
}
```

The same operation should be executable by:

```text
human UI
    +
LLM
```

That's the crucial architectural property.

---

# Phase 13 — Connect it to the build

Add:

```bash
make level LEVEL=forest
```

which does:

```text
forest.json
    ↓
validate
    ↓
compile
    ↓
generate C
    ↓
make ROM
```

Eventually:

```bash
make level LEVEL=forest RUN=1
```

could:

```text
compile
→ build ROM
→ launch emulator
```

---

# Phase 14 — Close the loop with testing

This is where your existing project's LLM/testing infrastructure becomes valuable.

The eventual pipeline becomes:

```text
LLM creates level
        ↓
validator
        ↓
compiler
        ↓
ROM
        ↓
test scenario
        ↓
emulator
        ↓
semantic state
        ↓
LLM
```

So an agent could effectively reason:

```text
"I created a dungeon."

        ↓

"Can the player reach the exit?"

        ↓

"No, the corridor is blocked."

        ↓

"Move the wall."

        ↓

"Rebuild."

        ↓

"Now it works."
```

That is **much more interesting than merely having ChatGPT generate C code.**

---

# Recommended implementation order

If you want the shortest path to something working, I'd literally make the milestones:

| Milestone | Result                      |
| --------- | --------------------------- |
| **M1**    | `level.schema.json`         |
| **M2**    | Hand-written `forest.json`  |
| **M3**    | JSON → C compiler           |
| **M4**    | JSON-generated ROM works    |
| **M5**    | Migrate one existing level  |
| **M6**    | Tileset JSON                |
| **M7**    | Basic React editor          |
| **M8**    | Paint/save/load             |
| **M9**    | Collision visualization     |
| **M10**   | Exits                       |
| **M11**   | Objects                     |
| **M12**   | Regions/descriptions        |
| **M13**   | LLM-friendly export         |
| **M14**   | LLM editing API             |
| **M15**   | Build + emulator            |
| **M16**   | Automated LLM level testing |

### And I'd stop after M8 for the first prototype.

At M8 you'll already have:

> **Open browser → choose Forest → paint tiles → save → compile → get a Game Boy ROM.**

That's the first "holy shit, this actually works" milestone.

---

## The three rules I'd keep throughout

### 1. JSON is the source of truth

Never edit generated C.

```text
JSON → C
```

not:

```text
JSON ↔ C
```

### 2. Semantic IDs above the engine

Prefer:

```text
forest.tree
forest.floor
npc.hermit
```

over:

```text
TILE_WALL
SPRITE_17
ACTOR_04
```

The compiler handles that translation.

### 3. Human and AI use the same abstraction

Don't build:

```text
Human → editor → JSON
AI → C
```

Build:

```text
                 Level Model
                /           \
           Human UI          AI API
                \           /
                 JSON
                   ↓
                Compiler
                   ↓
                  ROM
```

**That's the architectural decision that makes the whole idea work.**

And I'd start with **M1–M4 before writing a single line of React**. If the JSON → C → ROM pipeline is solid, the web editor becomes "just" a nice graphical front-end to a well-defined system rather than the thing you're gambling the project on.

# Phase 15 — Two-way sync (JSON ⇄ C tables)

The pipeline runs in both directions, with C tables as the interchange
point (the `.gb` is never parsed):

```text
levels/*.json --compile.py--> scenes_content.c + actors_content.c
scenes_content.c + actors_content.c --decompile.py--> levels/*.json
```

Rules:

- `scenes_content.c` and `actors_content.c` are **generated files** (`make
  levels` writes both). Never hand-edit them; `make levels-check` fails on
  drift.
- Every actor row is fully specified by its JSON object (`actor_id`,
  `facing`, `flags`, `visual`, `hp`/`max_hp`, `gold_reward`,
  `reward_currency`, plus the original `entity_id`/`battle`/`ai`/...).
  `actor_id` is stable and unique across scenes: it drives persistent
  defeat tracking, so never renumber it.
- Every scene row carries its compiled player spawn (`spawn_x/y/facing`
  from `player.spawn`; facing accepts compass aliases).  `game_new_game`
  and `world_init` read the FIELD row -- no hardcoded coordinates -- via
  the branch-free banked helper `scene_spawn()` (bank-5 body
  `scene_spawn_banked`, same pattern as `scene_load_tiles`), keeping the
  fixed-bank `_CODE` budget intact (see AGENTS.md 52.18/55.5).
- `decompile.py` merges compilable sections back into the JSON, preserving
  editorial sections (`name`, `regions`, spawn `animation_frames`, terrain
  block `comment`s), decoration
  objects without an `entity_id`, and author art choices that compile to
  the same tile. Anything it cannot map is a loud error, never a silent
  invention. Known normalizations: terrain blocks roundtrip verbatim (one
  JSON block per C block); collapsed `TILE_FLOOR`/`TILE_WALL` map to
  per-tileset canonical art; floor-only patches are design docs (they
  compile to the default background).
- Gates: `make levels-check` runs `validate` + `compile --check`.
  JSON is the source of truth, so "JSON differs from decompiled-canonical
  form" is not an error and is not gated. `decompile.py` is kept as a
  recovery/forensics tool (regenerate JSON from hand-edited C, audit a
  build); its `--check`/`--roundtrip` modes are dev diagnostics for that
  purpose, using C-equality, not JSON-text equality.

# Phase 16 — Manifest-driven tile traits (JSON ⇄ ROM)

Tileset manifests (`tools/level_editor/tilesets/*.json`) are the single
source of truth for what each tile *is*, for the editor and the ROM alike:

- `vram_block`: ordered VRAM block composition (sheet coords + manifest
  cross-refs) with exactly one `exit: true` tile per world tileset.
  Checked by `make tiles-check`.
- `generate_tiles.py` inverts the manifests into `TileType` value space
  and emits `generated/tiles/` (untracked build artifacts):
  `tile_walk.h` (walkable ranges + accessor, consumed by `world.c` and
  the bank-3 patrol body from one source) and `tile_glyph.h` (glyph
  ranges + accessor for `ui.c`, plus per-tileset exit-art indices).
- Forest VRAM art comes from per-tile PNGs (`rpg_forest_floor.inc`,
  `rpg_forest_tree.inc`, `rpg_forest_exit.inc`) plus the exterior stump
  cells (`rpg_forest_stumps.inc`), composed in `tiles_content.c` — one
  rule per source, no coordinate lists, no concat tooling. (An earlier
  `blocks.mk` experiment was removed: per-PNG rules are self-describing.)
- Legacy tiles predate manifests (generic floor/wall/exit, old desolate
  set, stumps) and stay hand-listed in ROM code; the generator documents
  the frozen set. No new legacy tiles, ever: new content flows through
  manifests.
- Exit gates render per-tileset art (desolate 40, forest 8, castle 26)
  while `TILE_EXIT` remains the single semantic gate marker, so movement,
  patrol, and exit lookup are untouched.
- Merging artist drops: `merge_tileset.py` recomputes every sheet cell
  and applies diffs only at changed indices (never run raw
  `import_tileset.py` against the curated manifests — it renames the
  `gb_constant`s the ROM build depends on).
- Gates: `make tiles-check` (manifest validity), `make tiles` (regrow),
  full harness after any ROM-side consumption change (movement and
  rendering both read these tables).

# Phase 17 — Editor↔disk fidelity (JSON is the source of truth both ways)

The web editor must never be a stale snapshot of the JSON:

- Levels/tilesets load from disk through the dev-server API (`GET
  /api/levels`, `/api/level`, `/api/tilesets`, `/api/tileset`); the
  bundled static imports in `App.tsx`/`Tileset.ts` are only the fallback
  for built bundles served without the dev API.  Hand-edited JSON shows
  up after a catalogue refresh, with no editor rebuild.
- Saves write back the as-loaded terrain form (block rects stay block
  rects, with `comment`s) unless the terrain was actually painted, in
  which case the edited grid is emitted (the compiler accepts both;
  `decompile.py` normalizes grids back to verbatim blocks).  View-only
  saves are byte-stable.
- `/api/save-level` maps editor screen ids to their real files
  (`battle_default` → `screens/battle/default.json`, etc.); unknown ids
  fall back to the battle/title heuristic.  All file APIs reject ids
  outside `[A-Za-z0-9_]`.
- There is no `collision.overrides` section: collision is derived solely
  from the tileset manifest's `walkable` flag (it never had a
  compiler/ROM consumer, so it was removed rather than carried).
- `default_walkable` names the ground tile for unpainted cells, declared
  once per level (elsewhere: first `plain` tile in manifest order).  The
  editor fill, the compiler expansion, the ROM fallback (a `SceneDefinition`
  field materialized at scene load, like spawn), and the parity gate all
  use this one field; `decompile.py` preserves it verbatim.
- WYSIWYG parity (`make parity`, `tools/parity_check.py`): the ROM must
  show what the editor shows for the same JSON — whole-map VRAM tile
  parity per map (terrain expanded from JSON, indices via the manifest
  `vram_block` cross-refs, compared against the live tilemap mirror) plus
  animation frame-set parity (every JSON `animation_frames` id must
  resolve in manifest + `vram_block`, and the ROM sprite-tile rules must
  load those sheet cells; timing may differ, sets and order must not).
  Actor-occupied cells are skipped (actor overlay rules belong to actor
  scenarios).  Named exceptions only, never silent divergence.
- CGB color is attribute-driven, not pixel-driven: tile bytes carry shade
  indices, the 8 BG palettes carry the hues (slot 3 = field green; the
  rest per `cgb_bg_palettes`).  Overworld ground cells get their tileset
  palette in `ui_draw_world_cell` (forest field-green; DMG output is
  unchanged grayscale).  Parity checks indices, not colors; color is
  verified by RGB probes + screenshots.

# Phase 18 — Screens into the ROM build (title + battle mockups)

`screens/*.json` is compiled exactly like `levels/*.json`:

- `title_compile.py screens/title.json` → `src/game/title_data.c`
  (live data: `title_content.c` reads it).
- `battle_compile.py --all` → `src/game/battle_screens.c` +
  `src/game/battle_types.c`, with enemy-type battle art consumed by the
  bank-4 loader + bank-3 BG stamper (HP row 1, art rows 3-4, caret row 5).
  Full hud_layout-driven positioning beyond the art rows is still future
  work; see `docs/BATTLE_SCREENS_COMPILER_ISSUE.md`.
- Enemy battle art: `screens/enemy_types/*.json` `sprite: { art, frames }`
  selects a variable-size art set from `assets/battle_sprites.png` (composed
  by `tools/compose_battle_sprites.py` from the curated combat PNGs; cell
  order pinned by the per-set `width`/`height` and mirrored by the
  `make gfx` rule).  The bank-4 loader (`battle_art_load_banked`)
  resolves art per battle, streams w*h tiles per enemy slot into BG tiles
  at battle entry (LCD-off), and the bank-3 stamper draws them at the
  compiled hud_layout rows (HP row 1, art rows 3-4, caret row 5).
  Per-enemy art lives in WRAM globals, never in `Battle` (struct growth
  would ripple 16-bit offsets through fixed-bank accessors).
- `make screens` regenerates all three; `make screens-check` fails on
  drift (both compilers support `--check`; `battle_compile.py` also
  supports `--validate`).  `screens` is a prerequisite of `debug` and
  `release`, and `/api/compile-rom` runs the same commands.
- Legacy note: `src/game/battle_data.c` (`screens/battle.json`) is orphan
  output no current generator writes and nothing reads; leave it alone
  until the renderer-consumption work decides its fate.

# Phase 19 — Future-proof content persistence (scene id registry)

`levels/registry.json` is the single source of truth for real-scene ids.
The editor writes it; the compiler and every host tool derive from it.

## Contract

* **Versioned**: `version` (currently `1`).  `scene_registry.load_registry`
  accepts `1`; a missing version is treated as pre-version and upgraded on
  the next write; an unknown (newer) version is a loud error.  Bump
  `REGISTRY_VERSION` only with an explicit migration.
* **Append-only, never reused**: a new level gets
  `max(scenes ∪ _retired) + 1`.  Deleted levels tombstone their id into
  `_retired`, so an id can never be handed to a different level (protecting
  saves and persistent-actor state).
* **Fixed TEST block**: `_test_base` (240) reserves the fixture range;
  real ids may never reach it.  `test_*` filenames are refused.
* **Registry ↔ files agree**: `validate.py` fails loudly when a level file
  has no id, or a registered id has no file.  A retired id with a lingering
  file is a warning (`registry_warnings`).
* **Atomic writes**: every level JSON and the registry are written to a
  sibling `.tmp` and renamed over the target, so a crash can never corrupt
  the registry.

## Editor operations

* **New / edit**: `/api/save-level` assigns the next id on first save of an
  unknown level, then writes the file and registry atomically.
* **Rename**: edit the Scene ID and save (the client sends `previousId`).
  The **numeric scene id is preserved** (saves/references stay valid), the
  file is renamed, and every `target_scene` referencing the old id is
  rewritten across other levels.  Engine-wired scenes (whose `MAP_*` /
  `SCENE_*` symbols appear in hand-written C — e.g. `field`) are refused
  with a clear message.
* **Delete**: `POST /api/delete-level` retires the id, clears every exit
  that targeted it (returns the count), and unlinks the file.  Same
  engine-wired guard.
* `make registry-check` locks the contract (versioning, never-reuse,
  registry/file agreement) against a temp root.

# Phase 20 — Invisible exits + whole-edge neighbors (the "ocean")

Placing an exit no longer stamps a staircase/ladder tile. Exits are
**invisible triggers**: the gate cell keeps whatever terrain art is
painted under it (doors, elevators, stairs the author paints by hand),
and stepping onto it teleports via the exit table. The trigger fires
before terrain walkability, so a solid-looking gate is not blocked — but
it must actually *look* like a portal, or the player cannot see it.
`open_ground_blocks` opens unpainted gate cells to default ground, so an
unpainted exit works but is invisible. `validate.py` (and the editor's
Validate view) warns when an exit sits on art that does not read as a
portal (no `category: exit`, no stair/door/gate/portal/exit/cave/warp/
ladder in the tile id); the shipped gates paint the tileset's stairs/exit
tile. Point exits are also checked cross-file: the destination
`target_x/target_y` must be walkable or the build fails (stuck spawn),
while visibility is a non-fatal warning.

Whole map borders can link to other maps through `neighbors`:

```json
"neighbors": { "north": "forest", "south": "south_field",
               "east": "town", "west": "" }
```

Stepping onto a non-wall cell of a linked border crosses to the neighbor
scene with a mirrored entry spawn (one cell inside the opposite edge,
clamped for size mismatches). Walls stay walls: a wall on a linked edge
blocks, everything else crosses. A point exit on the same cell wins over
the edge rule. Links should be reciprocal (walk back); one-sided links
warn unless a point exit covers the return.

### Crossing safety (paired openings, hard compile error)

Because the landing cell is *inside* the neighbour, a border opening only
works when the matching entry cell in the neighbour is walkable. A link
therefore needs a corridor on both sides:

* the source's linked border cells (the cells you step onto), and
* the target's entry line one cell inside the opposite edge (where you
  land), across the non-corner span.

`open_ground_blocks` opens *unpainted* border cells; the shipped levels
also carve the target entry lines explicitly so the whole edge is
traversable (block levels subtract the entry row/column from their wall
blocks; grid levels get the default floor). This is what stops a mapper
from digging a wall just inside an opening and trapping the player.

`tools/level_compiler/collision.py` holds the one implementation
(`mirrored_entry` + `edge_link_report`). It is enforced as a **hard
compile error** — `compile.py` refuses to build a trapping pairing — and
surfaced by `validate.py` and the editor's inline Edge-links panel
(`/api/neighbor-check`). It rejects:

* a walkable crossing whose mirrored landing cell is a wall (player stuck
  inside a wall);
* a declared reciprocal return whose edge has no walkable cell (player
  trapped in the neighbour — e.g. walking into a fully walled map);
* a reciprocal return that itself lands in a wall.

## Contract

* `levels/schema/level.schema.json` gains optional `neighbors`
  (north/south/east/west scene names, empty = no link).
* The compiler emits four neighbor bytes per `SceneDefinition` row
  (`MAP_NONE` = no link) plus open-ground rows for unpainted gates and
  linked edges, so the ROM fill loop stays branch-free. Unknown targets
  fail loudly, like exit targets.
* The ROM resolves a step as point-exit trigger first, then linked-edge
  crossing, then a normal walk (fixed-bank budget: the edge predicate
  and mirrored spawn run banked; see AGENTS.md 55.5).
* The editor paints linked borders with green `⇄ <scene>` strips, edits
  the four links in the Exits tab, rewires them on rename, and clears
  them on delete (same as exits). `validate.py` checks targets,
  reciprocity, gate art, and crossing safety; the Exits tab's Edge-links
  panel reports the same per-link status inline (via
  `/api/neighbor-check`, using the editor's unsaved level data).
* The walkthrough planner (`tools/walkthrough/route.py`) routes over
  exits and edges identically, and only across crossings whose landing
  cell is walkable; the content sweep visits every level through them.
* `collision.py` is the shared collision/neighbor helper module for the
  compiler, validator, and planner (single source of truth; avoids the
  compile↔validate import cycle).
* Harness fixtures (`tools/scenarios/fixtures/`) carry no neighbor links
  (frozen fixtures stay decoupled from the corridor geometry); edge
  coverage lives in the walkthrough, trigger coverage in the harness
  scenarios.

Supersedes Phase 16's exit-art bullet: gates no longer render
per-tileset stairs art and the ROM never stamps `TILE_EXIT`; the
`exit: true` manifest markings remain as decor-tile metadata only.

# Phase 21 — Two-way tunnels (linked exit pairs)

Point exits are one-way rows: going back needs a second exit in the
target scene. A **tunnel** is that return pair made first-class: two
exits sharing one `tunnel` id, one in each of two levels, with mutual
targets and each landing on the other's gate (spawn-tracks-gate). The
ROM format is unchanged — two plain `SceneExit` rows — so there is no
engine, bank, or memmap impact; the id lives only in JSON + tooling.

```json
"exits": [
  { "x": 12, "y": 11, "target_scene": "south_field",
    "target_x": 12, "target_y": 11, "direction": "SOUTH",
    "tile_char": "<", "tunnel": "tunnel_mountain_pass_south_field" }
]
```

No `tunnel` field = one-way exit, exactly as before. Tunnels are
opt-in per exit, never forced.

## Contract

* `levels/schema/level.schema.json` gains optional exit `tunnel`
  (lowercase letter-first id, same shape as scene ids, enforced by a
  `"pattern"` so JSON-schema-aware tools catch bad ids early; the
  Python/TS checks remain the hard gate).
* `tools/level_compiler/collision.py` owns the pairing check
  (`tunnel_report`/`tunnel_issues`, mirroring `edge_link_report`): each
  id must have exactly two mouths in different levels, mutual targets,
  and spawn-tracks-gate landings. Violations are a **hard compile
  error** (`compile.py` aborts) and a `validate.py` error — a dangling
  mouth strands the player with no way back, so it can never ship.
  The editor's `tunnelStatus()` mirrors these invariants; the shared
  corpus (`tools/level_compiler/tests/tunnel_parity_cases.json` +
  `test_tunnel_parity.py` and `tools/level_editor/tests/tunnel_parity.mjs`)
  guards both sides from drifting.
* `decompile.py` preserves `tunnel` ids across the JSON ⇄ C roundtrip
  (the C rows cannot hold them); a retargeted mouth loses its id and
  fails loudly at the next compile instead of silently unlinking.
* The editor keeps pairs in sync with **full auto-sync**:
  - the Exits tab's “Return exits & tunnels” panel shows 🔗 paired /
    ⚠ broken per exit, with one-click Create / Create tunnel /
    Make tunnel / Unlink;
  - every save syncs partner mouths (target back at the saver, landing
    on the saver's gate) and removes orphaned rows of deleted mouths
    (reported in the save notification, never silent), reading the
    levels directory once per save;
  - “Make tunnel” adopts an existing return coordinate-aware: with
    several untunneled returns to the source level only the one already
    landing on the new mouth's gate is adopted, otherwise a fresh
    partner is created (a mouth carrying a different tunnel id is never
    absorbed);
  - unlinking keeps both rows as independent one-way exits; deleting a
    mouth removes its partner on save (with confirm);
  - tunnel mouths render teal ⇄ on the canvas vs orange one-ways.
* Rename/delete rewire preserves tunnel ids (rename) and clears
  partners of deleted levels (delete), same as exits.
* Whole-edge `neighbors` links are out of scope — tunnels cover point
  exits only.
