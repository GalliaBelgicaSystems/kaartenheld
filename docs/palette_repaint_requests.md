# Palette repaint requests (exact-color wiring handoff)

Reproduce: `nix develop --command python3 tools/emit_sheet_sidecars.py`
(fails loudly, lists every cell below). World sheets (forest / castle /
desolate / village, 171 tiles) already pass exactly -- nothing to do there.

Rule: every pixel must equal a color of its assigned ramp
(`assets/palette.txt`), max 4 colors per 8x8 cell. No luminance sorting,
no nearest-color snapping anywhere in the pipeline anymore. The tables
below give, per cell, the off-ramp colors and the ramp it must fit.
How each cell gets there (repaint pixels vs. redesign a ramp) is the
artist's call -- the pipeline will not guess.

CGB budget note: all 8 BASE and all 8 OBJ hardware slots are consumed,
so a brand-new ramp needs a free slot; the only free slot is VILLAGE 7.
Anything below that fits no existing ramp needs repainted art (or a
slot eviction -- say which ramp goes).

## Provisional dev-side changes (review requested)

- `public/tiles/enemies/boss_ow_br.png`: 6 stray `#8d754a` pixels in
  column 4 (tile-seam bleed from the neighboring brown cell) snapped to
  `#565656` so the cell fits `sprites2`. One vertical seam, almost
  invisible -- confirm or repaint properly.
- Transparency restoration (hue-preserving, alpha-only, no color touched):
  `bat_f0/f1` (slate P-mode key to transparent), `dog_f0/f1`,
  `fire_f0/f1` (painted town-ground bg to transparent), all 27 OAM
  combat cells (border-connected white to transparent; interior paint
  such as kobold/mimic eyes kept).
- Data reassignments (art untouched, art already fits exactly):
  kobold OW `sprites5` -> `sprites`, boss OW `sprites5` -> `sprites2`,
  hero OW `sprites8` -> `sprites` (hero source `assets/sprites.png`
  indexes pure black `#000000`; it used to render dark brown only
  because the old luminance mapper put black on shade 3 -- now black).

## Combat battle art (`assets/combat-tile.png` -> `battle_sprites.png`)

Assigned ramp per set: `screens/combat_art/*.json` (`palette` for
BG-stamped, `obj_palette` for OAM). OAM cells flatten onto SPRITES
yellow `#f1eb03` (shade 0 = transparent), BG cells onto white.

| Set / cells | Off-ramp colors | Must fit |
|---|---|---|
| bat, all 6 cells (`combat_*_bat`) | `#3d3044` wings (+ `#c34e1b` eyes in `top_middle`) | `sprites2` = `f1eb03, 8b1b1b, 565656, 000000` (the anticipated `fightbat` refresh into `fightmimic` browns also resolves the BG side) |
| boss `left_middle`, `middle_center` | `#8b1b1b` scepter/eye accents | `fightboss` = `ffffff, 8d754a, 565656, 000000` |
| spider `top_middle` | `#c34e1b` eye | `fightspider` = `ffffff, 8f8f8f, 5a4a3d, 000000` |
| kobold `top_left`, `top_middle`, `bottom_middle` | `#ffffff` eyes (interior paint) | `battle_kobold` = `f1eb03, 844f4f, 4a2727, 4a2727` (no white entry -- either add one or repaint the eyes) |
| mimic `top_middle` | `#ffffff` eyes (2 px) | `sprites6` = `f1eb03, 8d754a, 6f5a34, 3f3017` (same white-entry question) |

Slime (9 cells) and the rest of kobold/mimic/spider/boss already pass.

## Overworld enemies (`enemy_sprites.png`)

Assigned ramp: `screens/enemy_types/*.json` `overworld.palette`.

| Cells | Off-ramp colors | Must fit |
|---|---|---|
| `dog_f0`, `dog_f1` | `#645233` body, `#8b1b1b` accent | `sprites5` = `f1eb03, ac9d23, 8d754a, 6f5a34` |
| `fire_f0`, `fire_f1` | `#26232e`, `#d7a726`, `#edc214` | `sprites5` (same) -- no OBJ ramp holds fire hues today |

Bat / spider / kobold / slime / mimic / boss OW all pass after the
reassignments above.

## NPC map overlays (`assets/npc_tiles.png`, village slots)

Overlay slots render with village BG palettes (`tiles_content.c`
`npc_pals`: guard/wizard/merchant/dogs on `town1`, mayor on `town2`).
The art is painted in SPRITES hues, so none fits:

| Cell | Off-ramp colors | Assigned |
|---|---|---|
| `actors_guard` | `#3f3017` | `town1` = `b6a27e, 8d754a, 79643e, 645233` |
| `actors_wizard` | `#3f3017, #475ca8, #b6b6b6` | `town1` |
| `actors_merchant` | `#3f3017, #8f591f` | `town1` |
| `actors_mayor` | `#3f3017, #8b1b1b, #b6b6b6` | `town2` = `b6a27e, f1cf91, ccaa6c, 645233` |
| `actors_dog_frame_1/2` | `#8b1b1b` | `town1` |

Repaint into town hues, or say which village slot to redesign (only
slot 7 is free -- one ramp max, but five hue groups need homes).

## Card-frame icons (`assets/card_frames.png`)

Frames (rows 0-2), bar segments and hp/poison icons pass. Each icon
below must fit its skin slot ramp (see `emit_sheet_sidecars.py`
`CARD_RAMPS` for the slot mapping):

| Cell | Off-ramp colors | Must fit |
|---|---|---|
| `combat_sword_icon` | `#755930` | `fight1` = `ffffff, dfbd8d, e3ae63, b09266` |
| `combat_shield_icon` | `#755930` (+`#dfbd8d` ok) | `fightmimic` = `ffffff, 8d754a, 6f5a34, 3f3017` |
| `combat_bow_icon` | `#755930` (+`#dfbd8d` ok) | `fight_text` = `ffffff, 8f8f8f, 565656, 000000` |
| `combat_dagger_icon` | `#755930` | `fight5` = `ffffff, dfbd8d, b09266, 7136c1` |
| `combat_ring_icon` | `#755930, #7ae3f3` (+`#dfbd8d` ok) | `fightslime` = `ffffff, 89cc5e, 5c903a, 000000` |
| `combat_fire_status` | `#b09266, #dfbd8d` | `fightgoblin` = `ffffff, c34e1b, 844f4f, 4a2727` |
| `combat_ice_status` | `#7ae3f3` | `fight1` (no ice-blue ramp exists) |
| `combat_ap_icon` | `#755930` (+`#b09266` ok) | `fight_text` |
| `combat_deck_icon` | `#755930` | `fight1` |
| `combat_arrow_pointing_up` | `#755930` | `fight1` |
| `combat_0/1/2/3_arrows_left`, `combat_nine_icon` | `#755930` | `fight1` |

## Castle sheet note (wired, plus one open question)

`assets/castle-tile.png` is 9x3 (27 cells) but the VRAM block only
covered 16. The 10 pure-ground spares are byte-identical repeats of
`castle_plain_floor`, now wired as `castle_plain_floor_2` (indices
16-25); the CSV-named exit art at (8,2) is wired as `castle_exit`
(index 26, fits `castle4` exactly). `TILE_CASTLE_16/17` fill the
reserved 72-82 enum gap. No map uses them; no rendered pixel changed.

Open question (pre-existing, not caused by this work): the ROM block
compiles in sheet-scan order while `vram_block` is 8-wide, so
`TILE_CASTLE_08+` may address shifted art/palettes in-game. Reconcile
the order (or re-lay the sheet 8-wide) before placing the new tiles
in any map. The repeat cells make this visible: slots 16+ now load
real ground instead of stale VRAM data.
