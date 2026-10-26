# Testing

How the foundation is validated — by machines and LLMs, not by watching the
screen.

## Test layers

1. **Harness scenarios** (`make test-harness`, 205 scenarios): deterministic,
   reproducible gameplay situations.  Each JSON scenario defines an
   `initial_state` (scene, position, flags, variables, party, inventory,
   currency, world, progression, equipment), an action sequence (presses,
   waits, semantic interactions, debug actions, save/load), and assertions.
   The ROM is a debug build; mGBA drives it with injected input and frame
   stepping; telemetry + semantic snapshots feed the assertions.
2. **Release validation** (`make test`): builds and validates the release
   ROM (header, checksums).
3. **Lint** (`make lint`): compile-to-assembly `-Wf-Wall` pass over every
   source; must report no warnings (the normal build cannot use `-Wall`).
4. **Memory budget** (`make memmap`): reproducible ROM/WRAM budget; fails on
   a `_HOME` >= 0x8000 violation.
5. **Save/load roundtrip** (`save_load_roundtrip` scenario): SAVE, mutate,
   LOAD, assert state identical.  Also `make roundtrip SCENARIO=<name>` for
   the host-side descriptor lossless check.
6. **Hardware-fidelity OAM checks** (`make verify-oam`): drives the debug ROM
   through real transitions and asserts real OAM (`0xFE00`) state at VBlank
   pauses, plus breakpoint-based execution checks for VBlank-timed rendering
   bugs the harness cannot see (the harness skips vsync, so a hide→commit
   cycle completes within one frame and OAM reads always show the revealed
   state — AGENTS.md §52.15).  Each section establishes its own scenario
   state and is negative-tested (AGENTS.md §52.16).  Run in CI after the
   harness.

## Scenario anatomy

```json
{
  "name": "merchant_deliver",
  "initial_state": { "scene": "TOWN", "player": {"x": 10, "y": 3, "facing": "RIGHT"},
                     "inventory": {"AMULET": 1}, "currency": {"GOLD": 50},
                     "variables": {"MERCHANT_QUEST": 1} },
  "actions": [ {"type": "interact", "actor": "MERCHANT"} ],
  "assertions": [
    {"type": "variable", "variable": "MERCHANT_QUEST", "expected": 2},
    {"type": "currency", "currency": "GOLD", "expected": 65}
  ]
}
```

## Rules

* **Never assert on pixels when semantic state exists.**  Use `screen_row`
  only for layout-level UI checks.
* Every significant gameplay feature gets a scenario that fails before the
  fix and passes after.
* Scenario setup writes state directly (no telemetry); runtime debug actions
  go through real mechanics (they emit telemetry).
* Deterministic RNG seeds for randomness.
* See `docs/DEBUG_PROTOCOL.md` for the full protocol, and AGENTS.md §41-48
  for the workflow and output conventions.

## Emulator coverage

* mGBA: the harness transport (all scenarios).
* SameBoy: `make run` / `make screenshot` for a visual smoke check.
* Gambatte and real hardware are not available in the build environment;
  behavior can diverge there (AGENTS.md §52), so a hardware smoke test is a
  known gap.
