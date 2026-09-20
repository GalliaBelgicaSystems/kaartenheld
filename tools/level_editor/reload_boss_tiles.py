#!/usr/bin/env python3
"""Deterministically reload Boss tiles from source sheets into curated PNGs.

Source of truth: `assets/combat-tile.png` +
`assets/combat-tileset-description.csv` (combat Boss art) and
`assets/sprites.png` + `assets/sprites-tileset-description.csv`
(overworld Boss art, consumed by BOTH the actors tileset
`actors_boss_*` and the enemies tileset `boss_ow_*`).

Curated outputs (`tools/level_editor/public/tiles/<set>/*.png` plus the
`color` swatch in `tools/level_editor/tilesets/<set>.json`) are DERIVED:
rerunning this script reproduces them byte-identically.  Every other JSON
field (`gb_constant`, `walkable`, `category`, `ascii`, `image_url`) is
curated and left untouched.

Canonical-sheet rule: `assets/sprites.png` is the ONLY overworld source.
`assets/actor-sprites.png` was removed ("removed unneeded actors"); this
script fails loudly if it ever reappears so the twin-sheet drift cannot
silently return.

Cell maps below are explicit (col, row) sheet coordinates cross-checked at
runtime against the CSV label (`tileset_id + slug == tile id`), so a sheet
re-layout fails loudly instead of slicing the wrong art.

Usage (inside nix develop for Pillow):
    python3 tools/level_editor/reload_boss_tiles.py
    python3 tools/level_editor/reload_boss_tiles.py --check   # nonzero on drift
"""
import argparse
import csv
import io
import json
import os
import sys

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from import_tileset import slugify, crop_tile_with_transparency

TILE_SIZE = 8
REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

COMBAT_SHEET = os.path.join(REPO_ROOT, "assets", "combat-tile.png")
COMBAT_CSV = os.path.join(REPO_ROOT, "assets", "combat-tileset-description.csv")
SPRITES_SHEET = os.path.join(REPO_ROOT, "assets", "sprites.png")
SPRITES_CSV = os.path.join(REPO_ROOT, "assets", "sprites-tileset-description.csv")
REMOVED_TWIN = os.path.join(REPO_ROOT, "assets", "actor-sprites.png")

# (col, row, combat tile id). Row 2 cols 6-14 = live 3x3 Boss; row 4
# cols 3-5 = glow-eyes variants (excluded from the ROM LAYOUT: 5 colors,
# over the 2bpp budget; reloaded for the editor only).
COMBAT_MAP = [
    (6, 2, "combat_bottom_left_boss"),
    (7, 2, "combat_bottom_middle_boss"),
    (8, 2, "combat_bottom_right_boss"),
    (9, 2, "combat_left_middle_boss"),
    (10, 2, "combat_middle_center_boss"),
    (11, 2, "combat_middle_right_boss"),
    (12, 2, "combat_top_left_boss"),
    (13, 2, "combat_top_middle_boss"),
    (14, 2, "combat_top_right_boss"),
    (3, 4, "combat_left_middle_boss_2"),
    (4, 4, "combat_middle_center_boss_2"),
    (5, 4, "combat_middle_right_boss_2"),
]

# (col, row, actors tile id, enemies tile id or None). Non-2 set feeds both
# tilesets (slime-lord OW); the _2 set exists only in actors/.
OW_MAP = [
    (10, 0, "actors_boss_top_left_corner", "boss_ow_tl"),
    (11, 0, "actors_boss_top_right_corner", "boss_ow_tr"),
    (10, 1, "actors_boss_bottom_left_corner", "boss_ow_bl"),
    (11, 1, "actors_boss_bottom_right_corner", "boss_ow_br"),
    (8, 1, "actors_boss_top_left_corner_2", None),
    (9, 1, "actors_boss_top_right_corner_2", None),
    (8, 2, "actors_boss_bottom_left_corner_2", None),
    (9, 2, "actors_boss_bottom_right_corner_2", None),
]

# Curated tile id -> (tileset json path, png dir).
TILESET_OF = {}
for _tid in [t for _, _, t in COMBAT_MAP]:
    TILESET_OF[_tid] = ("tools/level_editor/tilesets/combat.json",
                        "tools/level_editor/public/tiles/combat")
for _, _, _aid, _eid in OW_MAP:
    TILESET_OF[_aid] = ("tools/level_editor/tilesets/actors.json",
                        "tools/level_editor/public/tiles/actors")
    if _eid is not None:
        TILESET_OF[_eid] = ("tools/level_editor/tilesets/enemies.json",
                            "tools/level_editor/public/tiles/enemies")


def load_csv_grid(path):
    with open(path, newline="") as f:
        return list(csv.reader(io.StringIO(f.read())))


def cell_color(img_rgba):
    opaque = [p for p in img_rgba.getdata() if p[3] > 0]
    if opaque:
        return "#%02x%02x%02x" % (
            sum(p[0] for p in opaque) // len(opaque),
            sum(p[1] for p in opaque) // len(opaque),
            sum(p[2] for p in opaque) // len(opaque))
    return "#000000"


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", action="store_true",
                        help="Do not write; exit nonzero if output differs")
    args = parser.parse_args()

    if os.path.exists(REMOVED_TWIN):
        print("ERROR: %s exists: actor-sprites.png was removed;"
              " sprites.png is the canonical OW sheet" % REMOVED_TWIN,
              file=sys.stderr)
        return 1

    combat_img = Image.open(COMBAT_SHEET)
    sprites_img = Image.open(SPRITES_SHEET)
    combat_csv = load_csv_grid(COMBAT_CSV)
    sprites_csv = load_csv_grid(SPRITES_CSV)

    jobs = []  # (sheet_img, col, row, csv_label_expected, tile_id)
    for col, row, tid in COMBAT_MAP:
        label = combat_csv[row][col].strip()
        assert "combat_" + slugify(label) == tid, \
            "combat (%d,%d) label %r does not slug to %s" % (col, row, label, tid)
        jobs.append((combat_img, col, row, tid))
    for col, row, aid, eid in OW_MAP:
        label = sprites_csv[row][col].strip()
        assert "actors_" + slugify(label) == aid, \
            "sprites (%d,%d) label %r does not slug to %s" % (col, row, label, aid)
        jobs.append((sprites_img, col, row, aid))
        if eid is not None:
            jobs.append((sprites_img, col, row, eid))

    # Load each touched manifest once.
    manifests = {}
    for _, _, _, tid in jobs:
        js, _ = TILESET_OF[tid]
        if js not in manifests:
            with open(os.path.join(REPO_ROOT, js), encoding="utf-8") as f:
                manifests[js] = json.load(f)

    changed, drift = [], []
    for sheet, col, row, tid in jobs:
        js, pngdir = TILESET_OF[tid]
        left, upper = col * TILE_SIZE, row * TILE_SIZE
        fresh = crop_tile_with_transparency(sheet, left, upper,
                                            left + TILE_SIZE, upper + TILE_SIZE)
        assert fresh.size == (TILE_SIZE, TILE_SIZE), \
            "%s: unexpected crop size %s" % (tid, fresh.size)
        png_path = os.path.join(REPO_ROOT, pngdir, tid + ".png")
        cur = Image.open(png_path).convert("RGBA") if os.path.exists(png_path) else None
        if cur is not None and cur.size != (TILE_SIZE, TILE_SIZE):
            print("ERROR: %s: curated PNG is %s, expected 8x8;"
                  " refusing to change dimensions by reload" % (png_path, cur.size),
                  file=sys.stderr)
            return 1
        if cur is None or cur.tobytes() != fresh.tobytes():
            if args.check:
                drift.append(tid)
            else:
                fresh.save(png_path)
                changed.append(tid)
        # Color swatch follows the pixels (curated fields untouched).
        entry = next((t for t in manifests[js]["tiles"] if t["id"] == tid), None)
        if entry is None:
            print("ERROR: %s: tile id %r not in manifest" % (js, tid), file=sys.stderr)
            return 1
        want = cell_color(fresh)
        if entry.get("color") != want:
            if args.check:
                drift.append(tid + ":color")
            else:
                entry["color"] = want
                changed.append(tid + ":color")

    if args.check:
        if drift:
            print("reload-boss-tiles --check: %d drifted item(s):" % len(drift))
            for d in drift:
                print("  %s" % d)
            return 1
        print("reload-boss-tiles --check: clean (24 PNGs + swatches in sync).")
        return 0

    for js, manifest in manifests.items():
        with open(os.path.join(REPO_ROOT, js), "w", encoding="utf-8") as f:
            json.dump(manifest, f, indent=2)
            f.write("\n")
    if not changed:
        print("reload-boss-tiles: no changes; curated tiles already in sync.")
    else:
        print("reload-boss-tiles: %d updated item(s):" % len(changed))
        for c in changed:
            print("  %s" % c)
    return 0


if __name__ == "__main__":
    sys.exit(main())
