# Wiring New Assets Guide

This guide explains how to add and wire new assets into the engine, the level editor, and the compiled Game Boy ROM.

> **Key Rule**: Raw assets in `assets/` are **never** directly read by the Game Boy ROM build. Everything flows through a deterministic pipeline:
>
> `assets/<source>` → Curated Editor Files (`public/tiles/`) → Composition Scripts (`tools/compose_*.py`) → Generation Tools (`make gfx`, compilers) → `src/gfx/` & `src/game/` C data.

---

## Table of Contents

1. [Asset Types Overview](#1-asset-types-overview)
2. [Overworld Enemy Sprites (OAM)](#2-overworld-enemy-sprites-oam)
3. [Combat & Battle Enemy Art (BG)](#3-combat--battle-enemy-art-bg)
4. [World Tilesets & Background Terrain](#4-world-tilesets--background-terrain)
5. [Audio (Music & SFX)](#5-audio-music--sfx)
6. [Icons & Symbols (Asset Atlas)](#6-icons--symbols-asset-atlas)
7. [Validation & Verification Checklist](#7-validation--verification-checklist)

---

## 1. Asset Types Overview

| Asset Type | Source Directory | Editor Tile Path | Sheet Composer | Target C / Header |
|------------|------------------|------------------|----------------|-------------------|
| **Overworld Enemy** | `assets/actor-sprites.png` or custom PNG | `tools/level_editor/public/tiles/enemies/` | `tools/compose_enemy_sprites.py` | `src/gfx/enemy_ow_tiles.h`, `src/game/battle_types.c` |
| **Battle Art** | `assets/combat-tile.png` | `tools/level_editor/public/tiles/combat/` | `tools/compose_battle_sprites.py` | `src/gfx/battle_enemy_art.h`, `src/game/battle_types.c` |
| **World Tiles** | `assets/*-tile.png` | `tools/level_editor/public/tiles/<tileset>/` | `extract_tiles.py` / `png2gb.py` | `src/gfx/rpg_*_world_tiles.inc`, `src/game/scenes_content.c` |
| **Icons** | `assets/combat-tile.png` | `tools/level_editor/public/tiles/combat/` | `tools/compose_card_frames.py` | `src/gfx/card_frame_tiles.h` (VRAM 96–127) |
| **Music** | `assets/music/*.uge` | N/A | `uge2source` | `src/music/*.c`, `build/*/music/*.o` |
| **SFX** | `assets/sfx/*.uge` | N/A | SFX tables | `src/sfx/sfx_tables.c`, `src/sfx/sfx_index.c` |

---

## 2. Overworld Enemy Sprites (OAM)

Overworld enemy sprites are 8x8 (or NxM grid) tiles loaded into Game Boy sprite memory (OAM) at `ENEMY_OW_BASE` (tile 100). The budget is 28 tiles (100–127).

### Step-by-Step:
1. **Prepare the 8x8 Tile PNG(s)**:
   - Ensure pixel size is 8x8 (or multiples of 8).
   - Background must be transparent (chroma-key maps to OAM shade 0 / transparent).
   - Save to: `tools/level_editor/public/tiles/enemies/<tile_name>.png`.

2. **Register in Editor Tileset**:
   - Open `tools/level_editor/tilesets/enemies.json`.
   - Add entry:
     ```json
     {
       "id": "my_enemy_f0",
       "label": "My Enemy Frame 0",
       "gb_constant": "TILE_ENEMIES_MY_ENEMY_F0",
       "walkable": false,
       "color": "#826d37",
       "ascii": "E",
       "image_url": "/tiles/enemies/my_enemy_f0.png",
       "category": "enemy"
     }
     ```
   *(Only tiles with `"category": "enemy"` appear in the level editor's overworld enemy picker).*

3. **Add to Enemy Sprite Sheet Layout**:
   - Open `tools/compose_enemy_sprites.py`.
   - Add your tile name to the `LAYOUT` 2D array grid:
     ```python
     LAYOUT = [
         ['slime_f0', 'slime_f1', 'bat_f0', 'bat_f1'],
         ['boss_ow_tl', 'boss_ow_tr', 'boss_ow_bl', 'boss_ow_br'],
         ['kobold_f0', 'kobold_f1', 'spider_f0', 'spider_f1'],
         ['kobold_idle', 'mimic_f0', 'mimic_f1', 'my_enemy_f0'],
     ]
     ```
   - Running `python3 tools/compose_enemy_sprites.py` repacks `assets/enemy_sprites.png`.

4. **Attach to Enemy Definition**:
   - In `screens/enemy_types/<enemy_name>.json`, specify the overworld dimensions and animation cells:
     ```json
     "overworld": {
       "width": 1,
       "height": 1,
       "cells": ["my_enemy_f0", "my_enemy_f1"]
     }
     ```
   - In maps (`levels/<map>.json`), actors can reference `"animation_frames": ["enemies.my_enemy_f0", "enemies.my_enemy_f1"]`.

5. **Recompile Graphics and Types**:
   ```bash
   python3 tools/screen_compiler/battle_compile.py
   python3 tools/level_compiler/compile.py
   make gfx
   ```
   This updates `src/gfx/enemy_ow_tiles.h` and `src/game/battle_types.c` (`ow_tile`, `ow_frames`, `g_enemy_ow_tile_count`).

6. **Sprite Palettes & OAM Verification**:
   - If the sprite requires a specific CGB OBJ palette (e.g. green for slimes, brown for beasts/wood), configure in `src/ui/ui.c`.
   - Update tile ID expectations in `tools/verify_oam.py`.
   - Run `make verify-oam` to ensure OAM positions and tile indices match expectations.

---

## 3. Combat & Battle Enemy Art (OAM small enemies, BG boss)

Small battle enemies render as hardware sprites (each with its own OBJ
palette — mixed parties need no shared BG slot); the solo boss keeps the
background stamp. Art is 8x8 tiles, max 3 wide × 2 tall per enemy
(scanline budget: 3 enemies × 3 + caret must stay ≤ 10 sprites/line).

### Step-by-Step:
1. **Curate Tiles**:
   - Add 8x8 component tiles to `tools/level_editor/public/tiles/combat/<name>.png`.
   - One 4-color ramp per enemy: every tile's pixels must fit it exactly
     (the build has no luminance fallback here — a shared shade must be
     the same color in every tile using it; spider art is pending this).
2. **Combat-art JSON** (`screens/combat_art/<enemy>.json`):
   - `"oam": true` + `"obj_palette": "<SLOTS/OBJ ramp>"` for OAM enemies;
     omit both for BG-stamped (boss).
3. **Register in Tileset**:
   - Add entries to `tools/level_editor/tilesets/combat.json`.
3. **Add to Battle Sheet**:
   - In `tools/compose_battle_sprites.py`, add tile names to `LAYOUT`.
   - Run `python3 tools/compose_battle_sprites.py` to regenerate `assets/battle_sprites.png`.
4. **Define in Enemy Types**:
   - Author combat art in the editor's Combat Art Studio or edit `screens/enemy_types/<enemy>.json` under `"combat_art"`.
5. **Recompile**:
   ```bash
   python3 tools/screen_compiler/battle_compile.py
   make gfx
   ```
   This generates `src/gfx/battle_enemy_art.h` containing only tiles referenced by active combat sets.

---

## 4. World Tilesets & Background Terrain

Background maps (Forest, Castle, Village, Desolate Landscape, etc.) use background tiles.

### Step-by-Step:
1. **Source Asset & Description**:
    - Place source image in `assets/<tileset>-tile.png` (grid of 8x8 tiles).
      World sheets are **indexed PNGs**: exact ramp hexes only, ≤4 values
      per tile, background as index 0 (forest is migrated; new sheets
      ship indexed from day one).
    - Place corresponding metadata in `assets/<tileset>-tileset-description.csv` with `row, col, name, walkable, palette`.
    - Flag the sheet `"indexed": true` in `tools/level_editor/tilesets/<tileset>.json` once its art meets the contract above.
2. **Extract to Editor**:
   - Run tile extraction or update `tools/level_compiler/extract_tiles.py`:
     ```bash
     make extract-tiles
     ```
    - This writes `tools/level_editor/public/tiles/<tileset>/*.png` and generates `tools/level_editor/tilesets/<tileset>.json`.
3. **Compile to ROM Tiles**:
    - Run `make manifest` first: besides the palette manifests it emits
      `generated/tiles/<tileset>_shades.json` (per-tile exact shade maps)
      and strict-validates every tile's pixels against its ramp.
    - In `Makefile`, define `png2gb` rules to compile tiles into `src/gfx/rpg_<tileset>_world_tiles.inc`, passing `--shade-map generated/tiles/<tileset>_shades.json` for indexed sheets (exact values, no luminance guessing; off-ramp pixels fail loudly).
   - Update `tools/level_compiler/compile.py` to map level JSON tiles into C structures in `src/game/scenes_content.c`.
5. **Palette Assignment**:
   - Background tiles on Game Boy Color require palette attributes (0–7). Ensure palettes are mapped in `tools/level_compiler/palette_compiler.py` and `src/game/tiles_content.c`.

---

## 5. Audio (Music & SFX)

Sound and music use the [hUGETracker](https://nickfa.ro/huge-tracker) engine (bank 6 for music, bank 7 for SFX).

### Music Tracks (`.uge`):
1. Save the `.uge` tracker file into `assets/music/<track_name>.uge`.
2. The Makefile rule compiles `.uge` via `uge2source` into `build/<target>/music/<track_name>.o`.
3. Declare the track in `src/audio/huge_music.h` and link to scene playback in `levels/<map>.json` (`"music": "<TRACK_ID>"`).

### Sound Effects (`.uge`):
1. Save SFX track into `assets/sfx/<sfx_name>.uge`.
2. Transcribe voice parameters (duty, sweep, volume envelopes) into `src/sfx/sfx_tables.c`.
3. Register SFX identifier in `src/sfx/sfx_index.c` and enum in `src/audio/sfx.h`.

---

## 6. Icons (combat sheet) & gold glyph

Small 8x8 UI icons (weapons, HUD hp/ap/deck, status riders, select arrow,
arrow counters) come from `assets/combat-tile.png` via
`tools/level_editor/public/tiles/combat/` and `tools/compose_card_frames.py`
into `src/gfx/card_frame_tiles.h` (VRAM 96–127). Every icon cell's pixels
must fit its slot ramp exactly (see `tools/emit_sheet_sidecars.py`);
off-ramp pixels fail loudly. Gold prices use the font glyph `G`, not a tile.

## 7. Validation & Verification Checklist

Whenever new assets are wired or modified, perform this validation sequence:

```bash
# 1. Compile graphics
make gfx

# 2. Check parity between Level Editor JSON and ROM
python3 tools/parity_check.py

# 3. Check OAM sprite integrity & boundary bounds
make verify-oam

# 4. Validate ROM build and checksums
make test

# 5. Run scenario test harness (if gameplay state/code was touched)
make test-harness
```
