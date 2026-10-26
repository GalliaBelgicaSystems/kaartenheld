#!/usr/bin/env python3
"""Import a tileset from a source PNG sheet + CSV description file.

Reads a PNG of 8x8 tiles (e.g., 128x24 = 16x3, or 96x16 = 12x2) and a matching CSV
with one label per cell.  Produces:
  - individual tile PNGs in --output-dir
  - a tileset JSON at --output-json

Usage (inside nix develop for Pillow):
    python3 tools/level_editor/import_tileset.py \
      --sheet assets/forest-tile.png \
      --csv assets/forest-tileset-description.csv \
      --tileset-id forest \
      --label "Whispering Forest" \
      --gb-tileset-kind WORLD_TILESET_FOREST \
      --output-dir tools/level_editor/public/tiles/forest \
      --output-json tools/level_editor/tilesets/forest.json

NOTE: world tilesets only (forest, desolate_landscape, castle, village).
The actors sheet (assets/actor-sprites.png) was removed; the actors tileset
is curated-source now and its boss cells reload from assets/sprites.png via
tools/level_editor/reload_boss_tiles.py. Combat/enemy tilesets were never
covered by this importer (hand-curated, same reload script).
"""
import argparse
import csv
import io
import json
import os
import re
import sys
from collections import Counter

from PIL import Image

TILE_SIZE = 8


def slugify(text):
    """Convert description text to a lowercase underscored slug."""
    text = text.strip().lower()
    text = re.sub(r"[^a-z0-9]+", "_", text)
    text = text.strip("_")
    return text


def infer_walkable(description):
    # Only genuine terrain counts as walkable.  Sprite-sheet art that shares
    # the bank (hero/npc/merchant/enemy frames) must never be walkable: the
    # ROM renders those tiles as solid and blocks movement onto them, so a
    # walkable:true flag here would make host collision disagree with the
    # game (see world_is_walkable).
    d = description.lower()
    if "walkable" in d or "floor" in d or "exit" in d:
        return True
    return False


def infer_category(description):
    d = description.lower()
    if "hero" in d:
        return "hero"
    if "wall" in d:
        return "wall"
    if "tree" in d or "stump" in d or "rock" in d or "treetop" in d or "treetrunk" in d:
        return "nature"
    if "kobold" in d or "slime" in d or "bats" in d or "spider" in d or "boss" in d or "mimic" in d:
        return "enemy"
    if "guard" in d or "wizard" in d or "merchant" in d or "mayor" in d or "dog" in d:
        return "npc"
    if "fire" in d or "chest" in d:
        return "object"
    if "exit" in d:
        return "terrain"
    if "floor" in d:
        return "terrain"
    if "enemy" in d:
        return "enemy"
    if "merchant" in d:
        return "npc"
    if "empty" in d:
        return "object"
    if "black" in d and "emptyness" in d:
        return "object"
    return "terrain"


def ascii_char(walkable):
    return "." if walkable else "#"


# Transparency key color for sprite/actor sheets: the artist paints the
# background behind every actor with this exact yellow; those pixels become
# see-through (Game Boy sprite shade 0).  Chroma-keyed by RGB, independent
# of which palette index the color lands on.
KEY_TRANSPARENT_RGB = (241, 235, 3)


def crop_tile_with_transparency(source_img, left, upper, right, lower):
    """Crop a tile from a palette image, chroma-keying the yellow
    transparency color (KEY_TRANSPARENT_RGB) to alpha=0."""
    # Crop in palette mode to preserve palette indices
    tile_p = source_img.crop((left, upper, right, lower))
    palette = source_img.palette.palette if source_img.mode == 'P' and source_img.palette else None

    if palette is None:
        # RGBA source: key the yellow RGB directly
        rgba = tile_p.convert("RGBA")
        px = rgba.load()
        for yy in range(TILE_SIZE):
            for xx in range(TILE_SIZE):
                r, g, b, a = px[xx, yy]
                if a > 0 and (r, g, b) == KEY_TRANSPARENT_RGB:
                    px[xx, yy] = (0, 0, 0, 0)
        return rgba

    # Palette source: map each palette index through its RGB.
    pixels = list(tile_p.getdata())
    new_pixels = []
    for idx in pixels:
        i3 = idx * 3
        if i3 + 2 < len(palette):
            r, g, b = palette[i3], palette[i3 + 1], palette[i3 + 2]
            if (r, g, b) == KEY_TRANSPARENT_RGB:
                new_pixels.append((0, 0, 0, 0))
            else:
                new_pixels.append((r, g, b, 255))
        else:
            new_pixels.append((0, 0, 0, 255))
    tile_rgba = Image.new("RGBA", (TILE_SIZE, TILE_SIZE))
    tile_rgba.putdata(new_pixels)
    return tile_rgba


def main():
    parser = argparse.ArgumentParser(description="Import tileset from PNG + CSV")
    parser.add_argument("--sheet", required=True, help="Source PNG (e.g., 128x24 or 96x16)")
    parser.add_argument("--csv", required=True, help="Description CSV")
    parser.add_argument("--tileset-id", required=True, help="Tileset id (e.g. forest)")
    parser.add_argument("--label", required=True, help="Human label (e.g. Whispering Forest)")
    parser.add_argument("--gb-tileset-kind", required=True, help="GB constant (e.g. WORLD_TILESET_FOREST)")
    parser.add_argument("--output-dir", required=True, help="Directory for tile PNGs")
    parser.add_argument("--output-json", required=True, help="Output tileset JSON path")
    args = parser.parse_args()

    sheet_path = args.sheet

    # Open in palette mode to preserve palette indices for the yellow key
    source_img = Image.open(sheet_path)
    if source_img.mode != 'P':
        source_img = source_img.convert("P")

    # Open as RGBA for color analysis
    img_rgba = Image.open(sheet_path).convert("RGBA")
    w, h = source_img.size
    cols = source_img.width // TILE_SIZE
    rows = source_img.height // TILE_SIZE
    print(f"Sheet: {source_img.width}x{source_img.height} = {cols}x{rows} tiles")

    with open(args.csv, newline="") as f:
        reader = csv.reader(io.StringIO(f.read()))
        csv_rows = list(reader)

    assert len(csv_rows) == rows, f"Expected {rows} CSV rows, got {len(csv_rows)}"
    for i, row in enumerate(csv_rows):
        assert len(row) == cols, f"Row {i}: expected {cols} cols, got {len(row)}"

    os.makedirs(args.output_dir, exist_ok=True)

    # Build tile list: (tile_id, description, col, row)
    raw_tiles = []
    for r in range(rows):
        for c in range(cols):
            desc = csv_rows[r][c].strip()
            slug = slugify(desc)
            raw_tiles.append((slug, desc, c, r))

    # Deduplicate: count how many times each slug appears
    slug_counts = Counter(t[0] for t in raw_tiles)
    slug_seen = Counter()

    tiles = []
    for slug, desc, c, r in raw_tiles:
        if slug_counts[slug] > 1:
            slug_seen[slug] += 1
            tile_id = f"{args.tileset_id}_{slug}_{slug_seen[slug]}"
        else:
            tile_id = f"{args.tileset_id}_{slug}"

        # Extract 8x8 tile from sheet with transparency
        left = c * TILE_SIZE
        upper = r * TILE_SIZE
        right = left + TILE_SIZE
        lower = upper + TILE_SIZE

        # Crop tile with transparency (yellow chroma-key)
        tile_rgba = crop_tile_with_transparency(source_img, left, upper, right, lower)

        # Handle special "empty" description -> black_emptyness
        if desc.lower().strip() == "empty":
            tile_id = f"{args.tileset_id}_black_emptyness_{slug_seen[slug]}"
            tile_rgba = Image.new("RGBA", (TILE_SIZE, TILE_SIZE), (0, 0, 0, 255))
            color = "#000000"
            walkable = False
            category = "object"
            slug = "black_emptyness"
            gb_const = f"TILE_{args.tileset_id.upper()}_BLACK_EMPTYNESS"
        else:
            # Determine dominant color for the fallback swatch
            pixels = list(tile_rgba.getdata())
            opaque = [p for p in pixels if p[3] > 0]
            if opaque:
                r_avg = sum(p[0] for p in opaque) // len(opaque)
                g_avg = sum(p[1] for p in opaque) // len(opaque)
                b_avg = sum(p[2] for p in opaque) // len(opaque)
                color = f"#{r_avg:02x}{g_avg:02x}{b_avg:02x}"
            else:
                color = "#000000"

            walkable = infer_walkable(desc)
            category = infer_category(desc)
            gb_const = f"TILE_{args.tileset_id.upper()}_{slug.upper()}"

        png_name = f"{tile_id}.png"
        tile_rgba.save(os.path.join(args.output_dir, png_name))

        tiles.append({
            "id": tile_id,
            "label": desc.strip(),
            "gb_constant": gb_const,
            "walkable": walkable,
            "color": color,
            "ascii": ascii_char(walkable),
            "image_url": f"/tiles/{args.tileset_id}/{png_name}",
            "category": category,
        })

    tileset = {
        "id": args.tileset_id,
        "label": args.label,
        "gb_tileset_kind": args.gb_tileset_kind,
        "tiles": tiles,
    }

    with open(args.output_json, "w") as f:
        json.dump(tileset, f, indent=2)

    print(f"Wrote {len(tiles)} tiles to {args.output_dir}/")
    print(f"Wrote {args.output_json}")

    # Print summary of ID collisions resolved
    if any(v > 1 for v in slug_counts.values()):
        print("\nDeduplicated slugs:")
        for slug, count in slug_counts.items():
            if count > 1:
                print(f"  '{slug}' x{count}")


if __name__ == "__main__":
    main()