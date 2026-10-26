# CGB color tiles (current pipeline)

Supersedes the old luminance/anchor design below in spirit: there is no
guessing anymore. Artist ramps (`assets/palette.txt`) resolve to hardware
slots (`tools/palette_slots.json`) in `tools/palette_compiler.py`; every
tile is encoded with an existing ramp (`tools/png2gb.py --shade-map`) and
mismatches are reported, never blocked on.

## How a tile gets its colors

1. Each tile names its ramp: world tiles via `"palette"` in
   `tilesets/<set>.json`, battle art via `screens/combat_art/*.json`,
   sprites via `screens/enemy_types/*.json` + `hero.json`, icons via the
   card/HUD skins.
2. `make manifest` resolves ramp → slot and writes per-cell ordered
   shade maps (`generated/tiles/shades/*.json`) + slot tables
   (`generated/tiles/<set>.json`) + C includes (`cgb_palettes.inc`,
   `cgb_obj_palettes.inc`) + the report
   (`generated/tiles/ramp_mismatches.json`).
3. `make gfx` encodes each cell's pixels to shade indices of its ramp
   (exact match, else nearest shade within that ramp).
4. `make tiles` emits `tile_palette.h`: one CGB palette index per VRAM
   slot — the equivalent of png2asset `-use_map_attributes` / rgbgfx
   `-u`, i.e. every 8×8 zone carries which palette applies. The scene
   loader copies it to `g_active_tile_palette`; the renderer stamps it
   per cell into VBK1 tilemap attributes (`ui_draw_world_cell`; battle
   art uses per-enemy `art_palette` via `battle_color_span`; OAM cells
   use per-enemy `ow_palette`).

## Slot semantics (stable, `src/ui/ui.h` `UI_COLOR_*`)

0 NONE/gray, 1 FIRE, 2 IRON, 3 FIELD/ground, 4 POISON/accent, 5 WOOD,
6 GOLD/light, 7 DIM/dark. Ramps move between slots freely
(`tools/palette_slots.json`); slot numbers never change meaning.

## Per-scene loading (space)

CRAM is programmed per scene at every entry (`ui_load_cram_banked`,
bank 5): each world set only in its scenes, the base set only in
battle/UI. OBJ slots 0–4 carry the five overworld sprite ramps; 5–7
stay grey. No screen loads palettes it doesn't show.

---
*Historical note: this file previously described a `--palette auto`
luminance-sort + `--anchor-color` pipeline. That guessing is deleted.
The renderer rules in `docs/graphics.md` still apply.*
