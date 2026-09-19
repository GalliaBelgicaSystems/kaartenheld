# Agent Operating Instructions (AGENTS.md)

This file defines the operational contract and constraints for AI coding agents working on this project.

The project is an experimental Game Boy RPG. The development workflow is intentionally **LLM-first**: AI coding agents must be able to build, execute, inspect, test, and debug gameplay through deterministic machine-readable interfaces without relying on a human to play the game or interpret the screen.

---

# 1. Environment

This repository uses Nix flakes for complete, reproducible environment management.

Do not install dependencies manually using `apt`, `brew`, `npm`, `pip`, or other host package managers.

Enter the development environment with:

```bash
nix develop
```

All development commands must work inside the Nix development environment.

Do not introduce dependencies that are unavailable from the project's Nix environment without first updating the flake appropriately.

---

# 2. Primary Commands

All common operations are exposed through standard `make` targets.

## Build Release ROM

```bash
make release
```

Produces:

```text
build/kaartenheld.gb
```

The release ROM must not depend on debug-only functionality.

---

## Build Debug ROM

```bash
make debug
```

Produces:

```text
build/kaartenheld_debug.gb
```

The debug ROM includes development harness functionality such as:

* telemetry;
* semantic state inspection;
* event logging;
* deterministic RNG;
* scenario loading;
* assertions;
* debug input;
* development diagnostics.

---

## Run All Harness Scenario Tests

```bash
make test-harness
```

Runs all test scenarios in parallel using the host CPU cores (or up to 16 workers on capable development workstations):

```bash
make test-harness JOBS=16
```

On CI (GitHub Actions), parallel workers must be limited to **4 cores** (`JOBS=4`) to avoid runner resource exhaustion:

```bash
make test-harness JOBS=4
```

This must:

1. build the debug ROM;
2. execute the host-side harness across worker processes;
3. run all scenarios in `tools/scenarios/`;
4. evaluate their assertions;
5. return a non-zero exit code if any scenario fails.

Equivalent underlying commands:

```bash
python3 tools/dev.py test --jobs 16
python3 tools/dev.py test --jobs 4   # CI
```

---

## Run One Harness Scenario

```bash
make test-scenario SCENARIO=first_encounter
```

Example:

```bash
make test-scenario SCENARIO=town_event_01
```

The scenario must be deterministic and reproducible.

---

## Automated ROM Validation

```bash
make test
```

This validates the release build, including compilation, linking, ROM header, and checksum integrity.

---

## OAM Fidelity Check

```bash
make verify-oam
```

Asserts correct player-sprite OAM positions across battle, scene-change, and
dialogue transitions via the mGBA debugger.  Catches VBlank-timed sprite bugs
that the SameBoy harness cannot observe (see §52.15).  **Required** — CI runs
this on every push.  Must be included in the standard validate sequence alongside
`make test-harness` and `make test`.

---

## Run in Emulator

```bash
make run
```

Builds the ROM if necessary and launches it in the configured emulator.

---

## Capture Visual Screenshot

```bash
make screenshot
```

Produces:

```text
build/screenshot.png
```

Screenshots are useful for visual validation, but they are **not the primary debugging interface**.

Agents must inspect semantic state and telemetry first.

---

## Clean Build Artifacts

```bash
make clean
```

Removes generated artifacts in:

```text
build/
```

## Debug Harness & Protocol

The development harness is a first-class part of the project and must be used for gameplay development and validation.

The authoritative specification for the debug interface is:

```text
docs/DEBUG_PROTOCOL.md
```

Before modifying or extending the debug harness, telemetry system, scenario system, debug commands, or automated gameplay testing, read `docs/DEBUG_PROTOCOL.md`.

The protocol defines:

* Debug ROM behavior
* Scenario loading and state initialization
* Deterministic input injection
* Frame stepping
* Game-state inspection
* Entity/world inspection
* Telemetry events and sequence numbers
* Story flag inspection
* Battle-state inspection
* Audio-state inspection
* RNG control
* Assertions
* Scenario PASS/FAIL behavior
* LLM-oriented diagnostic output

### Agent Requirements

1. **Use the harness instead of manually playing through the game whenever a scenario can reproduce the behavior being tested.**
2. **Create or update a scenario when implementing important gameplay behavior that should be regression-tested.**
3. **Ensure important gameplay state transitions emit semantic telemetry events.**
4. **Ensure important gameplay state is exposed through `INSPECT`/`SNAPSHOT` where an automated agent needs it to diagnose behavior.**
5. **Use deterministic RNG seeds for scenarios involving randomness.**
6. **Do not make tests depend on screenshots when semantic debug state can provide the required information.**
7. **When a scenario fails, inspect the state and telemetry before modifying gameplay code.**
8. **Keep debug operations separate from real gameplay events.** For example, a debug teleport must not emit `PLAYER_MOVED`.
9. **Treat `docs/DEBUG_PROTOCOL.md` as the protocol contract.** If implementation and documentation disagree, resolve the discrepancy rather than silently inventing a new behavior.
10. **When adding a new gameplay system, consider its observability requirements as part of the implementation, not as a later debugging task.**

The goal is for an LLM to be able to reproduce, execute, inspect, and diagnose gameplay scenarios without playing through the entire game manually.

---

# 3. Target Platform & Toolchain

* Target Hardware: Nintendo Game Boy (DMG) / Game Boy Color (CGB)
* C Toolchain: GBDK-4 (`lcc`)
* Assembly Toolchain: RGBDS (`rgbasm`, `rgblink`, `rgbfix`)
* Development Emulator: SameBoy
* Build Environment: Nix

The code must remain compatible with the actual Game Boy target.

Do not accidentally introduce desktop-only assumptions into gameplay code.

---

# 4. Project Structure

The project is organized by gameplay responsibility.  The engine
(`core/`, `rpg/`, `world/`, `battle/`, `ui/`, `screens/`, `input/`, `audio/`,
`debug/`) is generic and owns no game content; the game layer (`game/`) owns
everything that makes THIS RPG (content tables, named ids, initial state,
stat hooks, per-shop stock).

```text
src/
├── main.c
├── crt0.s
│
├── game/                        ← game layer: content, registered with the engine
│   ├── game_ids.h               ← named story-flag/variable/currency semantics
│   ├── content.c                ← game_new_game, victory hook, stat hooks
│   ├── events.c                 ← scripted-event table (quests)
│   ├── dialogue.c               ← dialogue table
│   ├── actors.c                 ← per-map actor definitions
│   ├── items.c                  ← item catalog
│   └── shops.c                  ← per-shop stock lists
│
├── core/
│   ├── game.c                   ← Game struct + boot/update glue
│   ├── event.c                  ← generic event engine (registered table)
│   ├── dialogue.c               ← generic dialogue player (registered table)
│   └── story.c                  ← generic flag helpers
│
├── rpg/
│   ├── state.c                  ← generic GameState storage
│   ├── items.c                  ← generic item mechanics
│   ├── inventory.c
│   ├── currency.c
│   ├── party.c
│   └── progression.c            ← generic progression engine
│
├── world/
│   ├── world.c
│   ├── scene.c
│   ├── entity.c
│   ├── actor.c                  ← generic actor engine (registered tables)
│   └── interaction.c
│
├── battle/
│   ├── battle.c
│   └── combatant.c
│
├── input/
│   └── input.c
│
├── audio/
│   └── audio.c
│
├── ui/
│   ├── ui.c
│   └── menu.c                   ← shared MenuFrame menu layout
│
├── screens/
│   ├── screen.c
│   ├── overworld_screen.c
│   ├── battle_screen.c
│   ├── dialogue_screen.c
│   ├── item_screen.c
│   ├── shop_screen.c
│   ├── game_over_screen.c
│   ├── ending_screen.c
│   └── thanks_screen.c
│
└── debug/
    ├── telemetry.c
    ├── rng.c
    ├── scenarios.c
    └── assertions.c

tools/
├── dev.py
├── test_runner.py
├── emulator.py
└── scenarios/
    └── *.json

build/
└── generated artifacts
```

The exact file structure may evolve, but responsibilities must remain separated
and game content must stay in the game layer, never embedded in engine files.

---

# 5. Core Code Philosophy

## 5.1 Simple C

Prefer clear, explicit C over complex macro systems, excessive indirection, or clever abstractions.

Game Boy code should be understandable to another developer or coding agent.

---

## 5.2 Small Functions

Functions should have one clear responsibility.

Prefer:

```c
battle_start();
battle_update();
battle_end();
```

over giant functions containing an entire gameplay system.

---

## 5.3 Explicit State

Represent gameplay state using plain C structs and enums.

Prefer:

```c
typedef enum {
    GAME_STATE_OVERWORLD,
    GAME_STATE_BATTLE
} GameState;
```

over implicit state encoded through unrelated booleans.

---

## 5.4 Game Boy Native

Be conscious of:

* 8-bit arithmetic;
* RAM limits;
* VRAM access;
* VBlank timing;
* tile memory;
* stack usage;
* ROM/RAM layout.

Do not write desktop-style code and assume the compiler will make it appropriate for Game Boy hardware.

---

## 5.5 No Unnecessary Dependencies

Do not introduce modern game engines, scripting runtimes, JavaScript environments, Python runtime dependencies, or external libraries into the ROM.

Host-side tooling may use Python standard-library functionality where appropriate.

---

# 6. LLM-First Development Principle

The development harness is a **first-class game subsystem**.

The goal is not merely to provide convenient debugging tools for humans.

The goal is:

> An LLM must be able to start the game in a known state, control it, inspect what happened, determine whether behavior was correct, and reproduce failures without needing a human to interpret the screen.

The game screen is the player-facing interface.

The semantic debug interface is the development-agent-facing interface.

---

# 7. Semantic Observability Is Authoritative

When debugging or testing, prefer:

1. structured game state;
2. telemetry events;
3. scenario results;
4. semantic map/entity information;
5. screenshots.

Do not infer gameplay state from pixels when authoritative semantic information is available.

For example, this is insufficient:

```text
SCREEN:
@     E
```

The harness should expose information such as:

```text
GAME:
  state: OVERWORLD

PLAYER:
  map: field
  position: (8,5)
  facing: EAST

ENTITY:
  id: slime_01
  type: enemy
  position: (9,5)
```

Visual rendering is presentation.

Semantic state is authoritative.

---

# 8. Every Important Gameplay Event Must Be Observable

The following kinds of behavior MUST emit telemetry:

* game-state transitions;
* map transitions;
* player movement;
* collisions;
* encounter detection;
* encounter start;
* battle start;
* battle actions;
* damage;
* healing;
* entity defeat;
* battle victory;
* battle defeat;
* story event triggers;
* story flag changes;
* scripted event triggers (`SCRIPT_TRIGGERED` with the stable `EventId`);
* dialogue start/end;
* card actions;
* deck changes;
* random outcomes when relevant;
* audio track changes.

Use:

```c
telemetry_emit(...)
```

or the project's equivalent telemetry API.

Do not create important gameplay behavior that is invisible to the harness.

---

# 9. Telemetry Events

Events must have stable semantic names.

Prefer:

```text
PLAYER_MOVED
COLLISION
ENCOUNTER_STARTED
BATTLE_STARTED
DAMAGE_DEALT
STORY_FLAG_SET
GAME_STATE_CHANGED
MUSIC_CHANGED
```

Do not use vague messages such as:

```text
"something happened"
"battle!"
"hit"
```

Events should contain relevant structured information.

Example:

```text
PLAYER_MOVED
  entity: player
  from: (12,8)
  to: (13,8)
```

Example:

```text
COLLISION
  entity_a: player
  entity_b: town_guard_01
```

Example:

```text
GAME_STATE_CHANGED
  from: OVERWORLD
  to: BATTLE
```

---

# 10. Telemetry Ring Buffer

Game Boy memory is limited.

Telemetry must use a bounded ring buffer.

The initial target is approximately 32 recent events, unless the implementation demonstrates that another size is more appropriate.

Telemetry must never grow without bound.

The harness should expose recent events and, where supported:

```text
EVENTS SINCE <sequence>
```

This allows an LLM to inspect only what happened after its previous action.

---

# 11. Stable Entity IDs

Gameplay entities must have semantic IDs.

Examples:

```text
player
slime_01
town_guard_01
mayor
boss_01
```

Do not identify entities by their visual representation.

An enemy being rendered as:

```text
E
```

does not make `"E"` its identity.

The semantic entity may be:

```text
id: town_guard_01
type: enemy
```

This ensures tests continue working when ASCII graphics are eventually replaced by sprites.

---

# 12. Explicit Coordinates

World entities must have inspectable world coordinates.

Always report:

```text
map
x
y
```

and, where relevant:

```text
facing
```

Example:

```text
PLAYER
  map: town
  x: 12
  y: 8
  facing: EAST
```

Coordinates must represent world/game coordinates, not screen-pixel coordinates.

---

# 13. Machine-Readable Game Inspection

The debug harness must support semantic inspection.

Conceptual commands include:

```text
INSPECT
INSPECT AREA <radius>
SNAPSHOT
EVENTS
```

A snapshot should expose enough information for an LLM to understand the current gameplay state without seeing the screen.

Minimum information:

```text
GAME
PLAYER
PARTY
MAP
ENTITIES
STORY FLAGS
BATTLE
AUDIO
RNG
FRAME
RECENT EVENTS
```

Do not add large irrelevant data to every response.

Prefer concise, layered information.

---

# 14. Spatial Inspection

The harness must support a machine-readable representation of the current map/area.

For the current ASCII prototype, an ASCII representation is acceptable and useful.

Example:

```text
    01234567890123456789
00  ####################
01  #..................#
02  #..................#
03  #.......@..........#
04  #..................#
05  #.............E....#
06  #..................#
07  ####################
```

The harness should additionally provide semantic entity information.

Do not make spatial understanding dependent solely on ASCII.

---

# 15. Input Control

The development harness must support programmatic input.

At minimum:

```text
PRESS UP
PRESS DOWN
PRESS LEFT
PRESS RIGHT
PRESS A
PRESS B
PRESS START
PRESS SELECT
```

It must also support:

```text
WAIT <frames>
STEP <frames>
```

The test runner must be able to reproduce the exact same sequence of inputs.

---

# 16. Frame Control

Debug builds must support deterministic frame advancement.

Examples:

```text
STEP 1
STEP 10
STEP 60
```

This is required for investigating:

* timing bugs;
* state transitions;
* animation logic;
* audio transitions;
* input problems;
* race-like behavior between systems.

---

# 17. Deterministic Randomness

All gameplay randomness must go through a game RNG abstraction.

Do not directly use uncontrolled random functions throughout gameplay code.

The debug harness must support:

```text
SET_RNG <seed>
```

and scenarios must be able to define an initial seed.

The current RNG state/seed must be inspectable.

The same scenario, seed, and input sequence must produce the same behavior.

This becomes particularly important for:

* enemy behavior;
* damage;
* card draws;
* deck shuffling;
* random encounters;
* loot;
* critical hits;
* future procedural systems.

---

# 18. Scenarios Are First-Class Test Fixtures

A scenario represents a deterministic, named gameplay situation.

Examples:

```text
new_game
first_encounter
town_arrival
town_event_01
town_event_repeat
battle_basic
battle_victory
```

A scenario must establish state rather than bypass the behavior being tested.

Bad:

```text
scenario_town_event_01()
{
    start_town_event();
}
```

Good:

```text
scenario_town_event_01()
{
    set_map(TOWN);
    set_player_position(...);
    set_flag(MET_MAYOR);
    clear_flag(TOWN_ATTACK_STARTED);
}
```

Then normal game logic must detect the trigger and start the event.

This distinction is mandatory.

---

# 19. Every New Gameplay Feature Should Have a Scenario

When adding a significant gameplay feature, agents should add at least one reproducible scenario demonstrating it.

Examples:

```text
movement_wall_collision
first_encounter
town_arrival
town_event_01
town_event_repeat
battle_basic_attack
battle_victory
battle_defeat
card_draw
card_play
card_combo
boss_phase_02
```

The scenario becomes executable documentation of the feature.

---

# 20. Scenario Determinism

A scenario must specify enough state to reproduce its behavior.

This may include:

```text
map
player position
player facing
HP
party
inventory
story flags
enemy state
battle state
RNG seed
audio state
```

Do not rely on whatever state happened to remain in RAM from a previous test.

Every scenario must begin from a known state.

---

# 21. Assertions

Scenarios must support machine-checkable assertions.

Examples:

```text
game.state == BATTLE

player.hp == 20

story.TOWN_ATTACK_STARTED == true

audio.track == BATTLE

player.position == (12,8)

event_occurred("ENCOUNTER_STARTED")
```

Supported assertion categories should include:

```text
equals
not_equals
greater_than
less_than
contains
exists
event_occurred
event_not_occurred
```

Assertions must produce clear expected/actual information.

---

# 22. Scenario Test Results

A scenario must return one of:

```text
PASS
FAIL
```

Failure output must contain enough information for an LLM to diagnose the problem.

Example:

```text
SCENARIO: town_event_01
STATUS: FAIL

FAILED ASSERTION:
  game.state == BATTLE

EXPECTED:
  BATTLE

ACTUAL:
  OVERWORLD

PLAYER:
  map: town
  position: (16,8)

RECENT EVENTS:
  PLAYER_MOVED
  COLLISION

STORY:
  TOWN_ATTACK_STARTED: false

AUDIO:
  track: TOWN
```

The harness should report facts.

Do not fabricate explanations that cannot be supported by telemetry.

---

# 23. Host-Side Test Runner

The host-side harness in `tools/` is responsible for:

1. building the debug ROM;
2. launching SameBoy;
3. establishing the debug transport;
4. loading scenarios;
5. sending inputs;
6. advancing frames;
7. retrieving semantic state;
8. retrieving telemetry;
9. evaluating assertions;
10. producing machine-readable and human-readable results;
11. returning a meaningful process exit code.

The test runner must be independent of the game's visual presentation.

---

# 24. Emulator Transport

The Game Boy does not have a normal command-line interface.

Therefore the host-side harness must use an emulator-supported communication/control mechanism.

The transport must be isolated behind an abstraction.

Conceptually:

```text
DebugProtocol
      |
      v
Transport
      |
      +-- SameBoy implementation
      |
      +-- future hardware implementation
```

Do not spread SameBoy-specific details throughout gameplay or test logic.

Before changing the transport, inspect the actual SameBoy version/configuration available in the Nix environment.

Never assume an emulator feature exists without verifying it.

---

# 25. Debug Protocol

The development protocol should expose operations equivalent to:

```text
connect()
disconnect()

load_scenario(name)

press(button)

wait(frames)

step(frames)

inspect()

snapshot()

events()

assert(expression)
```

The exact wire format may change.

The semantic contract must remain stable.

---

# 26. Release/Debug Separation

Debug functionality must not alter the normal game architecture.

Prefer:

```text
GAME SYSTEMS
    |
    +---- normal gameplay APIs
    |
DEBUG HARNESS
    |
    +---- observes and controls normal systems
```

Avoid embedding large amounts of test-specific behavior directly into gameplay logic.

Debug code may construct state and invoke normal APIs.

It should not create an entirely separate implementation of gameplay.

---

# 27. Debug Commands

The initial debug command vocabulary should include:

```text
HELP

LOAD_SCENARIO <name>

RESET

PRESS <button>

WAIT <frames>

STEP <frames>

INSPECT

INSPECT AREA <radius>

SNAPSHOT

EVENTS

EVENTS SINCE <sequence>

ASSERT <expression>

SET_FLAG <flag>

CLEAR_FLAG <flag>

TELEPORT <map> <x> <y>

SET_HP <entity> <value>

SET_RNG <seed>
```

Commands should have stable names and predictable results.

---

# 28. Debug UI

A minimal in-ROM debug UI may exist for human developers.

It may expose:

```text
DEBUG MENU

> SCENARIO
  TELEPORT
  FLAGS
  STATE
  EVENTS
  AUDIO
  RNG
```

However, this UI is secondary.

The machine-readable harness is the primary development interface.

---

# 29. Layered Diagnostics

Diagnostics should have three levels.

## Summary

```text
STATE: BATTLE
PLAYER: 18/20 HP
ENEMY: 7/10 HP
```

## Full semantic state

All relevant state.

## Trace

Frame/event-level information.

Do not dump enormous amounts of information when a concise answer is sufficient.

Agents should be able to request more detail when necessary.

---

# 30. Trace Categories

Where useful, support selectively enabled trace categories:

```text
TRACE_INPUT
TRACE_WORLD
TRACE_COLLISION
TRACE_STORY
TRACE_BATTLE
TRACE_AUDIO
TRACE_STATE
```

This allows targeted investigation without overwhelming the test output.

---

# 31. Game Boy Memory Constraints

The harness must respect the target hardware.

Avoid:

* dynamic allocation where unnecessary;
* unbounded logs;
* giant strings;
* giant JSON buffers in Game Boy RAM;
* formatting large diagnostic reports every frame;
* unnecessary per-frame telemetry.

The Game Boy-side representation should remain compact.

The host-side tools should perform expensive formatting and aggregation.

---

# 32. Serialization

Do not build large JSON documents inside Game Boy RAM unless there is a demonstrated need.

Prefer compact debug messages/events.

The host-side tool may transform compact messages into structured JSON.

For example, Game Boy-side:

```text
EVENT
TYPE=COLLISION
A=player
B=town_guard_01
```

Host-side:

```json
{
  "type": "COLLISION",
  "entity_a": "player",
  "entity_b": "town_guard_01"
}
```

The semantic meaning is more important than the exact serialization format.

---

# 33. Story State

Story progression should be represented explicitly using stable flags or equivalent state.

Examples:

```text
MET_MAYOR
HAS_MAGIC_STONE
TOWN_ATTACK_STARTED
TOWN_ATTACK_COMPLETE
BOSS_DEFEATED
```

Story state must be inspectable.

Story state must be scenario-configurable.

Story transitions must emit telemetry.

---

# 34. Audio Observability

Audio state must be represented semantically.

For example:

```text
AUDIO
  track: TOWN
  playing: true
```

When music changes:

```text
MUSIC_CHANGED
  from: TOWN
  to: BATTLE
```

Tests should assert audio state semantically rather than attempting to analyze recorded audio.

---

# 35. Hardware Timer Sound Timing (music clock)

Never update music step timers directly inside the main `while(1)` loop.

Main-loop CPU variations can cause music to play at variable tempos between menus and gameplay.

Music must run on the **hardware Timer interrupt** (TIMA overflow), driven by
a dedicated RAM-resident ISR installed by the custom CRT0:

* `src/crt0.s` patches the timer vector (`0x0050`) to `JP 0xC900` (WRAM,
  always mapped regardless of ROM bank), and the VBlank vector (`0x0040`) to
  a plain `reti` (VBlank IE is off).
* At boot `init` copies a small ISR (`timer_isr`) from ROM to WRAM `0xC900`
  and enables **timer-only IE** (`IE = 0x04`).  The ISR calls `_audio_update`
  directly (a baked-in `call`, so no function pointers / banked-call
  helpers) and `reti`s.
* `audio_init()` programs the timer: `TMA = 0`, `TAC = TACF_START | TACF_65KHZ`
  (65536 Hz clock) → TIMA overflows at **256 Hz**, independent of CPU/frame
  pacing.  This is a hard requirement, not a style choice:

  > VBlank is a PPU mode (`STAT.md`).  Every screen/map transition performs a
  > full redraw with the LCD **off** (`ui_lcd_off()`/`ui_lcd_on()` in
  > `src/core/game.c`), and Pan Docs `LCDC.md` says the screen stays blank
  > for the first frame after re-enable — during those 1-2 frames **no
  > VBlank interrupt fires at all**, so a VBlank-driven scheduler stalls the
  > music while the APU keeps ringing (the audible 1-2 frame "stop" on every
  > gate crossing / dialogue / shop / menu transition).  The timer keeps
  > counting throughout, so the music clock is decoupled from the LCD.
  > The only things that disturb the timer rate are CGB double-speed mode
  > and writes to `DIV` — this game does neither (verified by grep), so
  > 256 Hz is stable.

* `audio_update`/`play_note` and the note tables must stay in the **fixed
  bank 0** (`< 0x4000`) so the ISR's `call` target and table reads are
  always mapped.
* Tempos are expressed in **timer ticks** (`audio_update`): OVERWORLD 43
  ticks/note (~6 notes/sec), BATTLE 17 ticks/note (~15 notes/sec) — these
  preserve the old VBlank-tick tempos (10 and 4 at ~59.73 fps).
* `main.c` calls `audio_init()` then `enable_interrupts()` (enables IME);
  the ISR and IE are already set up by CRT0.

Do NOT use `add_VBL()`: the custom CRT0 stubs it (the GBDK interrupt
manager is RAM-resident and the harness skips CRT0), so it is a no-op.

Always call:

```c
enable_interrupts();
```

after boot init.

Audio transitions must emit telemetry.

## 35.1 Tracker Music Architecture (hUGEDriver in Bank 6)

Soundtrack tracks authored in **hUGETracker** (`.uge`, e.g. `assets/music/Battle BGM.uge`) are compiled to C via `uge2source` into `generated/music/` and driven by **hUGEDriver**:

* **ROM Bank 6 Isolation**:
  * The driver assembly (`lib/hUGEDriver/src/hUGEDriver.asm` via `tools/rgb2sdas.py -b 6`) and all converted track data (`#pragma bank 6`, six songs) live in **ROM Bank 6**.  The driver reads song bytes through the mapped ROM window, so songs cannot live anywhere else.
  * This keeps the fixed Bank 0/1 memory budget (`_CODE`/`_HOME`) clean and prevents ROM0 overflow.
  * Bank 6 is full (16224/16384 B): the transcribed-SFX step tables plus stepper (`generated/sfx/sfx_tables.c`, `src/audio/sfx_step.c`, `#pragma bank 7`) live in **ROM Bank 7** alongside the icon table.  The timer ISR selects bank 7 around `sfx_step_tick()` and bank 6 around `hUGE_dosound()`; muting targets bank 6 (driver code + state).  Do NOT add another song to bank 6 -- `make memmap` fails on any `_CODE_N` over 16 KB.
* **Bank-7 overflow songs (dual-bank playback)**:
  * A seventh song (`Mimic.uge` -> `generated/music/mimic.c`, symbol `song_mimic`, `#pragma bank 7`) lives in **ROM Bank 7** next to a **second copy of the hUGE driver** (`build/*/lib/hUGEDriver_b7.o`: same code, all exports renamed `_b7` via repeatable `rgb2sdas.py -r`).  Driver + song must share a bank because the driver reads song bytes through the mapped window.
  * `huge_music_play_banked(song, bank)` records the song's bank in `s_huge_music_bank`; `huge_music_update()` / `huge_music_mute_channel()` select that bank around the `_b7` (bank 7) or plain (bank 6) driver call, then restore home bank 1.  `huge_music_play()` is the bank-6 shorthand.  New songs go to bank 7 through this path -- never bank 6.
  * The assembler exports underscore twins (`hUGE_init` + `_hUGE_init`, ...): a second-bank conversion must rename BOTH (missing twins surface as `Multiple definition of _hUGE_*` at link).  Recipe-only Makefile edits do not retrigger the rule (prerequisites unchanged) -- delete the `hUGEDriver_b7.o`/`.obj` pair first.
* **Tick Division (64 Hz from 256 Hz Timer)**:
  * The hardware timer ISR (`src/crt0.s`) calls `audio_update()` at **256 Hz**.
  * `huge_music_update()` divides this rate by 4 (`++divider >= 4`), stepping `hUGE_dosound()` at a steady **64 Hz** tracker clock.
* **Bank-Switch & Interrupt Safety**:
  * User-space APIs (`huge_music_play`, `huge_music_mute_channel`, `huge_music_stop`) are wrapped in `__critical` (`di`/`ei`).
  * When switching to Bank 6 (`*(volatile uint8_t *)0x2000 = 6`), they always restore the architectural home bank (`*(volatile uint8_t *)0x2000 = 1`) before leaving.
* **SFX Coexistence (Channel Muting)**:
  * Sound effects (CH2 tone pulses, CH4 noise bursts) call `huge_music_mute_channel(HT_CH2 / HT_CH4, HT_CH_MUTE)` on playback start.
  * When the SFX completes, `audio_update()` restores the channel with `HT_CH_PLAY` so the tracker music continues seamlessly without channel clicks.
* **NEVER `__critical`/`ei()` inside the timer ISR**: SDCC's `__critical`
  emits `di`/`ei` -- an `ei()` executed while already inside the 256 Hz
  timer ISR re-enables nested timer interrupts, stacking ISR frames until
  the WRAM below the stack is smashed.  Observed symptom (Sep 2026):
  ghost joypad input -- `pad_state`/`prev_pad_state` accumulate pressed
  bits and releases never register, freezing the intro screen in every
  real-timing emulator (mGBA, Gambatte) while PyBoy's frame pacing masked
  it.  `audio_update()`'s ISR-side unmute therefore calls
  `huge_music_mute_channel_isr()` (bank switch + driver call + restore,
  no di/ei); the user-context calls in `audio_play_sfx()` keep the
  `__critical` wrapper.  Rule: any driver call reachable from
  `audio_update()` must use the `_isr` (no-lock) variant, never the
  critical one.

---

# 36. Targeted Redrawing

Avoid calling full-screen clears:

```c
ui_clear_screen();
```

during frequent interactive events such as:

* menu navigation;
* cursor movement;
* player movement;
* UI updates.

Full clears are appropriate for major screen transitions such as:

```text
TITLE -> OVERWORLD
OVERWORLD -> BATTLE
BATTLE -> OVERWORLD
```

Use incremental tile updates wherever practical.

Visual performance should not compromise semantic game state.

---

# 37. Joypad Startup State

In:

```text
input_init()
```

initialize both:

```text
pad_state
prev_pad_state
```

to the current hardware:

```c
joypad()
```

Leaving:

```text
prev_pad_state = 0
```

can cause `input_pressed()` to incorrectly report a button press during boot.

Debug input injection must use the same input abstractions as normal input wherever practical.

---

# 38. Game Boy Color Palette & Attribute Mapping

Use:

```text
-Wm-yc
```

and appropriate:

```text
rgbfix -C
```

header flags for CGB compatibility.

Header byte `0x143` should indicate compatibility appropriately (`0xC0` for CGB-exclusive, `0x80` for dual compatibility).

### 38.1 Avoid Red/Blue Boot ROM Fallback: Program CGB Palettes Unconditionally

Per Pan Docs (`src/Palettes.md`, "LCD Color Palettes"): on CGB hardware / CGB emulators, if CGB Palette RAM (`0xFF68`–`0xFF6B`) is left uninitialized, the Game Boy Color PPU falls back to the built-in Nintendo Boot ROM compatibility palette table (Table index `0x1C`).
This turns all background graphics **red/orange** (`#D85840`) and sprites **blue** (`#0000F8`).

**Why this bug recurs**:
1. `_cpu` is set from register `A` on boot (`0x11` on CGB). When emulators or test harnesses skip the Nintendo boot animation, register `A` can be `0x00`.
2. Guarding CGB palette initialization with `if (_cpu == CGB_TYPE)` causes the entire palette setup to be skipped whenever `_cpu == 0`, immediately triggering the red/blue boot ROM fallback.
3. On DMG hardware, writing to `0xFF68`–`0xFF6B` (`BCPS`/`BCPD`/`OCPS`/`OCPD`) is completely harmless (these registers are unmapped no-ops on DMG).

**Rule**: In `ui_init()`, **always program both DMG palettes and all 8 CGB BG and OBJ palettes unconditionally** with explicit per-byte indexing (`0x80 | p`), ensuring clean grayscale/configured colors on every emulator and hardware model:

```c
    /* Set DMG palettes: 0xE4 = 11 10 01 00 (Lightest to Darkest) */
    BGP_REG = 0xE4;
    OBP0_REG = 0xE4;
    OBP1_REG = 0xE4;

    /* Set CGB Palettes 0-7 unconditionally. Safe on both DMG and CGB. */
    for (p = 0; p < 64; p++) {
        BCPS_REG = (uint8_t)(0x80 | p);
        BCPD_REG = ((const uint8_t *)cgb_palette)[p & 7];
    }
    for (p = 0; p < 64; p++) {
        OCPS_REG = (uint8_t)(0x80 | p);
        OCPD_REG = ((const uint8_t *)cgb_sprite_palette)[p & 7];
    }
```

Always reset:

```c
VBK_REG = 0;
```

after writing tile attributes to VRAM Bank 1.

Do not assume CGB hardware when reading CGB-only features on DMG.

### 38.2 CGB object palettes are NOT initialized by the boot ROM

Per Pan Docs (`src/Palettes.md`): in CGB mode the boot ROM leaves all object colors uninitialized ("somewhat random/unreliable"). `FF48–FF49` (`OBP0`/`OBP1`) are **Non-CGB-Mode only** registers; on CGB hardware they do not drive sprite colors at all.

Consequence: every visible sprite must have a valid CGB OBJ palette programmed in CRAM (as done in `ui_init()`), with full 4-shade contrast matching the background.

Reference: local Pan Docs checkout at `/home/brodrigues/Documents/repos/pandocs`
(`src/Palettes.md`, `src/Power_Up_Sequence.md`).

---

# 39. SDCC / GBDK C89 Rules

GBDK-4 uses an SDCC C89-style language environment.

Declare variables at the beginning of function blocks.

Avoid:

```c
if (condition) {
    uint8_t value = ...;
}
```

Prefer:

```c
uint8_t value;

if (condition) {
    value = ...;
}
```

Do not use C99/C11 features unless verified to compile correctly with the project's exact toolchain.

Avoid non-constant array initializers unsupported by the target compiler.

---

# 40. Screenshot Capture

Automated screenshot capture should allow sufficient startup time for the Game Boy boot sequence.

Allow at least:

```text
sleep 4
```

before capturing screenshots unless the capture system has a more reliable readiness signal.

Screenshots are for visual verification only.

Do not use screenshots as the primary automated gameplay assertion mechanism when semantic telemetry is available.

---

# 41. Agent Workflow

When modifying gameplay code, agents should follow this process:

## Step 1 — Understand

Inspect:

* relevant source files;
* current game state;
* existing telemetry;
* relevant scenarios;
* related tests.

Do not immediately modify code.

---

## Step 2 — Reproduce

Run the smallest relevant scenario.

Example:

```bash
make test-scenario SCENARIO=town_event_01
```

If no scenario exists, create one before implementing complex behavior when practical.

---

## Step 3 — Observe

Inspect:

* snapshot;
* telemetry;
* event sequence;
* assertion failure;
* RNG state;
* relevant game state.

Do not rely on visual inspection unless the problem is specifically visual.

---

## Step 4 — Implement

Make the smallest architectural change that fixes the behavior.

Preserve existing semantic interfaces.

Do not bypass normal game logic merely to make a scenario pass.

---

## Step 5 — Add/Update Tests

Every bug fix involving gameplay behavior should add or update a scenario or assertion that would have caught the bug.

The test should fail before the fix and pass after it whenever practical.

---

## Step 6 — Validate

Run:

```bash
make test-harness
```

then:

```bash
make test
```

then:

```bash
make verify-oam
```

`verify-oam` uses mGBA's debugger to assert correct OAM sprite positions
across transitions (battle, scene change, dialogue).  It catches VBlank-timed
sprite bugs that the SameBoy harness cannot observe (AGENTS.md §52.15).  This
step is **required**, not optional — CI runs it on every push.

Then run:

```bash
make verify-walkthrough
```

`verify-walkthrough` (docs/verify-walkthrough.md) drives the RELEASE ROM —
the real `levels/` content the fixtures never touch — headlessly under
PyBoy and asserts canonical gameplay state read from WRAM via the ROM's
`.sym` (scene ids, flags, gold, party HP, battle hand, music, save/load
roundtrip).  It is the real-content counterweight to the two-tier fixture
suite (§42.1) and is **required on push** (CI).  Host-side only: no ROM
changes → it does not replace `make test-harness` for ROM work, it
complements it.

If rendering or UI changed, also run:

```bash
make screenshot
```

or:

```bash
make run
```

## Step 6b — Level-editor-only changes skip the ROM harness

`make test-harness` boots the debug ROM and runs every gameplay scenario.  It
is only necessary when the ROM's C (or headers, or generated `.inc` tile data)
changed in a way that could affect gameplay.  Changes confined to the host-side
level editor — `tools/level_editor/**` (TypeScript, JSON tileset definitions,
extracted `public/tiles/**` PNGs), `tools/asset_atlas.py`, JSON compilation, or
`tools/level_editor/extract_tiles.py` — do **not** exercise the ROM:
`make test-harness` adds no signal and can take many minutes, so skip it.

For host-side-only work the proportional validation is:

* the TypeScript/JSON changes build (`npm run build` in `tools/level_editor/`);
* any source asset / coordinate mapping is consistent (`make atlas`, and where
  relevant `python3 tools/asset_atlas.py --check`);
* if new tileset definitions were added, verify they load in the editor.

If a change *also* regenerates ROM-included tile data (a `make gfx` output such
as `src/gfx/rpg_*_tiles.inc` linked into the ROM), then `make test` +
`make memmap` (fixed-bank / memory budget, §55.5) plus the sentinel scenarios
(§52.19) are the right gate — not the full 174-scenario suite.  Reserve the
full `make test-harness` for genuine ROM gameplay changes.

---

# 42. Scenario-Driven Development

For substantial gameplay features, follow this development pattern:

```text
Scenario
    ↓
Expected behavior
    ↓
Implementation
    ↓
Telemetry
    ↓
Assertions
    ↓
Automated test
```

Do not wait until the end of a feature to think about testability.

The scenario is part of the feature.

## 42.1 Two-tier content: fixtures vs working content

The scenario suite must never break because someone placed content on a
map.  Two tiers enforce that:

* **Tier 1 — test fixtures** (`tools/scenarios/fixtures/levels/test_*.json`):
  frozen copies of the levels as the scenarios know them (headline
  hostile last, no user-placed extras).  The **debug (harness) ROM links
  ONLY these** (`scenes_content_test.c` / `actors_content_test.c`, bank 4,
  `--bank 4`), selected at compile time by `-DTEST_LEVELS` + the
  `TEST_*` scene ids (fixed block at 240+, never moves).  Scenarios stage
  `scene: TEST_FIELD` etc.
* **Tier 2 — working content** (`levels/`): everything the editor, the
  LLM, and collaborators edit freely.  Feeds the **release ROM only**.
  Adding a level, enemy, or actor here cannot break a scenario.

## 42.2 Adding a level (human workflow — no agent required)

Scene ids are data, not code.  `levels/registry.json` is the single
source of truth (real scenes 0..N dense, TEST block fixed at 240+,
retired ids tombstoned and never reused); `src/world/scene_ids_generated.h`
is emitted from it by `compile.py --all` and carries every `MAP_*` /
`SCENE_*` value the engine needs (`world.h` / `screen.h` hold only the
`uint8_t` typedefs).

Human flow: create the level in the editor → **save it** (the dev server
assigns the next scene id on first save — no clicks, no C edits) → link
exits via the Target Scene combobox → compile.  Rules for agents:

* Never hand-edit ids in headers, `compile.py`, `validate.py`,
  `emulator.py`, or the walkthrough — they all derive from the registry.
  The only hand-written scene list left is `TEST_SCENE_ORDER` (the fixed
  fixture names).
* `validate.py` fails loudly on any registry/file disagreement (missing
  entry → "save the level in the editor"; missing file → "restore from
  git or retire the id"); `compile.py` refuses unknown sids the same way.
* Retired ids stay holes in `g_scenes[]` (inert empty rows keep direct
  indexing valid); never compact or reuse them.
* A new level is unreachable until some exit targets it — the content
  sweep fails loudly on unreachable levels by design, not by accident.
* **Registry is future-proofed**: `levels/registry.json` carries a
  `version` (currently 1); ids are append-only and never reused (deleted
  ids tombstone into `_retired`); every write is atomic (tmp + rename);
  `make registry-check` locks the contract.  The editor's rename keeps the
  **numeric scene id** and rewires referencing exits; delete retires the
  id and clears exits that targeted it.  Scenes whose `MAP_*`/`SCENE_*`
  symbols appear in hand-written C (e.g. `field`) cannot be renamed or
  deleted from the editor.

## 42.3 Human-authored text: dialogue & tutorial (data-driven)

NPC dialogue and the title-menu tutorial slides are editor content, not
C:

* Dialogues: `screens/dialogue/<id>.json` (`id`, `speaker`, `lines[]`,
  optional `completion_flag`).  Ids assign by sorted filename
  (`dialogue_ids.py` → `DIALOGUE_ID_*` + `GAME_DIALOGUE_COUNT` in the
  generated `dialogue_ids_generated.h`); nothing persists a numeric
  dialogue id, so adding/renaming is safe.  `dialogue_compile.py` emits
  `dialogue_content.c` (bank 2, same shape as before).
* Tutorial: `screens/tutorial.json` → `tutorial_compile.py` emits
  `tutorial_text_generated.h` (arrays, included once) +
  `tutorial_count_generated.h` (count, included by `screen.h`).
* The **editor** edits text only (Dialogue view: speaker + lines;
  title tab: slides).  Flag/event/scenario wiring stays LLM-authored:
  `completion_flag`, `EVENT_ACTION_*`, and `story` definitions are never
  shown in the editor.  Completion flags are a subset of `GameState.flags`
  (`src/game/game_ids.h` `STORY_FLAG_ID_*`); the compiler validates the
  reference.
* Gates: `make dialogues-check` (fresh-vs-committed + `validate.py
  --dialogue-refs`: actor/event references resolve, dangling ids fail
  loudly, unused dialogues warn).  `make screens-check` covers the
  tutorial.  Text limits (speaker 11, line 20 staging / 18 render; slide
  4 rows × 20) fail the compile loudly.
* Speakers are currently wired individually per NPC (shopkeeper/merchant/
  wizard carry empty `dialogue`; their greetings fire through events).
  Assigning a dialogue to an NPC is the editor's Dialogue-ID dropdown.

Rules:

* Never point a scenario at a real scene; never let the editor list or
  edit the fixtures dir.
* A new mechanic gets a new fixture + scenario deliberately; routine
  content never touches the fixtures.
* The event table is shared (TEST maps alias to real maps at match
  time in `core/event.c`); `game_new_game`/`world_init` derive the
  spawn map from canonical state, never a hardcoded real scene.
* `-DTEST_LEVELS` reaches ONLY the files that consume it, via explicit
  Makefile rules (scene/actors/scene_load/actor_load/content): the flag
  shifts codegen, and SDCC miscompiles are layout-sensitive (§52.19) —
  the full-flag variant broke the patrol sentinels.  If a new file
  needs the flag, add an explicit rule AND verify the three sentinels
  (`patrol_slime_cross`, `patrol_enemy_bumps_player`,
  `battle_multi_enemy_cycle_kill`) still pass.
* `make levels-test` / `levels-test-check` validate and compile the
  fixtures; `make debug` chains `levels-test` automatically.  memmap
  guards the bank-4 fixture budget.

---

# 43. Bug Reproduction

When an agent discovers a bug that depends on a particular state:

1. Create a deterministic scenario reproducing it.
2. Record the relevant RNG seed.
3. Record relevant story flags.
4. Record map/player/entity state.
5. Record the input sequence.
6. Record the expected result.
7. Add the scenario to the test suite.
8. Fix the bug.
9. Confirm the scenario passes.

A bug that cannot be reproduced should be treated as a development problem in its own right.

---

# 44. No "Magic" Test Fixes

Never make a scenario pass by adding test-only shortcuts to gameplay behavior.

For example, do not:

```text
if (debug && scenario == TOWN_EVENT_01)
    start_event();
```

Instead, construct the appropriate initial state and exercise the real gameplay path.

The harness must test the same logic used by the player.

---

# 45. Scenario Naming

Use stable, descriptive names.

Prefer:

```text
first_encounter
town_arrival
town_event_01
town_event_repeat
battle_basic_attack
battle_victory
```

Avoid:

```text
test1
foo
debugtest
newtest
```

Scenario names become part of the project's development API.

---

# 46. Test Output Must Be LLM-Friendly

Test output should:

* use stable names;
* explicitly state expected values;
* explicitly state actual values;
* include relevant recent events;
* include relevant state;
* avoid unnecessary noise;
* avoid ambiguous natural-language descriptions;
* preserve deterministic ordering.

A good failure should allow an LLM to answer:

> What happened, what was expected, and what should I inspect next?

without asking a human.

---

# 47. Example Good Failure

```text
SCENARIO: town_event_01
STATUS: FAIL

ASSERTION:
  game.state == BATTLE

EXPECTED:
  BATTLE

ACTUAL:
  OVERWORLD

PLAYER:
  map: town
  position: (16,8)

ENTITIES:
  town_guard_01:
    type: enemy
    position: (16,8)

STORY:
  MET_MAYOR: true
  TOWN_ATTACK_STARTED: false

AUDIO:
  track: TOWN

RECENT EVENTS:
  120 PLAYER_MOVED
  121 COLLISION

MISSING EVENTS:
  ENCOUNTER_STARTED
  GAME_STATE_CHANGED
  MUSIC_CHANGED
```

This is significantly more useful than:

```text
FAIL: town event didn't work
```

---

# 48. Current Harness Milestone

The current harness milestone is complete when an LLM can perform the following workflow without human intervention:

```text
1. Build debug ROM.

2. Launch emulator.

3. Load town_event_01.

4. Inspect initial state.

5. Send movement commands.

6. Advance frames.

7. Inspect player position.

8. Detect collision.

9. Inspect telemetry.

10. Confirm story event.

11. Confirm story flag.

12. Confirm battle state.

13. Confirm enemy initialization.

14. Confirm battle music.

15. Execute battle actions.

16. Confirm HP changes.

17. Complete battle.

18. Confirm return to overworld.

19. Confirm overworld music.

20. Produce PASS/FAIL.
```

No human should need to say:

> "Yes, the event happened on screen."

---

# 49. Future Harness Expansion

The architecture must support future systems including:

```text
Scenarios
Story Flags
Teleportation
Party State
Inventory
Equipment
Cards
Decks
Battle State
Enemy AI
Dialogue
NPCs
Cutscenes
Boss Phases
Audio
Save/Load
RNG
Map State
World State
```

For every new subsystem, expose semantic state and relevant telemetry.

For example, when the card system is added, the harness should eventually be able to report:

```text
BATTLE
  turn: 3
  player_hp: 18/20
  enemy_hp: 12/30

DECK
  draw_count: 14
  discard_count: 6

HAND
  fire_slash
  water_guard
  heal

EVENTS
  CARD_DRAWN
  CARD_SELECTED
  CARD_PLAYED
  DAMAGE_DEALT
  COMBO_RESOLVED
```

This should not require screenshot interpretation.

---

# 50. Definition of Done for Gameplay Work

A gameplay feature is not considered complete merely because it works when manually played.

For significant gameplay systems, completion requires:

* implementation;
* semantic state;
* telemetry;
* deterministic behavior where appropriate;
* at least one scenario;
* assertions;
* harness test passing;
* release ROM compiling.

For example, a town event is not complete merely because a human can walk into town and see it.

It is complete when:

```bash
make test-scenario SCENARIO=town_event_01
```

can prove that the event works.

---

# 51. Final Development Principle

The project should continuously move toward this development loop:

```text
                 ┌───────────────┐
                 │      LLM      │
                 └───────┬───────┘
                         │
                  write / inspect
                         │
                 ┌───────▼───────┐
                 │ Test Harness  │
                 └───────┬───────┘
                         │
                  scenario/input
                         │
                 ┌───────▼───────┐
                 │     ROM       │
                 └───────┬───────┘
                         │
              state/events/telemetry
                         │
                 ┌───────▼───────┐
                 │ Test Harness  │
                 └───────┬───────┘
                         │
                    PASS / FAIL
                         │
                 ┌───────▼───────┐
                 │      LLM      │
                 └───────────────┘
```

The long-term objective is:

> **An AI coding agent should be able to develop and test the RPG by interacting with its semantic development interface rather than by manually playing the game.**

The human-facing Game Boy UI remains important for the final player experience.

The development-facing semantic interface is equally important for building the game.

---

# 52. Verified GBDK, CRT0 & Harness Gotchas

These findings were discovered and verified during development. Treat them as
operating constraints. They are the most common sources of "it worked before,
now it hangs / renders wrong" regressions.

## 52.1 No function pointers in harness-exercised gameplay code

GBDK compiles any indirect call (function pointer) through `___sdcc_call_hl`
/ `___sdcc_banked_call`. Those helpers are **RAM-resident code copied to RAM
by CRT0 at boot**. The debug harness jumps straight to `main()` and skips
CRT0, so the helpers are not in RAM and any function-pointer call hangs the
ROM under the harness.

Do not use function pointers for gameplay dispatch (e.g. per-scene loader
functions). Use direct `switch` statements instead. Function pointers are
only safe in code that always runs through the full CRT0 boot.

## 52.2 The Makefile has no header dependencies

Changing a `.h` does not rebuild dependent `.c` files. Stale object files
compiled against an older header produce silent struct-layout mismatches
(e.g. `Entity` field offsets) that manifest as wrong HP values, wrong
positions, or crashes. After editing any shared header, always do a full
clean rebuild:

```bash
make clean && make debug && make release
```

## 52.3 Custom CRT0 `_DATA` layout is ABI-critical

GBDK expects its WRAM variables (`__cpu`, `_cpu`, `__current_bank`, `.mode`,
`.int`, `__shadow_OAM_base`, `.sys_time`) at specific addresses. In this
project `.mode` lives at `0xC0A4` — it was shifted from `0xC0A2` when `_cpu`
was added to the custom CRT0's `_DATA`. Never hardcode these addresses in C
code; expose them as C-visible symbols from `src/crt0.s`:

```asm
.mode:
        .ds     1
        .globl  _console_mode
_console_mode = .mode
```

and declare `extern uint8_t console_mode;` in C.

## 52.4 Custom CRT0 init order

The CRT0 WRAM clear (0xC000-0xDFFF) wipes anything stored before it. Set
`__cpu`, `_cpu`, `__is_GBA`, `__current_bank` **after** the clear. Detect the
CPU type from register A (set by the boot ROM: `0x11` CGB, `0x01` DMG) saved
before the clear, and store it into **both** `__cpu` and `_cpu` so
`_cpu == CGB_TYPE` works (this drives the CGB palette path).

## 52.5 `_HOME` area ordering in crt0.s

Declare areas in the order `_CODE`, `_HOME`, then `_DATA` (matching GBDK's
crt0.o). Declaring `_DATA` first makes sdldgb place gb.lib's `_HOME` code in
WRAM instead of ROM, breaking `joypad`, fonts, and rendering.

## 52.6 The mGBA CLI debugger does not advance VBlank/LY

While paused at a breakpoint, the mGBA debugger does not advance VBlank, so
any code that waits on VBlank (`vsync()`, `display_off()`, GBDK
`set_bkg_data()`) hangs under the harness. Keep VBlank waits out of
harness-reachable boot paths: the harness sets `g_harness_mode` to skip
vsync, and `ui_init()` turns the LCD off before `font_load()` so
`display_off()` returns immediately.

## 52.7 SDCC enums are 1 byte when all values fit

In this toolchain an `enum` whose values fit in a byte is 1 byte wide. Struct
layouts using small enums are compact. Mismatches come from stale objects
(52.2), not from enum sizing.

## 52.8 Host-side telemetry read contract

`g_telemetry_count` is a `uint8_t`; the byte after it is `g_telemetry_head`,
NOT a count high byte. Read it as a single byte, cap at
`TELEMETRY_CAPACITY`, and loop over `min(count, capacity)`. A 16-bit misread
caused ~3084 redundant reads and ~40s per scenario.

## 52.9 Emulator process teardown

`disconnect()` must kill the whole process group (SIGTERM then SIGKILL).
Killing only the `xvfb-run` wrapper orphans Xvfb/mgba processes, which
accumulate, exhaust display numbers, and slow or hang later connects. SIGKILL
also leaves stale `/tmp/.X11-unix/X*` sockets; clean them up if connects
start failing.

## 52.10 Debug input is edge-triggered

`g_inp_mask` is consumed once per `input_update()`. `input_pressed()` fires
only on a 0→1 edge. Consecutive presses of the same button therefore need a
reset frame (`WAIT 1`) between them; otherwise only the first press registers.
Scenario JSONs that walk multiple tiles must interleave `wait` actions.

## 52.11 GBDK RAM-resident library sections

Sections such as `.jpad`, `.hiramcpy`, and the banked-call helpers are
RAM-resident and copied by CRT0 at boot. The harness never runs that copy.
Keep `_HOME` in ROM (52.5) and avoid harness-exercised paths that depend on
RAM-resident library code.

The one RAM-resident section the custom CRT0 itself copies is the timer ISR
(see §35): `crt0.s` `init` copies `timer_isr` from ROM to WRAM `0xC900`
and enables timer IE (`IE = 0x04`; VBlank IE is off).  The harness skips this
(it never enables interrupts), so the ISR never runs under the harness.

### 52.11.1 Banked *code* execution: the banked-call trampoline

Moving a whole bank-0 *function* to a higher ROM bank is possible and does
NOT need GBDK's `__banked` / `___sdcc_banked_call` machinery (those helpers
are the RAM-resident library code that §52.1/§52.11 warn about).  Instead
mirror the `banked_copy` pattern:

* The module's logic moves to a banked file (`#pragma bank 2`) as a fixed
  **no-arg** function `xx_banked(void)` that reads its inputs only from the
  staging globals and writes its outputs only through a staged pointer.
  It must be fully self-contained — it may read its own banked const data
  and the staged WRAM globals, but must never `call` a fixed-bank function
  (the bank is switched away while it runs).
* A thin fixed-bank wrapper keeps the original signature, stages the bank +
  logical address + arguments into the `g_bk_call_*` globals
  (`src/core/banked.c`), then calls `banked_call_run()`.
* `crt0.s` `_banked_call_tramp` is the WRAM-resident body (copied by
  `_banked_call_init`, which `game_init()` calls alongside
  `banked_copy_init()` so it is resident under the harness too): it selects
  the bank, computes the runtime target `0x4000 | (addr & 0x3FFF)`, pushes
  the WRAM return address, and `jp`s to the target; the target's `ret` pops
  back to WRAM, which restores home bank 1, `__current_bank`, and (unless
  harness mode) `ei`.

First user: `src/battle/combo.c` (`combo_evaluate` wrapper) →
`src/battle/combo_content.c` (`combo_evaluate_banked`, bank 2).  This
relieved the fixed-bank budget enough for the battle HUD row work.
Second user: `src/world/scene.c` (`scene_spawn` wrapper) →
`src/world/scene_load.c` (`scene_spawn_banked`, bank 5, reads `g_scenes`
directly): the compiled player spawn reaches fixed-bank callers through
the `g_bk_byte_*` staging globals with no fixed-bank branching, which
cleared a `_HOME` overflow that presented as a harness-wide guest spin
(§52.18).

### 52.11.2 Battle HUD layout (rows)

The battle screen uses the fixed background rows: `0` centered banner
(DYNAMIC enemy-identity messages: `TARGET <name>` on the select phase --
follows UP/DOWN cycling via BATTLE_DIRTY_BANNER from the banked nav body,
`ATTACK <name>` on the ANIM phase from the victim snapshot
`g_battle_anim_target_name` (battle.c) because resolution may kill the
target and auto-advance target_idx before the banner draws,
`<name> ATTACKS!` on the telegraph, `<name>: DEFEND!` on the defend
phase; result/other phases keep the fixed literals VICTORY!/DEFEATED!/
FLED!/BLOCKED ATTACK!/RESHUFFLE!; fallbacks PLAYER TURN/PLAYER ATTACK!/
ENEMY ATTACK!/DEFENSE TURN keep every phase readable if a name slot is
invalid; composed in the bank-3 file-static `s_banner_text[24]`, names
cap at 11 chars so all banners fit 20 cols), `2` enemy HP (the former
name row; HP moved here when the name row was removed), `3-4` enemies
(art/caret), `7` hero, `8` deck counter (`DECK:` +
draw-pile count at columns 13-19, `battle_draw_deck_line`, drawn with the
hero row on BATTLE_DIRTY_HERO) + AP, `9` transient gameplay messages
(`NO ENERGY!`/`OUT OF USES!`/`ONE RING!`, BATTLE_DIRTY_MSG), `10` `COMBO:`
+ hand type (`PAIR`/`FLUSH`/`STRAIGHT` from `ui_combo_hand_name`, ui.c;
drawn with a full 20-col row clear -- the combo draw owns the row the
old blank band above the hand occupied), `11-14`
boxed hand cards (`cards_row`=14 is the BOTTOM
row; each card is a 3-wide x `box_h`-tall frame from the compiled
`card_frame_tiles` at VRAM `UI_TILE_CARD_FRAME_BASE` 118, weapon icon on
the first interior row, power digit on the second (the arrow-counter
type — the bow — instead draws its power icon there and the
remaining-uses glyph on the bottom border floor, keeping its frame
corners; the semantic screen
buffer keeps the type code + digit on row 13 for assertions), `15`
markers (`1-5` selection-order digits; the cursor and the enemy-target
caret render the up-arrow select icon tile `UI_TILE_SELECT_ARROW` 96
(combat tileset "arrow pointing up", card_frames sheet tile 14) -- the
enemy caret on the art's MIDDLE column (art_x+1; art is 3 wide, so the
arrow sits centered under the enemy, matching the editor preview)
while
the semantic buffer keeps `^` for text assertions), `16` card
description (`card_get_description`), `17` timer bar (window row,
`0x9A20`).  Card visuals are data-driven: `screens/cards_skin.json` via
`battle_compile.py` emits `g_card_skin` (bank 4), staged into the
`g_card_skin_wram` mirror by `battle_hud_load_banked()`.  There are
exactly TWO battle screens (`screens/battle/`): `default` (3 enemies)
and `boss` (1 enemy centered at x=8, art up to 3x3 on rows 3-5, caret
narrowed to 3 cols on row 6) -- selected by `battle_hud_load_banked()`
from `BATTLE_NONE` OR the `g_battle_solo` flag (solo minibosses, staged
by `battle_start(..., solo)`).  The HUD skin (`screens/battle_hud.json`
-> `g_hud_skin` -> `g_hud_skin_wram` mirror) owns the hero-HP / AP /
deck icon tiles + palettes and the turn-timer bar (segment tiles
VRAM 117/127, color, row, width; drawn by the bank-3
`ui_draw_battle_timer_banked` behind the fixed-bank wrapper -- the
fixed `_HOME` bank is hard against 0x8000, AGENTS.md 52.18).  The
hp/ap/deck icon tile DATA comes from the combat tileset
(`combat_hp_icon`/`combat_ap_icon`/`combat_deck_icon` via the card-frames
sheet, loaded by the bank-3 `ui_card_tiles_load_banked` at boot),
overwriting the atlas data at VRAM 113/114/116 (ids unchanged;
UI_TILE_DECK 116 keeps the combat deck art on the battle HUD deck
counter; the QUEST tab markers are font glyphs
'!' active / '*' complete, never a tile -- the 8x8 deck art read
poorly there).  Dialogue boxes stamp the **paper palette** (CRAM slot 4,
re-programmed to a pure white/black document ramp by `ui_draw_dialogue`)
over their whole footprint, so the box is white with black font ink in
every tileset -- palette 0 cannot be used because it anchors the world
tiles' background tone (tan in the village).  Slot 4 is free in every
world tileset (quick screen poison tints are its only other consumer and
cannot be open during a dialogue); every screen transition re-programs
CRAM, restoring the set's own slot-4 ramp.  Enemy art is
CENTERED on the 6-column name slot: `battle_enemy_art_x()` offsets the
stamp (and the 3-col caret) by (6-w)/2.  Hero/deck
row positions are layout-driven: label at (hero_label_row,
hero_label_col), HP block at hero_hp_col (icon 2 cols left of "HP:"),
DECK at deck_col (icon 1 left), AP at ap_col (icon 2 left).  VRAM BG
tiles 117-127 are FULLY allocated (bar segments + card frames).

## 52.12 Scenario state ordering

`scenario_begin()` (called by the declarative loader) resets
`g_game.state.flags.bytes[]`. The general loader re-applies descriptor flags
AFTER `scenario_begin()`. Any scenario code that pokes state directly must do
the same ordering, or its flags are silently wiped and story-dependent
scenarios fail in confusing ways.

## 52.13 Full clean build after structural changes

Whenever the `World`/`Game` struct layout, actor tables, or the CRT0 `_DATA`
area changes, the snapshot bytes and symbol addresses shift. The harness
resolves symbols via `get_symbol()`, but the ROM must be fully rebuilt; a
partial build leaves a stale `.sym`/`.gb` pair that reads garbage.

## 52.14 No large stack locals under the harness (SP = 0xFFFE)

The custom CRT0 sets `SP = 0xE000` (`ld sp, #0xE000` in `src/crt0.s`). The
harness skips CRT0 and jumps straight to `main()`, so under the harness `SP`
stays at the boot-ROM value **0xFFFE**.  From there the stack only has ~254
bytes of usable space before it grows down into the I/O register region
(0xFF00-0xFF7F), where reads return garbage and pushes are ignored.

Consequence: **any function whose stack frame is large (roughly a local
struct of ~100+ bytes, on top of the existing call depth) crashes the ROM
under the harness** — the return address gets corrupted and the CPU executes
an illegal opcode.  This bit the first SRAM save implementation, which
declared a ~200 byte `SaveSlot` on the stack.

Rules:

* Do not declare large locals (structs > ~64 bytes) in harness-exercised
  code.  Write to SRAM/state directly with small (byte/word) locals, or use a
  module-scope static buffer.
* `save.c` is the template: it copies `GameState` straight to SRAM at
  `0xA004` with a flat header, no staging local.
* If a new feature needs a big scratch buffer, put it in a `static`
  (WRAM or banked) buffer, never on the stack.

## 52.15 Harness vsync-skip blinds OAM reads to VBlank-timed rendering bugs

The harness runs with `g_harness_mode`, which skips `vsync()` (and the audio
ISR).  The sprite transition hide→commit cycle
(`ui_sprite_begin_transition()` in `game_render()`, `ui_sprite_commit()`
after `vsync()` in `main.c`) is therefore **entirely inside one frame** under
the harness: real/shadow OAM reads at any VBlank pause always show the
*revealed* state.  A bug that only breaks the VBlank-timed reveal is
invisible to every harness OAM/semantic read.

This bit the per-frame sprite re-hide: `game_render_reset()` initialized
`prev_map_id` to a `255` sentinel, so every non-overworld frame looked like a
map change and `game_render()` re-ran `ui_sprite_begin_transition()` every
frame.  On real hardware (vsync ON) the reveal only happened during VBlank,
leaving the sprite hidden for the whole fight/discussion; the harness could
not see it.

Rules:

* VBlank-timed rendering must be validated with **execution** checks
  (`make verify-oam`, mGBA breakpoints), not state reads: assert that
  `ui_sprite_begin_transition` does NOT fire on steady battle frames, that
  the dialogue render does NOT reach `ui_draw_world_full`, etc.
* Initialize transition-relevant render-cache sentinels to the current
  runtime value (e.g. `prev_map_id = g->world.map_id`), never a magic `255`
  that collides with "everything changed".

## 52.16 Regression checks must reach the bug's state, and be negative-tested

A debugger check that does not reproduce the bug's state passes on the buggy
ROM.  The steady-battle-frame check originally ran from boot (fresh session,
scenario never loaded); `ui_sprite_begin_transition` correctly never fires on
title frames, so it passed 5/5 on the buggy ROM and looked green.

Rules:

* A check must first drive the ROM to the exact state the bug needs (load
  the scenario, walk into the encounter, wait out the transition wipe)
  before arming breakpoints or reading OAM.
* Validate every new check with a **negative test**: temporarily restore the
  bug, confirm the check FAILS, then restore the fix and confirm it passes.
  A check that cannot fail on the bug is not a check.
* Confirm a breakpoint was actually armed (parse the `break` response for
  `Added breakpoint`) instead of silently continuing when it was not.

## 52.17 mGBA debugger `frame` pause parity

Where `frame` pauses (a VBlank wait point, the `game_render` breakpoint that
`connect()` arms, or a just-armed breakpoint) is parity/timing dependent
across runs (boot timing shifts).  Design breakpoint checks against the main
loop order instead: `game_render()` runs before `vsync()`, so from a
`game_render`-entry pause the first `frame` reliably reaches any code inside
`game_render` (e.g. the begin_transition hide) before the VBlank stop.
`connect()` also leaves the `game_render` and canary breakpoints armed, so
run checks that arm their own breakpoints in separate sessions (one per
section) to avoid cross-pauses.

## 52.18 Fixed-bank overflow presents as a mysterious "guest spin"

The `_CODE`/`_HOME` fixed area sits hard against `0x8000` (§55.5).  When an
addition pushes it over, rgblink emits `Warning: Write from one bank spans
into the next ... (bank 1 -> 2)` / `Possible overflow from Bank 1 into Bank
2` and silently overwrites the START of bank 2 with the overflowing bytes.
Symptom: any trampoline dispatch or execution path touching the corrupted
region spins the guest (mGBA ~300% CPU, debugger pipe dead,
game_render breakpoint never re-hit) — it looks like a nested-trampoline or
codegen bug but is a link overflow.  This caused the original victory-loot
"long-battle hang": wiring the drop hook grew the fixed bank ~30 B past the
boundary; whether it hung flipped with unrelated code-size changes.

Rules:

* After ANY new fixed-bank code, run `make memmap` (it fails on violation)
  AND grep the link step for `Possible overflow` — the build itself does not
  fail on the warning.
* Keep fat logic out of the fixed bank by structure, not by trimming: pure
  arithmetic (rolls, encodes) belongs in a bank-2 body behind one thin
  staging wrapper (see `src/game/loot_drop_banked.c` + `game_loot_drop()`),
  not inlined at the call site.
* `%`/`/` in new fixed-bank code pulls in the SDCC div/mod library; use
  masks for power-of-two bounds.
* 8-bit `*` anywhere (including banked bodies) pulls the SDCC mult
  routines (`__mulsuchar` et al.) into fixed `_CODE` (~69 B); use shifts,
  repeated addition, or a running counter for small tile-math instead.

## 52.19 SDCC miscompile instances are LAYOUT-SENSITIVE (Aug 2026)

The pointer-cache miscompile family (§52.11.1, `patrol_banked.c`,
`battle_update`) is not a fixed set of bugs — WHICH instance fires depends
on where the linker places neighboring banked bodies.  Changing any of the
following can flip a latent instance on or off somewhere else entirely:

* global optimization flags (`-Wf--max-allocs-per-node...`);
* `volatile` qualifiers on shared staging globals (`g_bk_*`) or function
  parameters (`volatile Battle *`, `const volatile ...`);
* the size/placement of ANY object in the same output bank.

Documented flip-flop (full post-mortem: `docs/roadmap.md`, Aug 2026):
global volatiles + alloc cap killed patrol stepping; function-local
volatiles alone fixed the battle HP corruption but `ui_battle_content.c`'s
volatile-parameter codegen shifted bank-3 layout and broke patrol step-
interval persistence (mGBA watchpoint: `ai_timer` zeroed every frame from
inside the patrol body's own window); capping allocs in the UI unit re-
exposed the HP bug.  Final validated config: function-local volatiles,
plain `g_bk_*` globals, default flags globally, per-file caps below.

Rules:

* NEVER diagnose from a dirty working tree — mixed stale objects produce
  plausible-but-wrong symptoms (this cost two wrong root causes in one
  day).  Bisect with clean worktrees: `git worktree add /tmp/x <commit>`,
  build there, run the three sentinel scenarios.
* After ANY change to optimization flags, volatile qualifiers on shared
  globals/params, or banked-body placement, run the sentinels BEFORE
  trusting the build: `patrol_slime_cross`, `patrol_enemy_bumps_player`,
  `battle_multi_enemy_cycle_kill`, plus the full harness.
* mGBA watchpoints (`watch <addr>`; delete connect()'s breakpoints 1/2
  first, then arm) catch wild writers red-handed — state probes cannot
  distinguish "bad data" from "good data rendered wrong".

## 52.20 Per-file alloc-cap escape hatch (Makefile)

When one unit needs different SDCC codegen than the global default,
add an explicit target rule AFTER the generic pattern rules (explicit
rules win; keep debug AND release variants in sync):

```make
build/debug/world/patrol_banked.o: src/world/patrol_banked.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) -c -DDEBUG_BUILD -Wf--max-allocs-per-node500 $(INCLUDES) -o $@ $<
```

Current users: `src/world/world.c` + `src/world/patrol_banked.c`
(both builds).  Removing these rules re-breaks patrol cadence; see §52.19.

## 52.21 Harness CLI exit codes

`python3 tools/dev.py scenario|state|roundtrip <name>`:
0 = PASS, 1 = assertion FAIL, **2 = scenario NOT FOUND** (with
close-name hints).  `make test-scenario` flattens everything to rc=2 —
never use make's exit code to decide whether a scenario name exists;
that ambiguity once produced a triage listing nonexistent scenarios as
"failing".

## 52.22 Signed BG tile addressing: raw tile-data writers must use 0x9000+

`ui_init()` clears LCDC bit 4 (`LCDC_REG &= ~0x90`) and nothing ever sets
it back, so the BG runs in **signed tile addressing** for the whole game:

* BG tile ids 0-127 are fetched by the PPU from `0x9000 + id*16`.
* BG tile ids 128-255 are fetched from `0x8800 + (id-128)*16`.
* OAM sprites ALWAYS fetch from `0x8000 + id*16` (bit 4 never affects
  sprites).

Consequence: any tile-data write to an id < 128 must target
`0x9000 + id*16`.  Writers that do this correctly:

* GBDK `set_bkg_data` (font 0-95, atlas icons 104-116) -- bank/mode aware.
* World tileset / enemy-art / NPC overlays (ids >= 128 at raw
  `0x8000 + id*16` = the same physical block both addressing modes use
  for ids >= 128).

The bug class: `ui_card_tiles_load_banked` (card frames 118-126, bar
117/127, HUD icons 113/114/116) originally wrote raw `0x8000 + id*16`.
The data landed in the sprite-only block the BG never fetches, so the
combat icons never appeared (the atlas heart/bolt/deck at the signed
locations kept rendering) and the card "frames" were unwritten flat
tiles.  **No harness assert can catch this**: scenarios assert tilemap
ids, never tile data; the mirror tracks ids only.  Regression tools:

* mGBA write watchpoint on the FETCHED address (e.g. `watch/w 0x9710`
  for tile 113): expect the atlas write first, then the loader's
  overwrite.  Watchpoints on 0x8000-block addresses silently validate
  the wrong block.
* Visual: `make screenshots` -- the hp/ap/deck cells must show the
  2-shade combat glyphs and the hand cards must show bordered frames.

PyBoy caveat: `pb.memory[0x8000..0x97FF]` exposes whichever VRAM block
its internal VBK state selects at read time, while the renderer fetches
per the LCDC.4 mode -- a memory dump and the rendered frame can
legitimately disagree without either being wrong.  Never treat a PyBoy
VRAM read as proof of BG visibility.

---

# 53. State Ownership

`GameState` (`src/rpg/state.h`) is the **canonical persistent game state**:
scene, party, inventory, flags, variables, and persistent world actor state.
Treat it as the single source of truth for anything a save file would
contain.

## 53.1 Canonical state vs runtime engine copies

* `g_game.state` is authoritative.
* `g_game.world` holds the runtime engine copy of the current scene: terrain,
  exits, the player entity, and spawned hostile actors.  `scene_sync_from_world()`
  copies the scene + player position back into `GameState` once per frame.
* `Battle`, `DialogueState`, `RenderCache`, and input are temporary runtime
  state, never persistent.

## 53.2 Single-writer rules

* Scene/position: `scene_load()`, `scene_update_from_map()`, and
  `scene_sync_from_world()` write `state.scene`.  Do not set
  `state.scene.*` from gameplay code directly.
* Party HP: `battle_start` reads the hero HP from `state.party.members[0]`;
  victory writes the post-battle HP back to both the party and the world copy.
* Flags/variables: use `game_flag_set/clear`, `game_variable_set/add`.
  Story flags are a sub-set of `GameState.flags`; `story.c` operates on
  `GameState*`.
* Persistent actor defeat: `world_on_battle_end()` records
  `ACTOR_STATE_DEFEATED` into `state.world` keyed by the stable ActorId.
  `actor_load_scene()` skips spawning defeated actors.
* Currency: `currency_add/currency_set` in `src/rpg/currency.{h,c}` mutate
  `state.currency` (dense slots indexed by `CurrencyId`).  Gold is
  `CURRENCY_ID_GOLD`, not a generic variable.
* Items: ownership via `inventory_add/remove`; effects via `item_use` /
  `item_purchase` in `src/rpg/items.{h,c}`.  `item_use` consumes only on a
  successful use; `item_purchase` is atomic (failed purchases leave state
  unchanged).
* Progression: `progression_add` / `progression_ensure` in
  `src/rpg/progression.{h,c}` mutate `state.progression`.  The engine is
  generic and contains no per-target-type gameplay logic; the game-specific
  consequence of a level-up lives in `game_on_level_up()` (called by the
  progress-granting caller when a level was crossed).
* Static actor definitions (`WorldActorDefinition`) must never hold mutable
  state; lifecycle lives in `state.world` + `World.actors`.

## 53.3 Debug injection must not emit gameplay telemetry

Scenario/`initial_state` setup writes state directly into `GameState`
(flags via `state.flags.bytes[]`, variables via `state.variables.values[]`,
currency via `state.currency.amount[]`, progression via `progression_ensure`,
etc.) and must NOT go through `game_flag_set` / `game_variable_set` /
`currency_add` / `progression_add`, which emit telemetry.  Setup that emits
gameplay telemetry breaks scenarios that assert `event_not_occurred`
(e.g. `town_reentry`).

Runtime **debug actions** (the `g_debug_action` channel) are different: they
are mid-scenario gameplay exercised through the real mechanic functions
(`add_item`, `buy_item`, `add_currency`, `add_progress`, `use_item_direct`),
so they legitimately emit telemetry.  Do not route scenario *setup* through
them.

## 53.4 Descriptor layout is a wire contract

The host serializes `initial_state` JSON into the fixed-size descriptor
`g_scen_state_buf` (`STATE_LOAD_DESC_*` in `src/debug/telemetry.h`), and the
ROM applies it in `scenario_load_state()`.  The extended snapshot
`g_state_snap_buf` (`STATE_SNAP_*`) mirrors the same sections for
host-side assertion.  Keep the host (`tools/emulator.py`) and ROM constants
in sync; changing one without the other silently breaks every scenario.

## 53.5 Snapshot / telemetry observability contract

* Core snapshot (`g_snap_buf`, 36 bytes): byte 12 is `state.flags.bytes[0]`,
  byte 19 is `state.scene.scene_id`.  Existing scenarios depend on these.
* Extended snapshot (`g_state_snap_buf`, 210 bytes, version 0x06): version
  byte 0, flags 1..8, variables 9..24, currency 25..37, party 38..50,
  inventory (collection) 51..83, world 84..132, progression 133..181,
  equipment 182, camera/world geometry 183..188, battle-deck count at 189
  followed by up to 20 deck card ids at 190..209.
* State-load descriptor (`g_scen_state_buf`, version 0x04): the optional
  deck section at 229 (present flag) / 230 (count) / 231..250 (card ids)
  replaces the starter deck when present — an explicit empty deck is how
  scenarios reach `battle_start`'s packed fallback path now that
  `DECK_MIN_CARDS` blocks emptying a deck through gameplay.
* Every important gameplay transition must emit a telemetry event; state
  assertions must be possible without screenshots.

## 53.6 Save/load boundary (design, not implementation)

`GameState` is the potential save unit.  Rule:

> **If a piece of state is part of `GameState`, it is potentially saveable;
> if it is temporary runtime state, it is not.**

Persistent state: scene, party (id/hp/max), inventory, flags, variables,
currency, world actor lifecycle, progression.  Runtime state — `Battle`,
`DialogueState`, `RenderCache`, input state, `World.actors` HP/facing (the
engine copy), `g_game.screen` — must never become part of the save format.
`World.actors` is rebuilt from scene definitions + `GameState.world` on every
scene load.

The wire descriptor `g_scen_state_buf` and the extended snapshot
`g_state_snap_buf` are the save-boundary probes: the host roundtrip check
(`python3 tools/dev.py roundtrip <scenario>`) loads an `initial_state`,
dumps the canonical state, rebuilds a descriptor from the dump, reloads,
and asserts the state is unchanged.  Any section that serializes in but not
out (or vice versa) breaks the roundtrip.

## 53.7 Semantic layers — never expose byte layouts to the LLM

Keep the layers separate:

```text
GameState
    ↓
semantic state representation (tools/test_runner.format_state)
    ↓
debug snapshot / protocol transport (g_snap_buf / g_state_snap_buf)
    ↓
LLM
```

The LLM-facing output is text such as:

```text
SCENE=FOREST
PLAYER=(10,8) FACING=RIGHT
FLAGS: ARRIVED_TOWN MET_MAYOR
VARIABLES: CHAPTER=1 GOLD=150
PARTY[0]: HERO lvl=3 24/30
INVENTORY: POTION x2
WORLD: SLIME_FOREST=DEFEATED
```

The byte offsets in `g_snap_buf` / `g_state_snap_buf` are an internal
transport contract and may change; scenarios must not depend on them.
Semantic assertions (`flag`, `variable`, `inventory`, `party_hp`,
`party_level`, `actor_state`, `screen_row`, ...) are the stable API.

---

# 54. State Ownership, Screens & Actor Lifecycle (audit)

## 54.1 State ownership

For every piece of persistent state, one clear owner:

| State             | Owner                        | Mutators                          | Persistent |
|-------------------|------------------------------|-----------------------------------|------------|
| Player HP/max     | `state.party.members[0]`     | battle, item_use, level-up        | yes        |
| Current scene/pos | `state.scene`                | scene_load / scene_sync_from_world| yes        |
| Quest state       | `state.variables[QUEST_MONSTER_HUNT]` | event actions             | yes        |
| Quest objective   | `state.variables[MONSTERS_REMAINING]` | MONSTER_DEFEATED event      | yes        |
| Slime defeated    | `state.world` (ActorId)      | world_on_battle_end               | yes        |
| Gold              | `state.currency[GOLD]`       | currency_add                      | yes        |
| Equipped weapon   | `state.equipment.weapon`     | item_equip                        | yes        |
| Hero attack       | derived (`game_hero_attack`) | from equipment                    | derived    |
| Screen            | `g_game.screen`              | screen_change                     | no         |

The quest/objective lives in **generic variables** owned by the event table
(`src/core/event.c`) — never in the Mayor actor or the dialogue screens.

## 54.2 Screen transition contract

Every screen implements `update()` + `render()` (+ shared `screen_change`
enter/exit).  Screens hold **no persistent state**: everything that matters
lives in `GameState`.  Transient UI state (`game_over_choice`,
`item_menu_index`, `item_menu_tab`, `shop_message`) lives in `Game` and is
reset on exit.  A screen must never become the home of gameplay state
(quest progress, HP, etc.).

The quick screen (`SCREEN_ITEM`) is a tabbed menu: CARDS (collection/deck
management: paired rows whose membership glyph is the decked-copy count,
A adds one copy and clears every copy once the card is fully decked, all
through the real `deck_add_card`/`deck_remove_card`; SELECT opens a detailpage whose FILTER/SORT entry opens an inline picker) and QUEST (ongoing
quests; SELECT shows a placeholder detail line).  START is the universal
open key (overworld and battle player-turn).  Inside, LEFT/RIGHT switches
tabs, UP/DOWN moves the list, A confirms/toggles, B backs out (two-step on
the CARDS list: first B jumps to the first card row, B again on the top row
closes), SELECT opens the detail submenu.  Rejections (`DECK FULL`) show a
transient message cleared by a frame TTL, never a silent no-op.  On a loot
card's detail page, A sells one copy while the player has engaged a buying
shop (`ShopDefinition.buys`; `g->shop_id` is set by the shop actor and
cleared on scene change) — docs/loot.md §24/§34.6, scenario
`merchant_sell_loot`.

## 54.3 Actor lifecycle

* `WorldActorDefinition` (static): scene-owned configuration, never mutable.
  Hostile actors may carry a conditional spawn (`spawn_variable`/`spawn_value`):
  the actor is only spawned when the variable equals the value — used for the
  final boss, which appears once the Monster Hunt quest is COMPLETE.
* `World.actors` (runtime): the engine's current-scene copy, rebuilt on every
  scene load; spawned hostiles live here.
* `GameState.world` (persistent): defeats/lifecycle keyed by stable `ActorId`.

## 54.4 Warnings / lint

The normal build cannot enable `-Wall` (sdcc's `--use-stdout` pipeline leaks
warnings into the assembly stream).  Use `make lint` to run a
compile-to-assembly `-Wf-Wall` pass over every source; it must report no
warnings.  This has caught real bugs (e.g. `uint8_t` progression thresholds
truncating values > 255).

## 54.5 Input bit layout is a wire contract

`InputButton` bits (`1 << InputButton`) must equal GBDK `joypad()`'s `J_*`
bits.  They diverge only for the real game, never for the harness (the
harness injects `g_inp_mask`, which uses the same `1 << InputButton` bits) —
so a mismatch silently breaks only hardware controls.  Two guards keep the
paths in sync:

* `src/input/input.c` contains a compile-time check (`g_input_bit_layout_ok`)
  that makes any `InputButton`/`J_*` mismatch a hard compile error.
* The harness reads `g_input_button_bits[]` from the ROM at connect and
  derives its injection masks from it (no hand-synced masks).

Never reorder `InputButton` without updating the `J_*` expectations, and
never hand-code button masks in `tools/emulator.py`.

## 54.6 Reusable menu screens (`MenuFrame`)

Every menu screen (the quick screen, the shop, future menus/subscreens)
draws through `MenuFrame` (`src/ui/menu.{h,c}`) so they all share one
layout: a centered title plus a bounded content area.  A subscreen is shown
by clearing the content area (`menu_clear_content`) and redrawing it, or by
nesting another `MenuFrame` inside it.

```c
MenuFrame f;
f.title      = "SHOP";   /* direct literal */
f.title_row  = 0;
f.top_row    = 3;        /* first content row */
f.bottom_row = 12;       /* one past the last content row */
f.boxed      = false;
menu_draw_frame(&f);                 /* clear + centered title + separator */
menu_draw_content(&f, idx, "text");  /* content rows, clamped to [top,bottom) */
menu_draw_centered(y, "title");      /* centered helper */
```

Rules:

* Instantiate the `MenuFrame` **locally** in the screen's draw function and
  pass titles/text as **direct string literals**.  NEVER store titles in a
  file-scope `const char *[]` pointer table — the linker can place such a
  table in a ROM bank that is not mapped when the screen draws, which makes
  that text render **blank** (plus a stray character).  This bit the quick
  screen's tab labels (`g_tab_labels`); the fix was direct literals.
  `const uint8_t[]` byte tables (e.g. `g_tab_x`) are safe.
* `menu_row(&f, idx)` returns the absolute row for a content index (callers
  pass in-range indices).
* Content rows are bounded by `top_row`/`bottom_row`; `menu_draw_content`
  ignores out-of-range indices and `menu_clear_content` blanks the area for
  subscreens.
* The quick screen keeps a dedicated tab row: full labels (`ITEM EQUIP
  QUEST STAT`) with the active tab marked by a `^` on the row below, and a
  per-tab centered title (`ITEMS`/`EQUIP`/`QUESTS`/`STATUS`).

---

# 55. Architecture Invariants (repository hardening)

These are the boundaries the architecture depends on.  Treat them as review
rules: a change that violates one must be justified, not silent.

## 55.1 Engine vs game layer dependency direction

The engine (`src/core`, `src/rpg`, `src/world`, `src/battle`, `src/ui`,
`src/screens`, `src/input`, `src/audio`, `src/debug`) is generic and owns no
game content.  The game layer (`src/game`) owns content tables, named ids,
initial state, and stat hooks, registered with the engine at boot.

* **Adding a new enemy, item, quest, shop, or dialogue line must be a
  `src/game/` content change only** — never a change to engine files or
  shared screens.
* **Engine files must not include game-layer headers** (`content.h`,
  `game_ids.h`, `shops.h`) — with one documented exception: `main.c` is the
  composition root and calls `game_content_init()` before `game_init()`, and
  `src/core/game.c` calls the game's `game_new_game` hook at boot.  These are
  the engine's *hooks into the game*, the intended dependency-inversion.
* **Engine files must not branch on game ids.**  A `switch` over
  `ENTITY_ID_MAYOR` / `ITEM_SWORD` / `QUEST_MONSTER_HUNT` in an engine file is
  a regression.  Game decisions live in `src/game` (event table, hooks like
  `game_screen_after_victory`, `game_hero_attack`).
* **Engine headers define only the ID *types* and engine sentinels.**
  `EntityId`/`EventId`/`DialogueId`/`ItemId` are `uint8_t` in
  `entity.h`/`event.h`/`dialogue.h`/`state.h`, along with the engine's own
  constants (`*_NONE`, `ENTITY_ID_PLAYER`) and the per-game content range
  base (`ENTITY_ID_FIRST_GAME = 0x80`, and the analogous `EVENT_ID_FIRST_GAME`,
  `DIALOGUE_ID_FIRST_GAME`, `ITEM_FIRST_GAME`).  Game-specific values
  (`ENTITY_ID_MAYOR`, `ITEM_SWORD`, ...) live in `src/game/game_ids.h` as
  `#define`s relative to those bases.  A second game defines its own ids in
  its own `game_ids.h` without ever touching the engine headers.  The
  *generic* ids (`FlagId`/`VariableId`/`CurrencyId`) are plain integers in
  `src/rpg/state.h`.  Values >= 0x80 must be `#define`s, never enum members
  (SDCC enums are signed 8-bit; 0x80 would wrap to -128).

## 55.2 Content is registered, not compiled in

Every content system follows the same pattern: an engine provider plus a
game-layer `game_*_register()` called from `game_content_init()`:

* events: `event_init` (`src/core/event.c`) / `src/game/events.c`
* dialogue: `dialogue_register` (`src/core/dialogue.c`) / `src/game/dialogue.c`
* actors: `actor_register_tables` (`src/world/actor.c`) / `src/game/actors.c`
* items: `item_register_defs` (`src/rpg/items.c`) / `src/game/items.c`
* quests: `quest_init` (`src/core/quest.c`) / `src/game/quests.c`
* shops: read directly by the shop screen via `game_shop_for_id` (`src/game/shops.c`)

A new content system should follow the same shape rather than inventing a new
registration mechanism.

## 55.3 Quests are data

A quest is **a row in the quest registry + event-table entries**, nothing
else.  The QUEST menu iterates the registered registry generically; it must
never switch on individual quest ids.  Quest state is a generic variable
encoded 0 = not started / 1 = active / 2 = complete (or equivalent
thresholds), mutated by events.  Known gaps (multi-stage quests, "key unlocks
a location", per-quest hints, repeatable quests) are logged in
`docs/roadmap.md`; do not add machinery for them until a real quest needs it.

## 55.4 Observability is non-optional

Every quest/event/item/actor state transition must remain visible to the
harness: `SCRIPT_TRIGGERED` (events), `VARIABLE_SET` (quest/variable state),
`ITEM_ADDED`/`ITEM_REMOVED`, `CURRENCY_ADDED`/`CURRENCY_SPENT`,
`ACTOR_STATE_CHANGE` (persistent lifecycle), `ACTOR_MOVED` (patrol steps),
plus the semantic snapshot.  A content change that makes
a gameplay outcome invisible to `make test-harness` is incomplete.

## 55.5 Memory budget

`make memmap` prints the reproducible memory budget and fails on a violation.
The non-bankable `_HOME` area must stay below `0x8000` (CPU addresses
`0x8000+` alias VRAM).  Keep `_CODE` (fixed bank) as small as practical and
track the budget in the roadmap; every substantial feature should be checked
against `make memmap`.

## 55.6 VRAM / palette ownership across screens

World tile art lives in VRAM block 1 (slots 128+) and battle enemy art
overwrites the same slots: any screen that writes block-1 VRAM must be
followed by a tileset reload before the next overworld redraw
(`ui_invalidate_tileset()` on overworld entry covers all return paths).
Likewise `ui_lcd_off()` wipes every CGB tile attribute to palette 0, so any
screen drawn over the live world (dialogue) must redraw the world (tilemap
+ attribute spans), not just stamp its own box.  `make verify-oam`
executes both regressions (battle VRAM restore, dialogue palette restore).

---

# 56. Headless Screenshot Walkthrough Capture

The game screen is the player-facing interface; the semantic debug interface
is the development-agent-facing one (§7).  Sometimes a human or agent still
needs to see the *rendered* game without booting an emulator.  For that,
`make screenshots` plays a deterministic walkthrough headlessly and saves the
frames to `screenshots/*.png`.

## 56.1 Command

```bash
make screenshots
```

Builds the release ROM if needed and runs:

```bash
python3 tools/capture_walkthrough.py
```

Output: raw 160×144 PNGs in `screenshots/`, one per milestone.  The directory
is committed (like the ROMs) so a reviewer can inspect the current look from
the commit/PR without booting anything.

## 56.2 Mechanics

* Headless PyBoy (`window="null"`) boots the **real release ROM** — no debug
  ROM, no harness mode, no scenario loader.  What is captured is exactly what
  a player would see.  `make verify-walkthrough` runs the same script with
  semantic assertions (docs/verify-walkthrough.md); the package lives in
  `tools/walkthrough/` (`state_reader.py` WRAM reader, `route.py` BFS
  planner, `session.py` driver, `walks.py` walk definitions).
* Semantic state comes from **WRAM symbol reads** (StateReader): `g_game`
  from the ROM's `.sym`, struct offsets mirrored from the headers and
  validated by boot anchors every session.  The BFS route planner reuses
  the level compiler's `derive_collision` + `load_tilesets` and reads
  patrol boxes/actors/exits from `levels/*.json`, so editor content
  changes update the walk automatically.
* The player entity is located in WRAM via its deterministic boot pattern
  (same technique as `tools/vram_dialogue_check.py`); the walk is
  **position-based**, not press-count based: each step is a single short
  press edge and a wait for the tile commit, and a dropped press is re-pressed
  so the route self-corrects (PyBoy button delays are lossy).
* Full-screen transitions eat input for ~10-40 frames (the FIELD→TOWN gate
  crossing wipes the display blank for ~54 frames and swallows START for
  ~40; shop/dialogue close swallows it for ~10).  A bare press-then-sleep
  lands in that dead window, and a dropped shop-close `B` makes `START`
  *close the shop* instead of opening the quick screen.  Every screen change
  is therefore **state-verified, not time-expected**, and retried on failure:
  * every screen is verified by its **BG tilemap text** (`bg_text` reads the
    visible tilemap through SCX/SCY; the font lives at tile base 0, so
    cells read directly as ASCII): guard dialogue by the `GUARD:` speaker
    tag, shop/save menu by their row-0 titles, the quick screen by the
    `CARDS QUEST` tab labels, the filter/sort picker by its
    `LR CYCLE  A:OK B:NO` footer.  Pixel heuristics are NOT usable here:
    an earlier LCDC-bit-5 check was vacuous (nothing in the ROM ever sets
    bit 5), and a dark-pixel caret scan false-positived on terrain glyphs,
    silently desynchronizing every later milestone.  Membership gotcha:
    `"X" in rows` on a *list* of strings tests element equality, never
    substrings — always use `any("X" in r ...)` / `all("X" not in r ...)`;
    a bare `in` made a close condition fire instantly and poisoned the run.
  * the quick screen's active tab is verified by the `^` caret column read
    from BG text row 3 (column 0 = CARDS, 6 = QUEST) and `RIGHT` is repeated
    until the target caret appears.
* Frames are saved with PyBoy's `screen.image` (headless framebuffer render);
  each save prints the first non-blank `bg_text` row so a mislabeled frame
  is obvious in the build log without decoding PNGs.
* Five fresh sessions are used: Walk A (overworld → Town → dialogue → shop →
  quick screen), Walk B (slime battle on the Field), Walk C (Forest gate),
  Walk D (title-menu + tutorial slides), and Walk E (castle mimic battle:
  FIELD → south → SOUTH_FIELD → south → MOUNTAIN_PASS → north → CASTLE
  ENTRY → north → GREAT HALL → north → CASTLE, then into the mimic; the
  planner routes through the expanded castle chain and all corridor
  columns avoid patrol boxes), so persistent state never bleeds
  between milestones.  Walk D stops at the boot title screen (START → menu,
  DOWN to the TUTORIAL entry, A, then RIGHT through the seven slides); it never
  enters the game.
* Determinism is verified: the walk's position/caret/text checks make the
  24 frames byte-identical across repeated runs.

## 56.3 Milestones

```text
00-boot-field        overworld at spawn with the HUD
01-field-scrolled    FIELD with the camera scrolled (SCX > 0)
02-town-arrived      TOWN just inside the east gate (camera at origin)
03-guard-dialogue    dialogue box over the scrolled town (camera offset)
04-dialogue-next     second dialogue line
05-shop              shopkeeper shop screen
06-cards-menu        START quick screen (CARDS tab)
07-filter-picker     filter/sort picker over the card list
08-quests-tab        QUEST tab
09-battle            slime encounter (battle screen)
10-battle-attack     after a player attack (damage dealt)
11-battle-victory    VICTORY result over the slime trio (loot gold asserted)
11-battle-aftermath  overworld after leaving the fight (VRAM restore)
12-wizard-save       save menu at the wizard
13-wizard-saved      after saving to Slot 1
14-forest-arrived    FOREST gate arrival after Walk B
23-mimic-battle      castle mimic encounter (solo boss screen, Walk E)
15-title-menu        title menu with the TUTORIAL entry (index 3)
16-tutorial-slide0   TUTORIAL BASICS
17-tutorial-slide1   CARD TYPES
18-tutorial-slide2   CARD TYPES 2
19-tutorial-slide3   COMBOS
20-tutorial-slide4   ENERGY & COMBAT (6/turn)
21-tutorial-slide5   DEFEND & STATUS
22-tutorial-slide6   SHIELD CARD
```

Frame `12-wizard-save` is gated on a stable menu/message state before the
capture (the transient save-confirmation TTL used to poison it); if a
regen diffs it, re-run before hunting a rendering bug.

## 56.4 Rules

* **The screenshot PNGs are a visual-review aid only** — they never gate
  CI.  The SEMANTIC CHECKS inside the same run (`make verify-walkthrough`,
  docs/verify-walkthrough.md) DO gate: the walkthrough drives the release
  ROM and asserts canonical gameplay state (scene ids, story flags, gold,
  party HP, battle hand, music, save/load roundtrip) read from WRAM via
  the ROM's `.sym`.  A run failing its checks returns non-zero even
  though every PNG saved fine.
* The milestone list should grow when a feature visibly changes the screen
  (a new screen, a new tab, a reworked battle view).  Routes are planned by
  BFS from `levels/*.json` (`tools/walkthrough/route.py` — reuses the level
  compiler's collision derivation), so content edits in the editor update
  the walk automatically; do not hardcode waypoints in walk bodies.
* **Content sweep** (`walk_sweep`): every level in `levels/` is visited on
  every run — a fresh session per level, BFS route from the field spawn,
  scene-id + music asserts from the level JSON, `sweep-<name>.png` per
  level.  A NEW level added by the editor is swept automatically on the
  next run (the planner assigns its scene id in the compiler's order);
  an UNREACHABLE level fails loudly (no exit targets it — content bug).
  Engine-only coverage of NPC dialogues / new enemies stays on the
  fixture suite (§42.1); the sweep proves every level boots, is enterable,
  and plays its music.
* **Milestone hygiene.** The committed set is explicit: `CLASSIC_MILESTONES`
  in `tools/capture_walkthrough.py` plus `sweep-<name>` per planner scene.
  A plain run only overwrites those; `--clean` (what CI's
  `make verify-walkthrough` uses) additionally deletes any top-level
  `screenshots/*.png` outside the set, so renamed/retired milestones can't
  linger and confuse reviewers.
* **Review shots** (`screenshots/review/`, committed) hold ad-hoc captures
  a walk can't stage deterministically (e.g. a shop-only bow dealt into a
  battle hand — `tools/capture_bow_shot.py`, which stages hand state
  through PyBoy memory exactly like the debug harness's `set_hand_card`).
  `review/manifest.json` is the source of truth: file, description, and
  the commit it was taken from.  `--clean` deletes review PNGs missing
  from the manifest; an unreadable manifest leaves review/ untouched with
  a warning — never nuke blindly.  Regenerating a shot after a visual
  change overwrites it in place (same filename, fresh `taken_from`); a
  renamed shot plus a manifest update lets `--clean` retire the orphan.
* Expected values are read from `levels/*.json` and the ROM's own const
  tables (card prices, shop stock, gold rewards) — never hardcoded.  When
  a gameplay constant must be mirrored (struct offsets, enum values), it
  cites its header and is validated by the boot anchors, which fail
  loudly on layout drift.
* Colors come from PyBoy's renderer and may differ slightly from SameBoy;
  layout and placement are what the frames are for.  The dialogue-box frame
  (`03-*`) is the ground-truth check for camera-scroll overlay alignment,
  alongside the VRAM assertion in `make vram-dialogue`.
