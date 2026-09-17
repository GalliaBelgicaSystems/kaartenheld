#!/usr/bin/env python3
"""palette_compiler.py -- Tileset tiles to named CGB palette slots.

Single source of truth for CGB background palette assignments. Consumes
tileset JSON (tools/level_editor/tilesets/*.json, every tile carries an
explicit `"palette": "<rampname>"`) and PNG assets, outputs JSON manifests
consumed by both the web editor (WYSIWYG canvas) and the ROM compiler
(generate_tiles.py -> tile_palette.h).

Ramp names resolve through the SLOTS tables (tools/palette_txt.py) to
positional hardware slots, so tile_palette.h indices correspond to the
generated ROM palettes. `make palette-check` fails on any drift.

`--suggest` mode keeps the old color-distance matcher as a proposal tool
for new art: it prints the nearest ramp per tile but writes nothing.
"""

import sys
import json
import argparse
from pathlib import Path
from typing import List, Tuple, Dict, Any, Optional
from PIL import Image
import math

sys.path.insert(0, str(Path(__file__).resolve().parent))
from palette_txt import (RAMPS as _AUTHOR_RAMPS, ANCHORS as _AUTHOR_ANCHORS,
                         RAMP_NAMES as _AUTHOR_RAMP_NAMES,
                         DEFAULT_FLOOR_PALETTES as _AUTHOR_FLOOR)

REPO_ROOT = Path(__file__).resolve().parent.parent
TILESETS_DIR = REPO_ROOT / "tools" / "level_editor" / "tilesets"
ASSETS_DIR = REPO_ROOT / "assets"
GENERATED_DIR = REPO_ROOT / "generated" / "tiles"

TILE_SIZE = 8

# Tilesets to process
TILESETS = ["forest", "castle", "desolate_landscape", "village"]

# PNG mapping
PNG_MAP = {
    "forest": "forest-tile.png",
    "castle": "castle-tile.png",
    "desolate_landscape": "desolate-tile.png",
    "village": "village-tile.png",
}

# Fixed CGB palettes, derived from assets/palette.txt via tools/palette_txt.py
# (RGB8 format -> 0-255). Each palette: 4 colors, each color is (R, G, B).
# Direct-replacement provenance per slot lives in palette_txt.build_ramps().
# NOTE: palette 4 is the mauve ROM truth from src/game/tiles_content.c (this
# file previously carried green-poison values here that disagreed with the
# ROM; `make palette-check` now forbids that drift).
FIXED_PALETTES = {
    "forest": _AUTHOR_RAMPS["forest"],
    "desolate_landscape": _AUTHOR_RAMPS["desolate_landscape"],
    "castle": _AUTHOR_RAMPS["castle"],
    "village": _AUTHOR_RAMPS["village"],
}

# Scene anchor colors (Color 0 of outdoor palettes), from palette.txt.
ANCHOR_COLORS = {
    "forest": _AUTHOR_ANCHORS["forest"],
    "desolate_landscape": _AUTHOR_ANCHORS["desolate_landscape"],
    "castle": _AUTHOR_ANCHORS["castle"],
    "village": _AUTHOR_ANCHORS["village"],
}

# Palette names for documentation in manifest (single-sourced from
# palette_txt so the manifests and assets/palettes.md can never disagree).
PALETTE_NAMES = {k: list(v) for k, v in _AUTHOR_RAMP_NAMES.items()
                 if k not in ("base", "obj")}


def rgb_to_hex(rgb: Tuple[int, int, int]) -> str:
    """Convert (R, G, B) to '#RRGGBB'."""
    return f"#{rgb[0]:02x}{rgb[1]:02x}{rgb[2]:02x}"


def color_distance(c1: Tuple[int, int, int], c2: Tuple[int, int, int]) -> float:
    """Euclidean distance in RGB space."""
    return math.sqrt(sum((a - b) ** 2 for a, b in zip(c1, c2)))


def load_tileset_json(tileset_id: str) -> Dict[str, Any]:
    """Load tileset JSON from tools/level_editor/tilesets/."""
    path = TILESETS_DIR / f"{tileset_id}.json"
    if not path.exists():
        raise FileNotFoundError(f"Tileset JSON not found: {path}")
    return json.loads(path.read_text())


def load_png(tileset_id: str) -> Image.Image:
    """Load PNG from assets/."""
    png_name = PNG_MAP[tileset_id]
    path = ASSETS_DIR / png_name
    if not path.exists():
        raise FileNotFoundError(f"PNG not found: {path}")
    img = Image.open(path).convert("RGB")
    return img


def get_tile_unique_colors(img: Image.Image, tx: int, ty: int) -> List[Tuple[int, int, int]]:
    """Get all unique RGB colors in a tile."""
    px = img.load()
    ox, oy = tx * TILE_SIZE, ty * TILE_SIZE
    unique = {px[ox + x, oy + y] for y in range(TILE_SIZE) for x in range(TILE_SIZE)}
    return list(unique)


def extract_tile_colors(img: Image.Image, tiles_x: int, tiles_y: int) -> List[List[Tuple[int, int, int]]]:
    """Extract unique colors for each tile in sheet order."""
    tile_colors = []
    for ty in range(tiles_y):
        for tx in range(tiles_x):
            tile_colors.append(get_tile_unique_colors(img, tx, ty))
    return tile_colors


def get_sheet_order_from_vram_block(tileset_json: Dict[str, Any]) -> List[str]:
    """Extract tile IDs in sheet order from vram_block.tiles[]."""
    vram_block = tileset_json.get("vram_block", {})
    tiles = vram_block.get("tiles", [])
    sorted_tiles = sorted(tiles, key=lambda t: t.get("index", 0))
    return [t["tile"] for t in sorted_tiles if "tile" in t]


def is_brown(rgb: Tuple[int, int, int]) -> bool:
    """Check if a color is brown-ish (wood/bark tones)."""
    r, g, b = rgb
    # Brown: R > G > B, with moderate saturation
    # Typical brown range: R 60-200, G 40-140, B 10-80
    return (r > g > b and
            r > 60 and g > 30 and b < 100 and
            (r - g) > 10 and (g - b) > 10)


def is_green(rgb: Tuple[int, int, int]) -> bool:
    """Check if a color is green-ish (foliage/grass)."""
    r, g, b = rgb
    return g > r and g > b and g > 40


# Anchor-only tiles fall back to these slots (single-sourced from
# palette_txt; the checker validates them against the slotmap).
DEFAULT_FLOOR_PALETTES = dict(_AUTHOR_FLOOR)


SETKEY_OF_TILESET = {
    "forest": "forest",
    "castle": "castle",
    "desolate_landscape": "desolate_landscape",
    "village": "village",
}


def match_tile_to_palette(
    tile_colors: List[Tuple[int, int, int]],
    palettes: List[List[Tuple[int, int, int]]],
    anchor_rgb: Tuple[int, int, int],
    tileset_id: str = "",
    usable: List[int] | None = None,
) -> int:
    """
    Match a tile's color set to the best fixed palette.

    For each unique color in the tile, find the closest color in each fixed
    palette (including anchor at index 0). The palette with the lowest total
    distance wins. Only `usable` slot indices are considered (padded magenta
    slots are unmatchable); the returned index is still the positional
    hardware slot.

    Special case for forest/desolate: tiles with both green (anchor-like)
    and brown colors should prefer the wood palette (index 5) which has
    harmonized Color 0 = anchor + brown foreground colors.
    """
    if usable is None:
        usable = list(range(len(palettes)))
    floor_palette_idx = DEFAULT_FLOOR_PALETTES.get(tileset_id, 0)

    # If tile only contains the scene's anchor backdrop color, assign floor palette directly
    if tile_colors and all(color_distance(c, anchor_rgb) < 5.0 for c in tile_colors):
        return floor_palette_idx

    # Detect if tile has both green and brown colors
    has_green = any(is_green(c) for c in tile_colors)
    has_brown = any(is_brown(c) for c in tile_colors)

    # For forest/desolate, wood palette is index 5
    wood_palette_idx = 5 if tileset_id in ("forest", "desolate_landscape") else -1

    best_palette = usable[0] if usable else 0
    best_total_dist = float('inf')

    for pal_idx in usable:
        palette = palettes[pal_idx]
        total_dist = 0.0
        for tile_color in tile_colors:
            # Find closest color in this palette
            min_dist = min(color_distance(tile_color, pal_color) for pal_color in palette)
            total_dist += min_dist

        # Normalize by number of tile colors
        avg_dist = total_dist / len(tile_colors) if tile_colors else float('inf')

        # Boost wood palette for mixed green/brown tiles (harmonized Color 0 case)
        if has_green and has_brown and pal_idx == wood_palette_idx:
            avg_dist *= 0.5  # Strong preference for wood palette

        # Prefer floor palette when distances tie
        if avg_dist < best_total_dist or (abs(avg_dist - best_total_dist) < 1e-4 and pal_idx == floor_palette_idx):
            best_total_dist = avg_dist
            best_palette = pal_idx

    return best_palette


def generate_manifest(
    tileset_id: str,
    anchor_rgb: Tuple[int, int, int],
    palettes: List[List[Tuple[int, int, int]]],
    tile_palettes: List[int],
    palette_names: List[str]
) -> Dict[str, Any]:
    """Generate the JSON manifest structure."""
    return {
        "tileset": tileset_id,
        "anchor_color": rgb_to_hex(anchor_rgb),
        "palettes": [
            {
                "index": i,
                "name": palette_names[i] if i < len(palette_names) else f"palette_{i}",
                "colors": [rgb_to_hex(c) for c in pal]
            }
            for i, pal in enumerate(palettes)
        ],
        "tile_palettes": tile_palettes,
    }


def write_manifest(tileset_id: str, manifest: Dict[str, Any]) -> Path:
    """Write manifest to generated/tiles/<tileset>.json."""
    GENERATED_DIR.mkdir(parents=True, exist_ok=True)
    out_path = GENERATED_DIR / f"{tileset_id}.json"
    out_path.write_text(json.dumps(manifest, indent=2))
    return out_path


def _slot_of(setkey: str, rampname: str, where: str) -> int:
    """Resolve a ramp name to its hardware slot (seeded + auto-assigned)."""
    from palette_txt import FULL_SLOTMAP as _SM
    for slot, name in sorted(_SM.get(setkey, {}).items()):
        if name == rampname:
            return slot
    raise ValueError(
        f"{where}: ramp '{rampname}' has no slot in SLOTS/{setkey.upper()} "
        f"(assign it there or fix the name)")


def process_tileset(tileset_id: str) -> Dict[str, Any]:
    """Process a single tileset: explicit per-tile ramp names to slots."""
    print(f"Processing {tileset_id}...")

    # Load inputs
    tileset_json = load_tileset_json(tileset_id)

    # Get sheet dimensions from vram_block
    vram_block = tileset_json.get("vram_block", {})
    tiles = vram_block.get("tiles", [])
    if not tiles:
        raise ValueError(f"No vram_block.tiles in {tileset_id}.json")

    # Get fixed palettes for this tileset
    palettes = FIXED_PALETTES[tileset_id]
    anchor_hex = ANCHOR_COLORS[tileset_id]
    anchor_rgb = tuple(int(anchor_hex.lstrip('#')[i:i+2], 16) for i in (0, 2, 4))
    print(f"  Anchor color: {anchor_hex} -> RGB{anchor_rgb}")

    setkey = SETKEY_OF_TILESET.get(tileset_id, tileset_id)
    sheet_ids = get_sheet_order_from_vram_block(tileset_json)
    tiles_by_id = {t.get("id"): t for t in tileset_json.get("tiles", [])}

    # Every vram tile names its ramp explicitly ("palette" in the tileset
    # JSON, set by the editor's Palette view). Names resolve through the
    # slotmap to positional hardware slots.
    tile_palettes = []
    for i in range(len(tiles)):
        tid = sheet_ids[i] if i < len(sheet_ids) else None
        rampname = tiles_by_id.get(tid, {}).get("palette") if tid else None
        if rampname is None:
            raise ValueError(
                f"{tileset_id}: tile {tid} (sheet {i}) has no explicit "
                f"\"palette\" ramp name — assign one in "
                f"tools/level_editor/tilesets/{tileset_id}.json (or run "
                f"palette_compiler.py --suggest for a proposal)")
        if isinstance(rampname, int) or str(rampname).isdigit():
            raise ValueError(
                f"{tileset_id}: tile {tid} palette {rampname!r} is numeric "
                f"— use a ramp name (see SLOTS/{setkey.upper()})")
        pal_idx = _slot_of(setkey, rampname,
                           f"{tileset_id}: tile {tid}")
        tile_palettes.append(pal_idx)

    print(f"  Tile palette assignments: {tile_palettes}")

    # Indexed sheets ("indexed": true in the tileset JSON): emit the strict
    # shade sidecar consumed by png2gb.py --shade-map, and validate every
    # tile's pixels against its slot colors up front (exact hex match, no
    # luminance guessing). Anything off-ramp fails here with tile + color.
    if tileset_json.get("indexed"):
        img = load_png(tileset_id)
        px = img.load()
        sidecar = {}
        for entry in tiles:
            tx, ty = entry.get("x", 0), entry.get("y", 0)
            idx = entry.get("index")
            slot = tile_palettes[idx] if idx is not None and idx < len(tile_palettes) else None
            if slot is None:
                raise ValueError(f"{tileset_id}: vram entry {entry} has no slot")
            ramp = [rgb_to_hex(c) for c in palettes[slot]]
            used = sorted({"#%02x%02x%02x" % px[tx * TILE_SIZE + x, ty * TILE_SIZE + y]
                           for y in range(TILE_SIZE) for x in range(TILE_SIZE)})
            outside = [c for c in used if c not in ramp]
            if outside:
                raise ValueError(
                    f"{tileset_id}: tile {entry.get('tile')} at ({tx},{ty}) uses "
                    f"{', '.join(outside)}, outside its {PALETTE_NAMES[tileset_id][slot]} "
                    f"ramp {ramp} -- repaint the pixels or repoint the tile's "
                    f"\"palette\" in tools/level_editor/tilesets/{tileset_id}.json")
            if len(used) > 4:
                raise ValueError(
                    f"{tileset_id}: tile {entry.get('tile')} at ({tx},{ty}) uses "
                    f"{len(used)} colors {used} -- Game Boy tiles hold 4 max")
            sidecar[f"{tx},{ty}"] = {c: ramp.index(c) for c in used}
        sidecar_path = GENERATED_DIR / f"{tileset_id}_shades.json"
        sidecar_path.write_text(json.dumps(sidecar, indent=2))
        print(f"  Wrote shade sidecar: {sidecar_path} ({len(sidecar)} tiles)")

    # Generate manifest
    manifest = generate_manifest(
        tileset_id, anchor_rgb, palettes, tile_palettes, PALETTE_NAMES[tileset_id]
    )

    # Write output
    out_path = write_manifest(tileset_id, manifest)
    print(f"  Wrote manifest: {out_path}")

    return manifest


def suggest_tileset(tileset_id: str) -> None:
    """Proposal tool for new art: nearest ramp per tile (writes nothing)."""
    from palette_txt import REAL_SLOTS as _REAL
    tileset_json = load_tileset_json(tileset_id)
    img = load_png(tileset_id)
    vram_block = tileset_json.get("vram_block", {})
    tiles = vram_block.get("tiles", [])
    max_x = max(t.get("x", 0) for t in tiles)
    max_y = max(t.get("y", 0) for t in tiles)
    tile_colors_list = extract_tile_colors(img, max_x + 1, max_y + 1)
    palettes = FIXED_PALETTES[tileset_id]
    anchor_hex = ANCHOR_COLORS[tileset_id]
    anchor_rgb = tuple(int(anchor_hex.lstrip('#')[i:i+2], 16) for i in (0, 2, 4))
    setkey = SETKEY_OF_TILESET.get(tileset_id, tileset_id)
    usable = _REAL.get(setkey, list(range(len(palettes))))
    names = PALETTE_NAMES[tileset_id]
    sheet_ids = get_sheet_order_from_vram_block(tileset_json)
    for i, tile_colors in enumerate(tile_colors_list):
        idx = match_tile_to_palette(tile_colors, palettes, anchor_rgb,
                                    tileset_id, usable)
        print(f"  sheet {i} ({sheet_ids[i] if i < len(sheet_ids) else '?'}): "
              f"suggest {names[idx]} (slot {idx})")


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--tilesets", nargs="+", default=TILESETS,
                        help="Tileset IDs to process (default: all)")
    parser.add_argument("--out-dir", type=Path, default=GENERATED_DIR,
                        help="Output directory for manifests")
    parser.add_argument("--suggest", action="store_true",
                        help="print nearest-ramp proposals, write nothing")
    args = parser.parse_args()

    for ts_id in args.tilesets:
        if ts_id not in FIXED_PALETTES:
            print(f"Unknown tileset: {ts_id} (no fixed palettes defined)", file=sys.stderr)
            sys.exit(1)
        try:
            if args.suggest:
                suggest_tileset(ts_id)
            else:
                process_tileset(ts_id)
        except Exception as e:
            print(f"Error processing {ts_id}: {e}", file=sys.stderr)
            import traceback
            traceback.print_exc()
            sys.exit(1)

    if not args.suggest:
        # OBJ ramp catalog for the level editor (index/name/colors).
        from palette_txt import OBJ_BY_SLOT, _NAMES as _OBJ_NAMES
        obj = {"ramps": [
            {"index": i, "name": _OBJ_NAMES["obj"][i],
             "colors": [rgb_to_hex(c) for c in OBJ_BY_SLOT[i]]}
            for i in range(len(OBJ_BY_SLOT))]}
        GENERATED_DIR.mkdir(parents=True, exist_ok=True)
        (GENERATED_DIR / "obj_ramps.json").write_text(
            json.dumps(obj, indent=2))
        print("  Wrote manifest: %s" % (GENERATED_DIR / "obj_ramps.json"))

    print("Done.")


if __name__ == "__main__":
    main()