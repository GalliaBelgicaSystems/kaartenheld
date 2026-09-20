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
assets/<composed>.png --\
    |                  +--> make manifest (palette_compiler: per-tile ramp
    |                        shade maps in generated/tiles/shades/ + report)
    |  make gfx  (png2gb --shade-map: shades resolve by exact ramp value,
    |             never by luminance sort; no legacy mode remains)
    v
src/gfx/*.h / *.inc  (linked into fixed or banked ROM code)
```

## Combat tileset (battle art)

- Source: `assets/combat-tile.png` (128x40, 16 cols x 5 rows = 80 tiles)
  + `assets/combat-tileset-description.csv` (canonical tile names; rows
  cover icons/cards, slime, bat/boss, kobold/spider).
- Curated: `tools/level_editor/public/tiles/combat/*.png` (8x8) — DERIVED,
  not hand-maintained: Boss cells reload via `make reload-boss-tiles`
  (`tools/level_editor/reload_boss_tiles.py`), never by hand-editing the
  PNGs. Boss = 9 live tiles (horns/head/torso rows) + 3 glow-eyes variants
  (editor-only: 5 colors, over the 2bpp budget, excluded from the ROM
  LAYOUT).
- Editor defs: `tools/level_editor/tilesets/combat.json`.
- Compose: `tools/compose_battle_sprites.py` LAYOUT (3 cols x N rows) ->
  `assets/battle_sprites.png`. Only names in the LAYOUT resolve to sheet
  cells for the ROM. The boss glow-eyes `_2` cells stay editor-only: the
  glow is an OAM overlay over the BG stamp (the `glow` block in
  `screens/combat_art/boss.json`: eye-cell mask + unlit/lit OBJ ramps),
  reusing the stamp's tile ids and flipping sprite palettes on the battle
  clock -- no sheet row, no extra VRAM. The overlay reuses stamp bytes,
  so both glow ramps must match the set ramp everywhere except index 1
  (compiler-enforced in `battle_compile.py`); the palette report stays
  silent.
- ROM: `src/gfx/battle_enemy_art.h` (only tiles referenced by a combat-art
  set are extracted, via `battle_compile.py --gfx-coords`; blob offsets in
  `battle_types.c`). Battle BG art, variable WxH per set (boss is 3x3).
- Editor pickers: Combat Art Studio brush = `SHEET_TILE_NAMES` (compiles)
  plus `COMBAT_BRUSH_NAMES` (all curated tiles; non-sheet tiles are
  flagged "not compiled to ROM"). The Inspector's per-object sprite
  pickers also list combat tiles (scoped `combat.*`).

## Actors tileset (shared NPC art)

- Curated: `tools/level_editor/public/tiles/actors/*.png` (8x8, real
  transparency → chroma-key yellow on compose) +
  `tools/level_editor/tilesets/actors.json`. No `make extract-tiles` entry
  (`assets/actor-sprites.png` was removed; `assets/sprites.png` is the
  canonical OW sheet). Boss cells (`actors_boss_*`, incl. the `_2` set)
  reload via `make reload-boss-tiles`, never by hand.
- NPC map art: `compose_npc_tiles.py` -> `assets/npc_tiles.png` (village
  overlay slots; per-cell display-slot ramps, see palette_compiler).

## Enemies sheet (shared overworld enemy sprites) — the overworld picker

Like combat art, these curated PNGs have no `make extract-tiles` entry —
but they are DERIVED, not hand-maintained:
`tools/level_editor/public/tiles/enemies/*.png`
(8x8, transparent background that maps to OAM shade 0) +
`tools/level_editor/tilesets/enemies.json`. Source: `assets/sprites.png` +
`sprites-tileset-description.csv`; Boss cells (`boss_ow_*`, the slime-lord
OW sprite) reload via `make reload-boss-tiles` (explicit alias map in the
script: sheet slug `boss_top_left_corner` → `boss_ow_tl`, etc.).

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

## Hero art (two paths — do not confuse)

- Player sprite in-game: `hero_desolate_sprite_tile` (from
  `assets/hero_sprites.png`, composed from `public/tiles/hero/`), loaded
  at OAM and encoded with the hero's OBJ ramp (`screens/hero.json`
  overworld.palette). This is what the player sees.
- `screens/hero.json`: hero name/stats/starter-deck/overworld. The starter
  deck compiles to `src/game/hero_content.c` (`g_hero_starter_deck_ids`,
  bank 2); both `game_new_game` and the battle fallback read it.

## World sheets (background terrain)

- Sources: `assets/forest-tile.png`, `assets/castle-tile.png`,
  `assets/village-tile.png`, `assets/desolate-tile.png` (+ CSVs).
- Curated via `make extract-tiles` -> `public/tiles/<id>/` +
  `tilesets/<id>.json` (per-tile `"palette"` ramp tags; palettes via
  `palette_compiler.py`, `generated/tiles/`).
- ROM: `make gfx` `png2gb --shade-map` rules (`rpg_*_world_tiles.inc`)
  plus single-tile extracts (floor/tree/exit/stumps). Castle encodes in
  vram-index order (9-wide sheet vs 8-wide slots; see palette_compiler).

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
