# Palette tutorial (for the artist)

You own every color in the game. All of it lives in one file:
`assets/palette.txt`. Nothing is hardcoded anywhere else — if a color on
screen doesn't come from this file, that's a bug, report it.

## The file has three parts

1. **Color dictionary** (`COMMON`, `GAMEPLAY`, `FOREST`, `DESOLATE`,
   `CASTLE`, `TOWN`, `SPRITES`, `COMBAT`, `TITLE`): named colors,
   `name: #hex`. Some document art pixels (most of `COMBAT`/`TITLE`);
   the ones ramps actually use are listed in `RAMPS/*` below.
2. **Ramp tables** (`RAMPS/BASE`, `RAMPS/FOREST`, `RAMPS/DESOLATE`,
   `RAMPS/CASTLE`, `RAMPS/VILLAGE`, `RAMPS/OBJ`): the colors the Game Boy
   really displays. Each set has 8 ramps of 4 shades (OBJ has 4 ramps) —
   that 8×4 limit is hardware, it cannot change.
3. **Anchors** (`ANCHORS`): which color each art sheet treats as its
   background (details in §5).

Lines starting with `#` are comments.

## Quick start: change a color

1. Open `assets/palette.txt`, find the color, change the hex.
   Example — make house walls warmer:
   `house_wall: #f1cf91` → `house_wall: #f5d49a` (in `TOWN`).
2. Run `make manifest`. This refreshes `assets/palettes.md` (the resolved
   tables) and the level-editor preview.
3. Run `make palette-check`. It fails loudly on any typo, bad reference,
   or stale generated file.
4. Run `make screenshots` and look at the frames (town, house close-ups).
   If a frame changed that you didn't expect, read §6 before touching
   anything else.

To see which dictionary names nothing uses (yet):
`python3 tools/palette_txt.py --unused`.

## Composing ramps

A ramp line looks like this:

```text
field: FOREST/grass, FOREST/tree_leaves_terrain_outline_big_grass, FOREST/dark_tree_leaves, FOREST/void_holes
```

- Four shades, **lightest first, darkest last**. Shade 0 is special:
  it is the ramp's background — ground tiles blend into it, and on
  sprites shade 0 is **transparent**.
- Each shade is a `SECTION/name` reference into the dictionary.
  Cross-set references are fine (`FOREST` gold uses `CASTLE/gold`:
  one edit updates every ramp that references it).
- To add a new color: add `my_pink: #ff9fd0` to a dictionary section,
  then reference it as `SECTION/my_pink` from a ramp.
- **Do not reorder ramps.** The file order is the ramp index (0–7) and
  tiles/art point at ramps by index. Edit contents freely; renaming a
  ramp only relabels the editor view, but moving one repaints tiles.
- Every set needs exactly 8 ramps of exactly 4 shades (4 ramps for
  `OBJ`). The checker rejects anything else with the file and line
  number.

## Hardware in two minutes

- The Game Boy Color stores each channel in **5 bits**, not 8. It keeps
  only the top 5 bits of your hex (`#7bb660` → `#78b060` on hardware).
  Consequence: two hexes that differ only in the low 3 bits look
  **identical** on screen. When you pick 4 ramp shades, make them differ
  in the top 5 bits (rule of thumb: channel steps of at least 8).
- A tile is 8×8 pixels, 4 shades max. If your art uses more than 4
  colors per tile, the build fails — split the art, don't dither
  (dithering pixels map to random shades).

## How art pixels become shades (read this before debugging colors)

The build converts each art tile to shade indices 0–3 in two steps:

1. **Anchor pinning.** The sheet's `ANCHORS` color is forced to shade 0.
   Forest tiles pin grass green, so grass pixels become transparent
   background / seamless ground.
2. **Brightness order.** Remaining colors sort brightest→darkest onto the
   leftover shades.

Then the ramp paints the shades. So a ramp is only half the story —
the *art pixels* decide which shade each pixel gets.

True story: the town braziers once rendered as solid orange squares.
The flame art was brighter than the sheet background, so the flame took
shade 0 (transparent core!) and the background landed on shade 2
(solid orange). The fix was one line — pin the sheet background in
`ANCHORS` (`enemy_ow: DESOLATE/ground`) — not new art. If your sprite
grows a mysterious solid box, check the anchor first.

Related: `npc_tiles` pins a raw `#f1eb03` instead of a color name.
That sheet is composited on chroma-key yellow (see
`tools/compose_npc_tiles.py`), so the anchor must equal those exact
pixels. Don't "fix" it to a pretty color or every NPC gets a yellow box.

## What each ramp paints

Resolved hexes: `assets/palettes.md` (generated, always current).

- **BASE** (battles + cards): `slime` = slime enemies *and* heal cards
  (they share — both stay green); `fire/iron_ice/wood/gold` = card
  colors by weapon; `poison` doubles as the dialogue paper during
  conversations; `dim` = grey-out + bat/boss art; `gray` = UI text.
- **FOREST**: `field` = grass + canopy, `wood` = trunks/stumps/merchant,
  `gold` = accents + mayor, `dim` = rocks.
- **DESOLATE**: `slate_rock` = ground, `deadwood` = trees,
  `campfire` = flames, `gold` = accents + chest.
- **CASTLE**: `stone` = floors/walls, `curtain`, `wood_furn` =
  furniture, `gold` = accents + chest.
- **VILLAGE**: `dirt_floor` = ground, `wood` = houses/barrels/merchant,
  `cream` = walls/roofs/mayor, `fire` = braziers/torches.
- **OBJ** (sprites): `grey` = player/bats, `orange` = braziers/kobolds,
  `brown` = hero/chests, `green` = overworld slimes.

## Guardrails (the checker enforces all of these)

- Exactly 8 ramps × 4 refs per set (4 ramps for OBJ).
- Every `SECTION/name` must exist; every anchor key must be present.
- `assets/palettes.md` must be freshly generated (`make manifest`).
- The same checks run on every push. A red palette check means the
  committed tables disagree with the file — never force it green by
  editing generated files; fix `palette.txt` and regenerate.

## Troubleshooting

- *"I changed a hex and half the game changed color."* That color is
  referenced from several ramps (e.g. `CASTLE/gold` feeds forest,
  desolate, and castle gold ramps). Check the Refs column in
  `assets/palettes.md`, or split it: add a second dictionary color.
- *"Two shades look identical in screenshots."* 5-bit rounding (§3).
  Spread the channels.
- *"My sprite has a solid box around it."* Anchor story (§5): the sheet
  background must be the darkest-or-anchored color in every tile, or be
  pinned in `ANCHORS`.
- *"palette-check fails on a ramp count."* You added/removed a line in a
  `RAMPS/*` section. Restore 8 (4 for OBJ).
