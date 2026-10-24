# Fixed-Bank Move-Out Plan (debug build)

## Problem

The debug build's fixed `_CODE`/`_HOME` region ends at `0x7FFF` -- 20 B of
headroom below `0x8000` (AGENTS.md §52.18). Release still has ~4.9 KB free,
so this binds the debug (harness) build only. The next fixed-bank addition
of any size breaks the debug link (`Possible overflow from Bank 1 into
Bank 2`, Makefile fails loudly). Merging master already tipped it over
once (recovered with a diet plus prep-side staging); the remaining margin
covers roughly two struct accesses, not a feature.

Rule: no feature work that adds fixed-bank code in debug until this lands.

## Strategy

Same pattern the codebase already uses everywhere (§52.11.1, §52.18):
move bodies to a switchable bank behind the existing thin fixed wrappers,
which keep their signatures. Candidates, biggest first:

### 1. `scenario_load_state` + `debug_run_action` (src/debug/scenarios.c)

`scenario_check_and_load()` itself is a ~20 B wrapper, but the two
`static` bodies it calls live in fixed bank and are the actual weight
(state-descriptor apply across every `GameState` section; the full debug
`g_debug_action` dispatch). Move both bodies to a banked unit (bank 5 has
~2.3 KB debug / ~0.6 KB release free; bank 2 ~475 B debug) behind the
current wrapper, staging through the `g_bk_*` globals. Both bodies
already talk to banked content in places; audit every callee for
fixed-bank calls first (anything fixed-only must be staged out, the way
`battle_defend_resolve_banked` stages magnitudes instead of emitting).

### 2. `debug_snapshot` + `debug_state_snapshot` wrappers (src/debug/telemetry.c)

Each is ~16 B of staging + `banked_call_run()` to bank 2. Merge into one
wrapper taking an opcode byte (core vs extended snapshot), or have the
two game.c call sites dispatch directly. Saves ~10-15 B -- only worth
doing if candidate 1 lands short of the target.

### 3. `ui_draw_font_test` wrapper (src/ui/ui.c)

Thin dispatch to the bank-3 body. Same merge treatment as (2) if a
sibling wrapper exists on the same call path; otherwise leave it -- the
body is already banked and the wrapper is ~15 B. Like (2): only if
candidate 1 lands short.

### Explicitly out of scope

- Release layout (healthy) and bank 3/4/5 budgets beyond keeping them green.
- The dagger-counter wrapper itself (`battle_defend_counter`, ~190 B):
  it calls fixed-bank mechanics (`combatant_take_damage`, `rng_next`,
  `status_apply`, `telemetry_emit`) and cannot run banked. It benefits
  from this plan without being part of it.
- Rewriting the banked-call ABI, touching CRT0, or changing telemetry
  wire layout (§53.4).

## Acceptance

- `make memmap`: debug fixed headroom **>= 256 B** (a typical feature's
  worth; the exact number goes into the §52.18 note, replacing the 20 B).
  Moving `scenario_load_state` + `debug_run_action` alone should clear
  this easily (gap-measured at ~1.9 KB combined in the debug `.sym`) --
  move one body first, re-measure, then decide whether the second is
  needed. Note bank 5 is also the AGENTS-recommended home for new banked
  bodies, so budget it against that guidance and update the §52.18
  sentence when the move lands.
- `make lint` (incl. `check_banked_copies.sh`), `make stack-budget`,
  `make test-sentinels` (layout-sensitive codegen, §52.19), full
  `make test-harness`, `make test`, `make verify-oam`, `make verify-music`,
  `make verify-walkthrough` -- all green, since moving code shifts bank
  layout.
- No behavior change: harness scenarios are byte-identical in outcome;
  telemetry event streams unchanged (new staging must not add, drop, or
  reorder events -- `town_reentry`-style `event_not_occurred` asserts
  would catch it).

## Process

1. Measure first: per-function fixed sizes from `build/debug/*/*.lst`
   (start address of next function minus start address).
2. Move one candidate at a time, smallest diff that links; re-run
   `make test-sentinels` + the debug build after each (bisectability --
   never diagnose from a dirty tree, §52.19).
3. Full `make -j$(nproc) validate` once at the end, then commit ROMs.

## Deletion

This file shall be deleted once the fix lands.
