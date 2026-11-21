#!/usr/bin/env python3
"""Slice assets/menus-tile.png into editor tiles + tilesets/menus.json.

Source of truth: the artist sheet (assets/menus-tile.png, 128x32 = 16x4
cells of 8x8) plus the artist label grid
(assets/menu-tile-description.csv, 4 rows x 16 cols, quoted).

Cell map (positional: CSV row/col == sheet cell):
  Row 0, cols 0-11: the 12 single-cell UI icons (scan order:
      bottom box border, inventory separator, inventory type arrow,
      cursor category, category border, category corner, category
      separator, list scroll arrow, quests separator, health points
      icon, action point icon, cards icon).
  Row 1, cols 0-3: the 4 status-bar icons (scan order: full heart,
      half heart, AP, arrow) for the battle status bar.
  Row 1, cols 4-10: the 7 single-cell button icons (scan order:
      a button, b button, start 1, start 2, start 3,
      dpad up-down, dpad left-right).
  Row 2, cols 0-2: the 3 select button cells (select 1-3).
  Cols 12-15, rows 0-3: the 4x4 hero-status portrait ("hero status RC":
      row digit + col digit, sheet order).  Corners (15,0)/(12,3) are
      blank by design (transparent portrait corners).
  "empty" cells must stay blank (fail loudly otherwise).

Outputs (committed, deterministic):
  tools/level_editor/public/tiles/menus/menus_<slug>.png
      (12 singles + 16 hero cells + 4 status + 10 buttons,
       one 8x8 PNG each)
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
SINGLES = [(c, 0) for c in range(12)]
STATUS = [(c, 1) for c in range(4)]
BUTTONS = [(4, 1), (5, 1), (6, 1), (7, 1), (8, 1), (9, 1), (10, 1),
           (0, 2), (1, 2), (2, 2)]
HERO = [(c, r) for r in range(4) for c in range(12, 16)]
HERO_BLANK = [(15, 0), (12, 3)]


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
        grid = [[c.strip() for c in row]
                for row in csv.reader(f) if any(row)]
    if len(grid) != 4 or any(len(row) != 16 for row in grid):
        raise SystemExit(
            "menu-tile-description.csv: want 4 rows of 16 names, got %r"
            % ([len(row) for row in grid],))
    names = [n for row in grid for n in row if n != "empty"]
    if len(names) != len(set(names)):
        raise SystemExit("menu-tile-description.csv: names must be unique")

    for (c, r) in SINGLES + STATUS + BUTTONS:
        if not cell_drawn(img, c, r):
            raise SystemExit(
                "expected drawn single at (%d,%d) for %r"
                % (c, r, grid[r][c]))
    for (c, r) in HERO:
        want = (c, r) not in HERO_BLANK
        if cell_drawn(img, c, r) != want:
            raise SystemExit(
                "hero cell (%d,%d) drawn=%s, want drawn=%s"
                % (c, r, not want, want))
    for r in range(4):
        for c in range(12):
            if (c, r) in SINGLES + STATUS + BUTTONS:
                continue
            if cell_drawn(img, c, r):
                raise SystemExit(
                    "expected blank empty cell at (%d,%d)" % (c, r))

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

    for (c, r) in SINGLES + HERO + STATUS + BUTTONS:
        label = grid[r][c]
        tid = "menus_" + slugify(label)
        cell = img.crop((c * TILE, r * TILE, (c + 1) * TILE, (r + 1) * TILE))
        add_tile(tid, label, tid + ".png", cell,
                 mean_color(img, (c * TILE, r * TILE,
                                  (c + 1) * TILE, (r + 1) * TILE)))

    # Drop the retired huge-character tile (replaced by 4x4 hero cells).
    stale = os.path.join(PNG_DIR, "menus_huge_character.png")
    if os.path.isfile(stale):
        os.remove(stale)

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
