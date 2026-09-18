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
Ramp assignment is always explicit (no color-distance guessing anywhere).
"""

import sys
import json
import argparse
from pathlib import Path
from typing import List, Tuple, Dict, Any, Optional
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
from palette_txt import (RAMPS as _AUTHOR_RAMPS, ANCHORS as _AUTHOR_ANCHORS,
                         RAMP_NAMES as _AUTHOR_RAMP_NAMES,
                         FULL_SLOTMAP as _FULL_SLOTMAP)

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


SETKEY_OF_TILESET = {
    "forest": "forest",
    "castle": "castle",
    "desolate_landscape": "desolate_landscape",
    "village": "village",
}


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
                f"tools/level_editor/tilesets/{tileset_id}.json")
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
        max_x = img.size[0] // TILE_SIZE
        max_y = img.size[1] // TILE_SIZE
        vram_by_coord = {(e.get("x", 0), e.get("y", 0)): e.get("tile")
                         for e in tiles}
        sidecar = {}
        for ty in range(max_y):
            for tx in range(max_x):
                tid = vram_by_coord.get((tx, ty))
                if tid is not None:
                    rampname = tiles_by_id.get(tid, {}).get("palette")
                    if rampname is None:
                        raise ValueError(
                            f"{tileset_id}: tile {tid} has no explicit "
                            f"\"palette\" ramp name")
                    slot = _slot_of(setkey, rampname,
                                    f"{tileset_id}: tile {tid}")
                else:
                    # Spare cell outside vram_block: no guessing. Every ROM
                    # cell must be covered by vram_block with an explicit
                    # "palette" ramp name -- extend vram_block.
                    used = sorted({"#%02x%02x%02x" % px[tx * TILE_SIZE + x, ty * TILE_SIZE + y]
                                   for y in range(TILE_SIZE) for x in range(TILE_SIZE)})
                    raise ValueError(
                        f"{tileset_id}: spare cell ({tx},{ty}) uses "
                        f"{used} but is outside vram_block -- add it to "
                        f"vram_block in tools/level_editor/tilesets/{tileset_id}.json "
                        f"with an explicit \"palette\" ramp name")
                ramp = [rgb_to_hex(c) for c in palettes[slot]]
                used = sorted({"#%02x%02x%02x" % px[tx * TILE_SIZE + x, ty * TILE_SIZE + y]
                               for y in range(TILE_SIZE) for x in range(TILE_SIZE)})
                outside = [c for c in used if c not in ramp]
                if outside:
                    raise ValueError(
                        f"{tileset_id}: tile {tid or 'spare'} at ({tx},{ty}) uses "
                        f"{', '.join(outside)}, outside its {PALETTE_NAMES[tileset_id][slot]} "
                        f"ramp {ramp} -- repaint the pixels or repoint the tile's "
                        f"\"palette\" in tools/level_editor/tilesets/{tileset_id}.json")
                if len(used) > 4:
                    raise ValueError(
                        f"{tileset_id}: tile {tid or 'spare'} at ({tx},{ty}) uses "
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


def fit_check_tileset(tileset_id: str) -> int:
    """Exact-fit reporter: for every vram-mapped tile, list the set ramps
    whose colors are a superset of the tile's pixels (or NONE -- the tile
    needs repainting or a new ramp). Writes nothing; returns misfit count.
    This is the anti-guesswork gate: every tile that ships indexed must
    fit its assigned ramp exactly (see also the indexed strict check in
    process_tileset)."""
    from palette_txt import REAL_SLOTS as _REAL
    tileset_json = load_tileset_json(tileset_id)
    img = load_png(tileset_id)
    vram_block = tileset_json.get("vram_block", {})
    tiles = vram_block.get("tiles", [])
    max_x = max(t.get("x", 0) for t in tiles)
    max_y = max(t.get("y", 0) for t in tiles)
    tile_colors_list = extract_tile_colors(img, max_x + 1, max_y + 1)
    palettes = FIXED_PALETTES[tileset_id]
    names = PALETTE_NAMES[tileset_id]
    setkey = SETKEY_OF_TILESET.get(tileset_id, tileset_id)
    usable = _REAL.get(setkey, list(range(len(palettes))))
    sheet_ids = get_sheet_order_from_vram_block(tileset_json)
    tiles_by_id = {t.get("id"): t for t in tileset_json.get("tiles", [])}
    misfits = 0
    for i, tile_colors in enumerate(tile_colors_list):
        hexes = {rgb_to_hex(c) for c in tile_colors}
        fits = [names[s] for s in usable
                if hexes <= set([rgb_to_hex(c) for c in palettes[s]])]
        tid = sheet_ids[i] if i < len(sheet_ids) else "?@%d" % i
        current = tiles_by_id.get(tid, {}).get("palette")
        if not fits:
            misfits += 1
            print(f"  sheet {i} ({tid}, now {current}): fits NO ramp -- "
                  f"repaint into {sorted(hexes)} or add a ramp")
        elif current not in fits:
            misfits += 1
            print(f"  sheet {i} ({tid}, now {current}): fits {fits}")
    if misfits == 0:
        print(f"  {tileset_id}: all {len(tile_colors_list)} tiles exact-fit")
    return misfits


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--tilesets", nargs="+", default=TILESETS,
                        help="Tileset IDs to process (default: all)")
    parser.add_argument("--out-dir", type=Path, default=GENERATED_DIR,
                        help="Output directory for manifests")
    parser.add_argument("--fit-check", action="store_true",
                        help="exact-fit report per tile (ramp it fits, or "
                             "NONE with its colors); exits non-zero on any "
                             "misfit. Writes nothing.")
    args = parser.parse_args()

    if args.fit_check:
        bad = 0
        for ts_id in args.tilesets:
            if ts_id not in FIXED_PALETTES:
                print(f"Unknown tileset: {ts_id} (no fixed palettes defined)", file=sys.stderr)
                sys.exit(1)
            try:
                bad += fit_check_tileset(ts_id)
            except Exception as e:
                print(f"Error processing {ts_id}: {e}", file=sys.stderr)
                import traceback
                traceback.print_exc()
                sys.exit(1)
        if bad:
            print(f"fit-check: {bad} misfit tile(s)", file=sys.stderr)
            sys.exit(1)
        print("fit-check: all tiles exact-fit")
        return

    for ts_id in args.tilesets:
        if ts_id not in FIXED_PALETTES:
            print(f"Unknown tileset: {ts_id} (no fixed palettes defined)", file=sys.stderr)
            sys.exit(1)
        try:
                process_tileset(ts_id)
        except Exception as e:
            print(f"Error processing {ts_id}: {e}", file=sys.stderr)
            import traceback
            traceback.print_exc()
            sys.exit(1)

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