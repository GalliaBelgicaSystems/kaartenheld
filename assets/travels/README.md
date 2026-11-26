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
  "vista": [[ /* 3 rows x 8 cols of tile names (destination side) */ ]],
  "transition": ["<wash tile>", "<wash tile>", "<dirt tile>"]
}
```

- `tileset` must be a key of `TILESET_KIND_MAP`
  (`tools/level_compiler/compile.py`): `forest`, `village`,
  `desolate_landscape`, ... Every tile name in the file must start
  with `"<tileset>."` and resolve in that tileset's manifest
  (`tools/level_editor/tilesets/<tileset>.json`).
- `grove` is the origin-side arrangement (cols 0-7 of the strip):
  edge, 3x feature pair, wash col. `vista` is the destination-side
  arrangement (cols 10-17): skyline, accent, far peak, ground.
- `transition` is one tile per row, stamped in both cols 8-9 (the
  strip's middle seam). Use the biome's plain wash/wash/dirt tiles.

## How a layout is used

Each journey route declares `origin_biome` / `destination_biome`.
`tools/screen_compiler/travel_art_compile.py` fetches the origin grove
plus dest vista per route, resolves every name through the tileset
manifest to a `TILE_*` symbol (validated against `src/world/world.h`,
uint8_t range), and emits `src/game/travel_art_content.c` with one
composed 3-row x 18-col strip per route (grove 8 + transition 2 +
vista 8), parallel to `g_travel_routes`. The horse, progress bar and
flag are journey UI drawn on top.
