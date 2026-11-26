#!/usr/bin/env python3
"""travel_art_compile.py -- journey strip tilesets to ROM tile data.

Each journey leg renders a landscape strip whose left half shows the
ORIGIN biome and whose right half shows the DESTINATION biome (see
assets/travel-screen.tiles.json, "scenery").  Each biome provides one
8x3-tile sheet, assets/travels/<biome>.png (64x24 px), laid out
row-major in scenery grove_slots + vista_slots order.

Usage:
  python3 tools/screen_compiler/travel_art_compile.py --all -o src/game/travel_art_content.c
  python3 tools/screen_compiler/travel_art_compile.py --all --check

The compiler fetches the sheet named by each route's origin_biome /
destination_biome (fail loudly on missing files), validates exact size
and the Game Boy 2bpp limit (at most 4 shades per 8x8 tile), converts
each tile to planar bytes (lightest shade first, png2gb canonical
order), and emits one flat blob per biome plus a route table parallel
to g_travel_routes (same sorted order as journey_compile.py).

Linked into the travel-demo ROM only (TRAVEL_SRCS, bank 13).  Paint
stamping reads these blobs through banked_copy; CRAM palette setup is
wiring-step work and deliberately not part of this emit.
"""

import argparse
import json
import sys
from pathlib import Path

from PIL import Image

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools" / "screen_compiler"))
from journey_compile import TRAVEL_BIOMES  # noqa: E402  (single source of truth)

ROUTES_DIR = REPO_ROOT / "journeys" / "routes"
TRAVEL_DIR = REPO_ROOT / "assets" / "travels"
SPEC_PATH = REPO_ROOT / "assets" / "travel-screen.tiles.json"

OUT_DEFAULT = REPO_ROOT / "src" / "game" / "travel_art_content.c"

TILE_PX = 8
SHEET_COLS = 8
SHEET_ROWS = 3
SHEET_W = SHEET_COLS * TILE_PX
SHEET_H = SHEET_ROWS * TILE_PX

# Fixed biome order: blob and route-table indices.  Stable forever;
# append only, never reorder (paint code indexes by it, AGENTS.md 52.18
# layout rules apply to the reader side too).
BIOME_ORDER = ["forest", "village", "desolate", "snow"]
assert set(BIOME_ORDER) == set(TRAVEL_BIOMES), (BIOME_ORDER, TRAVEL_BIOMES)


def fail(msg):
    raise ValueError(msg)


def luminance(rgb):
    r, g, b = rgb
    return 0.299 * r + 0.587 * g + 0.114 * b


def load_routes():
    routes = []
    seen = set()
    for path in sorted(ROUTES_DIR.glob("*.json")):
        doc = json.loads(path.read_text(encoding="utf-8"))
        rid = doc.get("id")
        if not rid or rid in seen:
            fail("route %s: missing or duplicate id" % path.name)
        seen.add(rid)
        for key in ("origin_biome", "destination_biome"):
            if doc.get(key) not in TRAVEL_BIOMES:
                fail("%s: %s must be one of %s (assets/travels/<biome>.png)"
                     % (rid, key, sorted(TRAVEL_BIOMES)))
        routes.append(doc)
    if not routes:
        fail("no routes in journeys/routes/")
    return routes


def load_spec_slots():
    spec = json.loads(SPEC_PATH.read_text(encoding="utf-8"))
    grove = list(spec["scenery"]["grove_slots"])
    vista = list(spec["scenery"]["vista_slots"])
    if len(grove) + len(vista) != SHEET_COLS * SHEET_ROWS:
        fail("spec slot count %d != %d sheet cells"
             % (len(grove) + len(vista), SHEET_COLS * SHEET_ROWS))
    return grove + vista


def convert_sheet(biome, slot_names):
    """Fetch assets/travels/<biome>.png, validate, return 24 planar tiles."""
    path = TRAVEL_DIR / (biome + ".png")
    if not path.is_file():
        fail("missing strip sheet %s (artist deliverable, see %s/README.md)"
             % (path.relative_to(REPO_ROOT), TRAVEL_DIR.relative_to(REPO_ROOT)))
    try:
        img = Image.open(path).convert("RGB")
    except Exception as e:
        fail("%s: unreadable (%s)" % (path.name, e))
    if img.size != (SHEET_W, SHEET_H):
        fail("%s: must be exactly %dx%d (%dx%d tiles of %dx%d), got %dx%d"
             % (path.name, SHEET_W, SHEET_H, SHEET_COLS, SHEET_ROWS,
                TILE_PX, TILE_PX, img.size[0], img.size[1]))
    tiles = []
    for idx in range(SHEET_COLS * SHEET_ROWS):
        cx, cy = idx % SHEET_COLS, idx // SHEET_COLS
        cell = img.crop((cx * TILE_PX, cy * TILE_PX,
                         (cx + 1) * TILE_PX, (cy + 1) * TILE_PX))
        colors = sorted(set(cell.getdata()), key=luminance, reverse=True)
        if len(colors) > 4:
            fail("%s: cell %d (%s) uses %d shades, max 4 per 8x8 Game Boy tile"
                 % (path.name, idx, slot_names[idx], len(colors)))
        index = {c: i for i, c in enumerate(colors)}
        out = bytearray(16)
        px = list(cell.getdata())
        for row in range(8):
            lo = hi = 0
            for col in range(8):
                v = index[px[row * 8 + col]]
                lo |= (v & 1) << (7 - col)
                hi |= ((v >> 1) & 1) << (7 - col)
            out[row * 2] = lo
            out[row * 2 + 1] = hi
        tiles.append((slot_names[idx], bytes(out)))
    return tiles


def emit_c(routes, blobs):
    L = []
    L.append("/**")
    L.append(" * Generated by travel_art_compile.py.")
    L.append(" * Do not edit directly -- edit assets/travels/<biome>.png and re-run.")
    L.append(" * Linked into the travel-demo ROM only (TRAVEL_SRCS).")
    L.append(" *")
    L.append(" * One 24-tile blob per biome in BIOME_ORDER below "
             + str(BIOME_ORDER) + ",")
    L.append(" * row-major in scenery slot order (grove kit then vista kit).")
    L.append(" * Shade order per tile is lightest-first (png2gb canonical);")
    L.append(" * CRAM palette programming is wiring-step work, not part of this emit.")
    L.append(" */")
    L.append("")
    L.append('#include "travel.h"')
    L.append("")
    L.append("#pragma bank 13")
    L.append("")
    for biome in BIOME_ORDER:
        tiles = blobs[biome]
        L.append("/* %s: 24 tiles, row-major grove kit then vista kit */" % biome)
        L.append("const uint8_t travel_art_%s[%d] = {" % (biome, 24 * 16))
        flat = []
        for _slot, data in tiles:
            flat.extend(data)
        for i in range(0, len(flat), 12):
            chunk = flat[i:i + 12]
            L.append("    " + ", ".join("0x%02X" % b for b in chunk) + ",")
        L.append("};")
        L.append("")
    L.append("/* Per-route strip biomes, parallel to g_travel_routes order")
    L.append(" * (same sorted journeys/routes/*.json order as journey_compile.py). */")
    L.append("typedef struct { uint8_t origin; uint8_t dest; } TravelArtRoute;")
    L.append("const TravelArtRoute travel_art_routes[] = {")
    for r in routes:
        o = BIOME_ORDER.index(r["origin_biome"])
        d = BIOME_ORDER.index(r["destination_biome"])
        L.append("    { /* %s: %s -> %s */ %d, %d }," % (r["id"], r["origin_biome"], r["destination_biome"], o, d))
    L.append("};")
    L.append("")
    return "\n".join(L)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("-o", default=str(OUT_DEFAULT))
    args = ap.parse_args()
    if not args.all:
        ap.error("--all is required")
    routes = load_routes()
    slots = load_spec_slots()
    blobs = {b: convert_sheet(b, slots) for b in BIOME_ORDER}
    text = emit_c(routes, blobs)
    out = Path(args.o)
    if args.check:
        if not out.is_file():
            fail("%s missing (run without --check first)" % out)
        if out.read_text(encoding="utf-8") != text + "\n":
            fail("committed travel art is stale (re-run travel_art_compile.py --all)")
        print("travel-art-check: fresh")
        return
    out.write_text(text + "\n", encoding="utf-8")
    print("All travel art compiled to %s" % out)


if __name__ == "__main__":
    main()
