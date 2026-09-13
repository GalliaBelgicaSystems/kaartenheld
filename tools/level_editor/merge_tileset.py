#!/usr/bin/env python3
"""Surgically merge an updated sheet + CSV descriptions into a curated
tileset manifest.

A raw import_tileset.py run would destroy hand curation (numeric
TILE_DESOLATE_LANDSCAPE_* constants wired to world.h, shared forest
constants, curated walkable flags). This instead recomputes every cell
and applies changes ONLY at cells whose CSV description differs from
the manifest label, preserving every curated field elsewhere.

Two manifest models are supported:

- Flat manifests (no ``vram_block``): tiles[] maps 1:1 to sheet cells in
  scan order; the diff is positional.
- VRAM-block manifests (forest, desolate_landscape, castle, village):
  tiles[] holds unique defs and ``vram_block.tiles`` places them on sheet
  cells (several cells may share one def, e.g. repeated plain floors).
  The diff is per placement: a drifted cell reuses the def whose label
  matches the new description when one exists, otherwise a new
  ``<tileset>_<slug>`` def is appended.

Rules shared by both models:

- index-derived tilesets (every gb_constant matches TILE_<ID>_<nn>):
  gb_constant stays positional (TILE_<ID>_<cell>) so the world.h/VRAM 1:1
  mapping holds.  Rendering is positional (ui.c draws slot
  ``t - TILE_<ID>_00``), so a reused def takes the constant of the first
  cell referencing it (e.g. plain floor moving slots keeps floor art
  under floor cells).
- other tilesets: gb_constant follows generator naming for new cells.
- walkable/ascii/category follow the current infer_* rules, so manifest
  and importer can never silently disagree again -- except exit-marked
  placements, which force walkable=true (validate_tilesets.py requires
  the single exit:true tile to be walkable; inference knows nothing of
  exits, e.g. "stairs").
- tile PNGs are (re-)extracted only for changed cells.
- defs left unreferenced by every placement are reported, never deleted
  (they may still be referenced by level JSON sprite fields).

Usage (inside nix develop for Pillow):
    python3 tools/level_editor/merge_tileset.py \
      --sheet assets/forest-tile.png \
      --csv assets/forest-tileset-description.csv \
      --tileset-json tools/level_editor/tilesets/forest.json \
      --png-dir tools/level_editor/public/tiles/forest

Exits nonzero on dimension mismatch; prints the full cell diff.
"""
import argparse
import csv
import io
import json
import os
import re
import sys

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from import_tileset import slugify, infer_walkable, infer_category, ascii_char

TILE_SIZE = 8


def uses_index_constants(manifest):
    """True when every def's gb_constant is positional (TILE_<ID>_<nn>)."""
    pat = re.compile(r"TILE_%s_\d+$" % re.escape(manifest["id"].upper()))
    return all(pat.fullmatch(t.get("gb_constant", "")) for t in manifest.get("tiles", []))


def cell_color(img, c, r):
    tile_img = img.crop((c * TILE_SIZE, r * TILE_SIZE,
                         c * TILE_SIZE + TILE_SIZE, r * TILE_SIZE + TILE_SIZE))
    opaque = [p for p in tile_img.getdata() if p[3] > 0]
    if opaque:
        return tile_img, "#%02x%02x%02x" % (
            sum(p[0] for p in opaque) // len(opaque),
            sum(p[1] for p in opaque) // len(opaque),
            sum(p[2] for p in opaque) // len(opaque))
    return tile_img, "#000000"


def merge_flat(args, manifest, csv_rows, cols, rows, img):
    """Legacy path: tiles[] maps 1:1 to sheet cells in scan order."""
    tileset_id = manifest["id"]
    tiles = manifest["tiles"]
    assert len(tiles) == cols * rows, \
        f"manifest has {len(tiles)} tiles, sheet has {cols * rows}"

    os.makedirs(args.png_dir, exist_ok=True)
    changed = []
    for r in range(rows):
        for c in range(cols):
            idx = r * cols + c
            desc = csv_rows[r][c].strip()
            entry = tiles[idx]
            if entry.get("label", "").strip() == desc:
                continue
            slug = slugify(desc)
            tile_id = f"{tileset_id}_{slug}"
            if tileset_id == "desolate_landscape":
                gb_const = f"TILE_DESOLATE_LANDSCAPE_{idx:02d}"
            else:
                gb_const = f"TILE_{tileset_id.upper()}_{slug.upper()}"
            walkable = infer_walkable(desc)
            tile_img, color = cell_color(img, c, r)
            png_name = f"{tile_id}.png"
            tile_img.save(os.path.join(args.png_dir, png_name))
            old_id = entry.get("id")
            entry.update({
                "id": tile_id,
                "label": desc.strip(),
                "gb_constant": gb_const,
                "walkable": walkable,
                "color": color,
                "ascii": ascii_char(walkable),
                "image_url": f"/tiles/{tileset_id}/{png_name}",
                "category": infer_category(desc),
            })
            changed.append((idx, old_id, tile_id, walkable))
    return changed


def merge_vram(args, manifest, csv_rows, cols, rows, img):
    """VRAM-block path: diff per placement, reuse defs by label match."""
    tileset_id = manifest["id"]
    tiles = manifest["tiles"]
    block = manifest["vram_block"]["tiles"]
    assert len(block) == cols * rows, \
        f"vram block has {len(block)} entries, sheet has {cols * rows}"
    index_consts = uses_index_constants(manifest)

    os.makedirs(args.png_dir, exist_ok=True)
    changed = []
    for idx, entry in enumerate(block):
        r, c = idx // cols, idx % cols
        desc = csv_rows[r][c].strip()
        cur = next((t for t in tiles if t["id"] == entry.get("tile")), None)
        if cur is not None and cur.get("label", "").strip() == desc:
            continue
        # Reuse the def whose label already matches, else append a new one.
        tile = next((t for t in tiles if t.get("label", "").strip() == desc), None)
        if tile is None:
            base = f"{tileset_id}_{slugify(desc)}"
            tile_id = base
            n = 2
            taken = {t["id"] for t in tiles}
            while tile_id in taken:
                tile_id = f"{base}_{n}"
                n += 1
            tile = {"id": tile_id}
            tiles.append(tile)
        tile_id = tile["id"]
        if index_consts:
            first = min(i for i, e in enumerate(block)
                        if e.get("tile") == tile_id or i == idx)
            gb_const = f"TILE_{tileset_id.upper()}_{first:02d}"
        else:
            gb_const = f"TILE_{tileset_id.upper()}_{slugify(desc).upper()}"
        walkable = infer_walkable(desc)
        if entry.get("exit"):
            walkable = True  # exit art must stay walkable (validate gate)
        tile_img, color = cell_color(img, c, r)
        png_name = f"{tile_id}.png"
        tile_img.save(os.path.join(args.png_dir, png_name))
        old_id = entry.get("tile")
        tile.update({
            "id": tile_id,
            "label": desc.strip(),
            "gb_constant": gb_const,
            "walkable": walkable,
            "color": color,
            "ascii": ascii_char(walkable),
            "image_url": f"/tiles/{tileset_id}/{png_name}",
            "category": infer_category(desc),
        })
        entry["tile"] = tile_id
        changed.append((idx, old_id, tile_id, walkable))

    referenced = {e.get("tile") for e in block}
    for t in tiles:
        if t["id"] not in referenced:
            print(f"warning: tile '{t['id']}' unreferenced by vram_block "
                  f"(kept; may still be used by level sprite fields)")
    return changed


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--sheet", required=True)
    parser.add_argument("--csv", required=True)
    parser.add_argument("--tileset-json", required=True)
    parser.add_argument("--png-dir", required=True)
    args = parser.parse_args()

    img = Image.open(args.sheet).convert("RGBA")
    cols = img.size[0] // TILE_SIZE
    rows = img.size[1] // TILE_SIZE

    with open(args.csv, newline="") as f:
        csv_rows = list(csv.reader(io.StringIO(f.read())))
    assert len(csv_rows) == rows, f"CSV rows {len(csv_rows)} != sheet rows {rows}"
    for i, row in enumerate(csv_rows):
        assert len(row) == cols, f"CSV row {i} cols {len(row)} != {cols}"

    with open(args.tileset_json, encoding="utf-8") as f:
        manifest = json.load(f)

    if "vram_block" in manifest:
        changed = merge_vram(args, manifest, csv_rows, cols, rows, img)
    else:
        changed = merge_flat(args, manifest, csv_rows, cols, rows, img)

    with open(args.tileset_json, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")

    if not changed:
        print("merge: no cell changes; manifest already in sync.")
        return 0
    print(f"merge: {len(changed)} changed cell(s):")
    for idx, old_id, new_id, walkable in changed:
        print(f"  [{idx:2d}] {old_id} -> {new_id} "
              f"({'walkable' if walkable else 'solid'})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
