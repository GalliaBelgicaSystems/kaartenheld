# Plan: Add DungeonTileset.png and Houses_and_various_things.png to Level Editor

## Overview
Add two new tilesets to the level editor pipeline:
1. **DungeonTileset.png** - 14×13 = 182 tiles (152 unique), all extracted
2. **Houses_and_various_things.png** - 128×128 = 16,384 tiles, organized into categorized subdivisions

Both use the existing GB green 4-color palette.

---

## Current Architecture Recap

```
asset_atlas.py (TILESETS + FOREST_COORDS) 
    ↓
extract_tiles.py → public/tiles/{tileset}/{tile_id}.png (web editor)
    ↓
tilesets/*.json → compiler validation
    ↓
Tileset.ts (BUILTIN_TILESETS) → web editor UI
    ↓
Makefile gfx target → png2gb.py → .inc files (Game Boy ROM)
    ↓
compile.py → scenes_content.c
```

---

## 1. DungeonTileset.png - Complete Extraction (182 tiles)

### Analysis Results
- 14 columns × 13 rows = 182 tiles
- 152 unique patterns (30 duplicates - mostly solid walls)
- All 4 GB palette colors used

### Tile Categories Observed
| Coordinate Range | Visual Type | Suggested Category |
|------------------|-------------|-------------------|
| Row 0-2, Col 0-9 | Floor variants, cracks, debris | `dungeon_floor_*` |
| Row 0-2, Col 10-13 | Solid walls, corners | `dungeon_wall_*` |
| Row 3 | Empty/transition tiles | `dungeon_empty_*` |
| Row 4-6 | Wall variants, pillars, arches | `dungeon_wall_*`, `dungeon_pillar_*`, `dungeon_arch_*` |
| Row 7-9 | Brick/stone walls, decorated | `dungeon_brick_*`, `dungeon_decor_*` |
| Row 10-12 | Solid walls, filled | `dungeon_solid_*` |

### Implementation
- Add all 182 tiles to `asset_atlas.py` TILESETS with semantic names `DUNGEON_*`
- Create `tools/level_editor/tilesets/dungeon.json` with all tiles
- Map to C constants: reuse `TILE_FLOOR`, `TILE_WALL`, `TILE_BUILDING` where possible; add new constants for unique dungeon tiles (e.g., `TILE_DUNGEON_CRACK`, `TILE_DUNGEON_PILLAR`)

---

## 2. Houses_and_various_things.png - Subdivision System (16,384 → ~200 curated)

### Structure Analysis
- **8×8 macro-blocks** of 16×16 tiles each (128×128 pixels per macro)
- **64 macro-blocks** total, each with 4 sub-blocks of 8×8 tiles = 256 tiles per macro
- Clear visual categories per macro-block region

### Identified Subdivisions (Categories)

| Category | Macro Region | Description | Est. Tiles |
|----------|--------------|-------------|------------|
| **houses_walls** | (0,0), (1,0), (3,0), (0,2), (1,5-6), (0,5-6) | House walls, corners, windows, doors | ~64 |
| **houses_roofs** | (1,0) sub, (2,0), (4,1) | Roof tiles, corners, ridges | ~32 |
| **houses_floors** | (0,1), (1,1), (3,1), (2,2), (3,2), (3,3) | Interior floors, carpets, patterns | ~48 |
| **houses_doors** | Scattered in wall macros | Door variants (closed, open, arched) | ~16 |
| **houses_windows** | Scattered in wall macros | Window variants, shutters | ~16 |
| **nature_ground** | (2,0), (4,0), (6,0), (2,1), (3,3), (4,3) | Grass, dirt, paths, gravel | ~32 |
| **nature_vegetation** | (2,3), (4,4), (6,4) | Trees, bushes, flowers | ~16 |
| **objects_furniture** | (4,2), (5,2), (1,3), (2,3) | Tables, chairs, barrels, crates | ~24 |
| **structures_fences** | (0,5-6), (1,5-6), (7,6) | Fences, gates, posts | ~24 |
| **structures_props** | (5,4), (6,4), (7,4) | Wells, signs, lamps, decorations | ~16 |

**Total curated: ~288 tiles** (manageable for editor/compiler)

### Implementation Approach: Subdivision Tilesets
Instead of one massive `houses.json`, create **multiple tileset JSONs** per category:

```
tools/level_editor/tilesets/
├── dungeon.json              # All 182 dungeon tiles
├── houses_walls.json         # House walls, corners, windows, doors
├── houses_roofs.json         # Roof tiles
├── houses_floors.json        # Interior floors
├── houses_doors.json         # Door variants
├── houses_windows.json       # Window variants
├── nature_ground.json        # Grass, dirt, paths
├── nature_vegetation.json    # Trees, bushes
├── objects_furniture.json    # Tables, chairs, barrels
├── structures_fences.json    # Fences, gates
└── structures_props.json     # Wells, signs, lamps
```

Each tileset gets:
- Unique `id` (e.g., `houses_walls`, `houses_roofs`)
- `gb_tileset_kind`: `WORLD_TILESET_HOUSES` (shared) or new kinds per category
- Tile definitions with semantic IDs: `houses_walls.corner_tl`, `houses_roofs.ridge_n`, etc.

---

## 3. C Constants Strategy

### Existing Constants (src/world/world.h or similar)
Check what's available. Likely:
- `TILE_FLOOR` (0)
- `TILE_WALL` (1)
- `TILE_EXIT` / `TILE_GATE` (2)
- `TILE_BUILDING` (3)
- `TILE_STUMP_*` (4-6)
- `TILE_DUNGEON_*` - may need additions

### New Constants Needed
| Category | New Constants |
|----------|---------------|
| Dungeon | `TILE_DUNGEON_FLOOR_CRACK`, `TILE_DUNGEON_PILLAR`, `TILE_DUNGEON_ARCH`, `TILE_DUNGEON_DECOR` |
| Houses | `TILE_HOUSE_WALL`, `TILE_HOUSE_ROOF`, `TILE_HOUSE_FLOOR`, `TILE_HOUSE_DOOR`, `TILE_HOUSE_WINDOW`, `TILE_FENCE`, `TILE_PROP` |

**Recommendation**: Add new constants to engine header, map tiles in tileset JSONs to appropriate constants.

---

## 4. File Changes Required

### A. `tools/asset_atlas.py`
```python
# Add to TILESETS dict:
TILESETS = {
    # ... existing ...
    "DungeonTileset.png": {
        "DUNGEON_FLOOR_00": (0, 0),
        "DUNGEON_FLOOR_01": (1, 0),
        # ... all 182 tiles with semantic names
    },
    "Houses_and_various_things.png": {
        # Curated subset ~288 tiles with names like:
        "HOUSES_WALL_CORNER_TL": (0, 0),
        "HOUSES_WALL_CORNER_TR": (15, 0),
        "HOUSES_ROOF_RIDGE_N": (16, 1),
        # ... etc.
    },
}
```

### B. `tools/level_editor/extract_tiles.py`
```python
# Add to processing loop:
for png_name in ["RPG_exterior.png", "RPG_interior.png", "DungeonTileset.png", "Houses_and_various_things.png"]:
    # ...

# Extend TILESET_ID_MAP and COORDS_MAP for new PNGs
# For Houses: map to subdivision tileset IDs based on macro region
```

### C. New tileset JSONs (11 files)
- `tools/level_editor/tilesets/dungeon.json` (182 tiles)
- `tools/level_editor/tilesets/houses_walls.json` (~64 tiles)
- `tools/level_editor/tilesets/houses_roofs.json` (~32 tiles)
- `tools/level_editor/tilesets/houses_floors.json` (~48 tiles)
- `tools/level_editor/tilesets/houses_doors.json` (~16 tiles)
- `tools/level_editor/tilesets/houses_windows.json` (~16 tiles)
- `tools/level_editor/tilesets/nature_ground.json` (~32 tiles)
- `tools/level_editor/tilesets/nature_vegetation.json` (~16 tiles)
- `tools/level_editor/tilesets/objects_furniture.json` (~24 tiles)
- `tools/level_editor/tilesets/structures_fences.json` (~24 tiles)
- `tools/level_editor/tilesets/structures_props.json` (~16 tiles)

### D. `tools/level_editor/src/model/Tileset.ts`
- Add 11 new `TilesetDefinition` constants
- Add to `BUILTIN_TILESETS` registry
- Include `image_url` pointing to extracted tiles

### E. `Makefile` - gfx target
```make
# Add png2gb.py calls for new PNGs with --tile-coords matching asset_atlas.py
@python3 tools/png2gb.py assets/DungeonTileset.png --name rpg_dungeon_tiles \
    --palette gb_green --tile-coords "0,0 1,0 2,0 ..." \
    --raw -o $(GFX_OUT_DIR)/rpg_dungeon_tiles.inc

@python3 tools/png2gb.py assets/Houses_and_various_things.png --name rpg_houses_tiles \
    --palette gb_green --tile-coords "0,0 1,0 15,0 16,1 ..." \
    --raw -o $(GFX_OUT_DIR)/rpg_houses_tiles.inc
```

### F. Engine C headers (if new constants needed)
- `src/world/world.h` or `src/game/tiles_content.c` - add new `TILE_*` constants
- Ensure tile indices in `.inc` files match constant values

---

## 5. Editor UI Considerations

### Tileset Palette Organization
With 11 new tilesets, the palette dropdown will have 14 options. Consider:
- **Grouped dropdown**: "Dungeon", "Houses → Walls/Roofs/Floors/Doors/Windows", "Nature → Ground/Vegetation", "Objects → Furniture", "Structures → Fences/Props"
- Or keep flat list with clear prefixes

### Tile Search/Filter
- Add category filter in `TilesetPalette.tsx`
- Show tile count per tileset

---

## 6. Validation & Testing

```bash
# 1. Extract tiles for web editor
make extract-tiles

# 2. Regenerate asset atlas
make atlas

# 3. Validate all levels (including new tilesets)
make levels

# 4. Build ROM
make release

# 5. Run scenario tests
make test-scenario SCENARIO=new_game
make test-scenario SCENARIO=first_encounter
```

---

## 7. Phased Implementation

### Phase 1: DungeonTileset (Complete)
- [ ] Map all 182 tiles in `asset_atlas.py`
- [ ] Create `dungeon.json` tileset
- [ ] Update `extract_tiles.py` for dungeon
- [ ] Add `TILESET_DUNGEON` to `Tileset.ts`
- [ ] Add `gfx` target for dungeon
- [ ] Test compilation

### Phase 2: Houses Subdivisions
- [ ] Define curated tile list per subdivision (coordinate mapping)
- [ ] Add to `asset_atlas.py`
- [ ] Create 10 subdivision JSON tilesets
- [ ] Update `extract_tiles.py` for houses (with subdivision logic)
- [ ] Add 10 tilesets to `Tileset.ts`
- [ ] Add `gfx` target for houses
- [ ] Test compilation

### Phase 3: Engine Integration
- [ ] Add new C tile constants if needed
- [ ] Verify tile indices match between `.inc` and constants
- [ ] Test full pipeline: editor → JSON → compiler → ROM → scenarios

---

## 8. Clarifying Questions

1. **C Constants**: Should I check `src/world/world.h` and `src/game/tiles_content.c` first to see existing `TILE_*` constants and available indices?

2. **Houses Subdivision Granularity**: Is 10 subdivisions the right number, or should some be merged (e.g., doors+windows, fences+props)?

3. **Tileset Kind**: Should all houses subdivisions share `WORLD_TILESET_HOUSES` or get distinct kinds (`WORLD_TILESET_HOUSES_WALLS`, etc.)? Distinct kinds allow different palettes/tilemaps in engine but add complexity.

4. **Priority**: Dungeon first (complete), then Houses? Or parallel?

5. **Tile Naming**: Use format `category.subcategory_variant` (e.g., `houses_walls.corner_tl`) or flat `houses_wall_corner_tl`?

---

## 9. Risk Assessment

| Risk | Mitigation |
|------|------------|
| 16K tiles → compiler bloat | Curated ~288 tiles via subdivisions |
| Tile index mismatch (JSON vs .inc vs C) | Single source of truth in `asset_atlas.py` coordinates |
| Editor UI overload | Grouped tileset selector, search filter |
| New C constants break existing | Add at end of enum, verify indices match .inc files |
| Houses macro structure changes | Document macro coordinates in asset_atlas.py comments |

---

## Next Steps
1. Confirm C constants availability
2. Finalize Houses subdivision list with exact tile coordinates
3. Begin Phase 1 implementation