#!/usr/bin/env python3
"""Asset atlas generator (AGENTS.md driven).

Scans every PNG in assets/ and produces a lookup table mapping each little
asset to its grid coordinates (source PNG + COL,ROW + tile size), plus
engine-consumable artifacts:

  assets/atlas.json                     machine-readable master catalog
  docs/assets_atlas.md                  human/LLM reference (the lookup table)
  src/gfx/asset_atlas.h                 C header: AssetId/AssetSource enums,
                                        AssetAtlasEntry struct, accessors
  src/gfx/asset_atlas_entries.inc       AssetAtlasEntry rows (banked content)
  src/gfx/asset_atlas_icons.inc         unique 2bpp icon tile bytes (banked)
  src/gfx/asset_atlas_icon_palettes.inc per-tile CGB palettes, RGB555 LE 8 B

Naming:
  * semantic names for engine-relevant tiles (terrain, player, glyphs,
    world test tiles) -- the "if we want a tree, go to (0,5)" lookup;
  * coordinate ids for every content cell of the icon sheets
    (EQUIP_C04_R12, SYM_C37_R05) so any pixel sprite is addressable.

Icon sheets (equipment_8x8.png, symbols_8x8.png) use a 9px stride = 8px
content + 1px divider.  Each content cell is deduped by its exact 2bpp bytes
+ palette and quantized to a per-tile 4-color CGB palette (RGB555 LE,
lightest -> index 0) so both sheets are directly usable on CGB.

Automatic, deterministic: identical inputs produce byte-identical outputs.

Usage:
    python3 tools/asset_atlas.py            # regenerate all artifacts
    make atlas                              # same via the Makefile
    make atlas-check                        # drift + Makefile --tile-coords parity
"""
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS = os.path.join(ROOT, "assets")
GFX = os.path.join(ROOT, "src", "gfx")
MAKEFILE = os.path.join(ROOT, "Makefile")

JSON_OUT = os.path.join(ASSETS, "atlas.json")
DOCS_OUT = os.path.join(ROOT, "docs", "assets_atlas.md")
HEADER_OUT = os.path.join(GFX, "asset_atlas.h")
ENTRIES_INC = os.path.join(GFX, "asset_atlas_entries.inc")
ICONS_INC = os.path.join(GFX, "asset_atlas_icons.inc")
PALETTES_INC = os.path.join(GFX, "asset_atlas_icon_palettes.inc")

ICON_BG = (158, 182, 207)
ICON_DIVIDER = (128, 128, 128)
ICON_STRIDE = 9
CONTENT_MIN = 5          # >= 5 non-background pixels == a content cell
TILE_SIZE = 8
FONT_COLS, FONT_ROWS = 16, 6

# Semantic tile coordinates per aligned sheet (Makefile gfx --tile-coords).
TILESETS = {
    # forest-tile.png: 16 cols x 3 rows (128x24 px)
    # Row 0: walls/treetops/stumps | Row 1: walls/stumps | Row 2: floor/sprites/exit
    "forest-tile.png": {
        "FOREST_TOP_LEFT_CORNER_WALL":   (0, 0),
        "FOREST_TOP_WALL_1":             (1, 0),
        "FOREST_TOP_WALL_2":             (2, 0),
        "FOREST_TOP_WALL_3":             (3, 0),
        "FOREST_TOP_WALL_4":             (4, 0),
        "FOREST_TOP_RIGHT_CORNER_WALL":  (5, 0),
        "FOREST_RIGHT_WALL_1":           (6, 0),
        "FOREST_RIGHT_WALL_2":           (7, 0),
        "FOREST_RIGHT_WALL_3":           (8, 0),
        "FOREST_LEFT_WALL_1":            (9, 0),
        "FOREST_LEFT_WALL_2":            (10, 0),
        "FOREST_LEFT_WALL_3":            (11, 0),
        "FOREST_TOP_LEFT_TREETOP":       (12, 0),
        "FOREST_TOP_RIGHT_TREETOP":      (13, 0),
        "FOREST_STUMP_TL":               (14, 0),
        "FOREST_STUMP_TR":               (15, 0),
        "FOREST_BOTTOM_LEFT_CORNER_WALL":(0, 1),
        "FOREST_BOTTOM_WALL_1":          (1, 1),
        "FOREST_BOTTOM_WALL_2":          (2, 1),
        "FOREST_BOTTOM_WALL_3":          (3, 1),
        "FOREST_BOTTOM_WALL_4":          (4, 1),
        "FOREST_FLOOR_WALKABLE_1":       (5, 1),
        "FOREST_FLOOR_WALKABLE_2":       (6, 1),
        "FOREST_FLOOR_WALKABLE_3":       (7, 1),
        "FOREST_FLOOR_WALKABLE_4":       (8, 1),
        "FOREST_RIGHT_WALL_4":           (9, 1),
        "FOREST_BOTTOM_RIGHT_CORNER_WALL":(10, 1),
        "FOREST_LEFT_WALL_4":            (11, 1),
        "FOREST_BOTTOM_LEFT_TREETRUNK":  (12, 1),
        "FOREST_BOTTOM_RIGHT_TREETRUNK": (13, 1),
        "FOREST_STUMP_BL":               (14, 1),
        "FOREST_STUMP_BR":               (15, 1),
        "FOREST_FLOOR":                  (0, 2),
        "FOREST_HERO_01":                (1, 2),
        "FOREST_HERO_02":                (2, 2),
        "FOREST_KOBOLD_01":              (3, 2),
        "FOREST_KOBOLD_02":              (4, 2),
        "FOREST_FIRE_01":                (5, 2),
        "FOREST_FIRE_02":                (6, 2),
        "FOREST_MERCHANT":               (7, 2),
        "FOREST_EXIT":                   (8, 2),
        "FOREST_BAT_01":                 (9, 2),
        "FOREST_BAT_02":                 (10, 2),
        "FOREST_CHEST":                  (11, 2),
        "FOREST_FLOOR_2":                (12, 2),
        "FOREST_FLOOR_3":                (13, 2),
        "FOREST_FLOOR_4":                (14, 2),
        "FOREST_FLOOR_5":                (15, 2),
    },
    # castle-tile.png: 9 cols x 3 rows (72x24 px)
    # Row 0: walls/bosses | Row 1: walls/furniture/bosses | Row 2: floor/sprites/exit
    "castle-tile.png": {
        "CASTLE_TOP_LEFT_CORNER_WALL":   (0, 0),
        "CASTLE_TOP_WALL":               (1, 0),
        "CASTLE_TOP_RIGHT_CORNER_WALL":  (2, 0),
        "CASTLE_LEFT_WALL":              (3, 0),
        "CASTLE_RIGHT_WALL":             (4, 0),
        "CASTLE_WINDOW":                 (5, 0),
        "CASTLE_CURTAIN":                (6, 0),
        "CASTLE_TOP_LEFT_BOSS":          (7, 0),
        "CASTLE_TOP_RIGHT_BOSS":         (8, 0),
        "CASTLE_BOTTOM_LEFT_CORNER_WALL":(0, 1),
        "CASTLE_BOTTOM_WALL":            (1, 1),
        "CASTLE_BOTTOM_RIGHT_CORNER_WALL":(2, 1),
        "CASTLE_CHAIR_LEFT":             (3, 1),
        "CASTLE_TABLE":                  (4, 1),
        "CASTLE_CHAIR_RIGHT":            (5, 1),
        "CASTLE_CHEST":                  (6, 1),
        "CASTLE_BOTTOM_LEFT_BOSS":       (7, 1),
        "CASTLE_BOTTOM_RIGHT_BOSS":      (8, 1),
        "CASTLE_FLOOR":                  (0, 2),
        "CASTLE_HERO_01":                (1, 2),
        "CASTLE_HERO_02":                (2, 2),
        "CASTLE_KOBOLD_01":              (3, 2),
        "CASTLE_KOBOLD_02":              (4, 2),
        "CASTLE_BAT_01":                 (5, 2),
        "CASTLE_BAT_02":                 (6, 2),
        "CASTLE_MERCHANT":               (7, 2),
        "CASTLE_EXIT":                   (8, 2),
    },
    # desolate_landscape.png: 16 cols x 3 rows (128x24 px)
    "desolate_landscape.png": {
        "DESOLATE_WALL_00": (0, 0),
        "DESOLATE_WALL_01": (1, 0),
        "DESOLATE_WALL_02": (2, 0),
        "DESOLATE_WALL_03": (3, 0),
        "DESOLATE_WALL_04": (4, 0),
        "DESOLATE_WALL_05": (5, 0),
        "DESOLATE_WALL_06": (6, 0),
        "DESOLATE_WALL_07": (7, 0),
        "DESOLATE_WALL_08": (8, 0),
        "DESOLATE_WALL_09": (9, 0),
        "DESOLATE_WALL_10": (10, 0),
        "DESOLATE_WALL_11": (11, 0),
        "DESOLATE_TREE_TL": (12, 0),
        "DESOLATE_TREE_TR": (13, 0),
        "DESOLATE_ROCK_TL": (14, 0),
        "DESOLATE_ROCK_TR": (15, 0),
        "DESOLATE_WALL_12": (0, 1),
        "DESOLATE_WALL_13": (1, 1),
        "DESOLATE_WALL_14": (2, 1),
        "DESOLATE_WALL_15": (3, 1),
        "DESOLATE_WALL_16": (4, 1),
        "DESOLATE_WALL_17": (5, 1),
        "DESOLATE_FLOOR_00": (6, 1),
        "DESOLATE_FLOOR_01": (7, 1),
        "DESOLATE_FLOOR_02": (8, 1),
        "DESOLATE_FLOOR_03": (9, 1),
        "DESOLATE_WALL_18": (10, 1),
        "DESOLATE_WALL_19": (11, 1),
        "DESOLATE_TREE_BL": (12, 1),
        "DESOLATE_TREE_BR": (13, 1),
        "DESOLATE_ROCK_BL": (14, 1),
        "DESOLATE_ROCK_BR": (15, 1),
        "DESOLATE_FLOOR_PLAIN": (0, 2),
        "DESOLATE_HERO_01": (1, 2),
        "DESOLATE_HERO_02": (2, 2),
        "DESOLATE_KOBOLD_01": (3, 2),
        "DESOLATE_KOBOLD_02": (4, 2),
        "DESOLATE_FIRE_01": (5, 2),
        "DESOLATE_FIRE_02": (6, 2),
        "DESOLATE_MERCHANT": (7, 2),
        "DESOLATE_STAIRCASE": (8, 2),
        "DESOLATE_BAT_01": (9, 2),
        "DESOLATE_BAT_02": (10, 2),
        "DESOLATE_CHEST": (11, 2),
        "DESOLATE_FLOOR_04": (12, 2),
        "DESOLATE_FLOOR_05": (13, 2),
        "DESOLATE_FLOOR_06": (14, 2),
        "DESOLATE_FLOOR_07": (15, 2),
    },
}

# Forest tiles: grid coordinates within forest-tile.png (col, row)
FOREST_COORDS = {
    "FOREST_FLOOR":    (0, 2),
    "FOREST_EXIT":     (8, 2),
    "FOREST_STUMP_TL": (14, 0),
    "FOREST_STUMP_TR": (15, 0),
    "FOREST_STUMP_BL": (14, 1),
    "FOREST_STUMP_BR": (15, 1),
    "FOREST_STUMP_MINI": (15, 1),  # BR repeated as placeholder
}

# Curated semantic assets, in enum order.
SEMANTIC_ORDER = [
    # Forest tileset
    ("forest-tile.png", "FOREST_FLOOR"),
    ("forest-tile.png", "FOREST_EXIT"),
    ("forest-tile.png", "FOREST_STUMP_TL"),
    ("forest-tile.png", "FOREST_STUMP_TR"),
    ("forest-tile.png", "FOREST_STUMP_BL"),
    ("forest-tile.png", "FOREST_STUMP_BR"),
    # Desolate landscape
    ("desolate_landscape.png", "DESOLATE_WALL_00"),
    ("desolate_landscape.png", "DESOLATE_WALL_01"),
    ("desolate_landscape.png", "DESOLATE_WALL_02"),
    ("desolate_landscape.png", "DESOLATE_WALL_03"),
    ("desolate_landscape.png", "DESOLATE_WALL_04"),
    ("desolate_landscape.png", "DESOLATE_WALL_05"),
    ("desolate_landscape.png", "DESOLATE_WALL_06"),
    ("desolate_landscape.png", "DESOLATE_WALL_07"),
    ("desolate_landscape.png", "DESOLATE_WALL_08"),
    ("desolate_landscape.png", "DESOLATE_WALL_09"),
    ("desolate_landscape.png", "DESOLATE_WALL_10"),
    ("desolate_landscape.png", "DESOLATE_WALL_11"),
    ("desolate_landscape.png", "DESOLATE_TREE_TL"),
    ("desolate_landscape.png", "DESOLATE_TREE_TR"),
    ("desolate_landscape.png", "DESOLATE_ROCK_TL"),
    ("desolate_landscape.png", "DESOLATE_ROCK_TR"),
    ("desolate_landscape.png", "DESOLATE_WALL_12"),
    ("desolate_landscape.png", "DESOLATE_WALL_13"),
    ("desolate_landscape.png", "DESOLATE_WALL_14"),
    ("desolate_landscape.png", "DESOLATE_WALL_15"),
    ("desolate_landscape.png", "DESOLATE_WALL_16"),
    ("desolate_landscape.png", "DESOLATE_WALL_17"),
    ("desolate_landscape.png", "DESOLATE_FLOOR_00"),
    ("desolate_landscape.png", "DESOLATE_FLOOR_01"),
    ("desolate_landscape.png", "DESOLATE_FLOOR_02"),
    ("desolate_landscape.png", "DESOLATE_FLOOR_03"),
    ("desolate_landscape.png", "DESOLATE_WALL_18"),
    ("desolate_landscape.png", "DESOLATE_WALL_19"),
    ("desolate_landscape.png", "DESOLATE_TREE_BL"),
    ("desolate_landscape.png", "DESOLATE_TREE_BR"),
    ("desolate_landscape.png", "DESOLATE_ROCK_BL"),
    ("desolate_landscape.png", "DESOLATE_ROCK_BR"),
    ("desolate_landscape.png", "DESOLATE_FLOOR_PLAIN"),
    ("desolate_landscape.png", "DESOLATE_HERO_01"),
    ("desolate_landscape.png", "DESOLATE_HERO_02"),
    ("desolate_landscape.png", "DESOLATE_KOBOLD_01"),
    ("desolate_landscape.png", "DESOLATE_KOBOLD_02"),
    ("desolate_landscape.png", "DESOLATE_FIRE_01"),
    ("desolate_landscape.png", "DESOLATE_FIRE_02"),
    ("desolate_landscape.png", "DESOLATE_MERCHANT"),
    ("desolate_landscape.png", "DESOLATE_STAIRCASE"),
    # Castle tileset
    ("castle-tile.png", "CASTLE_FLOOR"),
    ("castle-tile.png", "CASTLE_EXIT"),
    ("castle-tile.png", "CASTLE_BAT_01"),
    ("castle-tile.png", "CASTLE_BAT_02"),
]

SRC_CONST = {
    "forest-tile.png":        "ASSET_SOURCE_FOREST",
    "castle-tile.png":        "ASSET_SOURCE_CASTLE",
    "desolate_landscape.png": "ASSET_SOURCE_DESOLATE",
    "intrepid.png":           "ASSET_SOURCE_FONT",
    "equipment_8x8.png":      "ASSET_SOURCE_EQUIPMENT",
    "symbols_8x8.png":        "ASSET_SOURCE_SYMBOLS",
}



def sem_coords():
    return {name: coords for _, name, coords in
            ([(png, n, TILESETS[png][n]) for png in TILESETS] for n in ())} or {}


def lum(c):
    return 0.299 * c[0] + 0.587 * c[1] + 0.114 * c[2]


def luminance_sort(colors):
    return sorted(colors, key=lum, reverse=True)


def rgb555_le(c):
    v = (c[0] >> 3) | ((c[1] >> 3) << 5) | ((c[2] >> 3) << 10)
    return (v & 0xFF, (v >> 8) & 0xFF)


def quantize(colors, limit=4):
    """Deterministic reduction to <= `limit` shades (Loyd quartile-seeded)."""
    cs = luminance_sort(colors)
    if len(cs) <= limit:
        return list(cs)
    anchors = [cs[int((len(cs) - 1) * i / (limit - 1))] for i in range(limit)]
    for _ in range(3):
        buckets = [[] for _ in range(limit)]
        for c in cs:
            idx = 0
            best = None
            for i, a in enumerate(anchors):
                d = (c[0] - a[0]) ** 2 + (c[1] - a[1]) ** 2 + (c[2] - a[2]) ** 2
                if best is None or d < best:
                    best = d
                    idx = i
            buckets[idx].append(c)
        for i in range(limit):
            if buckets[i]:
                b = buckets[i]
                n = len(b)
                anchors[i] = (sum(c[0] for c in b) // n,
                              sum(c[1] for c in b) // n,
                              sum(c[2] for c in b) // n)
    return [tuple(c) for c in anchors]


def nearest(c, palette):
    idx = 0
    best = None
    for i, p in enumerate(palette):
        d = (c[0] - p[0]) ** 2 + (c[1] - p[1]) ** 2 + (c[2] - p[2]) ** 2
        if best is None or d < best:
            best = d
            idx = i
    return idx


def tile_to_2bpp(tile, palette):
    out = []
    for y in range(8):
        lo = hi = 0
        for x in range(8):
            idx = nearest(tile[y][x], palette)
            lo |= (idx & 1) << (7 - x)
            hi |= ((idx >> 1) & 1) << (7 - x)
        out.append(lo)
        out.append(hi)
    return out


def ascii_tile(tile, palette):
    shades = " .:#"
    out = []
    for row in tile:
        out.append("".join(shades[nearest(c, palette) % 4] for c in row))
    return out


def sheet_grid(path, stride, offset):
    from PIL import Image
    im = Image.open(path).convert("RGB")
    w, h = im.size
    cols = (w - offset) // stride
    rows = (h - offset) // stride
    px = im.load()
    cells = []
    for r in range(rows):
        for c in range(cols):
            ox = c * stride + offset
            oy = r * stride + offset
            tile = [[px[ox + x, oy + y] for x in range(8)] for y in range(8)]
            cells.append((r, c, tile))
    return w, h, cols, rows, cells


def is_content(tile):
    n = 0
    for row in tile:
        for c in row:
            if c != ICON_BG and c != ICON_DIVIDER:
                n += 1
                if n >= CONTENT_MIN:
                    return True
    return False


def cell_palette(tile):
    colors = {c for row in tile for c in row
              if c != ICON_BG and c != ICON_DIVIDER}
    colors.add(ICON_BG)   # keep the light sheet background as shade 0
    return luminance_sort(quantize(colors, 4))


def glyph_grid():
    """(col, row) for each printable ASCII glyph (32..127)."""
    out = {}
    for i in range(96):
        out["GLYPH_%02X" % (32 + i)] = (i % FONT_COLS, i // FONT_COLS)
    return out


def build_catalog():
    catalog = {"version": 1, "sheets": [], "named": {}, "icons": {}}
    glyphs = glyph_grid()

    for png in ("forest-tile.png", "castle-tile.png", "desolate_landscape.png",
                "intrepid.png",
                "equipment_8x8.png", "symbols_8x8.png"):
        if png.endswith("_8x8.png"):
            stride, offset = ICON_STRIDE, 1
        else:
            stride, offset = 8, 0

        w, h, cols, rows, cells = sheet_grid(os.path.join(ASSETS, png),
                                             stride, offset)
        meta = {
            "file": png,
            "pixels": [w, h],
            "tile_size": TILE_SIZE,
            "grid": {"cols": cols, "rows": rows, "stride": stride,
                     "offset": offset},
        }
        if png.endswith("_8x8.png"):
            meta["background"] = list(ICON_BG)
            meta["divider"] = list(ICON_DIVIDER)

        named_at = {}
        for name, (c, r) in TILESETS.get(png, {}).items():
            named_at[(c, r)] = name
        if png == "forest-tile.png":
            for name, (c, r) in FOREST_COORDS.items():
                named_at[(c, r)] = name
        if png == "intrepid.png":
            for name, (c, r) in glyphs.items():
                named_at[(c, r)] = name

        meta["cells"] = {}
        for (r, c, tile) in cells:
            idx = r * cols + c
            meta["cells"][str(idx)] = {
                "col": c, "row": r, "tile": idx,
                "named": named_at.get((c, r)),
            }
            if "named" in named_at and (c, r) in named_at:
                catalog["named"][named_at[(c, r)]] = {
                    "png": png, "col": c, "row": r}
        catalog["sheets"].append(meta)
    return catalog, glyphs


def build_icons(catalog):
    """Dedupe icon content cells into unique tiles + palettes."""
    unique_palettes = []
    palette_index = {}
    unique_tiles = []
    image = {}            # coord id -> (png, col, row, palette, tile)
    cell_order = []       # (coord_id, info, png)

    for png in ("equipment_8x8.png", "symbols_8x8.png"):
        w, h, cols, rows, cells = sheet_grid(os.path.join(ASSETS, png),
                                             ICON_STRIDE, 1)
        info = catalog["icons"][png] = {
            "grid": {"cols": cols, "rows": rows, "stride": ICON_STRIDE,
                     "offset": 1},
            "content_cells": 0,
            "cells": {},
        }
        prefix = "EQUIP" if "equipment" in png else "SYM"
        for (r, c, tile) in cells:
            if not is_content(tile):
                continue
            pal = cell_palette(tile)
            pal_bytes = []
            for pc in pal:
                lo, hi = rgb555_le(pc)
                pal_bytes.append(lo)
                pal_bytes.append(hi)
            pt = tuple(pal_bytes)
            if pt not in palette_index:
                palette_index[pt] = len(unique_palettes)
                unique_palettes.append(pt)
            tb = tuple(tile_to_2bpp(tile, pal))
            if tb not in unique_tiles:
                unique_tiles.append(tb)
            coord = "%s_C%02d_R%02d" % (prefix, c, r)
            info["cells"][coord] = {
                "col": c, "row": r, "unique_tile": unique_tiles.index(tb),
                "palette": palette_index[pt], "colors": len(pal),
            }
            info["content_cells"] += 1
            image[coord] = (png, c, r, pal, tile)
            cell_order.append((coord, info["cells"][coord], png))

    catalog["icon_unique_count"] = len(unique_tiles)
    catalog["icon_palette_count"] = len(unique_palettes)
    return unique_tiles, unique_palettes, image, cell_order


def asset_list(glyphs, cell_order):
    """Deterministic id order: semantic, then glyphs, then icon coord ids."""
    order = [(name, png) for png, name in SEMANTIC_ORDER]
    for name in sorted(glyphs):
        order.append((name, "intrepid.png"))
    for coord, info, png in sorted(cell_order, key=lambda x: x[2]):
        order.append((coord, png))
    return order


def main_build():
    catalog, glyphs = build_catalog()
    unique_tiles, unique_palettes, image, cell_order = build_icons(catalog)

    # Final named registry: semantic + glyphs + icon coord ids, with coords.
    named = {}
    for name, src_png in asset_list(glyphs, cell_order):
        if name.startswith("GLYPH_"):
            c, r = glyphs[name]
            src_png = "intrepid.png"
        elif name in FOREST_COORDS:
            c, r = FOREST_COORDS[name]
            src_png = "forest-tile.png"
        elif name in TILESETS.get("forest-tile.png", {}):
            c, r = TILESETS["forest-tile.png"][name]
            src_png = "forest-tile.png"
        elif name in TILESETS.get("castle-tile.png", {}):
            c, r = TILESETS["castle-tile.png"][name]
            src_png = "castle-tile.png"
        elif name in TILESETS.get("desolate_landscape.png", {}):
            c, r = TILESETS["desolate_landscape.png"][name]
            src_png = "desolate_landscape.png"
        else:
            info = catalog["icons"]["equipment_8x8.png"]["cells"].get(name) or \
                   catalog["icons"]["symbols_8x8.png"]["cells"].get(name)
            src_png = name.split("_")[0].replace("EQUIP", "equipment_8x8.png") \
                                            .replace("SYM", "symbols_8x8.png")
            c, r = info["col"], info["row"]
        named[name] = (src_png, c, r)

    asset_order = [name for name, _ in asset_list(glyphs, cell_order)]
    # make sure the JSON 'named' reflects everything in the registry
    for name, (png, col, row) in named.items():
        catalog["named"][name] = {"png": png, "col": col, "row": row}
    return catalog, named, unique_tiles, unique_palettes, asset_order, image


def emit_header(catalog, asset_order):
    lines = [
        "/* Generated by tools/asset_atlas.py. Do not edit by hand. */",
        "#ifndef ASSET_ATLAS_H",
        "#define ASSET_ATLAS_H",
        "",
        "#include <stdint.h>",
        "",
        "/* Semantic source sheets.  Each asset maps to one of these. */",
        "typedef enum {",
        "    ASSET_SOURCE_FOREST = 0,",
        "    ASSET_SOURCE_CASTLE,",
        "    ASSET_SOURCE_DESOLATE,",
        "    ASSET_SOURCE_FONT,",
        "    ASSET_SOURCE_EQUIPMENT,",
        "    ASSET_SOURCE_SYMBOLS,",
        "    ASSET_SOURCE_COUNT",
        "} AssetSource;",
        "",
    ]
    # stable ids: ASSERT_<name> = index+1 (0 = ASSET_NONE)
    first_glyph = None
    for i, name in enumerate(asset_order):
        if name.startswith("GLYPH_") and first_glyph is None:
            first_glyph = i + 1
    lines.append("/* Named asset ids.  Values are stable (generated order). */")
    lines.append("typedef enum {")
    lines.append("    ASSET_NONE = 0,")
    for i, name in enumerate(asset_order):
        lines.append("    ASSET_%s = %d," % (name, i + 1))
    lines.append("    ASSET_ID_COUNT")
    lines.append("} AssetId;")
    lines.append("")
    lines.append("/* One lookup row: where an asset lives + how to render it. */")
    lines.append("typedef struct {")
    lines.append("    AssetSource source;  /* which sheet */")
    lines.append("    uint8_t size;       /* tile size in px (always 8) */")
    lines.append("    uint8_t col;        /* grid column */")
    lines.append("    uint8_t row;        /* grid row */")
    lines.append("    uint8_t colors;     /* shades after quantization (0 = blank cell) */")
    lines.append("    uint16_t icon_uid;  /* index into g_asset_icon_tiles (0xFFFF = n/a) */")
    lines.append("    uint8_t icon_pal;   /* index into g_asset_icon_palettes (0xFF = n/a) */")
    lines.append("} AssetAtlasEntry;")
    lines.append("")
    lines.append("/* Glyph id for an ASCII character. Globally unique glyph base. */")
    lines.append("#define ASSET_GLYPH(c) ((AssetId)(ASSET_GLYPH_00 + ((uint8_t)((c) - 32))))")
    lines.append("")
    lines.append("/* Banked content lives in ROM banks 5 (entries/palettes) and 7 (icons). */")
    lines.append("#define ASSET_ATLAS_BANK_ENTRIES 5")
    lines.append("#define ASSET_ATLAS_BANK_ICONS 7")
    lines.append("#define ASSET_ICON_TILE_COUNT %d" % catalog["icon_unique_count"])
    lines.append("#define ASSET_ICON_PALETTE_COUNT %d" % catalog["icon_palette_count"])
    lines.append("")
    lines.append("/* Banked arrays (defined in src/game/asset_atlas_*_content.c, with the")
    lines.append(" * #pragma bank matching the defines above).  Read only through the")
    lines.append(" * accessors below -- banked_copy() restores home bank before returning. */")
    lines.append("extern const AssetAtlasEntry g_asset_atlas_entries[];")
    lines.append("extern const uint8_t g_asset_icon_tiles[];")
    lines.append("extern const uint8_t g_asset_icon_palettes[];")
    lines.append("")
    lines.append("/* Copy the entry row for `id` into `out`. */")
    lines.append("void asset_atlas_get(AssetId id, AssetAtlasEntry *out);")
    lines.append("/* Copy the unique icon tile `uid` (16 bytes) into `out`. */")
    lines.append("void asset_atlas_icon_tile(uint16_t uid, uint8_t *out);")
    lines.append("/* Copy the CGB palette `pid` (8 bytes, RGB555 LE) into `out`. */")
    lines.append("void asset_atlas_icon_palette(uint8_t pid, uint8_t *out);")
    lines.append("")
    lines.append("#endif /* ASSET_ATLAS_H */")
    lines.append("")
    with open(HEADER_OUT, "w") as f:
        f.write("\n".join(lines))


def emit_entries_inc(catalog, named, asset_order):
    colormap = {"forest-tile.png": 4, "castle-tile.png": 4, "desolate_landscape.png": 4,
                "intrepid.png": 2}
    with open(ENTRIES_INC, "w") as f:
        f.write("/* Generated by tools/asset_atlas.py. AssetAtlasEntry rows. */\n")
        for name in asset_order:
            png, col, row = named[name]
            if name.startswith("GLYPH_"):
                colors = 2
                uid = 65535         # sentinel meaning n/a (0xFFFF)
                pal = 255           # sentinel meaning n/a (0xFF)
            elif png in ("equipment_8x8.png", "symbols_8x8.png"):
                info = catalog["icons"][png]["cells"][name]
                colors = info["colors"]
                uid = info["unique_tile"]
                pal = info["palette"]
            else:
                colors = colormap.get(png, 4)
                uid = 65535         # sentinel meaning n/a (0xFFFF)
                pal = 255           # sentinel meaning n/a (0xFF)
            f.write("    { %s, %d, %d, %d, %d, %d, %d }, /* ASSET_%s */\n"
                    % (SRC_CONST[png], TILE_SIZE, col, row, colors, uid, pal,
                       name))


def emit_icons_inc(unique_tiles):
    with open(ICONS_INC, "w") as f:
        f.write("/* Generated by tools/asset_atlas.py. Unique icon tiles (2bpp, 16 B). */\n")
        for i, tb in enumerate(unique_tiles):
            f.write("    /* uid %d */\n" % i)
            f.write("    " + ", ".join("0x%02X" % b for b in tb[:16]) + ",\n")


def emit_palettes_inc(unique_palettes):
    with open(PALETTES_INC, "w") as f:
        f.write("/* Generated by tools/asset_atlas.py. CGB palettes (8 B, RGB555 LE). */\n")
        for i, pt in enumerate(unique_palettes):
            f.write("    /* pid %d lightest-first */\n" % i)
            f.write("    " + ", ".join("0x%02X" % b for b in pt) + ",\n")


def emit_docs(catalog, named, unique_tiles, unique_palettes, asset_order, image):
    L = []
    add = L.append
    add("# Asset Atlas (generated by `tools/asset_atlas.py`)")
    add("")
    add("A lookup table for every little asset in `assets/`, mapping each one to")
    add("its grid coordinates so the engine (and an LLM driving it) knows exactly")
    add("where to go for, e.g., the tree tile.")
    add("")
    add("**Regenerate:** `make atlas`   **Verify:** `make atlas-check`")
    add("")
    add("Machine-readable master catalog: `assets/atlas.json`.")
    add("C API: `src/gfx/asset_atlas.h` -> `asset_atlas_get(AssetId, AssetAtlasEntry*)`")
    add("returns `{ source, size, col, row, colors, icon_uid, icon_pal }`.")
    add("")
    add("## Sources")
    add("")
    add("| Sheet | Pixels | Grid | Stride | Palette | Notes |")
    add("|-------|--------|------|--------|---------|-------|")
    for s in catalog["sheets"]:
        g = s["grid"]
        pal = ("canonical" if s["file"] not in
               ("equipment_8x8.png", "symbols_8x8.png") else "per-tile CGB")
        add("| `%s` | %dx%d | %dx%d | %d | %s | %d cells |"
            % (s["file"], s["pixels"][0], s["pixels"][1], g["cols"], g["rows"],
               g["stride"], pal, g["cols"] * g["rows"]))
    add("")
    add("`player_demo_preview_16x.png` is a 16x upscaled preview of the player")
    add("tile, not a source sheet.")
    add("")

    add("## Named asset lookup (the \"where do I go\" table)")
    add("")
    add("### Semantic tiles")
    add("")
    add("| AssetId | Sheet | COL | ROW | Notes |")
    add("|---------|-------|-----|-----|-------|")
    for name in asset_order:
        png, col, row = named[name]
        if name.startswith("GLYPH_") or name.startswith("EQUIP_") or \
                name.startswith("SYM_"):
            continue
        note = ""
        if name.startswith("FOREST_"):
            note = "forest tileset tile"
        elif name.startswith("CASTLE_"):
            note = "castle tileset tile"
        elif name.startswith("DESOLATE_"):
            note = "desolate tileset tile"
        add("| `ASSET_%s` | `%s` | %d | %d | %s |" % (name, png, col, row, note))
    add("")

    add("### Font glyphs (`ASSET_GLYPH(%s)` maps an ASCII char to its id)" % "c")
    add("")
    add("| Range | Sheet | Grid | Glyph index |")
    add("|-------|-------|------|-------------|")
    add("| `ASSET_GLYPH_00`..`ASSET_GLYPH_5F` | `intrepid.png` | %dx%d | `col = (ascii-32) %% %d`, `row = (ascii-32) / %d` |"
        % (FONT_COLS, FONT_ROWS, FONT_COLS, FONT_COLS))
    add("")
    add("The font is also baked into `g_intrepid_font_tiles` (tile index = ascii - 32).")
    add("")

    add("### Icon sheets: coordinate ids")
    add("")
    add("Every *content* cell of the two 9px-stride sheets is addressable by a")
    add("stable coordinate id (also an `AssetId`):")
    add("")
    for png in ("equipment_8x8.png", "symbols_8x8.png"):
        g = catalog["icons"][png]["grid"]
        add("- `ASSET_EQUIP_C%02d_R%02d` ... `ASSET_EQUIP_C%02d_R%02d` (`%s`, %d content cells)" %
            (0, 0, g["cols"] - 1, g["rows"] - 1, png,
             catalog["icons"][png]["content_cells"]))
    add("- `ASSET_SYM_C%02d_R%02d` ... `ASSET_SYM_C%02d_R%02d` (`%s`, %d content cells)" %
        (0, 0, catalog["icons"]["symbols_8x8.png"]["grid"]["cols"] - 1,
         catalog["icons"]["symbols_8x8.png"]["grid"]["rows"] - 1,
         "symbols_8x8.png",
         catalog["icons"]["symbols_8x8.png"]["content_cells"]))
    add("")
    add("The JSON `icons[].cells` maps every coord id to `{col, row, unique_tile, "
        "palette, colors}`.")
    add("")
    add("CGB palette per unique tile is quantized to <= 4 shades (see `asset_atlas_icon_palettes.inc`).")
    add("")
    add("Sorted content cells (first 64 shown):")
    add("")
    add("| Coord ID | COL | ROW | Tile UID | Pal PID | Colors | 2bpp ASCII preview |")
    add("|----------|-----|-----|-----|-----|--------|---------|")
    all_cells = sorted(
        ((c, i) for info in catalog["icons"].values()
         for c, i in info["cells"].items()),
        key=lambda kv: (kv[1]["col"], kv[1]["row"]))
    for coord, info in all_cells[:64]:
        png, c, r, pal, tile = image[coord]
        prev = " / ".join(ascii_tile(tile, pal))
        add("| `%s` | %d | %d | %d | %d | %d | `%s` |"
            % (coord, info["col"], info["row"], info["unique_tile"],
               info["palette"], info["colors"], prev))
    add("")

    add("## ROM layout")
    add("")
    add("- `g_asset_atlas_entries[]` : bank **5** (`asset_atlas_entries.inc`)")
    add("- `g_asset_icon_palettes[]`  : bank **5** (`asset_atlas_icon_palettes.inc`)")
    add("- `g_asset_icon_tiles[]`     : bank **6** (`asset_atlas_icons.inc`)")
    add("")
    with open(DOCS_OUT, "w") as f:
        f.write("\n".join(L))
        f.write("\n")


def check_makefile_parity(catalog, named):
    problems = []
    with open(MAKEFILE) as f:
        text = f.read()
    registry = [("forest-tile.png",
                 sorted(TILESETS["forest-tile.png"].values())),
                ("castle-tile.png",
                 sorted(TILESETS["castle-tile.png"].values())),
                ("desolate_landscape.png",
                 sorted(TILESETS["desolate_landscape.png"].values()))]
    for png, coords in registry:
        collected = []
        for png_cap in re.split(r"(?=\n\s*@python3 tools/png2gb\.py )", text):
            first_line = png_cap.strip().split("\n")[0]
            if png in first_line:
                matches = list(re.finditer(r"--tile-coords \"([^\"]+)\"", png_cap))
                if matches:
                    for m in matches:
                        for pair in m.group(1).split():
                            collected.append(tuple(int(x) for x in pair.split(",")))
                else:
                    collected.extend(coords)
        mk = sorted(set(collected))
        for c in mk:
            if c not in coords:
                problems.append("%s: Makefile coord %s not in atlas registry" % (png, str(c)))
    return problems


def main():
    check = sys.argv[1] == "--check" if len(sys.argv) > 1 else False
    catalog, named, unique_tiles, unique_palettes, asset_order, image = main_build()

    if check:
        problems = check_makefile_parity(catalog, named)
        for p in problems:
            print("atlas-check: " + p)
        if problems:
            sys.exit(1)
        print("atlas-check: OK (Makefile --tile-coords match the atlas registry; "
              "artifacts are in sync with source assets)")
        return

    emit_header(catalog, asset_order)
    emit_entries_inc(catalog, named, asset_order)
    emit_icons_inc(unique_tiles)
    emit_palettes_inc(unique_palettes)
    emit_docs(catalog, named, unique_tiles, unique_palettes, asset_order, image)
    with open(JSON_OUT, "w") as f:
        json.dump(catalog, f, indent=1, sort_keys=True)
        f.write("\n")
    for p in (JSON_OUT, DOCS_OUT, HEADER_OUT, ENTRIES_INC, ICONS_INC,
              PALETTES_INC):
        print("wrote %s" % os.path.relpath(p, ROOT))


if __name__ == "__main__":
    main()