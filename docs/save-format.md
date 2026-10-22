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
  Version 2 and older saves are rejected outright; the demo ships no
  migration while the loot CardId space is still changing.
* Future migrations should read the old version and upgrade in place before
  writing back.
* `GameState` is the *only* persistent payload: scene, party, inventory,
  flags, variables, currency, persistent world actor state, progression,
  equipment.  Runtime state (`Battle`, `DialogueState`, `World.actors` HP,
  the active screen) is never saved; it is rebuilt from `GameState` on load.

## Harness coverage

`make test-scenario SCENARIO=save_load_roundtrip` saves a distinctive state,
mutates it, loads it back, and asserts the state is byte-identical at the
semantic level (currency, variables, inventory, party HP, story flags).

Version gating is pinned by a scenario pair using the harness `sram_write`
poke (raw SRAM byte write with the MBC5 RAM gate opened first):
`save_version_accept` saves GOLD 50, gains 25 and loads -- GOLD reads 50
again (control: the save path works); `save_version_reject` does the same
but backdates slot 1's version byte (`0xA002`) to 2 before loading --
GOLD stays 75, proving the load is refused and state untouched.

Power-cycle persistence (writing SRAM to a `.sav` file on hardware/mGBA) is
emulator/hardware behavior and is not covered by the in-session harness test.
