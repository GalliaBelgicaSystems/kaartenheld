# Graphics (pipeline, first slice implemented)

The current prototype renders with the console font (ASCII); the player is a
real OAM sprite (see the `sprites` branch work).  This document specifies the
planned graphics pipeline.

## Goal

Replace the ASCII presentation with real Game Boy tiles/sprites while keeping
the game **mechanically identical**: same `GameState`, scenes, actors, quests,
battle, inventory, equipment, boss — different presentation.

## Pipeline

```text
assets/palette.txt (artist colors + ramps)
  + tools/palette_slots.json (dev ramp -> slot)
  + content ramp tags (tilesets/*.json, screens/*.json)
      ↓  make manifest (tools/palette_compiler.py)
generated/tiles/ (slot tables, per-cell shade maps, C includes, mismatch report)
      ↓  make gfx (tools/png2gb.py --shade-map)
GB-native data (tileset bytes; per-tile palette attributes in tile_palette.h)
      ↓
ROM (banked const tables; CRAM programmed per scene at entry)
```

`tools/png2gb.py` has two modes, no guessing in either: `--shade-map`
(per-tile ordered ramp shades from the compiler; off-ramp pixels take
the nearest shade of the tile's ramp and are reported) and strict
`--palette canonical|gb_green` (font art; any off-palette pixel fails).
`tools/palette_compiler.py` must produce actionable output (which sheet,
which tile, which ramp, which colors) rather than silently emitting
wrong graphics.

## Status

Implemented: everything above (`make manifest`, `make palette-check`,
`make gfx`, `make tiles`), per-scene CRAM loading
(`ui_load_cram_banked`), OAM sprite engine, CGB palettes with DMG
fallback. See `assets/WIRING_ASSETS.md` for the asset wiring guide and
`assets/palette_tutorial.md` for the artist contract.

## Renderer requirements (DMG + CGB)

* Tilemap background rendering via `set_bkg_data`/`set_bkg_tiles` replacing
  the console (`putchar`) path in `src/ui/ui.c`.
* OAM sprite engine for the player, NPCs, and enemies (40 sprite limit).
* CGB palettes (BG + OBJ) with a DMG grayscale fallback; reset `VBK_REG = 0`
  after attribute writes (AGENTS.md §38).
* CGB OBJ palettes must be set explicitly via `set_sprite_palette()`. The CGB
  boot ROM leaves object colors uninitialized and `OBP0`/`OBP1` are
  Non-CGB-Mode-only registers, so a sprite without an explicit OBJ palette
  renders with emulator-dependent garbage (AGENTS.md §38.1).
* Harness-safe: no VBlank waits in harness paths (`g_harness_mode` skips
  vsync; `ui_init` turns the LCD off before `font_load`) — AGENTS.md §52.6.
* Keep incremental redraws for player movement / HUD (AGENTS.md §36).

Hardware reference: local Pan Docs checkout at
`/home/brodrigues/Documents/repos/pandocs` (`src/Palettes.md`,
`src/Power_Up_Sequence.md`).  Note: CRAM reads return `0xFF` during Mode 3,
which is why mGBA-debugger `OCPD` reads are unreliable for palette checks.

### Sprite transition-hide timing rule

A full-screen redraw (clear + redraw, ~360 per-char `putchar` writes) takes
several display sweeps — the screen visibly blanks and redraws top-to-bottom.
Any sprite state change around a transition must therefore be written to
**real OAM before the redraw starts**, not at the end of the frame:

* `game_render()` calls `ui_sprite_begin_transition()` (forces real OAM Y=0,
  preserves shadow position) before `screen_render()` whenever a full redraw
  is about to run.
* `ui_sprite_commit()` (shadow OAM → real OAM DMA) runs once per frame in
  `main.c` **after `vsync()`**, i.e. during VBlank, so each displayed frame
  shows exactly the intended sprite state and the transition hide never
  reveals the sprite at a stale position over the wipe.

There are exactly three hide triggers, and nothing else:

1. render cache invalid (`rc->valid == false`) — screen change / boot /
   restart (a `game_render_reset()`);
2. screen change detected mid-transition (`rc->prev_screen != g->screen`);
3. map change (`g->world.map_id != rc->prev_map_id`) — a gate crossing goes
   through `world_change_map()` without a reset, so the overworld renderer
   wipes based on this mismatch alone.

`game_render_reset()` must therefore initialize `prev_map_id` to the current
map (`g->world.map_id`) and `prev_screen` to the screen being left, **never**
magic `255` sentinels.  A `255` `prev_map_id` makes every non-overworld frame
(battle/dialogue/menu) look like a map change, re-running the hide every
frame — on real hardware the reveal only happens during VBlank, so the sprite
is invisible for the whole fight/discussion.  Steady frames on those screens
must never re-hide.  This regression is caught by `make verify-oam` (break
at `ui_sprite_begin_transition` and assert it never fires on steady battle
frames); see AGENTS.md §52.15-52.17.

### Dialogue overlay rule

A dialogue started from the overworld draws **only its box** over the
overworld's last full redraw (the box covers the bottom map rows); it must NOT call
`ui_draw_world_full()` — that would wipe the map the player just saw.
`dialogue_screen_render()` skips the world draw when `rc->prev_screen ==
SCREEN_OVERWORLD`.  A dialogue that is NOT preceded by the overworld
(scenario boot) must force the world draw via `render_cache.prev_screen =
SCREEN_DIALOGUE` (done in `scenarios.c`); never poke `g_game.prev_screen`,
which item-menu close navigation reads.

## Screens to rewire

overworld (tileset + animated player + NPC/enemy sprites), battle (enemy
sprite + text box), dialogue (text box), item/shop menus (menu frame),
ending/game-over.

## Stress test

Large varied tilemaps, many actors, simultaneous animation, DMG+CGB palette
switching, and the full transition chain
`overworld → town → dialogue → battle → victory → overworld`.  Measure ROM
growth, VRAM/WRAM, sprite/tile limits, and CPU timing — before the real game
depends on them.

## Harness

Semantic state stays authoritative; screenshots then show real pixels.  Add a
`GRAPHICS:` block (tileset, sprite set) to the semantic dump so an LLM never
needs to infer state from graphics.
