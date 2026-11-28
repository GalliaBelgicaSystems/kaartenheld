# Travel strip layouts (`assets/travels/<biome>.json`)

One JSON layout per journey biome. Each layout names **existing
tileset tiles** (`"<tileset>.<tile_id>"`, same refs as levels use) —
no new pixel art is compiled here; the tiles already live in the ROM's
tilesets and the generated strip is just TileType ids the paint code
stamps verbatim.

Current biomes: `forest`, `village`, `desolate` (plus `snow`, which has
no routes yet and needs no layout file until one does).  Add a new
`<biome>.json` here to support a new journey biome; the compiler
(`tools/screen_compiler/travel_art_compile.py`) picks it up automatically
when a route names it, and fails loudly otherwise.

## Layout format

```json
{
  "biome": "forest",
  "tileset": "forest",
  "grove": [[ /* 3 rows x 8 cols of tile names (origin side) */ ]],
  "vista": [[ /* 3 rows x 8 cols of tile names (destination side) */ ]]
}
```

- `tileset` must be a key of `TILESET_KIND_MAP`
  (`tools/level_compiler/compile.py`): `forest`, `village`,
  `desolate_landscape`, ... Every tile name in the file must start
  with `"<tileset>."` and resolve in that tileset's manifest
  (`tools/level_editor/tilesets/<tileset>.json`).
- `grove` is the origin-side arrangement (cols 0-7 of the strip).
  `vista` is the destination-side arrangement (cols 8-15). The strip
  is 16 wide to match the journey screen's scenery area (cols 2-17);
  there is no middle seam.
- Ground rows (row 2 of grove/vista) name travel-sheet floor icons:
  `"travel.forest_floor_icon_1"` etc. (only the 3 floor icons, cycled
  across the 8 cols). Trees always use `"<tileset>.<tile_id>"` from
  the layout's own tileset.
- CRAM budget: a strip may use at most 3 distinct world ramps
  (STRIP_RAMP_BUDGET in the compiler). The journey screen keeps 5
  CRAM slots live (text/box/floors/markers/heart); only slots 4/6/7
  are free, so a strip needing more keeps the wash fallback with a
  compile WARNING (not an error -- the road still works). Trim the
  layout's tiles to light the strip up.

## How a layout is used

Each journey route declares `origin_biome` / `destination_biome`.
`tools/screen_compiler/travel_art_compile.py` fetches the origin grove
plus dest vista per route and emits `src/game/travel_art_content.c`
with one composed strip per route, parallel to `g_travel_routes`.
Trees resolve through the tileset manifest to a `TILE_*` symbol
(validated against `src/world/world.h`, uint8_t range); they live in
`cells` (rows 0-1, 16 cols: grove 8 + vista 8). Ground rows (row 2)
name travel-sheet floor icons (`"travel.<tile_id>"`, e.g.
`travel.forest_floor_icon_1` — names match
`assets/travel-tileset.csv` case/space/underscore-insensitively) and
land in `floor_o` / `floor_d` as chrome indices (`TCHROME_*` order),
staged from the chrome window by the paint, not the world tileset.
Ground rows must name exactly the 3 distinct floor icons; anything
else in a ground row fails the compile loudly. The horse, progress
bar and flag are journey UI drawn on top.
