# Level Compiler & Toolchain

This directory provides tools for building, validating, describing, and programmatically manipulating levels for the Game Boy RPG engine.

---

## 1. Pipeline Overview

```text
levels/*.json (JSON level source of truth)
    ↓
validate.py (Schema, boundary, spawn, and tile checks)
    ↓
compile.py (Resolves semantic tiles, merges terrain blocks, generates C)
    ↓
src/game/scenes_content.c (C scene & terrain tables in ROM Bank 5)
```

---

## 2. Tools

### `compile.py`

Compiles one or all JSON level files into `src/game/scenes_content.c`:

```bash
# Compile all levels into the game
python3 tools/level_compiler/compile.py --all

# Compile specific level(s) to a custom output
python3 tools/level_compiler/compile.py levels/forest.json -o build/forest_scene.c
```

### `validate.py`

Validates level files against schema constraints, tileset IDs, player spawn safety, exit linkages, and object placements:

```bash
python3 tools/level_compiler/validate.py levels/forest.json
python3 tools/level_compiler/validate.py levels/*.json
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

### `describe.py`

Extracts an LLM-native semantic description (Markdown or JSON) of a level:

```bash
# Human / LLM readable Markdown
python3 tools/level_compiler/describe.py levels/forest.json

# Machine-readable JSON summary
python3 tools/level_compiler/describe.py levels/forest.json --format json
```

### `apply_ops.py`

Executes high-level structured operations against level files (used by both LLM agents and editor backends):

```bash
python3 tools/level_compiler/apply_ops.py levels/forest.json \
  --op '{"operation": "paint_rectangle", "tile": "forest.tree", "x": 4, "y": 2, "width": 2, "height": 1}'
```

---

## 3. Makefile Targets

From the repository root:

* `make level LEVEL=forest`: Validates and compiles `levels/forest.json` (or all levels).
* `make levels`: Compiles all `levels/*.json` into `src/game/scenes_content.c`.
