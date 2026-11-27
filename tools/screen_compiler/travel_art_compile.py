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

OUT_DEFAULT = REPO_ROOT / "src" / "game" / "travel_art_content.c"


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
    L.append(" * assets/travels (naming existing tileset tiles).")
    L.append(" * Linked into the travel-demo ROM only (TRAVEL_SRCS).")
    L.append(" *")
    L.append(" * Per-route composed strip, 3 rows x 18 cols, row-major:")
    L.append(" * cols 0-7 origin grove, cols 8-9 origin transition (same tile")
    L.append(" * twice), cols 10-17 destination vista.  Parallel to")
    L.append(" * g_travel_routes (route declaration order).")
    L.append(" * CRAM palette setup is wiring-step work, not part of this emit.")
    L.append(" */")
    L.append("")
    L.append('#include "scene.h"')
    L.append("")
    L.append("#pragma bank 13")
    L.append("")
    L.append("typedef struct {")
    L.append("    uint8_t origin_tileset;")
    L.append("    uint8_t dest_tileset;")
    L.append("    uint8_t cells[54];")
    L.append("} TravelArtStrip;")
    L.append("")
    L.append("const TravelArtStrip travel_art_strips[] = {")
    for r, s in strips:
        L.append("    /* %s: %s -> %s */" % (r["id"], r["origin_biome"], r["destination_biome"]))
        L.append("    { %s, %s, {" % (s["origin_tileset"], s["dest_tileset"]))
        cells = s["cells"]
        assert len(cells) == 54, (r["id"], len(cells))
        for i in range(0, len(cells), 6):
            chunk = cells[i:i + 6]
            L.append("        " + ", ".join(chunk) + ",")
        L.append("    } },")
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
        for row in range(3):
            # Origin grove, cols 0-7.
            for ci, name in enumerate(grove[row]):
                syms.append(resolve_tile(
                    o, lo["tileset"], im_o, world_tiles, name,
                    "grove row %d col %d" % (row, ci)))
            # Transition, cols 8-9 (same tile twice).
            for _ in (8, 9):
                syms.append(resolve_tile(
                    o, lo["tileset"], im_o, world_tiles,
                    trans[row], "transition row %d" % row))
            # Destination vista, cols 10-17.
            for ci, name in enumerate(vista[row]):
                syms.append(resolve_tile(
                    d, ld["tileset"], im_d, world_tiles, name,
                    "vista row %d col %d" % (row, 10 + ci)))
        assert len(syms) == 54, (r["id"], len(syms))
        strips.append((r, {"origin_tileset": tileset_syms[o],
                           "dest_tileset": tileset_syms[d],
                           "cells": syms}))
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
