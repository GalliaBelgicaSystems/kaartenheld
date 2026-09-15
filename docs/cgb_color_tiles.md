# Full-Color Background Tiles on Game Boy Color: Analysis & Implementation Blueprint

This document analyzes how background color currently flows through the engine, why multi-hue elements (such as a green treetop with a brown tree trunk) exhibit color limitations or visual artifacts, what components are hardcoded versus missing, and what architecture is required to achieve full-color background rendering on Game Boy Color (CGB).

---

## 1. Executive Summary & Direct Answers

| Question | Status | Explanation |
| :--- | :--- | :--- |
| **Is stuff missing to achieve full-color tiles?** | **YES** | Missing: automated CGB palette extraction from PNGs, per-tile attribute mapping in manifests/level compiler, per-scene dynamic CRAM loading, and level editor palette support. |
| **Do we need a palette?** | **YES** | CGB hardware requires up to 8 background palettes in CRAM, each containing 4 colors (15-bit RGB555). We specifically need **per-scene palette sets** rather than the single global set currently used. |
| **Is it hardcoded?** | **YES** | All 8 CGB BG palettes are hardcoded into a single compile-time array (`cgb_bg_palettes[8][4]` in `src/ui/ui_color_banked.c`). Furthermore, palette assignment per tile is hardcoded via an ASCII character glyph heuristic in `ui_cell_palette()`. |
| **What do?** | **ROADMAP** | 1. **[DONE]** Add `--anchor-color` to `png2gb.py` to pin the scene backdrop to index 0.<br>2. **[DONE]** Build palette compiler/exporter in `generate_tiles.py` to emit `tile_palette.h`.<br>3. **[DONE]** Replace ASCII glyph heuristic with direct ROM `g_tile_pal_*` lookup.<br>4. **[DONE]** Address double-booking collision via screen-transition CRAM reloading (`ui_set_cram_palette`).<br>5. **[DONE]** Introduce per-scene CRAM palette sets for distinct scene aesthetics (Castle, Desolate).<br>6. **[DONE]** Establish single-source JSON manifest for web editor canvas color preview (`tools/palette_compiler.py`). |

---

## 2. End-to-End Trace: How Colors Flow Today

> For a simplified walk-through of this pipeline, see [Appendix A](#appendix-a-eli5--the-coloring-book-analogy).

To understand what is missing, we must trace how an authored graphic reaches the Game Boy screen in the current codebase:

```
[assets/forest-tile.png]
         │
         │  (1) tools/png2gb.py --palette auto
         ▼
[src/gfx/rpg_forest_world_tiles.inc]  (2bpp raw bitplanes: shades 0..3 only, NO RGB colors)
         │
         │  (2) Makefile compile -> Bank 5 ROM
         ▼
[VRAM Bank 0 @ 0x8800..0x8FFF]  (Character tile pattern data loaded by ui_init)
         │
         │  (3) levels/forest.json -> compile.py -> scenes_content.c
         ▼
[world->map[row][col]]  (Runtime 1-byte TileType array in WRAM)
         │
         │  (4) ui_draw_world_cell()
         ├────────────────────────────────────────┬───────────────────────────────────────┐
         ▼                                        ▼                                       ▼
[tile_landscape_glyph(t)]               [ui_cell_palette(glyph)]                [cgb_bg_palettes[8][4]]
Maps tile ID -> ASCII char              Maps ASCII char -> 0..7                 Hardcoded global CRAM colors
(e.g. TILE_FOREST_12 -> 'T')            (e.g. 'T' -> 3, 't' -> 5)               (programmed once in ui_init)
         │                                        │                                       │
         ▼                                        ▼                                       ▼
[VRAM Bank 0 @ 0x9800]                  [VRAM Bank 1 @ 0x9800]                  [CRAM Palette RAM]
Tile index in BG tilemap                Attribute byte (palette 0..7)           BCPS/BCPD register data
```

### Step 1: Source Art (`assets/forest-tile.png`)
`assets/forest-tile.png` is an indexed 128×24 pixel PNG (16 columns × 3 rows of 8×8 tiles) containing 16 global colors.
A 2×2 tree consists of:
- **Top-left treetop** `TILE_FOREST_12` (col 12, row 0): 3 shades of green (`#1d3e0f`, `#2a4f1a`, `#7bb660`).
- **Top-right treetop** `TILE_FOREST_13` (col 13, row 0): 3 greens + 1 brown (`#4a3b1c`).
- **Bottom-left trunk** `TILE_FOREST_28` (col 12, row 1): 2 greens (grass floor + leaf overhang) + 1 brown (trunk bark).
- **Bottom-right trunk** `TILE_FOREST_29` (col 13, row 1): 2 greens + 1 brown.

### Step 2: Conversion via `tools/png2gb.py`
The build rule in `Makefile` runs:
```bash
python3 tools/png2gb.py assets/forest-tile.png --name rpg_forest_world_tiles \
    --palette auto --raw -o src/gfx/rpg_forest_world_tiles.inc
```
Under `--palette auto`, `png2gb.py` does the following for each 8×8 tile:
1. Gathers up to 4 unique RGB colors in that tile.
2. Sorts the colors by luminance: `colors = sorted(..., key=lum, reverse=True)`.
3. Assigns shade indices — but **not** always `0, 1, 2, 3`. The mapping depends on how many unique colors the tile has:
   - **4 colors**: `{brightest: 0, light: 1, dark: 2, darkest: 3}` — full ramp.
   - **3 colors**: `{brightest: 0, mid: 2, darkest: 3}` — index 1 is skipped to maximize contrast.
   - **2 colors**: `{brightest: 0, darkest: 3}` — indices 1 and 2 are skipped.
   - **1 color**: `{only: 0}`.
4. Packs them into Game Boy 2bpp bitplanes (16 bytes per tile).

> [!WARNING]
> **Loss of Color Information**: `png2gb.py` completely discards the actual RGB hues. It does not output any palette definitions or indicate which CGB palette a tile belongs to. The beige-box seam bug described in [§3.1](#31-the-background-color-0-mismatch-the-beige-box-bug) is a direct consequence.

### Step 3: Level Editing and Compilation
- The web level editor places `forest_top_left_treetop` at `(3, 3)` and `forest_bottom_left_treetrunk` at `(3, 4)`.
- `tools/level_compiler/compile.py` compiles `levels/forest.json` into `SceneTerrainBlock` entries in `src/game/scenes_content.c`:
  - `TILE_FOREST_12` at `(3, 3)`
  - `TILE_FOREST_28` at `(3, 4)`
- `tools/level_compiler/generate_tiles.py` generates `generated/tiles/tile_glyph.h`, assigning each tile an ASCII glyph via this priority:
  1. Explicit `"glyph"` field in the tileset JSON (e.g. `"glyph": "T"` for treetops).
  2. Exit tile → `'>'`.
  3. `"category": "object"` → `'*'`.
  4. `"walkable": true` → `'.'`.
  5. Default → `'#'`.

  For forest tiles: `TILE_FOREST_12`/`13` → `'T'` (tree canopy), `TILE_FOREST_28`/`29` → `'t'` (tree trunk).

### Step 4: Hardcoded Global Palettes in Engine Memory
In `src/ui/ui_color_banked.c`, exactly 8 background palettes are defined for the entire game:
```c
const palette_color_t cgb_bg_palettes[8][4] = {
    /* 0 gray */     { RGB8(255,255,255), RGB8(170,170,170), RGB8(85,85,85),  RGB8(0,0,0)      },
    /* 1 fire */     { RGB8(255,255,224), RGB8(255,140,40),  RGB8(220,50,20), RGB8(100,10,0)   },
    /* 2 iron/ice */ { RGB8(235,242,250), RGB8(140,180,214), RGB8(70,105,138), RGB8(27,43,58) },
    /* 3 field */    { RGB8(120,176,96),  RGB8(40,72,24),    RGB8(24,56,8),   RGB8(0,0,0)     },
    /* 4 poison */   { RGB8(240,255,240), RGB8(100,220,100), RGB8(30,140,50), RGB8(10,50,20) },
    /* 5 wood */     { RGB8(245,230,210), RGB8(196,138,72),  RGB8(138,82,34), RGB8(61,32,10)  },
    /* 6 gold */     { RGB8(255,252,224), RGB8(255,215,0),   RGB8(200,140,8), RGB8(90,58,0)   },
    /* 7 dim */      { RGB8(200,200,200), RGB8(150,150,150), RGB8(90,90,90),   RGB8(40,40,40)   }
};
```
In `ui_init()` (`src/ui/ui.c`), these 8 palettes are loaded unconditionally into CRAM by copying from Bank 3 via `banked_copy()`, then streaming each palette's 8 bytes through raw `BCPS_REG` / `BCPD_REG` writes (with auto-increment bit 7 set). They are never reloaded or updated when switching scenes.

### Step 5: Screen Rendering & Palette Selection
When drawing the world map, `ui_draw_world_cell()` writes the tile index to VRAM Bank 0 and the CGB attribute byte to VRAM Bank 1:
```c
/* 1. Write 2bpp tile index to VRAM Bank 0 */
tilemap[(row & 31) * 32 + (col & 31)] = tile_idx;

/* 2. Write CGB attribute byte to VRAM Bank 1 */
VBK_REG = 1;
((volatile uint8_t *)0x9800)[(row & 31) * 32 + (col & 31)] =
    ui_cell_palette((uint8_t)glyph, (uint8_t)world->tileset_kind);
VBK_REG = 0;
```
The palette selection is determined solely by `ui_cell_palette()`:
```c
static uint8_t ui_cell_palette(uint8_t glyph, uint8_t kind)
{
    if (glyph == 'T') return UI_COLOR_FIELD;                /* Palette 3 (greens) */
    if (glyph == 't' || glyph == 's') return UI_COLOR_WOOD; /* Palette 5 (browns) */
    if (glyph == 'R') return UI_COLOR_DIM;                 /* Palette 7 (grays)  */
    if (glyph == '.' || glyph == ',') {
        if (kind == WORLD_TILESET_FOREST) return UI_COLOR_FIELD;
        if (kind == WORLD_TILESET_DESOLATE) return UI_COLOR_DIM;
    }
    return UI_COLOR_NONE;                                   /* Palette 0 (grayscale) */
}
```

> [!NOTE]
> **Second VRAM Bank 1 writer**: `ui_color_span_banked()` in `src/ui/ui_color_banked.c` (Bank 3) also writes palette attributes to VRAM Bank 1. It operates on horizontal spans (used for UI chrome) via `color_vram_sync_write()`, which waits for Mode 0/1 PPU timing.

---

## 3. Why the Tree Currently Fails (The Technical Breakdown)

Inspecting the actual pixels rendered in `screenshots/14-forest-arrived.png` confirms these exact hardware limitations:

### 3.1 The Background Color 0 Mismatch ("The Beige Box" Bug) [RESOLVED]
On Game Boy Color, **Color 0 in a background palette is opaque**. It is not transparent like sprite Color 0.
- Grass floor tiles use `UI_COLOR_FIELD` (Palette 3). Color 0 is **Forest Grass Green** (`RGB8(120, 176, 96)`).
- Tree trunk tiles use `UI_COLOR_WOOD` (Palette 5). Color 0 is **Light Beige/Tan** (`RGB8(245, 230, 210)`).
- When `png2gb.py` encodes the tree trunk tile, the grass floor surrounding the trunk in the bottom corners has the highest luminance, so it gets mapped to **shade 0**.
- Because the trunk tile uses Palette 5, shade 0 renders as **Beige** — not grass green.
- **Result**: The grass immediately surrounding the trunk appears as a jarring beige rectangular box instead of blending into the surrounding green lawn.

> [!IMPORTANT]
> **The fix**: Color 0 of every outdoor palette in a scene must be the **same RGB value** (the scene's ground color). If Palette 3 (Field) has Color 0 = Grass Green and Palette 5 (Wood) also has Color 0 = Grass Green, the shade-0 grass around the trunk renders identically to the shade-0 grass on the floor tile. No seam.

### 3.2 The Monochromatic Palette Trap [DONE]
- `UI_COLOR_FIELD` contains **only green shades**: `[RGB8(120,176,96), RGB8(40,72,24), RGB8(24,56,8), RGB8(0,0,0)]`.
- `UI_COLOR_WOOD` contains **only brown shades**: `[RGB8(245,230,210), RGB8(196,138,72), RGB8(138,82,34), RGB8(61,32,10)]`.
- In `assets/forest-tile.png`, the top-right treetop has a brown branch `#4a3b1c`. Because it is assigned Palette 3, that brown branch is rendered as dark green.
- The bottom-left trunk has green leaves hanging down over the bark. Because it is assigned Palette 5, those green leaves are rendered as brown wood.

### 3.3 ASCII Glyph Heuristic vs. Data-Driven Attributes [RESOLVED]
Because `ui_cell_palette` relies on ASCII characters:
- Only `'T'`, `'t'`, `'s'`, `'R'`, `'.'`, and `','` receive color.
- All castle walls, roofs, doors, signs, water, counters, and chests (`'O'`) fall through to `UI_COLOR_NONE` (Palette 0 = monochrome black/white).
- If a new tile is added to the level editor, it cannot have color unless a unique ASCII glyph is invented, added to `docs/glyphs.md`, registered in `generate_tiles.py`, and handled with a branch in `ui_cell_palette()`.

---

## 4. Structural Gaps in the Current Codebase

### Gap 1: Exactly One Global Palette Set, Not One Per Tileset [DONE]
Originally, `src/ui/ui_color_banked.c` defined a single `cgb_bg_palettes[8][4]` array programmed once at boot.
- **Resolution [DONE]**: We now have dedicated CRAM palette sets in Bank 5 for each world tileset:
  - `cgb_bg_palettes_forest` (Forest, Field, Town) with grass green backdrop (`#7bb660`)
  - `cgb_bg_palettes_desolate` (Mountain Pass, South Field) with slate rock backdrop (`#938da1`)
  - `cgb_bg_palettes_castle` (Castle Bastion) with authored ground backdrop (`#a99fc0`, assets/palette.txt CASTLE ground)
  - `cgb_bg_palettes` (Battle/Status/Card UI) with crisp white backdrop
  These are dynamically reloaded via `ui_set_cram_palette(world->tileset_kind)` during LCD-safe map loads and screen transitions.

### Gap 2: Tileset JSON `color` Field is Cosmetic Only [DONE]
Previously, the `"color"` field in `tilesets/*.json` was only used for web preview swatches.
- **Resolution [DONE]**: `tools/level_compiler/generate_tiles.py` now analyzes each tile's declared `"color"` hex code, maps it to the closest CGB palette index, and outputs `const uint8_t g_tile_pal_<tileset>[]` lookup arrays into `generated/tiles/tile_palette.h`, which are compiled into Bank 5 ROM.

### Gap 3: The 8 Palette Slots are Already Double-Booked [DONE]
The 8 CGB background palette slots are shared between terrain and battle/card status colors (`ui_color_card()` in `src/ui/ui.h`).

- **Resolution [DONE] (Mitigation 1 Implemented)**:
  - When entering battle (`ui_draw_battle_full`), CRAM is reloaded with canonical UI/Battle palettes (`ui_set_cram_palette(0)`).
  - When returning to the overworld (`ui_draw_world_map`), CRAM is reloaded with overworld terrain palettes (`ui_set_cram_palette(1)`).
  - Both reloaders live in Bank 5 (`ui_load_cram_banked()`), preserving the fixed bank budget.

---

## 5. Game Boy Color Hardware Constraints

To design the solution properly, we must adhere to the physical capabilities of the Game Boy Color PPU:

1. **CRAM (Palette RAM)**:
   - Holds 8 Background Palettes (`BGP0`–`BGP7`) and 8 Object Palettes (`OBP0`–`OBP7`).
   - Each palette has exactly **4 colors**.
   - Each color is a 15-bit RGB555 value (`0b0BBBBBGGGGGRRRRR`, 2 bytes per color, 8 bytes per palette).
   - Total background colors active simultaneously across the entire screen: **32 colors**.
2. **Tile Attributes (VRAM Bank 1)**:
   - The tilemap at `0x9800`–`0x9BFF` (32×32 tiles) has two physical memory banks selected via `VBK_REG`:
     - `VBK_REG = 0`: Character tile indices (0–255).
     - `VBK_REG = 1`: Attribute bytes for each tile.
   - Bit layout of each attribute byte:
     - **Bits 0–2**: Background Palette number (0–7).
     - **Bit 3**: Tile VRAM Bank (Bank 0 or Bank 1).
     - **Bit 4**: Unused.
     - **Bit 5**: Horizontal flip (X-flip).
     - **Bit 6**: Vertical flip (Y-flip).
     - **Bit 7**: BG-to-OAM priority.
3. **No Background Transparency**:
   - Background Color 0 is drawn whenever a pixel's 2bpp index is 0.
   - To have a non-rectangular object (like a tree trunk or rock) sit on grass without a visible rectangular seam, **Color 0 of the object's palette MUST be the exact same RGB color as the grass**!
4. **CRAM Write Timing Constraints**:
   - In CGB Mode 3 (pixel transfer), Palette RAM is completely inaccessible to the CPU. Reads return `0xFF` and writes are ignored or cause bus noise.
   - All CRAM updates must occur when the LCD is off (`display_off()` / `LCDC_REG &= ~0x80`) or strictly within VBlank (`vsync()`).
   - The existing codebase uses **raw `BCPS_REG` / `BCPD_REG` writes** (not GBDK's `set_bkg_palette()`). Any new CRAM write path must be consistent with the existing pattern or explicitly migrate to the GBDK helper.

---

## 6. Architectural Blueprint: What Needs to Be Done ("What Do?")

```
┌─────────────────────────────────────────────────────────────────────────────┐
│ 1. ASSET & PALETTE PIPELINE (tools/png2gb.py & palette compiler)            │
│    - Pinned Anchor Color Mode: pin scene floor (grass) strictly to index 0  │
│    - Cluster remaining colors into up to 8 CGB palettes (<= 4 colors each)  │
│    - Enforce Harmonized Color 0 across outdoor nature palettes              │
│    - Generates:                                                             │
│        * 2bpp tile data (.inc)                                              │
│        * CGB palette definitions (8 x 4 RGB555 entries)                     │
│        * Shared JSON manifest for level editor & ROM compiler               │
└──────────────────────────────────────┬──────────────────────────────────────┘
                                       │
                                       ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ 2. HARMONIZED COLOR 0 CONVENTION ("Backdrop Harmony")                       │
│    In a given scene, all outdoor palettes share Color 0:                    │
│      Palette 0 (Walls)   : [Grass Green, Wall Light, Wall Mid, Wall Dark]   │
│      Palette 1 (Tree Top): [Grass Green, Leaf Light, Leaf Mid, Leaf Dark]   │
│      Palette 2 (Trunk)   : [Grass Green, Bark Light, Bark Mid, Bark Dark]   │
│      Palette 3 (Mixed)   : [Grass Green, Leaf Green, Bark Brown, Dark Outline]
│    => Zero rectangular seam boxes around objects!                           │
└──────────────────────────────────────┬──────────────────────────────────────┘
                                       │
                                       ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ 3. DATA-DRIVEN ROM LOOKUP (Zero __mulint Overhead)                          │
│    Flat 1D pointer selected via switch(kind), avoiding non-power-of-2 mul:  │
│        const uint8_t *pal_tbl = ui_get_tileset_palette_table(kind);        │
│        VBK_REG = 1;                                                         │
│        tilemap[offset] = pal_tbl[tile_index];                               │
│        VBK_REG = 0;                                                         │
│    => O(1) direct pointer index. Zero glyph guessing, zero __mulint calls. │
└──────────────────────────────────────┬──────────────────────────────────────┘
                                       │
                                       ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ 4. SAFE PER-SCENE CRAM STREAMING & SCREEN-TRANSITION RELOADING              │
│    - Overworld transitions stream active scene palettes during LCD-off      │
│    - Screen transitions (Overworld <-> Battle) reload battle/card palettes   │
│    - Gated via BCPS/BCPD write loop consistent with existing ui_init()      │
│    - DMG fallback remains 100% byte-identical (DMG ignores VBK & BCPS)      │
└─────────────────────────────────────────────────────────────────────────────┘
```

### Pillar 1: Harmonized Color 0 & `png2gb.py` Anchor Color Mode [DONE]

> [!IMPORTANT]
> **Resolving the Auto-Luminance Sort Conflict [DONE]**:
> Currently, `png2gb.py --palette auto` independently sorts every tile's colors by luminance. If a tile contains white highlights (e.g. flowers, shiny rocks, sparkles) that have higher luminance than grass `#7bb660`, luminance sorting puts white into index 0 and demotes grass to index 1 or 2. This silently breaks the rule that index 0 is always the shared backdrop.
>
> **The Mitigation [DONE]**:
> Add a pinned anchor-color argument to `tools/png2gb.py`:
> ```bash
> python3 tools/png2gb.py assets/forest-tile.png --anchor-color "#7bb660" ...
> ```
> When `--anchor-color` is set:
> 1. Color matching the anchor is **strictly pinned to index 0**.
> 2. The remaining ≤ 3 distinct colors in the 8×8 tile are sorted by luminance and assigned to indices following the existing shade-skipping logic (e.g. 2 remaining colors → `{2, 3}`, not `{1, 2}`).
> 3. If a tile does not contain the anchor color (e.g. solid wall), index 0 is still filled with the scene's harmonized backdrop in CRAM, ensuring zero border clash.

### Pillar 2: Multi-Hue Palettes for Complex 8×8 Tiles [DONE]
A CGB palette does not have to be a single monochromatic ramp.
If an 8×8 tile needs both foliage and bark (or if a small tree is drawn in a single tile):
```c
/* 4 colors: Floor + Foliage + Wood + Outline */
{ RGB8(120, 176, 96), RGB8(40, 110, 30), RGB8(138, 82, 34), RGB8(20, 30, 10) }
```
This allows a single tile to contain **green treetop and brown trunk** simultaneously.

### Pillar 3: Data-Driven ROM Palette Lookup (Avoiding SDCC `__mulint`) [DONE]

> [!CAUTION]
> **The Banked-Code Multiplication Trap**:
> In SDCC / Game Boy development, indexing a 2D array like `g_scene_tile_palettes[tileset_kind][tile_index]` where the second dimension is 48 generates a library call to `__mulint` because 48 is not a power of 2 ($48 = 32 + 16$).
> In banked code and per-cell rendering loops, `__mulint` causes fixed-bank code bloat, cycle waste, and potential link errors.

**The Solution [DONE]**: Use a flat 1D pointer selected via `switch`:
```c
/* In banked content (e.g. Bank 5) */
const uint8_t g_forest_tile_palettes[48] = {
    0, 0, 0, 0, 0, 0, 0, 0,  /* 00..07 Walls */
    0, 0, 0, 0,              /* 08..11 Walls */
    1, 1,                    /* 12..13 Treetops (PAL_CANOPY green) */
    2, 2,                    /* 14..15 Stumps   (PAL_TRUNK brown)  */
    /* ... */
    2, 2,                    /* 28..29 Trunks   (PAL_TRUNK brown)  */
    2, 2,                    /* 30..31 Stumps   (PAL_TRUNK brown)  */
    1                        /* 32     Floor                       */
};

const uint8_t *ui_get_tileset_palette_table(uint8_t tileset_kind)
{
    switch (tileset_kind) {
        case WORLD_TILESET_FOREST:   return g_forest_tile_palettes;
        case WORLD_TILESET_CASTLE:   return g_castle_tile_palettes;
        case WORLD_TILESET_DESOLATE: return g_desolate_tile_palettes;
        default:                     return NULL;
    }
}
```
In `ui_draw_world_cell()`:
```c
const uint8_t *pal_tbl = ui_get_tileset_palette_table(world->tileset_kind);
uint8_t pal = pal_tbl ? pal_tbl[tile_idx - RPG_TILE_BASE_WORLD] : 0;

VBK_REG = 1;
((volatile uint8_t *)0x9800)[(row & 31) * 32 + (col & 31)] = pal;
VBK_REG = 0;
```
**Benefits**:
- Zero multiplication: SDCC compiles this into direct pointer indexing (`HL + A`).
- Instant $O(1)$ lookup with no ASCII glyph guessing.
- Complete separation of terrain presentation from collision logic.

### Pillar 4: Safe CRAM Write Timing & Screen Transition Reloading [DONE]
To guarantee reliable execution across real hardware and emulators without Mode 3 corruption or slot collisions:
1. **Screen Transitions (Overworld <-> Battle) [DONE]**:
   - When entering battle, reload CRAM with the canonical UI / Status / Card palettes (`cgb_bg_palettes`).
   - When returning to the overworld, reload CRAM with the active scene's terrain palettes (`ui_set_cram_palette((uint8_t)world->tileset_kind)`).
2. **Scene Transitions (LCD-off / Full Screen Wipe) [DONE]**:
   - When changing maps in `world_change_map()` / `ui_draw_world_map()`, palette loading is gated through the LCD-off full-draw window (`ui_set_cram_palette((uint8_t)world->tileset_kind)`), loading the tileset's harmonized CRAM set without Mode 3 bus conflicts.
   - Uses direct `BCPS_REG` / `BCPD_REG` streaming in Bank 5 (`ui_load_cram_banked()`).
3. **Harness Safety [DONE]**:
   - In harness mode (`g_harness_mode`), VBlank waits are bypassed; loading with LCD off ensures instantaneous, deterministic execution in tests.

---

## 7. Single Source of Truth: Web Level Editor Parity [DONE]

To prevent drift between the Python/C build pipeline and the TypeScript/React web editor (`tools/level_editor/src/`):

```
                       [assets/forest-tile.png]
                                  │
                                  ▼
                     [tools/palette_compiler.py]
                                  │
                   ┌──────────────┴──────────────┐
                   ▼                             ▼
        [generated/tiles/forest.json]  [src/gfx/forest_palettes.inc]
        (Consumed by Web Editor)       (Compiled into ROM Bank 5)
```

### 7.1 Manifest as Contract [DONE]
- `palette_compiler.py` outputs a JSON manifest containing:
  - The 8 CGB palettes in hex (`["#7bb660", "#2a4f1a", "#1d3e0f", "#000000"]`).
  - The per-tile palette index array (`"tile_palettes": [0, 0, ..., 1, 1, 2, 2]`).

### 7.2 Editor Canvas Rendering [TODO]
- The web level editor's `MapCanvas.tsx` loads the JSON manifest.
- When rendering the canvas preview, instead of displaying the raw unquantized 24-bit PNG, it shades the tile using the manifest's palette colors.
- Guaranteed 100% WYSIWYG parity between browser canvas and Game Boy display.

---

## 8. Verification Plan

### 8.1 Deterministic Attribute Mirroring in Test Harness (`g_tilemap_attr_mirror`) [DONE]
- In debug builds, pair `g_tilemap_mirror` with an attribute mirror:
  ```c
  #ifdef DEBUG_BUILD
  uint8_t g_tilemap_attr_mirror[32 * 32];
  #endif
  ```
- The SameBoy test harness can assert that cell `(3, 3)` (treetop) has attribute `1` (`PAL_CANOPY`) and cell `(3, 4)` (trunk) has attribute `2` (`PAL_TRUNK`).
- Semantic assertions are authoritative over raw pixels (AGENTS.md §7).

### 8.2 Visual Smoke Tests & Screenshot Hash Checks [DONE]
- RGB probing at known coordinates (`G > R && G > B` for treetop; `R > B && G < R` for bark) serves as an initial sanity check.
- Full regression protection uses deterministic screenshot perceptual hashing against reference frames in `screenshots/`.

### 8.3 Hardware Fidelity Check (`make verify-oam`) [DONE]
- Run `make verify-oam` and mGBA single-stepping to verify that CRAM updates do not corrupt OAM sprite timing or trip VBlank deadlines.

### 8.4 DMG Backward Compatibility [DONE]
- Running the ROM in DMG mode (`BGP_REG = 0xE4`) must remain byte-identical in gameplay and visual grayscale output.

---

## Appendix A: ELI5 — The Coloring Book Analogy

> For full technical detail, see [§2](#2-end-to-end-trace-how-colors-flow-today) and [§3](#3-why-the-tree-currently-fails-the-technical-breakdown).

### The Hardware Analogy

Think of the Game Boy Color screen as an interactive coloring book. Creating the final picture requires four distinct parts working together:

```
1. Rubber Stamps        2. Stamp Grid           3. Crayon Boxes         4. Coloring Instructions
   (VRAM Bank 0)           (Tilemap Ring)          (CRAM Palettes)         (VRAM Bank 1 Attributes)
 ┌───────────────┐       ┌───────────────┐       ┌───────────────┐       ┌────────────────────────┐
 │ 8x8 Tile Art  │       │ Which stamp   │       │ 8 boxes of    │       │ Which crayon box to    │
 │ (shades 0..3, │ ────► │ goes in each  │ ────► │ 4 crayons     │ ────► │ color each stamp with  │
 │  no color)    │       │ square        │       │ each          │       │ on the grid            │
 └───────────────┘       └───────────────┘       └───────────────┘       └────────────────────────┘
                                                                                     │
                                                                                     ▼
                                                                           [Final Colored Screen]
```

#### 1. The Rubber Stamps (Tile Patterns in VRAM Bank 0)
The artist creates 8×8 pixel shapes: a treetop curve, a piece of bark, a patch of grass.
On the cartridge, these shapes don't have any real color! They are molded like rubber stamps with **4 shades of grey ink**:
- Shade 0 (lightest / background)
- Shade 1 (light tone)
- Shade 2 (dark tone)
- Shade 3 (black / deepest shadow)

#### 2. The Floorplan (The Tilemap at `0x9800`)
The Game Boy has a 32×32 grid called the Tilemap. It tells the hardware:
- *"Put Rubber Stamp #12 at square (3, 3)."* (The treetop)
- *"Put Rubber Stamp #28 at square (3, 4)."* (The tree trunk)

#### 3. The Crayon Boxes (Palettes in CRAM)
The Game Boy Color has a small shelf called CRAM that can only hold **8 small boxes of crayons**.
Each box contains exactly **4 crayons**:
- **Crayon Box 3 (Field)**: [Grass Green, Forest Green, Deep Forest Green, Black]
- **Crayon Box 5 (Wood)**:  [Light Beige, Light Brown, Medium Brown, Dark Brown]

#### 4. The Coloring Instructions (Attributes in VRAM Bank 1)
Behind the stamp grid lies a second sheet called the Attribute Map. For each square, it specifies:
- *"Color Stamp #12 using Crayon Box 3 (Field)."*
- *"Color Stamp #28 using Crayon Box 5 (Wood)."*

### Why Does the Tree Trunk Get an Ugly Beige Box?

> This is a simplified version of the technical explanation in [§3.1](#31-the-background-color-0-mismatch-the-beige-box-bug).

1. The rubber stamp for the tree trunk contains the brown bark, but its bottom corners also have a bit of **grass** where the trunk meets the lawn.
2. The computer saw that the grass was the brightest part of that stamp, so it marked the grass as **Shade 0**, and the tree bark as **Shade 2 and 3**.
3. Next, the game says: *"This tile is a tree trunk! Color it using Crayon Box 5 (Wood)!"*
4. The Game Boy picks up Crayon Box 5. Crayon #0 in that box is **Light Beige**!
5. So the Game Boy colors the grass around the trunk with **Beige**, while the rest of the lawn was colored with **Grass Green** from Crayon Box 3!
6. **Result**: A visible beige rectangular box surrounds the trunk.

### Why Can't We Just Put 50 Colors on Screen Like a Modern PC?
1. An 8×8 rubber stamp can only touch **ONE Crayon Box** at a time. You cannot use Box 3 for the top half of a tile and Box 5 for the bottom half.
2. If you want an 8×8 tile to have both green leaves and brown bark, you must create a Crayon Box that contains **both green and brown crayons**.
3. You only have 8 boxes for the whole background, so they must be shared smartly.

### The 7 Steps: From Drawing to Screen

1. **Draw the art as a PNG [DONE]**
   Put your artwork in `assets/` (e.g. `assets/forest-tile.png`). It has to be a grid of 8×8 tiles (Game Boy hardware only understands 8×8 chunks), and each individual 8×8 tile can use at most 4 colors — that's a hardware limit, not a style choice.
2. **Convert it with `png2gb.py` [DONE]**
   Run `tools/png2gb.py` on the PNG. It slices it into 8×8 tiles and packs each into Game Boy 2bpp format (2 bits per pixel = 4 shade indices per tile). Pick a palette mode: `canonical` (grayscale), `gb_green` (classic GB tint), or `auto` with `--anchor-color` to pin the scene backdrop to index 0.
3. **It becomes a `.inc`/`.h` file in `src/gfx/` [DONE]**
   The output lands as a generated header, e.g. `src/gfx/rpg_forest_world_tiles.inc` — this is what actually gets compiled into the ROM. `make gfx` regenerates all of these from the `assets/` PNGs automatically, and CI fails if a committed `.inc` drifts from its source PNG.
4. **Register the tile in a tileset manifest [DONE]**
   If it's a world tile (not a sprite), add an entry to the matching JSON in `tools/level_editor/tilesets/` (`forest.json`, `castle.json`, etc.) with its `gb_constant`, `walkable` flag, and `color` hex swatch (which now drives the compiled palette lookup table in `generated/tiles/tile_palette.h`).
5. **Place it in a level and compile [DONE]**
   Use the web level editor (or hand-edit `levels/*.json`) to place the tile on a map, then run `tools/level_compiler/compile.py` — it turns the JSON into `SceneTerrainBlock` C data in `src/game/scenes_content.c`.
6. **Color comes from data-driven palette lookup [DONE]**
   At render time, `ui_draw_world_cell()` maps the tile index to its palette via the direct ROM lookup table `g_tile_pal_*` in `generated/tiles/tile_palette.h` (compiled into Bank 5), avoiding SDCC `__mulint` overhead. Screen transitions (`ui_set_cram_palette`) reload CRAM between battle and overworld modes.
7. **Build and check it on real timing [DONE]**
   Run `make` to build the ROM. Use `tools/verify_oam.py`, the screenshot harness, or the pyboy-based dev tools to actually look at it — CGB palette bugs (like the beige-box seam) usually only show up visually, not in a compile.
