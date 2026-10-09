# LLM Agent Guide: Game Boy RPG Development

This guide is the operational handbook for AI coding agents working on this repository. It explains the mental model, subsystem boundaries, observability requirements, testing workflows, and non-negotiable Game Boy hardware constraints.

---

## 1. Core Mental Model: LLM-First Development

This repository is designed so that an AI agent can build, execute, inspect, test, and debug gameplay loops through deterministic, machine-readable interfaces without human intervention.

### The Golden Rules of Observability

1. **Semantic State Is Authoritative**: Visual rendering is presentation; semantic game state is the source of truth. Always inspect state variables, telemetry events, and snapshots rather than attempting to parse pixel frames.
2. **Every Gameplay Transition Must Emit Telemetry**: State changes, map transitions, player movement, collisions, dialogue events, combat actions, quest updates, and flag changes must emit structured events via `telemetry_emit()`.
3. **Scenarios Are First-Class Test Fixtures**: Whenever you add or modify gameplay behavior, create or update a deterministic scenario in `tools/scenarios/*.json`.
4. **Never Bypass Engine Behavior in Tests**: Test scenarios must set initial state (e.g. coordinates, flags, items) and trigger game logic via simulated inputs rather than forcibly calling end-state functions directly.

---

## 2. Codebase Architecture: Engine vs. Game Content

The repository enforces a strict boundary between generic engine subsystems and game-specific content:

```text
src/
├── core/       Generic boot glue, event engine, dialogue runner, story flags, quests
├── rpg/        Generic GameState storage, inventory, items, currency, party, progression, save
├── world/      Generic scene loader, actors, movement, collision, interactions
├── battle/     Generic battle lifecycle, turn controller, combatant stats
├── ui/         Generic screen renderer, menu frame layouts, font rendering
├── screens/    Individual game screen controllers (overworld, battle, dialogue, shop, save)
├── input/      Joypad abstraction and programmatic debug input injection
├── audio/      Hardware Timer-driven sound ISR and audio registers
├── debug/      Telemetry ring buffer, scenario loader, assertions, deterministic RNG
│
└── game/       GAME CONTENT (All game-specific content lives here)
    ├── game_ids.h          Named story flags, variables, currencies, and actors
    ├── content.c           New game initialization, victory hooks, stat formulas
    ├── events_content.c    Scripted events and quest triggers
    ├── dialogue_content.c  Dialogue scripts and NPC lines
    ├── actors_content.c    Per-map actor spawn tables and behaviors
    ├── items_content.c     Item catalog and equipment definitions
    ├── scenes_content.c    Scene definitions and map layouts (Bank 2)
    ├── shops_content.c     Shop inventories and pricing
    └── tiles_content.c     World tilesets and graphical definitions (Bank 2)
```

### Where to Put New Code:
- **Game Content**: If it defines *what* happens in this specific game (NPC dialogue, quest requirements, map layouts, item stats, shop inventory), it belongs in `src/game/`.
- **Generic Engine**: If it defines *how* an RPG system operates (how scenes load, how collision is evaluated, how menus layout text, how save data serializes), it belongs in `src/core/`, `src/world/`, `src/rpg/`, or `src/ui/`.

---

## 3. Testing & Verification Workflows

All development is performed inside the reproducible Nix environment (`nix develop`).

### Primary Make Targets

| Target | Command | Purpose |
| :--- | :--- | :--- |
| **All Scenarios (Parallel)** | `make test-harness` | Runs all 100+ scenario tests concurrently (default `--jobs auto`). |
| **Workstation Parallelism** | `make test-harness JOBS=16` | Runs on up to 16 workers on capable host workstations (~7x speedup). |
| **CI Parallelism** | `make test-harness JOBS=4` | Runs on 4 workers on CI (GitHub Actions) to avoid runner exhaustion. |
| **Single Scenario** | `make test-scenario SCENARIO=<name>` | Runs one scenario with full diagnostic trace. |
| **Memory Budget Check** | `make memmap` | Verifies ROM and WRAM memory budget invariants (`_HOME < 0x8000`). |
| **Regression Checks** | `make verify-scroll verify-patrol verify-oam verify-music verify-endurance` | Verifies scrolling, patrol AI (`patrol_slime_cross` + `patrol_enemy_bumps_player`), OAM sprite fidelity, timer audio clock, and endurance. |
| **Header & Release Test** | `make test` | Validates release ROM compilation and header checksum integrity. |

### How Scenarios Work (`tools/scenarios/*.json`)

A scenario establishes a known initial state, injects an input sequence, and evaluates machine-checkable assertions:

```json
{
  "name": "town_arrival",
  "description": "Walk east on Field into gate tile to enter Town map",
  "initial_state": {
    "screen": "OVERWORLD",
    "scene": "FIELD",
    "player_x": 16,
    "player_y": 7,
    "player_facing": "RIGHT",
    "seed": 42
  },
  "inputs": [
    {"button": "RIGHT", "hold_frames": 8},
    {"wait_frames": 2}
  ],
  "assertions": [
    {"type": "equals", "target": "game_state", "expected": "OVERWORLD"},
    {"type": "equals", "target": "player_position", "expected": [2, 7]},
    {"type": "event_occurred", "event": "MAP_CHANGED"},
    {"type": "event_occurred", "event": "STORY_FLAG_SET", "flag": "ARRIVED_TOWN"}
  ]
}
```

---

## 4. Game Boy Hardware Invariants (Non-Negotiable)

When writing C or assembly for this project, you are targeting real 8-bit Game Boy (SM83) hardware. You must observe these critical architectural rules:

### 1. Bank 0 / `_HOME` Space is Limited (`make memmap`)
- Bank 0 (the unbanked ROM area `0x0000–0x7FFF`) must house all non-bankable code and data (`_HOME`, `_CODE`).
- **Never place large data tables in Bank 0**. Large arrays (scenes, tilesets, dialogue tables) must be placed in **Bank 2** using `BANKED_DATA` and accessed via `banked_copy()` or banked pointers.
- Always run `make memmap` to verify Bank 0 headroom.

### 2. Unconditional CGB Palette Initialization
- When running on Game Boy Color hardware or GBC emulators, uninitialized palette RAM falls back to the Nintendo GBC Boot ROM's default DMG compatibility palette (turning the screen **red and blue**).
- `_cpu` detection relies on register `A` at boot (`0x11` on CGB), but automated test runners and headless emulators can bypass the boot animation leaving `A == 0x00`.
- **Rule**: `ui_init()` must **always** program both DMG registers (`BGP_REG`, `OBP0_REG`, `OBP1_REG`) and all 8 CGB BG & OBJ palettes **unconditionally** using explicit per-byte indexing `(0x80 | p)`. (Writes to `0xFF68`–`0xFF6B` are safe hardware no-ops on DMG).

### 3. Hardware Timer Sound Timing (`TIMA` ISR at `0xC900`)
- **Never advance music step timers in the main loop or inside VBlank**. Main loop execution time varies between menus and overworld, causing audible tempo drift.
- Music runs strictly on the **hardware Timer interrupt** (TIMA overflow at 256 Hz) driven by a dedicated RAM-resident ISR installed at WRAM `0xC900`.
- CRT0 patches the timer vector `0x0050` to `JP 0xC900` and enables timer-only interrupts (`IE = 0x04`).

### 4. WRAM Memory Layout & Pinning (`_DATA = 0xC940`)
- The custom timer ISR occupies `0xC900–0xC93F`.
- `_DATA` is explicitly pinned at `0xC940` in `Makefile` (`LDFLAGS = -Wl-b_DATA=0xC940`) to prevent SDCC from auto-placing C symbols into the ISR memory region.

### 5. VRAM Access & LCD Timing
- VRAM (`0x8000–0x9FFF`) cannot be written while the PPU is actively drawing the screen (Mode 3).
- Full map redraws and font loads must either turn off the LCD safely (`LCDC_REG &= ~0x80` during VBlank) or perform incremental updates during VBlank (`vsync()`).

---

## 5. How-To Guide for Common Agent Tasks

### Adding a New Map / Scene
1. Define the scene ID in `src/game/game_ids.h` and the tileset kind (`WORLD_TILESET_EXTERIOR`, `INTERIOR`, or `FOREST`).
2. Add the terrain layout and exit triggers in `src/game/scenes_content.c` (Bank 2).
3. If new terrain blocks are needed, add their `SceneTerrainBlock` entries.
4. Register the scene in `g_scenes[]` in `src/game/scenes_content.c`.
5. Add a scenario in `tools/scenarios/` to test map entry, collisions, and exits.

### Adding a New Quest or Scripted Event
1. Define named flags or variables in `src/game/game_ids.h`.
2. Add dialogue strings in `src/game/dialogue_content.c`.
3. Add event conditions and actions in `src/game/events_content.c`.
4. Register quest metadata in `src/game/quests_content.c`.
5. Ensure `telemetry_emit()` fires on quest progress.
6. Create an automated test scenario in `tools/scenarios/` verifying the end-to-end quest trigger.

### Modifying or Adding Graphics / Tilesets
1. Source PNG images live in `assets/`.
2. PNGs must use exact 4-color palettes (`#FFFFFF`, `#AAAAAA`, `#555555`, `#000000` or standard Game Boy green ramp).
3. Run `make gfx` to invoke `tools/png2gb.py --raw`, emitting `.inc` tile byte arrays into `src/gfx/`.
4. `#include` the generated `.inc` files into `src/game/tiles_content.c`.
5. Update `src/gfx/rpg_tile_lookup.h` to map ASCII scene glyphs to the generated tile IDs.
