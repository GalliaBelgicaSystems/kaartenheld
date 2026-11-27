#!/usr/bin/env python3
"""travel_art_compile.py -- journey strip tiles from per-biome tile layouts.

Each journey route declares origin_biome/destination_biome.  Each biome
provides one layout, assets/travels/<biome>.json, naming existing
tileset tiles ("tileset.tile_id", same refs as levels use).

The compiler fetches the origin grove + dest vista per route, resolves
each name through the tileset manifest to a gb_constant TILE_* symbol
(validated against src/world/world.h, uint8_t range), and emits one
composed 3-row x 18-col strip of TileType values per route, parallel to
g_travel_routes (same sorted journeys/routes/*.json order as
journey_compile.py).

Linked into the travel-demo ROM only (TRAVEL_SRCS, bank 13).  Paint
stamping reads these strips through banked_copy; CRAM palette setup is
wiring-step work and deliberately not part of this emit.

Usage:
  python3 tools/screen_compiler/travel_art_compile.py --all -o src/game/travel_art_content.c
  python3 tools/screen_compiler/travel_art_compile.py --all --check
"""

import argparse
import csv
import json
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools" / "screen_compiler"))
from journey_compile import TRAVEL_BIOMES  # noqa: E402 (single source of truth)
sys.path.insert(0, str(REPO_ROOT / "tools" / "level_compiler"))
from compile import TILESET_KIND_MAP  # noqa: E402 (single source of truth)

ROUTES_DIR = REPO_ROOT / "journeys" / "routes"
LAYOUTS_DIR = REPO_ROOT / "assets" / "travels"
MANIFESTS_DIR = REPO_ROOT / "tools" / "level_editor" / "tilesets"
WORLD_H = REPO_ROOT / "src" / "world" / "world.h"
CHROME_CSV = REPO_ROOT / "assets" / "travel-tileset.csv"

OUT_DEFAULT = REPO_ROOT / "src" / "game" / "travel_art_content.c"

# Travel-chrome floor icons (assets/travel-tile.png sheet indices,
# == TCHROME_* in src/game/travel.h): the only travel-sheet tiles a
# biome layout may name, and only in ground rows (row 2).  Everything
# else must come from the layout's own tileset.
CHROME_FLOOR_ICONS = frozenset((29, 30, 31))


def fail(msg):
    raise ValueError(msg)


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
                fail("%s: %s must be one of %s"
                     % (rid, key, sorted(TRAVEL_BIOMES)))
        routes.append(doc)
    if not routes:
        fail("no routes in journeys/routes/")
    return routes


def load_world_enums():
    """Parse TILE_* = N and WORLD_TILESET_* = N out of world.h."""
    text = WORLD_H.read_text(encoding="utf-8")
    tiles = {}
    for m in re.finditer(r"^\s*(TILE_[A-Z0-9_]+)\s*=\s*(\d+)\s*,?\s*$",
                         text, re.MULTILINE):
        tiles[m.group(1)] = int(m.group(2))
    if not tiles:
        fail("no TILE_* symbols parsed from %s" % WORLD_H)
    tilesets = {}
    for m in re.finditer(r"^\s*(WORLD_TILESET_[A-Z0-9_]+)\s*=\s*(\d+)\s*,?\s*$",
                         text, re.MULTILINE):
        tilesets[m.group(1)] = int(m.group(2))
    return tiles, tilesets


def load_manifest(tileset):
    """Map tile id -> gb_constant for one tileset manifest."""
    path = MANIFESTS_DIR / ("%s.json" % tileset)
    if not path.is_file():
        fail("missing tileset manifest %s (unknown tileset %r)"
             % (path.relative_to(REPO_ROOT), tileset))
    try:
        doc = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as e:
        fail("%s: invalid JSON (%s)" % (path.name, e))
    tiles = doc.get("tiles")
    if not isinstance(tiles, list) or not tiles:
        fail("%s: expected non-empty 'tiles' list" % path.name)
    mapping = {}
    for t in tiles:
        if isinstance(t, dict) and "id" in t and "gb_constant" in t:
            mapping[t["id"]] = t["gb_constant"]
    if not mapping:
        fail("%s: no id->gb_constant entries found" % path.name)
    return mapping


def load_layout(biome):
    """Fetch assets/travels/<biome>.json (shape validated by caller)."""
    path = LAYOUTS_DIR / ("%s.json" % biome)
    if not path.is_file():
        fail("missing strip layout %s (route references biome %r)"
             % (path.relative_to(REPO_ROOT), biome))
    try:
        doc = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as e:
        fail("%s: invalid JSON (%s)" % (path.name, e))
    if doc.get("biome") != biome:
        fail("%s: 'biome' field %r does not match filename"
             % (path.name, doc.get("biome")))
    tileset = doc.get("tileset")
    if tileset not in TILESET_KIND_MAP:
        fail("%s: unknown tileset %r (expected one of %s)"
             % (path.name, tileset, sorted(TILESET_KIND_MAP)))
    return doc


def load_chrome():
    """Map normalized travel-tileset.csv names to sheet indices.

    Row-major over the 20x2 sheet (idx == TCHROME_* in travel.h).
    Names normalize case/space/underscore-insensitively, so the CSV's
    "forest floor icon2" and a layout's "forest_floor_icon_2" meet.
    """
    rows = list(csv.reader(CHROME_CSV.read_text(encoding="utf-8").splitlines()))
    if len(rows) != 2 or any(len(r) != 20 for r in rows):
        fail("%s: expected a 2-row x 20-col grid of tile names"
             % CHROME_CSV.name)

    def norm(s):
        return s.lower().replace(" ", "").replace("_", "")

    mapping = {}
    for y, row in enumerate(rows):
        for x, name in enumerate(row):
            key = norm(name)
            if not key:
                fail("%s: empty tile name at cell (%d, %d)"
                     % (CHROME_CSV.name, x, y))
            if key in mapping:
                fail("%s: duplicate tile name %r" % (CHROME_CSV.name, name))
            mapping[key] = y * 20 + x
    return mapping, norm


def resolve_cell(biome, tileset, id_to_const, world_tiles, chrome,
                 norm, name, row, where, is_ground):
    """Resolve one layout cell.

    Returns ("tile", TILE_* symbol) for the layout's own tileset, or
    ("chrome", sheet idx) for travel-sheet floor icons.  Travel refs
    are only legal in ground rows (row 2 of grove/vista, never the
    transition seam), and only the three floor icons at that.
    """
    if not isinstance(name, str) or "." not in name:
        fail("%s: %s: tile ref %r must look like '<tileset>.<tile_id>'"
             % (biome, where, name))
    prefix, tile_id = name.split(".", 1)
    if prefix == "travel":
        if not is_ground:
            fail("%s: %s: travel-sheet tiles are only allowed in "
                 "ground rows (row 2 of grove/vista), never %s"
                 % (biome, where, where.split()[0]))
        idx = chrome.get(norm(tile_id))
        if idx is None:
            fail("%s: %s: unknown travel tile %r (see assets/travel-tileset.csv)"
                 % (biome, where, name))
        if idx not in CHROME_FLOOR_ICONS:
            fail("%s: %s: only the forest floor icons may come from "
                 "the travel sheet (got sheet index %d)"
                 % (biome, where, idx))
        return ("chrome", idx)
    return ("tile", resolve_tile(
        biome, tileset, id_to_const, world_tiles, name, where))


def resolve_floors(biome, tileset, id_to_const, world_tiles, chrome, norm,
                   row_cells):
    """Ordered-distinct chrome indices of a ground row (row 2).

    Ground rows name travel-sheet floor icons only (forest.* tiles
    there fail loudly: the paint stages ground from the chrome window,
    so a tileset tile would silently never render).  Exactly three
    distinct icons, in first-seen order, matching the cycling pattern.
    """
    idxs = []
    names = []
    for ci, name in enumerate(row_cells):
        kind, val = resolve_cell(
            biome, tileset, id_to_const, world_tiles, chrome, norm,
            name, 2, "ground row col %d" % ci, True)
        if kind != "chrome":
            fail("%s: ground row col %d: %r is not a travel floor icon "
                 "(ground rows must name travel.* floor icons only)"
                 % (biome, ci, name))
        if val not in idxs:
            idxs.append(val)
            names.append(name.split(".", 1)[1])
    if len(idxs) != 3:
        fail("%s: ground row must name exactly 3 distinct floor icons "
             "(got %d: %s)" % (biome, len(idxs), ", ".join(names)))
    return idxs, names


def resolve_tile(biome, tileset, id_to_const, world_tiles, name, where):
    """Resolve "tileset.tile_id" to a world.h TILE_* symbol."""
    if not isinstance(name, str) or "." not in name:
        fail("%s: %s: tile ref %r must look like '<tileset>.<tile_id>'"
             % (biome, where, name))
    prefix, tile_id = name.split(".", 1)
    if prefix != tileset:
        fail("%s: %s: tile %r does not belong to layout tileset %r"
             % (biome, where, name, tileset))
    if tile_id not in id_to_const:
        close = [k for k in id_to_const if tile_id.split("_")[-1] in k]
        hint = (" (did you mean one of: %s?)" % ", ".join(sorted(close)[:3])
                if close else "")
        fail("%s: %s: unknown tile id %r in tileset %r%s"
             % (biome, where, tile_id, tileset, hint))
    sym = id_to_const[tile_id]
    if sym not in world_tiles:
        fail("%s: %s: %s is not a defined TILE_* in src/world/world.h"
             % (biome, where, sym))
    val = world_tiles[sym]
    if not 0 <= val <= 255:
        fail("%s: %s: %s = %d does not fit uint8_t"
             % (biome, where, sym, val))
    return sym


def emit_c(routes, strips):
    L = []
    L.append("/**")
    L.append(" * Generated by travel_art_compile.py -- do not edit.")
    L.append(" * Source of truth: the journey route declarations (origin_biome /")
    L.append(" * destination_biome) plus per-biome JSON layouts under")
    L.append(" * assets/travels (naming existing tileset tiles, or the travel")
    L.append(" * sheet's forest floor icons in ground rows).")
    L.append(" * Linked into the travel-demo ROM only (TRAVEL_SRCS).")
    L.append(" *")
    L.append(" * Per-route composed strip, row-major (trees only):")
    L.append(" * cols 0-7 origin grove, cols 8-9 origin transition,")
    L.append(" * cols 10-17 destination vista; rows 0-1 in cells, row 2")
    L.append(" * (ground) in floor_o/floor_d as TCHROME_* indices.")
    L.append(" * Parallel to g_travel_routes (route declaration order).")
    L.append(" * Paint staging (dual-tileset load) is wiring-step work,")
    L.append(" * not part of this emit.")
    L.append(" */")
    L.append("")
    L.append('#include "scene.h"')
    L.append("")
    L.append("#pragma bank 13")
    L.append("")
    L.append("typedef struct {")
    L.append("    uint8_t origin_tileset;")
    L.append("    uint8_t dest_tileset;")
    L.append("    uint8_t cells[36];")
    L.append("    uint8_t trans[3];")
    L.append("    uint8_t floor_o[3];")
    L.append("    uint8_t floor_d[3];")
    L.append("} TravelArtStrip;")
    L.append("")
    L.append("const TravelArtStrip travel_art_strips[] = {")
    for r, s in strips:
        L.append("    /* %s: %s -> %s */" % (r["id"], r["origin_biome"], r["destination_biome"]))
        L.append("    { %s, %s, {" % (s["origin_tileset"], s["dest_tileset"]))
        cells = s["cells"]
        assert len(cells) == 36, (r["id"], len(cells))
        for i in range(0, len(cells), 6):
            chunk = cells[i:i + 6]
            L.append("        " + ", ".join(chunk) + ",")
        L.append("    },")
        L.append("      { %s }," % ", ".join(s["trans"]))
        L.append("      { %s }, /* origin floors: %s */"
                 % (", ".join(str(v) for v in s["floor_o"]),
                    ", ".join(s["floor_o_names"])))
        L.append("      { %s }, /* destination floors: %s */"
                 % (", ".join(str(v) for v in s["floor_d"]),
                    ", ".join(s["floor_d_names"])))
        L.append("    },")
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
    world_tiles, world_tilesets = load_world_enums()
    chrome, norm = load_chrome()
    # Only biomes actually referenced need layouts (snow is aspirational).
    needed = set()
    for r in routes:
        needed.add(r["origin_biome"])
        needed.add(r["destination_biome"])
    layouts = {}
    manifests = {}
    for b in sorted(needed):
        layouts[b] = load_layout(b)
    # Validate every referenced WORLD_TILESET_* symbol exists.
    tileset_syms = {}
    for b in sorted(needed):
        ts = layouts[b]["tileset"]
        sym = TILESET_KIND_MAP[ts]
        if sym not in world_tilesets:
            fail("tileset %r maps to unknown symbol %s (not in world.h)"
                 % (ts, sym))
        tileset_syms[b] = sym
    # Compose one 3x18 strip per route (row-major: grove 8 and vista 8).
    strips = []
    for r in routes:
        o = r["origin_biome"]
        d = r["destination_biome"]
        lo = layouts[o]
        ld = layouts[d]
        cells = []
        # NOTE: grove/vista grids are indexed [row][col]; transition is
        # one tile name per row, stamped in both cols 8 and 9.
        if ("transition" not in lo or not isinstance(lo["transition"], list)
                or len(lo["transition"]) != 3
                or not all(isinstance(t, str) for t in lo["transition"])):
            fail("%s: 'transition' must be a list of 3 tile names (one per row)"
                 % o)
        grove, vista, trans = lo["grove"], ld["vista"], lo["transition"]
        # Validate grid shapes (rectangular 3x8 grove+gutter+vista handled below).
        if not (isinstance(grove, list) and len(grove) == 3
                and all(isinstance(rr, list) and len(rr) == 8
                        for rr in grove)):
            fail("%s: 'grove' must be 3 rows x 8 cols of tile names" % o)
        if not (isinstance(vista, list) and len(vista) == 3
                and all(isinstance(rr, list) and len(rr) == 8
                        for rr in vista)):
            fail("%s: 'vista' must be 3 rows x 8 cols of tile names" % d)
        syms = []
        # Only the origin biome needs a loaded manifest up front; the
        # destination manifest loads lazily below (cache in manifests).
        if lo["tileset"] not in manifests:
            manifests[lo["tileset"]] = load_manifest(lo["tileset"])
        if ld["tileset"] not in manifests:
            manifests[ld["tileset"]] = load_manifest(ld["tileset"])
        im_o = manifests[lo["tileset"]]
        im_d = manifests[ld["tileset"]]
        for row in range(2):
            # Origin grove, cols 0-7 (trees only; travel chrome rejected).
            for ci, name in enumerate(grove[row]):
                kind, val = resolve_cell(
                    o, lo["tileset"], im_o, world_tiles, chrome, norm,
                    name, row, "grove row %d col %d" % (row, ci), False)
                assert kind == "tile", (o, name)
                syms.append(val)
            # Transition, cols 8-9 (same tile twice; tileset wash only).
            for _ in (8, 9):
                syms.append(resolve_tile(
                    o, lo["tileset"], im_o, world_tiles,
                    trans[row], "transition row %d" % row))
            # Destination vista, cols 10-17 (trees only).
            for ci, name in enumerate(vista[row]):
                kind, val = resolve_cell(
                    d, ld["tileset"], im_d, world_tiles, chrome, norm,
                    name, row, "vista row %d col %d" % (row, 10 + ci),
                    False)
                assert kind == "tile", (d, name)
                syms.append(val)
        assert len(syms) == 36, (r["id"], len(syms))
        # Transition wash, one tile per row (origin tileset).
        trans_syms = [resolve_tile(
            o, lo["tileset"], im_o, world_tiles,
            trans[row], "transition row %d" % row)
            for row in range(3)]
        # Ground rows (row 2): travel-sheet floor icons only. The paint
        # stages these from the chrome window, not the world tileset.
        floor_o, floor_o_names = resolve_floors(
            o, lo["tileset"], im_o, world_tiles, chrome, norm, grove[2])
        floor_d, floor_d_names = resolve_floors(
            d, ld["tileset"], im_d, world_tiles, chrome, norm, vista[2])
        strips.append((r, {"origin_tileset": tileset_syms[o],
                           "dest_tileset": tileset_syms[d],
                           "cells": syms,
                           "trans": trans_syms,
                           "floor_o": floor_o,
                           "floor_o_names": floor_o_names,
                           "floor_d": floor_d,
                           "floor_d_names": floor_d_names}))
    text = emit_c(routes, strips)
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
