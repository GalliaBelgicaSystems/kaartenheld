#!/usr/bin/env python3
"""Render journey strip layouts as a review PNG (artist eyeball aid).

Reads the same sources as travel_art_compile.py (journeys/routes/*.json
+ assets/travels/<biome>.json + tileset manifests + sheets) and draws
each route's 18x3 strip exactly as laid out: cols 0-7 origin grove,
cols 8-9 transition, cols 10-17 destination vista. Ground rows come
from assets/travel-tile.png, trees from the world tileset sheets.

Usage: python3 tools/render_travel_strips.py (inside nix develop).
Output: screenshots/review/travel-strips-placeholder.png
"""
import csv
import json
import sys
from pathlib import Path

from PIL import Image, ImageDraw

REPO_ROOT = Path(__file__).resolve().parents[1]
ROUTES_DIR = REPO_ROOT / "journeys" / "routes"
LAYOUTS_DIR = REPO_ROOT / "assets" / "travels"
MANIFESTS_DIR = REPO_ROOT / "tools" / "level_editor" / "tilesets"
SHEETS = {
    "forest": "assets/forest-tile.png",
    "village": "assets/village-tile.png",
    "desolate_landscape": "assets/desolate-tile.png",
}
TRAVEL_SHEET = "assets/travel-tile.png"
OUT = REPO_ROOT / "screenshots" / "review" / "travel-strips-placeholder.png"
SCALE = 4


def sheet_cell(png, coord):
    im = Image.open(png).convert("RGB")
    x, y = coord
    return im.crop((x * 8, y * 8, x * 8 + 8, y * 8 + 8))


def main():
    routes = sorted(
        (json.loads(p.read_text(encoding="utf-8"))
         for p in ROUTES_DIR.glob("*.json")),
        key=lambda r: r["id"])
    layouts = {}
    manifests = {}
    sheets = {}
    for r in routes:
        for b in (r["origin_biome"], r["destination_biome"]):
            if b not in layouts:
                layouts[b] = json.loads(
                    (LAYOUTS_DIR / ("%s.json" % b)).read_text(
                        encoding="utf-8"))
    for b, lay in layouts.items():
        ts = lay["tileset"]
        if ts not in manifests:
            manifests[ts] = json.loads(
                (MANIFESTS_DIR / ("%s.json" % ts)).read_text(
                    encoding="utf-8"))
        if ts not in sheets:
            sheets[ts] = REPO_ROOT / SHEETS[ts]
    travel_rows = list(csv.reader(
        (REPO_ROOT / "assets" / "travel-tileset.csv").read_text(
            encoding="utf-8").splitlines()))
    travel_im = Image.open(REPO_ROOT / TRAVEL_SHEET).convert("RGB")

    def cell_img(ref):
        prefix, tile_id = ref.split(".", 1)
        if prefix == "travel":
            for yy, row in enumerate(travel_rows):
                for xx, name in enumerate(row):
                    if name.lower().replace(" ", "").replace("_", "") == \
                            tile_id.lower().replace(" ", "").replace("_", ""):
                        return travel_im.crop(
                            (xx * 8, yy * 8, xx * 8 + 8, yy * 8 + 8))
            raise ValueError("unknown travel tile %r" % ref)
        man = manifests[prefix]
        vram = {v["tile"]: (v["x"], v["y"])
                for v in (man.get("vram_block") or {}).get("tiles", [])
                if "tile" in v}
        if tile_id not in vram:
            raise ValueError("no vram cell for %r" % ref)
        return sheet_cell(sheets[prefix], vram[tile_id])

    strips = []
    for r in routes:
        lo, ld = layouts[r["origin_biome"]], layouts[r["destination_biome"]]
        img = Image.new("RGB", (18 * 8, 3 * 8), (255, 255, 255))
        for row in range(3):
            for ci, ref in enumerate(lo["grove"][row]):
                img.paste(cell_img(ref), (ci * 8, row * 8))
            for cx in (8, 9):
                img.paste(cell_img(lo["transition"][row]), (cx * 8, row * 8))
            for ci, ref in enumerate(ld["vista"][row]):
                img.paste(cell_img(ref), ((10 + ci) * 8, row * 8))
        strips.append((r["id"], img))

    draw_h = 12
    gap = 8
    w = 18 * 8
    h = sum(3 * 8 + draw_h + gap for _, _ in strips)
    canvas = Image.new("RGB", (w, h), (0, 0, 0))
    d = ImageDraw.Draw(canvas)
    y = 0
    for rid, img in strips:
        d.text((2, y), rid, fill=(255, 255, 255))
        canvas.paste(img, (0, y + draw_h))
        y += 3 * 8 + draw_h + gap
    canvas = canvas.resize((w * SCALE, h * SCALE), Image.NEAREST)
    OUT.parent.mkdir(parents=True, exist_ok=True)
    canvas.save(OUT)
    print("wrote %s" % OUT.relative_to(REPO_ROOT))


if __name__ == "__main__":
    sys.exit(main())
