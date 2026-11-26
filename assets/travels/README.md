# Travel strip sheets (`assets/travels/*.png`)

One 8×3-tile sheet per journey biome. The current PNGs are
**programmer-art placeholders** — flat fills in distinct hues so the
import pipeline and the wiring can be verified before real art exists.
Overwrite them in place with real art; file names and layout stay fixed.

## Sheet layout (24 tiles, 8 wide x 3 tall, 8px tiles, row-major)

- Cells 0-9: the **grove kit** (origin side arrangement), in this order:
  `grove edge left`, `grove feature left`, `grove feature right`,
  `grove edge right`, `grove wash`, `grove stem`, `grove wash edge`,
  `grove ground`, `grove ground speckles`, `grove ground edge`.
  (Full order lives in `assets/travel-screen.tiles.json`, `scenery.grove_slots`.)
- Cells 10-23: the **vista kit** (destination side arrangement), in this order:
  `vista skyline left`, `vista skyline`, `vista skyline right`,
  `vista sky`, `vista accent top`, `vista far`, `vista edge`,
  `vista base left`, `vista base`, `vista base right`,
  `vista accent bottom`, `vista far base`, `vista ground`,
  `vista ground speckles`.
  (Full order lives in `assets/travel-screen.tiles.json`, `scenery.vista_slots`.)

Draw each cell as a self-contained 8x8 tile; the engine stamps cells
verbatim, no blending. Keep the palette Game Boy-native: at most
4 shades per tile (the import pipeline fails loudly otherwise).

## How a sheet is used

Each journey route declares `origin_biome` / `destination_biome`.
`tools/screen_compiler/travel_art_compile.py` fetches this sheet plus the
destination sheet, converts both to planar tile data, and emits
`src/game/travel_art_content.c` with a per-route strip table. The ROM
stamps the origin half (cols 1-10) from the origin sheet and the
destination half (cols 11-18) from the destination sheet; the horse,
progress bar and flag are journey UI drawn on top.
