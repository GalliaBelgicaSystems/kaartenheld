# Tileset inventory — which tilesets are used for what

This file is the map of every tile source in the project, so future work
starts from the right sheet. Rule of thumb: **source PNGs in `assets/` are
never read by the ROM build directly except through the listed pipeline
(curated PNGs → compose script → `src/gfx/*` header)**. The one documented
exception is noted below.

Pipeline stages (top to bottom), repeated per tileset:

```text
assets/<source>.png + assets/<source>-description.csv
    |  make extract-tiles  (world/actor sheets only; combat/enemy sheets
    |                        are hand-curated, see below)
    v
tools/level_editor/public/tiles/<id>/*.png   (curated editor PNGs)
tools/level_editor/tilesets/<id>.json        (tile defs; SOURCE OF TRUTH
                                              for the editor — never blindly
                                              re-run extract-tiles, it would
                                              overwrite hand fields)
    |  tools/compose_*.py  (packs curated PNGs into a ROM source sheet)
    v
assets/<composed>.png  (+ generated/tiles/<id>_shades.json from
    |                    make manifest: per-tile exact shade maps +
    |                    strict pixel-vs-ramp validation for indexed sheets)
    |  make gfx  (png2gb --tile-coords from a compiler --*-coords query;
    |             indexed sheets add --shade-map so shades resolve by
    |             exact value, never by luminance sort)
    v
src/gfx/*.h / *.inc  (linked into fixed or banked ROM code)
```

## Combat tileset (battle art)

- Source: `assets/combat-tile.png` (128x32, 16 cols x 4 rows = 64 tiles)
  + `assets/combat-tileset-description.csv` (canonical tile names; rows
  cover icons/cards, slime, bat/boss, kobold/spider).
- Curated: `tools/level_editor/public/tiles/combat/*.png` — hand-curated,
  NOT produced by `make extract-tiles`. Enemy art is 32x32 (4x pixel art);
  HUD/cards/icons are 8x8. Boss = 9 tiles (horns/head/torso rows).
- Editor defs: `tools/level_editor/tilesets/combat.json`.
- Compose: `tools/compose_battle_sprites.py` LAYOUT (3 cols x N rows) ->
  `assets/battle_sprites.png`. Only names in the LAYOUT resolve to sheet
  cells for the ROM.
- ROM: `src/gfx/battle_enemy_art.h` (only tiles referenced by a combat-art
  set are extracted, via `battle_compile.py --gfx-coords`; blob offsets in
  `battle_types.c`). Battle BG art, variable WxH per set (boss is 3x3).
- Editor pickers: Combat Art Studio brush = `SHEET_TILE_NAMES` (compiles)
  plus `COMBAT_BRUSH_NAMES` (all curated tiles; non-sheet tiles are
  flagged "not compiled to ROM"). The Inspector's per-object sprite
  pickers also list combat tiles (scoped `combat.*`).

## Actors tileset (shared NPC/enemy art)

- Source: `assets/actor-sprites.png` + `assets/actor-tileset-description.csv`
  (12x2 grid; yellow `(241,235,3)` chroma-keys to transparent on import).
- Curated: `tools/level_editor/public/tiles/actors/*.png` (24 tiles) +
  `tools/level_editor/tilesets/actors.json`, via `make extract-tiles`.
- Crucially, `actor-sprites.png` is **never read by `make gfx`**. The
  curated actors PNGs are editor previews and per-object sprite choices
  (Inspector lists every tileset). To use actors art in a *compiled*
  pipeline, copy the PNGs into the target sheet's curated directory:
  - boss corners -> `public/tiles/enemies/boss_ow_*` (2x2 shared boss
    overworld sprite),
  - kobold/spider frames -> `public/tiles/enemies/kobold_*`,
    `spider_*` (pickable enemy overworld art).

## Enemies sheet (shared overworld enemy sprites) — the overworld picker

Like combat art, these curated PNGs are hand-maintained (no
`make extract-tiles` entry): `tools/level_editor/public/tiles/enemies/*.png`
(8x8, transparent background that maps to OAM shade 0) +
`tools/level_editor/tilesets/enemies.json`.

- Curated: `tools/level_editor/public/tiles/enemies/*.png` (8x8,
  transparent background that maps to OAM shade 0) +
  `tools/level_editor/tilesets/enemies.json` (only `category: 'enemy'`
  tiles appear in the Enemies-view picker).
- **This is the overworld sprite picker's sole source.** To make any art
  pickable as an enemy overworld sprite: add the PNG here, register it in
  `enemies.json`, and add a row to `tools/compose_enemy_sprites.py`
  LAYOUT. `EnemyManager.tsx` (`owTiles` filter) and `MapCanvas.tsx`
  (`owImgs` loader) both key off this tileset, so no editor code changes
  are needed for new tiles.
- Compose: `compose_enemy_sprites.py` -> `assets/enemy_sprites.png`.
- ROM: `src/gfx/enemy_ow_tiles.h` via `battle_compile.py --ow-coords`
  (**enemies only** — the hero uses its own sprite path, see below), then
  streamed into OAM at `ENEMY_OW_BASE` (100). Per-type base/frames/w/h in
  `battle_types.c` (`ow_tile`, `ow_w`, `ow_h`, `ow_frames`). OAM budget:
  28 tiles (100-127); the blob only includes tiles actually referenced by
  an enemy type, so unreferenced sheet rows are free.
- The OAM writer (`ui_world_sprite_banked.c`) draws a w*h grid per actor;
  enemy `overworld` JSON is `{ width, height, cells }` with `cells` a flat
  frame-major list of `width*height*frames` tile names.

## Hero art (three separate paths — do not confuse)

- Player sprite in-game: `hero_desolate_sprite_tile` (from
  `assets/desolate_landscape.png` tile-coords `1,2 2,2`), loaded at OAM
  tile 98 (`HERO_DESOLATE_SPRITE_TILE_ID`). This is what the player sees.
- `public/tiles/hero/` + `tilesets/hero.json` + `compose_hero_sprites.py`
  -> `assets/hero_sprites.png`: currently **no ROM consumer** (dead
  artifact kept for editor previews). Do not add hero art here expecting
  it in-game. Likewise `g_hero_ow_*` in `battle_types.c` is emitted but
  unread by the ROM.
- `screens/hero.json`: hero name/stats/starter-deck/overworld. The starter
  deck compiles to `src/game/hero_content.c` (`g_hero_starter_deck_ids`,
  bank 2); both `game_new_game` and the battle fallback read it.

## World sheets (background terrain)

- Sources: `assets/forest-tile.png`, `assets/castle-tile.png`,
  `assets/village-tile.png`, `assets/desolate_landscape.png` (+ CSVs).
- Curated via `make extract-tiles` -> `public/tiles/<id>/` +
  `tilesets/<id>.json` (palettes via `palette_compiler.py`,
  `generated/tiles/`).
- ROM: `make gfx` `png2gb` direct-sheet rules (`rpg_*_world_tiles.inc`)
  plus single-tile extracts (floor/tree/exit/stumps).
  Migrated sheets (`"indexed": true`, forest first) encode via
  `--shade-map` exact values; the rest still use the legacy
  luminance sort until their art ships indexed.
- `assets/desolate_landscape.png` triple duty: world sheet + reference
  palette for enemy-sheet quantization + `hero_desolate_sprite_tile`.

## Fonts, icons, audio, mockups

- `assets/intrepid.png` -> `intrepid_font_tiles.inc` (font).
- `assets/title-red.png` (128×24, 16×3 tiles) -> `title_logo_tiles.inc`
  (`make gfx`), the bitmap title logo.  Loaded into the world BG block
  (ids 128-175) with CGB palette 1 by the title screen; a copy is
  published to `tools/level_editor/public/tiles/title/logo.png` so the
  editor's title preview mirrors the ROM 1:1.  `title-brun.png` is the
  unused brown variant (reference only).
- `assets/gallia_belgica_systems.png` (104×40, 13×5 cells) -> the deduped
  `splash_logo_tiles.h` blob + raster map (`make gfx`,
  `tools/screen_compiler/splash_logo_compile.py`), the boot studio splash.
  Rendered from the bank-2 `ui_splash_logo_render_banked` body with one CGB
  palette `[white, red, blue, black]` (the gray anti-aliasing folds into
  black).  LLM-only content: the editor has no splash screen.
- UI icons come from the combat sheet (`compose_card_frames.py` ->
  `card_frame_tiles.h`); gold prices use the font glyph `G`.
- `assets/music/*.uge` -> hUGETracker soundtrack, ROM bank 6.
  `assets/sfx/*.uge` -> transcribed SFX tables, ROM bank 7.
- Reference/mockups only (never build inputs): `battle_screen_mockup.jpg`,
  `desolate_landscape_example.png`, `forest-json.png`, `title-brun.png`.
