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


# WORLD_TILESET_* numbering (src/world/world.h). Only sets listed here
# participate in background animation.
ANIM_KIND = {"forest": 2, "village": 12, "desolate_landscape": 14}


def emit_anim_pairs(tilesets):
    """Background animation pairs: anim_pairs.h.

    Tiles named *_fire_frame_1 with a *_fire_frame_2 sibling form a flicker
    pair (campfires). Emits (tileset-kind, VRAM-slot-A, VRAM-slot-B) triples
    the renderer alternates on its animation timer -- data-driven, no
    hardcoded coordinates (the old MAP_SOUTH_FIELD special case is gone).
    Dog/hero *_frame_* tiles are NOT animation (static decor) and never match.
    """
    out = []
    out.append("/* Generated by tools/level_compiler/generate_tiles.py -- DO NOT EDIT DIRECTLY */")
    out.append("/* Campfire flicker pairs: (WORLD_TILESET kind, VRAM slot frame 1,")
    out.append(" * VRAM slot frame 2). The renderer shows A on even steps, B on odd. */")
    out.append("#ifndef ANIM_PAIRS_H")
    out.append("#define ANIM_PAIRS_H")
    out.append("")
    out.append("#include <stdint.h>")
    out.append("")
    pairs = []
    for ts_id in ("forest", "village", "desolate_landscape"):
        ts = tilesets.get(ts_id, {})
        by_tile = {}
        for e in ts.get("vram_block", {}).get("tiles", []):
            if "tile" in e and "index" in e:
                by_tile[e["tile"]] = e["index"]
        ids = {t["id"] for t in ts.get("tiles", [])}
        for t in sorted(ids):
            if not t.endswith("_fire_frame_1"):
                continue
            sib = t[:-1] + "2"
            if sib not in ids or t not in by_tile or sib not in by_tile:
                print(f"generate_tiles: {ts_id}: unpaired fire frame '{t}'",
                      file=sys.stderr)
                return None
            pairs.append((ANIM_KIND[ts_id], by_tile[t], by_tile[sib]))
    out.append(f"#define ANIM_PAIR_COUNT {len(pairs)}")
    out.append("static const uint8_t kAnimPairs[] = {%s};" %
               ", ".join(f"{k}, {a}, {b}" for k, a, b in pairs))
    out.append("")
    out.append("#endif /* ANIM_PAIRS_H */")
    out.append("")
    return "\n".join(out) + "\n"


def emit_palette():
    """Per-tileset palette index arrays: tile_palette.h.

    Each tileset gets the manifest's full VRAM-slot-order array
    (generated/tiles/<tileset>.json, tools/palette_compiler.py). The
    runtime lookup is a single pointer-indexed read:
        pal = pal_tbl[tile_id - TILESET_BASE];
    No multiplication, no glyph intermediate, O(1).

    Lengths (48/48/16/48) must match rpg_tile_lookup.h; a mismatch is a
    loud SDCC error, never silent wrong colors. There is no heuristic
    fallback (luminance guessing was deleted with the old pipeline).
    """
    out = []
    out.append("/* Generated by tools/level_compiler/generate_tiles.py -- DO NOT EDIT DIRECTLY */")
    out.append("/* Per-tileset CGB palette lookup tables: tileset-tiles order -> palette 0..7. */")
    out.append("#ifndef TILE_PALETTE_H")
    out.append("#define TILE_PALETTE_H")
    out.append("")
    out.append("#include <stdint.h>")
    out.append("")

    kind_id_map = {"forest": "FOREST", "desolate_landscape": "DESOLATE",
                   "castle": "CASTLE", "village": "VILLAGE"}

    for ts_id in ("forest", "desolate_landscape", "castle", "village"):
        kind = kind_id_map.get(ts_id, ts_id.upper())

        manifest = load_palette_manifest(ts_id)
        if not manifest or "tile_palettes" not in manifest:
            print(f"generate_tiles: missing manifest generated/tiles/{ts_id}.json "
                  f"(run make manifest)", file=sys.stderr)
            return None
        # Full VRAM-slot-order array, emitted as-is (lengths 48/48/16/48
        # must match rpg_tile_lookup.h; SDCC fails loudly otherwise).
        pal_values = manifest["tile_palettes"]

        arr_name = f"g_tile_pal_{ts_id}"
        if ts_id == "desolate_landscape":
            arr_name = "g_tile_pal_desolate"
        out.append(f"/* {kind} tileset: {len(pal_values)} tiles (palette_compiler manifest) */")
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
    palette_text = emit_palette()
    if palette_text is None:
        return 1
    (out_dir / "tile_palette.h").write_text(palette_text)
    anim_text = emit_anim_pairs(tilesets)
    if anim_text is None:
        return 1
    (out_dir / "anim_pairs.h").write_text(anim_text)
    walk_n = sum(1 for _, _, w, _ in ranges if w)
    print(f"generate_tiles: {len(ranges)} ranges ({walk_n} walkable), "
          f"exits {exit_idx} -> {out_dir}/tile_walk.h + tile_glyph.h + tile_palette.h")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
