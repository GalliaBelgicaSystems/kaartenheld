#!/usr/bin/env python3
"""Slice assets/menus-tile.png into editor tiles + tilesets/menus.json.

Source of truth: the artist sheet (assets/menus-tile.png, 128x32 = 16x4
cells of 8x8) plus the artist label list
(assets/menu-tile-description.csv, 13 names).

Cell map (positional convention, documented assumption -- the CSV order
is NOT sheet scan order):
  CSV[0]  "bottom box border" -> the 4x4 frame kit at cols 12-15, rows 0-3
                              (14 drawn cells; (15,0) and (12,3) are blank
                              corners and stay white)
  CSV[1..12] -> row-0 single cells cols 0-11, in order
                (inventory separator, inventory type arrow, cursor category,
                 category border, category corner, category separator,
                 list scroll arrow, quests separator, health points icon,
                 action point icon, cards icon, huge character)

Outputs (committed, deterministic):
  tools/level_editor/public/tiles/menus/menus_<slug>.png (12 x 8x8 plus
      menus_bottom_box_border.png, the 32x32 assembled frame)
  tools/level_editor/tilesets/menus.json (flat curated manifest, same entry
      shape as tilesets/combat.json; no vram_block, so world generators
      and tiles-check skip it like the other UI sets)

Usage (inside nix develop for Pillow):
    python3 tools/level_editor/slice_menus_tile.py
"""

import csv
import json
import os
import sys

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from import_tileset import slugify

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))
SHEET = os.path.join(REPO_ROOT, "assets", "menus-tile.png")
CSV = os.path.join(REPO_ROOT, "assets", "menu-tile-description.csv")
PNG_DIR = os.path.join(REPO_ROOT, "tools", "level_editor", "public",
                       "tiles", "menus")
JSON_PATH = os.path.join(REPO_ROOT, "tools", "level_editor", "tilesets",
                         "menus.json")

TILE = 8
EXPECT_W, EXPECT_H = 128, 32

# (col, row) cells of the bottom-box frame kit (bounding cols 12-15,
# rows 0-3 minus the two blank corners).
BOX_CELLS = [(12, 0), (13, 0), (14, 0),
             (12, 1), (13, 1), (14, 1), (15, 1),
             (12, 2), (13, 2), (14, 2), (15, 2),
             (13, 3), (14, 3), (15, 3)]
BOX_BLANK = [(15, 0), (12, 3)]
BOX_BOUNDS = (12, 0, 16, 4)  # cols 12-15, rows 0-3 (exclusive end)


def cell_drawn(img, c, r):
    tile = img.crop((c * TILE, r * TILE, (c + 1) * TILE, (r + 1) * TILE))
    return len(tile.getcolors(maxcolors=65) or []) > 1


def mean_color(img, box=None):
    region = img.crop(box) if box else img
    px = [p for p in region.getdata() if p[:3] != (255, 255, 255)]
    if not px:
        return "#ffffff"
    n = len(px)
    return "#%02x%02x%02x" % (sum(p[0] for p in px) // n,
                              sum(p[1] for p in px) // n,
                              sum(p[2] for p in px) // n)


def main():
    img = Image.open(SHEET).convert("RGB")
    if img.size != (EXPECT_W, EXPECT_H):
        raise SystemExit(
            "menus-tile.png is %dx%d, want %dx%d" % (
                img.size[0], img.size[1], EXPECT_W, EXPECT_H))
    with open(CSV, newline="") as f:
        rows = [r for r in csv.reader(f) if any(c.strip() for c in r)]
    if len(rows) != 1 or len(rows[0]) != 13:
        raise SystemExit(
            "menu-tile-description.csv: want 1 row of 13 names, got %r"
            % ([len(r) for r in rows],))
    names = [c.strip() for c in rows[0]]
    if len(set(names)) != 13:
        raise SystemExit("menu-tile-description.csv: names must be unique")

    singles = [(c, 0) for c in range(12)]
    for c, r in singles:
        if not cell_drawn(img, c, r):
            raise SystemExit(
                "expected drawn single at (%d,%d) for %r" % (c, r, names[c + 1]))
    for c, r in BOX_CELLS:
        if not cell_drawn(img, c, r):
            raise SystemExit("expected drawn box cell at (%d,%d)" % (c, r))
    for c, r in BOX_BLANK:
        if cell_drawn(img, c, r):
            raise SystemExit(
                "expected blank box corner at (%d,%d)" % (c, r))

    os.makedirs(PNG_DIR, exist_ok=True)
    tiles = []

    def add_tile(tid, label, png_name, png_img, color):
        png_img.save(os.path.join(PNG_DIR, png_name))
        tiles.append({
            "id": tid,
            "label": label,
            "gb_constant": "TILE_MENUS_" + slugify(label).upper(),
            "walkable": False,
            "color": color,
            "ascii": "#",
            "image_url": "/tiles/menus/" + png_name,
            "category": "ui",
        })

    for i, (c, r) in enumerate(singles):
        label = names[i + 1]
        tid = "menus_" + slugify(label)
        cell = img.crop((c * TILE, r * TILE, (c + 1) * TILE, (r + 1) * TILE))
        add_tile(tid, label, tid + ".png", cell,
                 mean_color(img, (c * TILE, r * TILE,
                                  (c + 1) * TILE, (r + 1) * TILE)))

    box_label = names[0]
    box_tid = "menus_" + slugify(box_label)
    x0, y0, x1, y1 = BOX_BOUNDS
    box = img.crop((x0 * TILE, y0 * TILE, x1 * TILE, y1 * TILE))
    add_tile(box_tid, box_label, box_tid + ".png", box,
             mean_color(img, (x0 * TILE, y0 * TILE, x1 * TILE, y1 * TILE)))

    manifest = {
        "id": "menus",
        "label": "Menu Chrome",
        "gb_tileset_kind": "WORLD_TILESET_FOREST",
        "tiles": tiles,
    }
    with open(JSON_PATH, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")
    print("wrote %d tiles -> %s" % (len(tiles), PNG_DIR))
    print("wrote %s" % JSON_PATH)


if __name__ == "__main__":
    main()
