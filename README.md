# Kaartenheld

**Kaartenheld** (Luxembourgish for "Card Hero") is an upcoming top-down adventure game for the Nintendo Game Boy by **Gallia Belgica Systems**, a two-person indie team of inexperienced-but-relentless developers.

This repository contains the playable **battle demo** for Kaartenheld: a top-down adventure with a deck-building card combat system in which you build **poker hands to trigger combos**, explore the world, and grow your deck — with **no traditional RPG mechanics**: no equipment, no levels, no XP. Your power is entirely determined by the cards you carry.

Every combat victory rewards you with a **new random card**: add it to your deck, or sell it.

## Demo

<p align="center">
  <img src="screenshots/boot.gif" alt="Kaartenheld boot: the Gallia Belgica Systems splash, then the title screen" width="480">
  <br>
  <em>Boot — the <strong>Gallia Belgica Systems</strong> splash, then the title screen.</em>
</p>

<p align="center">
  <img src="screenshots/battle.gif" alt="Kaartenheld card combat: selecting a TWO PAIR combo and attacking a kobold trio" width="480">
  <br>
  <em>Card combat — playing a <strong>TWO PAIR</strong> combo against a kobold trio.</em>
</p>

<p align="center">
  <img src="screenshots/overworld.gif" alt="Kaartenheld overworld: walking from the Field through the gate into the Forest, out to the treasure chest" width="480">
  <br>
  <em>Overworld — from the Field through the gate into the Forest, out to the chest.</em>
</p>

<p align="center">
  <img src="screenshots/boss.gif" alt="Kaartenheld boss battle: the Lord of Slimes, its glowing eyes blinking red and orange" width="480">
  <br>
  <em>Boss battle — the <strong>Lord of Slimes</strong>, its glowing eyes blinking.</em>
</p>

## How It Is Made

Kaartenheld is built with a deliberately split creative process:

- **Art and music are entirely human-made** — every sprite, tileset, and track is crafted by hand by Gallia Belgica Systems.
- **The code is written by AI coding agents** — an LLM-first development workflow in which agents build, test, and debug the game through a deterministic, machine-readable harness (see the technical documentation below).
- **Levels are authored in a bespoke level editor** built specifically for this game, which compiles directly into the game's build pipeline.

## The Game

- **Top-down adventure**: explore tile-based maps, talk to characters, follow the story.
- **Card combat**: battles are fought with a deck of up to 20 cards; you play a 5-card hand.
- **Poker combos**: the values of the cards in your hand form poker hands — PAIR, TWO PAIR, THREE KIND, STRAIGHT, FLUSH, FULL HOUSE, FOUR KIND, STRAIGHT FLUSH, FIVE KIND — and stronger hands mean stronger effects. Ring cards substitute any *number* in the combo (they stay heal cards, so they break a flush).
- **No RPG grind**: there is no equipment and no leveling. Deck composition is your entire progression.
- **Loot cards**: every victory drops a procedurally generated card (names, values, riders, rarity tiers). Keep it, build it into your deck, or sell it to a merchant for gold, the in-game currency.
- **Status effects**: POISON, BURN, and FREEZE ride on card hits with stacking rules and per-round ticks.
- **Persistence**: battery-backed SRAM save/load with slots.

### What's in the demo

The demo's world is **14 connected rooms**: the Field and Town; a Forest branch
(Mossy Glade → Deep Woods → Sunken Grove, plus the Overgrown Shrine); a
desolate South Field / Howling Ridge spur; and a Castle chain (Entry → Great
Hall → the Ruined Castle → the Throne Room, where the Lord of Slimes waits).
The rooms are populated with enemy encounters (slimes, bats, kobolds, spiders,
and a mimic) and a few NPCs who offer advice.

## About Gallia Belgica Systems

Gallia Belgica Systems is two inexperienced indie developers making a Game Boy game the hard way: authentic hardware constraints, a fully reproducible Nix toolchain, and an AI-agent-first development workflow (more on that below).

---

## Development & Technical Documentation

The following sections cover how this repository is engineered and tested.

### LLM-First Development Architecture

Developing Game Boy games with AI agents requires solving retro hardware opacity: LLMs cannot reliably play real-time games with a joypad or interpret low-resolution pixels.

This repository solves that by making **semantic observability a first-class subsystem**:

```text
Host-Side AI Agent / Test Runner (Python)
                   │
                   ▼  PTY / Debug Protocol
      mGBA / SameBoy Emulator Session
                   │
                   ▼  Memory & Registers
      Game Boy Debug ROM (kaartenheld_debug.gb)
    ┌──────────────────────────────────────────────┐
    │ • Declarative State Injection (Scene/Player) │
    │ • Bounded Telemetry Ring Buffer (Events)     │
    │ • Semantic State Snapshots (Game/World/Party)│
    │ • Hardware Timer Sound Clock (TIMA ISR)      │
    │ • Deterministic RNG Seeding                  │
    └──────────────────────────────────────────────┘
```

#### Key LLM-First Capabilities

- **Declarative Scenario Fixtures**: 193 deterministic scenarios in `tools/scenarios/tests/*.json` that configure map coordinates, story flags, party stats, decks, hands, inventory, and enemy states before running scripted inputs.
- **Parallel Test Harness**: Runs test scenarios concurrently across host CPU cores (`make test-harness JOBS=16`), achieving over 7x speedup compared to serial execution.
- **Structured Telemetry**: Every state transition, map crossing, collision, dialogue event, quest update, card action, combo resolution, loot drop, and battle action emits structured telemetry into a bounded ring buffer.
- **Semantic State Snapshots**: Agents inspect exact world coordinates, active screens, dialogue trees, story flags, deck/collection contents, and combat stats rather than parsing pixel frames.
- **Deterministic Randomness**: RNG is fully seeded and reproducible across test runs (card draws, deck shuffles, loot rolls, status chances).
- **OAM Fidelity Checks**: `make verify-oam` drives mGBA's debugger to assert sprite positions across battle/scene/dialogue transitions — catching VBlank-timed rendering bugs the SameBoy harness cannot observe.

See [`docs/DEBUG_PROTOCOL.md`](docs/DEBUG_PROTOCOL.md) and [`AGENTS.md`](AGENTS.md) for full protocol and operational contracts.

### Engine Features

The codebase includes a complete, playable vertical slice proving all core systems:

- **World & Overworld**:
  - Tile-based maps with smooth sub-tile hardware scrolling;
  - Collision detection, walkability allowlists, and warp gates;
  - Persistent actors, NPC patrolling, and proximity interaction.
- **Card Combat** (see [`docs/card-battle.md`](docs/card-battle.md)):
  - Attack/defend phases with a 5-card hand drawn from your deck (up to 20 copies);
  - Number-based **poker-style combos**: the hand you make determines the tier — PAIR, TWO PAIR, THREE KIND, STRAIGHT, FLUSH, FULL HOUSE, FOUR KIND, STRAIGHT FLUSH, FIVE KIND;
  - **Ring cards substitute any number** in the combo (they stay heal cards, so they break a flush);
  - Card types (sword, shield, bow, heal, dagger) with deterministic effect resolution and a visible turn timer;
  - Live combo preview on the battle HUD (mirrors resolution, including ring trials).
- **Deck & Collection** (see [`docs/deck.md`](docs/deck.md)):
  - 20-card combat deck with draw/discard/reshuffle;
  - Collection management through the quick screen (add/remove deck copies, filter/sort);
  - Deck state is persistent and saveable.
- **Procedural Card Loot** (Borderlands-inspired, see [`docs/loot.md`](docs/loot.md)):
  - Enemies drop **cards**, not generic items — every drop is either deck material or sellable loot;
  - Procedural card synthesis (names, values, riders) with rarity tiers;
  - Cards can be sold back to the merchant for **gold**, the game's currency.
- **Status Effects** (see [`docs/combo-system.md`](docs/combo-system.md)):
  - POISON, BURN, and FREEZE as on-hit riders with deterministic chance rolls, stacking rules, and per-round ticks.
- **Story & Events**:
  - Dialogue player with multi-step choices and branch conditions;
  - Scripted quest system with multi-stage state tracking;
  - Global story flags and variables.
- **Persistence**:
  - Battery-backed SRAM save/load with versioned formats and slot management.
- **Audio Architecture**:
  - Hardware Timer-driven music clock (`TIMA` overflow ISR) running at fixed 256 Hz tempo regardless of main loop load or LCD redraws.

### Engine vs. Game Content Separation

The codebase strictly enforces the separation of generic engine primitives from game-specific data:

```text
src/
├── core/       Generic boot glue, event engine, dialogue runner, story flags, quests
├── rpg/        Generic GameState, cards, deck, currency, party, progression, status, save
├── world/      Generic scene loader, actors, movement, collision, interactions
├── battle/     Generic battle lifecycle, hand/combo evaluation, combatant stats
├── ui/         Generic screen renderer, menu frame layouts, font rendering
├── screens/    Overworld, battle, dialogue, quick screen, shop, save/load screens
├── input/      Joypad abstraction and programmatic debug input injection
├── audio/      Timer-driven audio ISR and sound registers
├── debug/      Harness telemetry, scenario loader, assertions, RNG control
│
└── game/       GAME CONTENT (All game-specific tables live here)
    ├── game_ids.h          Named story flags, variables, currencies, and actors
    ├── content.c           New game initialization and victory hooks
    ├── events_content.c    Scripted events and quest triggers
    ├── dialogue_content.c  Dialogue scripts and NPC lines
    ├── actors_content.c    Per-map actor spawn tables and behaviors
    ├── cards_content.c     Card catalog and effect definitions
    ├── quests_content.c    Quest registry rows and objectives
    ├── scenes_content.c    Scene definitions and map layouts
    ├── shops_content.c     Shop inventories and pricing
    ├── loot_drop_banked.c  Per-enemy drop tables and loot synthesis
    └── tiles_content.c     World tilesets and graphical definitions
```

When creating a new game from this template, developers and agents modify `src/game/` while leaving the engine subsystems in `src/core/`, `src/world/`, `src/rpg/`, and `src/ui/` untouched.

### Hardware & Toolchain

- **Target Hardware**: Nintendo Game Boy (DMG) / Game Boy Color (CGB)
- **C Toolchain**: GBDK-4 (`lcc` / SDCC)
- **Assembly Toolchain**: RGBDS (`rgbasm`, `rgblink`, `rgbfix`)
- **Environment**: Nix flakes (100% reproducible, zero host dependencies)
- **Development Emulator**: SameBoy & mGBA

### Quick Start

#### 1. Enter the Nix Development Shell

```bash
nix develop
```

All build tools, compilers, emulators, and test runners are automatically provided.

### 2. Primary Make Targets

| Target | Description | Output |
| :--- | :--- | :--- |
| `make release` | Build optimized release ROM | `build/kaartenheld.gb` |
| `make debug` | Build debug ROM with harness & telemetry | `build/kaartenheld_debug.gb` |
| `make editor` | Launch the web Level Editor (React + Vite) | Web dev server (`localhost:3000`) |
| `make level LEVEL=<name>` | Validate and compile specific JSON level | `src/game/scenes_content.c` |
| `make levels` | Validate and compile all `levels/*.json` to C | `src/game/scenes_content.c` |
| `make test-harness` | Run all scenarios in parallel | Parallel test results (PASS/FAIL) |
| `make test-scenario SCENARIO=<name>` | Run one scenario with full diagnostics | PASS/FAIL + state + telemetry |
| `make test` | Validate ROM header and checksums | ROM verification |
| `make verify-oam` | mGBA debugger OAM fidelity checks across transitions | OAM verification |
| `make verify-walkthrough` | Drive the release ROM (real `levels/` content) and assert canonical state | PASS/FAIL checks |
| `make verify-scroll` / `verify-music` / `verify-patrol` / `verify-endurance` | Extra PyBoy regression checks | PASS/FAIL |
| `make memmap` | Check ROM and WRAM memory budget | Invariant check (`_HOME < 0x8000`) |
| `make lint` | Compile-to-assembly `-Wall` pass over all sources | Warning report |
| `make screenshots` | Headless PyBoy walkthrough capture | `screenshots/*.png` |
| `make gifs` | Regenerate the README GIFs (boot, kobold battle, overworld, boss) | `screenshots/boot.gif`, `screenshots/battle.gif`, `screenshots/overworld.gif`, `screenshots/boss.gif` |
| `make run` | Launch release ROM in emulator | Game window |
| `make run-debug` | Launch debug ROM in emulator | Debug game window |
| `make screenshot` | Capture headless emulator screenshot | `build/screenshot.png` |
| `make clean` | Remove all generated build artifacts | Clean directory |

---

## Level Editor & End-to-End Level Authoring

The repository includes a web-based visual level editor and an automated compiler pipeline that transforms JSON level definitions directly into Game Boy C scene tables.

### 1. Running the Web Level Editor with Nix

Node.js and npm are provided directly by the project's Nix flake (`flake.nix`).

Enter the Nix development shell (first run needs `--accept-flake-config`, see Quick Start):

```bash
nix develop
```

Then start the editor with a single command:

```bash
make editor
```

Or run via `npm` directly:

```bash
cd tools/level_editor
npm install
npm run dev
```

Open `http://localhost:3000` in your browser.

**Features & Shortcuts:**
* **Tool Palette**: Brush (`B`), Eraser (`E`), Filled Rectangle (`R`), Flood Fill (`G`), Eyedropper (`I`), Select / Move (`V`).
* **History**: Undo (`Ctrl+Z`), Redo (`Ctrl+Shift+Z` / `Ctrl+Y`).
* **Save & Export**: Download updated JSON (`Ctrl+S`), live in-browser level validation, and LLM semantic representation export.
* **Layers**: Switch between **Terrain**, **Player Spawn**, **Exits / Warps**, **Objects & NPCs**, and **Regions (LLM)**.
* **Overlays**: Toggle grid display and live walkability collision overlays.

---

### 2. End-to-End Level Authoring Pipeline

To create a new level or modify an existing scene and test it in the game:

```text
┌─────────────────────────┐
│ 1. Design Level         │ → Use web level editor or edit levels/<name>.json
└────────────┬────────────┘
             │
             ▼
┌─────────────────────────┐
│ 2. Validate             │ → python3 tools/level_compiler/validate.py levels/<name>.json
└────────────┬────────────┘
             │
             ▼
┌─────────────────────────┐
│ 3. Compile to C         │ → make levels (generates src/game/scenes_content.c)
└────────────┬────────────┘
             │
             ▼
┌─────────────────────────┐
│ 4. Build ROM            │ → make release (or make debug)
└────────────┬────────────┘
             │
             ▼
┌─────────────────────────┐
│ 5. Test in Emulator     │ → make run (or make run-debug)
└─────────────────────────┘
```

#### Step-by-Step Commands:

1. **Create or modify a level**:
   Save your level definition to `levels/<level_id>.json` (or use presets like `forest.json`, `town.json`, `field.json`, `mountain_pass.json`, `castle.json`).

2. **Validate the level**:
   ```bash
   python3 tools/level_compiler/validate.py levels/forest.json
   ```

3. **Compile all levels into the game**:
   ```bash
   make levels
   # Or compile a specific level:
   make level LEVEL=forest
   ```

4. **Build and test the ROM**:
   ```bash
   make release
   make run
   ```

5. **Generate LLM semantic summaries (optional)**:
   ```bash
   python3 tools/level_compiler/describe.py levels/forest.json
   ```

6. **Automate level edits programmatically (optional)**:
   ```bash
   python3 tools/level_compiler/apply_ops.py levels/forest.json \
     --op '{"operation": "paint_rectangle", "tile": "forest.tree", "x": 4, "y": 2, "width": 2, "height": 1}'
   ```

See [`docs/level-editor.md`](docs/level-editor.md) and [`tools/level_compiler/README.md`](tools/level_compiler/README.md) for full format specifications and compiler details.

### 3. Running Scenario Tests

Run all scenarios in parallel (defaults to all host cores, or specify `JOBS`):

```bash
make test-harness JOBS=16
```

Run a single scenario with full diagnostic trace:

```bash
make test-scenario SCENARIO=town_arrival
```

### Documentation

#### Protocol & Agent Contracts

- [`AGENTS.md`](AGENTS.md) — Operational contract and rules for AI coding agents.
- [`docs/DEBUG_PROTOCOL.md`](docs/DEBUG_PROTOCOL.md) — Authoritative debug protocol and LLM state inspection contract.
- [`docs/dev-harness.md`](docs/dev-harness.md) — Deterministic scenario design and emulator bridge.
- [`docs/LLM_AGENT_GUIDE.md`](docs/LLM_AGENT_GUIDE.md) — Practical guide for agents working in this repo.

#### Architecture & Engineering

- [`docs/architecture.md`](docs/architecture.md) — Subsystem architecture and dependency direction.
- [`docs/game-vs-engine.md`](docs/game-vs-engine.md) — Engine/game layer boundary rules.
- [`docs/FOUNDATION_CONTRACT.md`](docs/FOUNDATION_CONTRACT.md) — Foundation boundaries and golden rules.
- [`docs/rpg-foundation.md`](docs/rpg-foundation.md) — Foundation design notes.
- [`docs/save-format.md`](docs/save-format.md) — SRAM save format and versioning specification.
- [`docs/memory-budget.md`](docs/memory-budget.md) — ROM and WRAM memory budgets.
- [`docs/graphics.md`](docs/graphics.md) — Graphics conversion pipeline (`png2gb.py`) and tile layout.
- [`docs/level-editor.md`](docs/level-editor.md) — Visual level editor & compiler architecture specification.
- [`docs/level-editor-tutorial.md`](docs/level-editor-tutorial.md) — Level editor tutorial: first level, tools/layers reference, troubleshooting.
- [`docs/testing.md`](docs/testing.md) — Testing strategy and harness usage.
- [`docs/roadmap.md`](docs/roadmap.md) — Known gaps and future work.

#### Gameplay Design

- [`docs/card-battle.md`](docs/card-battle.md) — Card combat design: attack/defend phases, card roles, timer.
- [`docs/combo-system.md`](docs/combo-system.md) — Combo tiers, ring jokers, effect and status architecture.
- [`docs/deck.md`](docs/deck.md) — Deck system: 20-card deck, draw/discard, collection.
- [`docs/deck-management.md`](docs/deck-management.md) — Quick-screen deck management UX.
- [`docs/loot.md`](docs/loot.md) — Procedural card loot, rarity, and gold economy.
- [`docs/implementing-card-phases.md`](docs/implementing-card-phases.md) — Implementation status of the card phases.
- [`docs/dialogue-boxes.md`](docs/dialogue-boxes.md) — Dialogue box rendering and camera alignment.

## License

This project is **split-licensed**: the code and the assets are covered by
different terms.

- **Code** (`src/`, `tools/`, `levels/`, build scripts) is released under the
  [European Union Public Licence v. 1.2](LICENSE) (EUPL-1.2). You may use,
  modify, and redistribute it — including commercially — provided that
  derivative source stays under the EUPL (or a licence listed as compatible in
  its Appendix) and the copyright/attribution notices are kept.
- **Art, music, and other assets** are **not** covered by the EUPL and remain
  under their own terms. In particular the assets credited to Florent Bérault
  (e.g. `desolate_landscape.png`, `assets/music/Battle BGM.uge`,
  `assets/music/desolate_landscape.uge`) are offered to the public under
  **CC BY-NC-ND 4.0** (attribution, **non-commercial**, **no derivatives**); the
  imported tilesets follow their source pages, and the combat art is original
  work by Florent Bérault. See [`assets/README.md`](assets/README.md) and
  [`assets/tilesets.md`](assets/tilesets.md) for the per-asset credits.

What that means in practice:

- **The project and its contributors retain all rights, and the game will be
  sold as a commercial release** — including the ROM that bundles the art and
  music. The non-commercial / no-derivatives terms bind public licensees, not
  the rights holders.
- **Third parties** may reuse the **code** under the EUPL, including in
  commercial projects, but may **not** commercially use the CC BY-NC-ND assets
  or distribute modified or derivative versions of them. A commercial fork
  needs its own replacement art and music, or separate permission from each
  rights holder. Attribution must be preserved.

This is a plain-language summary, not legal advice; the licence texts and the
per-asset sources are authoritative.
