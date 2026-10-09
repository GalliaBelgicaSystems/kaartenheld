# Battle Screen Compiler Issue — RESOLVED (wiring landed)

> Status update: the designated-initializer syntax was fixed in `6204f34`
> (the compiler already emits positional initializers); `screens/`
> JSON is now wired into the build (`make screens`, `make screens-check`,
> both in `debug`/`release` deps, `/api/compile-rom` runs them).
> Enemy-type battle art (`sprite: { art, frames }` → bank-4 art table) is
> consumed by the battle art loader + BG stamper (HP row 1, art rows 3-4,
> caret row 5).  Still open: full hud_layout-driven positioning, and the
> legacy `src/game/battle_data.c` (`screens/battle.json`) orphan.
> See below for the original report.

## Problem Summary (historical)

The battle screen data compiler (`tools/screen_compiler/battle_compile.py`) generates `src/game/battle_screens.c` using C designated initializers syntax (`.field = value`). The lcc C compiler (used by GBDK-4 for Game Boy development) does not support this syntax, causing compilation errors:

```
src/game/battle_screens.c:10: error 1: Syntax error, declaration ignored at 'BattleScreenDef'
src/game/battle_screens.c:10: syntax error: token -> 'g_battle_screen_duo' ; column 48
```

## Root Cause

The lcc compiler (version 4.3.0 in this project) does not support GCC/Clang designated initializer syntax. The generated code uses patterns like:

```c
static const BattleScreenDef g_battle_screen_duo = {
    .id = "duo",
    .label = "Duo Battle (2 enemies)",
    .max_enemies = 2,
    ...
};
```

But lcc requires regular initialization syntax:

```c
static const BattleScreenDef g_battle_screen_duo = {
    "duo",
    "Duo Battle (2 enemies)",
    2,
    ...
};
```

Or using assignment after declaration:
```c
static const BattleScreenDef g_battle_screen_duo = {0};
g_battle_screen_duo.id = "duo";
g_battle_screen_duo.label = "Duo Battle (2 enemies)";
...
```

## Current Workaround

The `battle.c` file compiles successfully using hardcoded values. The `battle_start_from_data()` function in `battle.c` contains hardcoded default battle setup that works correctly. The data-driven battle screen system is partially implemented:

1. **JSON definitions exist**: `screens/battle/*.json` and `screens/enemy_types/*.json`
2. **Compiler generates output**: `tools/screen_compiler/battle_compile.py` → `src/game/battle_screens.c`, `src/game/battle_types.c`
3. **Renderer reads extern data**: `src/screens/battle_screen.c` declares `extern const struct BattleScreenDef g_battle_screens[]`
4. **Compilation fails**: `battle_screens.c` cannot be compiled with lcc due to designated initializers

## Fix Options

### Option 1: Modify Screen Compiler (Recommended)

Update `tools/screen_compiler/battle_compile.py` to output regular C assignment syntax instead of designated initializers. The generated code would look like:

```c
static const BattleScreenDef g_battle_screen_duo = {0};
g_battle_screen_duo.id = "duo";
g_battle_screen_duo.label = "Duo Battle (2 enemies)";
g_battle_screen_duo.max_enemies = 2;
g_battle_screen_duo.allowed_categories = 3;
// ... enemy positions, timer config, HUD layout
```

This is the cleanest fix because it keeps the data-driven architecture intact while making the generated code compatible with lcc.

### Option 2: Modify battle_screens.c Post-Processing

Add a post-processing step after the compiler runs that converts designated initializers to regular assignment. This is more complex and error-prone.

### Option 3: Keep Hardcoded Values (Current State)

The battle system works with hardcoded values in `battle.c`. The data-driven battle screen JSON definitions exist but cannot be loaded at runtime due to the compiler issue. This is the current state - functional but not fully data-driven.

### Option 4: Switch C Compiler

Change the toolchain to use sdcc instead of lcc, or use a newer lcc that supports designated initializers. This is a significant change with broader implications.

## Recommended Fix Implementation

Modify `tools/screen_compiler/battle_compile.py`'s `build_battle_screens_output()` function to use regular assignment instead of designated initializers:

Current (fails):
```python
lines.append("static const BattleScreenDef g_battle_screen_%s = {" % screen['id'])
lines.append('    .id = "%s",' % screen['id'])
lines.append('    .label = "%s",' % screen['label'])
# ... designated initializers
lines.append("};")
```

Fixed:
```python
lines.append("static const BattleScreenDef g_battle_screen_%s = {0};" % screen['id'])
lines.append("    g_battle_screen_%s.id = \"%s\";" % (screen['id'], screen['id']))
lines.append("    g_battle_screen_%s.label = \"%s\";" % (screen['id'], screen['label']))
lines.append("    g_battle_screen_%s.max_enemies = %d;" % (screen['id'], screen['max_enemies']))
# ... regular assignments
```

This change would make the generated `battle_screens.c` compatible with the lcc compiler while keeping the full data-driven battle screen architecture functional.

## Files Affected

- `tools/screen_compiler/battle_compile.py` - Modify `build_battle_screens_output()` function
- `src/game/battle_screens.c` - Regenerated output (would be auto-generated)
- `src/game/battle_types.c` - Already works (enemy types use different pattern)

## Validation

After the fix, run:

```bash
make release
make test-harness
make test
```

All scenarios should pass, including battle scenarios.