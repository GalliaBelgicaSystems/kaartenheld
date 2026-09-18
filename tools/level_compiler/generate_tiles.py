#!/usr/bin/env python3
"""Generate ROM tile-trait tables from the editor tileset manifests.

The manifests (tools/level_editor/tilesets/*.json) are the single source
of truth for what each tile IS (walkable, glyph role, VRAM block slot,
exit art). This tool inverts them into the TileType value space
(src/world/world.h) and emits generated/tiles/tile_traits.h: walk ranges,
glyph ranges, and per-tileset exit-art indices consumed by world.c,
patrol_banked.c, and ui.c.

What stays hand-written (frozen legacy, predates manifests): the generic
tiles (FLOOR/WALL/EXIT), the old desolate set (DESOLATE_FLOOR_*,
DESOLATE_FLOOR_PLAIN, DESOLATE_STAIRCASE), stumps, and the t<8 semantic
map. Those never change; only manifest-covered ranges are generated.
The patrol replica dissolves into the same generated table (one source,
two call sites) instead of a hand-kept copy.

Glyph roles mirror the historic renderer exactly: the vram_block exit
tile renders '>', category "object" (campfires) renders '*', walkable
renders '.', everything else '#'.

CGB palette lookup tables (tile_palette.h) are now sourced from the
palette compiler manifests (generated/tiles/<tileset>.json) produced by
tools/palette_compiler.py, ensuring web editor / ROM parity.

Usage:
    python3 tools/level_compiler/generate_tiles.py --out generated/tiles/tile_traits.h
"""

import argparse
import json
import re
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent.parent
sys.path.insert(0, str(SCRIPT_DIR))

from validate import load_tilesets

WORLD_H = REPO_ROOT / "src" / "world" / "world.h"
GENERATED_TILES_DIR = REPO_ROOT / "generated" / "tiles"

# Frozen legacy walkables (no manifest covers them; never change).
LEGACY_WALK_DOC = "TILE_FLOOR, TILE_EXIT, TILE_DESOLATE_FLOOR_00..03, " \
    "TILE_DESOLATE_FLOOR_PLAIN, TILE_DESOLATE_STAIRCASE (kept in ROM code)"


def load_palette_manifest(tileset_id: str):
    """Load palette manifest from generated/tiles/<tileset>.json.

    Returns dict with 'tile_palettes' list, or None if not found.
    """
    manifest_path = GENERATED_TILES_DIR / f"{tileset_id}.json"
    if not manifest_path.exists():
        return None
    try:
        return json.loads(manifest_path.read_text())
    except Exception:
        return None


def load_tiletype_numbers():
    mapping = {}
    for m in re.finditer(r"^\s*(TILE_[A-Z0-9_]+)\s*=\s*(\d+)",
                         WORLD_H.read_text(), re.M):
        mapping[m.group(1)] = int(m.group(2))
    return mapping


def landscape_entries(tilesets, const_by_value):
    """(name, value, walkable, glyph) for all world tileset ranges,
    derived purely from the manifests (+ the vram_block exit markings).
    A tile's explicit `glyph` field wins (see docs/glyphs.md registry);
    otherwise the historic derivation applies (exit '>', object '*',
    walkable '.', else '#')."""
    exit_consts = set()
    for ts in tilesets.values():
        for e in ts.get("vram_block", {}).get("tiles", []):
            if e.get("exit") and e.get("tile"):
                info = next((t for t in ts.get("tiles", [])
                              if t["id"] == e["tile"]), None)
                if info:
                    exit_consts.add(info["gb_constant"])
    entries = []
    for ts_id, ts in sorted(tilesets.items()):
        if not ts.get("vram_block"):
            continue
        by_const = {t["gb_constant"]: t for t in ts.get("tiles", [])}
        for name in sorted(by_const,
                           key=lambda n: const_by_value.get(n, 9999)):
            value = const_by_value.get(name)
            if value is None:
                continue
            info = by_const[name]
            walkable = bool(info.get("walkable", False))
            override = info.get("glyph", "")
            if isinstance(override, str) and len(override) == 1:
                glyph = override
            elif name in exit_consts:
                glyph = ">"
            elif info.get("category") == "object":
                glyph = "*"
            elif walkable:
                glyph = "."
            else:
                glyph = "#"
            entries.append((name, value, walkable, glyph))
    entries.sort(key=lambda e: e[1])
    return entries


def to_ranges(entries):
    """Contiguous same-(walkable, glyph) runs -> (lo_name, hi_name, walk, glyph)."""
    ranges = []
    start = prev = None
    for name, value, walk, glyph in entries:
        if start is None:
            start = prev = (name, value, walk, glyph)
        elif value == prev[1] + 1 and walk == prev[2] and glyph == prev[3]:
            prev = (name, value, walk, glyph)
        else:
            ranges.append((start[0], prev[0], start[2], start[3]))
            start = prev = (name, value, walk, glyph)
    if start is not None:
        ranges.append((start[0], prev[0], start[2], start[3]))
    return ranges


def c_escape(ch):
    if ch == "'":
        return "\\'"
    if ch == "\\":
        return "\\\\"
    return ch


def emit_walk(ranges, legacy_doc):
    """Walk table + accessor. Included by world.c (fixed) AND
    patrol_banked.c (bank 3): separate copies in separate banks, one
    source. Fixed-bank cost stays minimal by not dragging the glyph
    tables along (AGENTS.md 55.5)."""
    walk = [(lo, hi) for lo, hi, w, g in ranges if w]
    out = []
    out.append("/* Generated by tools/level_compiler/generate_tiles.py -- DO NOT EDIT DIRECTLY */")
    out.append("/*")
    out.append(" * Desolate-landscape walkable ranges derived from")
    out.append(" * tools/level_editor/tilesets/desolate_landscape.json.")
    out.append(" * Legacy tiles predate manifests and stay hand-listed in ROM")
    out.append(f" * code ({legacy_doc}).")
    out.append(" */")
    out.append("#ifndef TILE_WALK_H")
    out.append("#define TILE_WALK_H")
    out.append("")
    out.append("#include <stdint.h>")
    out.append("")
    out.append("/* Walkable landscape ranges (inclusive). */")
    out.append("static const uint8_t kWalkLo[] = {%s};" % ", ".join(lo for lo, hi in walk))
    out.append("static const uint8_t kWalkHi[] = {%s};" % ", ".join(hi for lo, hi in walk))
    out.append("")
    out.append("static inline uint8_t tile_landscape_walkable(uint8_t t) {")
    out.append("    uint8_t i;")
    out.append("    for (i = 0; i < (uint8_t)sizeof(kWalkLo); i++)")
    out.append("        if (t >= kWalkLo[i] && t <= kWalkHi[i]) return 1;")
    out.append("    return 0;")
    out.append("}")
    out.append("")
    out.append("#endif /* TILE_WALK_H */")
    out.append("")
    return "\n".join(out) + "\n"


def emit_glyph(ranges, exit_idx, const_by_value):
    """Glyph table + accessor (ui.c only) and the per-tileset exit-art
    indices consumed by the exit renderer.

    The Lo bound array is omitted for fixed-bank budget (each entry would
    cost a byte in _CODE): the accessor scans Hi-only for the first
    range with Hi >= t.  This is exactly equivalent to the Lo/Hi scan
    ONLY when every gap between ranges maps to '#' under both rules; the
    check below proves it for all 256 inputs and fails generation
    otherwise (so a future tileset layout that breaks the property can
    never silently ship wrong glyphs)."""
    vals = [(const_by_value[lo], const_by_value[hi], g)
            for lo, hi, w, g in ranges]

    def classic(t):
        for lo, hi, g in vals:
            if lo <= t <= hi:
                return g
        return '#'

    def hionly(t):
        for hi, g in [(h, g) for _, h, g in vals]:
            if t <= hi:
                return g
        return '#'

    for t in range(256):
        if classic(t) != hionly(t):
            print("generate_tiles: glyph Hi-only scan differs at %d "
                  "(ranges no longer gap-safe)" % t, file=sys.stderr)
            return None
    out = []
    out.append("/* Generated by tools/level_compiler/generate_tiles.py -- DO NOT EDIT DIRECTLY */")
    out.append("#ifndef TILE_GLYPH_H")
    out.append("#define TILE_GLYPH_H")
    out.append("")
    out.append("#include <stdint.h>")
    out.append("")
    out.append("/* Glyph range ends (inclusive): first Hi >= t wins. Gap-safe")
    out.append(" * by construction (see the generator-side proof); landscape")
    out.append(" * cells render per role. */")
    out.append("static const uint8_t kGlyphHi[] = {%s};" % ", ".join(hi for lo, hi, w, g in ranges))
    out.append("static const char kGlyphCh[] = {%s};" % ", ".join("'%s'" % c_escape(g) for lo, hi, w, g in ranges))
    out.append("")
    out.append("static inline char tile_landscape_glyph(uint8_t t) {")
    out.append("    uint8_t i;")
    out.append("    for (i = 0; i < (uint8_t)sizeof(kGlyphHi); i++)")
    out.append("        if (t <= kGlyphHi[i]) return kGlyphCh[i];")
    out.append("    return '#';")
    out.append("}")
    out.append("")
    out.append("/* VRAM block indices of each tileset's exit art (from the")
    out.append(" * vram_block exit markings; all tileset bases are 128). */")
    for kind, idx in sorted(exit_idx.items()):
        out.append(f"#define TILESET_EXIT_{kind} {idx}")
    out.append("")
    out.append("#endif /* TILE_GLYPH_H */")
    out.append("")
    return "\n".join(out) + "\n"


def emit_palette(entries, tilesets, const_by_value):
    """Per-tileset palette index arrays: tile_palette.h.

    Each tileset gets a flat const uint8_t array mapping sheet-order tile
    index (0-based within the tileset) to a CGB background palette index
    (0-7).  The runtime lookup is a single pointer-indexed read:
        pal = pal_tbl[tile_id - TILESET_BASE];
    No multiplication, no glyph intermediate, O(1).

    Palette indices are sourced from palette compiler manifests
    (generated/tiles/<tileset>.json) for web editor / ROM parity.
    A missing manifest is a hard error (no heuristic fallback exists).
    """
    # Group entries by tileset (sheet index + glyph per tile; glyphs are
    # carried for documentation only, palette comes from the manifest).
    kind_tiles = {}  # kind_id -> [(sheet_idx, glyph)]
    for ts_id, ts in sorted(tilesets.items()):
        if not ts.get("vram_block"):
            continue
        tiles = ts.get("tiles", [])
        by_const = {t["gb_constant"]: t for t in tiles}
        tile_list = []
        for i, t in enumerate(tiles):
            gb = t["gb_constant"]
            value = const_by_value.get(gb)
            if value is None:
                continue
            # Find glyph from entries
            glyph = '#'
            for name, val, walk, g in entries:
                if name == gb:
                    glyph = g
                    break
            tile_list.append((i, glyph))
        kind_tiles[ts_id] = tile_list

    out = []
    out.append("/* Generated by tools/level_compiler/generate_tiles.py -- DO NOT EDIT DIRECTLY */")
    out.append("/* Per-tileset CGB palette lookup tables: sheet index -> palette 0..7. */")
    out.append("#ifndef TILE_PALETTE_H")
    out.append("#define TILE_PALETTE_H")
    out.append("")
    out.append("#include <stdint.h>")
    out.append("")

    kind_id_map = {"forest": "FOREST", "desolate_landscape": "DESOLATE",
                   "castle": "CASTLE", "village": "VILLAGE"}

    for ts_id in ("forest", "desolate_landscape", "castle", "village"):
        kind = kind_id_map.get(ts_id, ts_id.upper())
        tiles = kind_tiles.get(ts_id, [])
        if not tiles:
            continue

        # Manifests are required: palette indices come only from
        # generated/tiles/<tileset>.json (make manifest). No glyph
        # fallback exists anymore -- a missing manifest fails loudly.
        manifest = load_palette_manifest(ts_id)
        if not (manifest and "tile_palettes" in manifest):
            raise ValueError(
                f"{ts_id}: missing generated/tiles/{ts_id}.json manifest "
                f"(run make manifest) -- no heuristic fallback exists")
        pal_values = manifest["tile_palettes"]
        source = "manifest"

        arr_name = f"g_tile_pal_{ts_id}"
        out.append(f"/* {kind} tileset: {len(pal_values)} tiles (source: {source}) */")
        out.append(f"const uint8_t {arr_name}[{len(pal_values)}] = {{")
        # Format as rows of 16
        for row_start in range(0, len(pal_values), 16):
            chunk = pal_values[row_start:row_start+16]
            vals = ", ".join(str(v) for v in chunk)
            out.append(f"    {vals},  /* {row_start:2d}..{row_start+len(chunk)-1:2d} */")
        out.append("};")
        out.append("")

    out.append("#endif /* TILE_PALETTE_H */")
    out.append("")
    return "\n".join(out) + "\n"


def load_tileset_kind_ids():
    """Parse WORLD_TILESET_* = N from src/world/world.h -> {kind: id}."""
    ids = {}
    for m in re.finditer(r"WORLD_TILESET_([A-Z_]+)\s*=\s*(\d+)",
                         WORLD_H.read_text()):
        ids[m.group(1)] = int(m.group(2))
    return ids


# Tileset JSON id -> C data/pal symbols (convention: g_tileset_<id>,
# g_tile_pal_<id>; cram arrays are cgb_bg_palettes* in tiles_content.c).
# A new biome adds its JSON + PNG + palette theme; this table (and the
# ROM loaders reading it) needs no C changes.
def emit_tileset_tables(tilesets):
    """Per-tileset loader tables indexed by WORLD_TILESET_* id (0..15).

    Unmapped ids render the forest row (same as the old default branch).
    """
    kind_ids = load_tileset_kind_ids()
    # tileset JSON id -> WORLD kind suffix, for JSONs carrying world art.
    want = {}
    for ts_id, ts in tilesets.items():
        kind = (ts.get("gb_tileset_kind") or "")
        if not kind.startswith("WORLD_TILESET_"):
            continue
        vb = ts.get("vram_block", {}).get("tiles", [])
        if not vb:
            continue
        suffix = kind[len("WORLD_TILESET_"):]
        if suffix not in kind_ids:
            print(f"generate_tiles: {ts_id}: {kind} has no world.h id",
                  file=sys.stderr)
            return None
        want[ts_id] = (kind_ids[suffix], len(vb))
    out = []
    out.append("/* Generated by tools/level_compiler/generate_tiles.py -- DO NOT EDIT DIRECTLY */")
    out.append("/* Per-tileset loader tables indexed by WORLD_TILESET_* id")
    out.append(" * (0..15). Unmapped ids use the forest row. A new biome is")
    out.append(" * tileset JSON + PNG + palette theme only (no C changes). */")
    forest = want.get("forest")
    if forest is None:
        print("generate_tiles: no forest tileset", file=sys.stderr)
        return None
    f_id, f_n = forest
    rows = {}
    for ts_id, (kid, n) in sorted(want.items(), key=lambda kv: kv[1][0]):
        rows[kid] = (f"g_tileset_{ts_id}", f"g_tile_pal_{ts_id}",
                     f"cgb_bg_palettes_{ts_id}", n)
    fdata, fpal, fcram, _ = rows[f_id]
    ptrs = lambda vals: "{ " + ", ".join(vals) + " }"
    out.append("static const uint8_t *const s_cram_set[16] = {")
    out.append("    " + ",\n    ".join(
        "(const uint8_t *)" + rows.get(i, (None, None, fcram, 0))[2]
        for i in range(16)))
    out.append("};")
    out.append("static const uint8_t *const s_tile_data[16] = {")
    out.append("    " + ",\n    ".join(
        rows.get(i, (fdata, None, None, 0))[0] for i in range(16)))
    out.append("};")
    out.append("static const uint8_t *const s_tile_pal[16] = {")
    out.append("    " + ",\n    ".join(
        rows.get(i, (None, fpal, None, 0))[1] for i in range(16)))
    out.append("};")
    out.append("static const uint8_t s_tile_count[16] = {")
    out.append("    " + ", ".join(
        str(rows.get(i, (None, None, None, f_n))[3]) for i in range(16)))
    out.append("};")
    out.append("")
    return "\n".join(out) + "\n"


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", required=True,
                    help="Output directory for tile_walk.h + tile_glyph.h")
    args = ap.parse_args(argv)
    tilesets = load_tilesets()
    const_by_value = load_tiletype_numbers()
    entries = landscape_entries(tilesets, const_by_value)
    if not entries:
        print("generate_tiles: no landscape entries derived", file=sys.stderr)
        return 1
    ranges = to_ranges(entries)
    # exit indices per tileset kind (WORLD_TILESET_* numbering lives in
    # scene.h; 2 = FOREST, 14 = DESOLATE, 15 = CASTLE).
    kind_of = {"forest": "FOREST", "desolate_landscape": "DESOLATE",
               "castle": "CASTLE", "village": "VILLAGE"}
    exit_idx = {}
    for ts_id, kind in kind_of.items():
        ts = tilesets.get(ts_id, {})
        for e in ts.get("vram_block", {}).get("tiles", []):
            if e.get("exit"):
                exit_idx[kind] = e["index"]
    for kind in ("FOREST", "DESOLATE", "CASTLE", "VILLAGE"):
        if kind not in exit_idx:
            print(f"generate_tiles: no exit marked for {kind}", file=sys.stderr)
            return 1
    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "tile_walk.h").write_text(emit_walk(ranges, LEGACY_WALK_DOC))
    glyph_text = emit_glyph(ranges, exit_idx, const_by_value)
    if glyph_text is None:
        return 1
    (out_dir / "tile_glyph.h").write_text(glyph_text)
    (out_dir / "tile_palette.h").write_text(emit_palette(entries, tilesets, const_by_value))
    tables_text = emit_tileset_tables(tilesets)
    if tables_text is None:
        return 1
    (out_dir / "tileset_tables.inc").write_text(tables_text)
    walk_n = sum(1 for _, _, w, _ in ranges if w)
    print(f"generate_tiles: {len(ranges)} ranges ({walk_n} walkable), "
          f"exits {exit_idx} -> {out_dir}/tile_walk.h + tile_glyph.h + tile_palette.h")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
