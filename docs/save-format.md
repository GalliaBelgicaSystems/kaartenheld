# Save Format

Battery-backed SRAM persistence of the canonical `GameState`
(`src/rpg/save.{h,c}`).  The format is versioned so saves can be migrated if
`GameState` changes.

## Storage

* MBC5 cart with RAM + battery (header cart type `0x1B`, RAM size 8 KB).
* SRAM base `0xA000`.

## Layout (version 3)

| Offset | Size | Field |
|---|---|---|
| 0xA000 | 2 | magic `0x5247` (`"GR"`) |
| 0xA002 | 1 | format version (`SAVE_VERSION = 3`) |
| 0xA003 | 1 | checksum |
| 0xA004 | `sizeof(GameState)` | the serialized `GameState` |

* Checksum: 8-bit sum of the `GameState` bytes (mod 256).
* `save_present()`: magic + version + checksum valid.
* `load_game()`: returns false (and leaves state untouched) when no valid
  save is present.

## Versioning / migration policy

* `SAVE_VERSION` lives in `src/rpg/save.h`.  Bump it whenever the
  `GameState` layout or field semantics change in a way that breaks old
  saves.
* A loader must reject a version it does not understand (it already does).
  Future migrations (v1 -> v2) should read v1 and upgrade in place before
  writing back.
* `GameState` is the *only* persistent payload: scene, party, inventory,
  flags, variables, currency, persistent world actor state, progression,
  equipment.  Runtime state (`Battle`, `DialogueState`, `World.actors` HP,
  the active screen) is never saved; it is rebuilt from `GameState` on load.

## Harness coverage

`make test-scenario SCENARIO=save_load_roundtrip` saves a distinctive state,
mutates it, loads it back, and asserts the state is byte-identical at the
semantic level (currency, variables, inventory, party HP, story flags).

Power-cycle persistence (writing SRAM to a `.sav` file on hardware/mGBA) is
emulator/hardware behavior and is not covered by the in-session harness test.
