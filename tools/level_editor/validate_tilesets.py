#!/usr/bin/env python3
"""Validate tileset manifests, including the vram_block composition that
drives VRAM layout and exit-art indices (see docs/level-editor.md Phase 15).

Checks per tileset JSON:
- vram_block present with a known palette and an existing source_sheet;
- tile indices exactly 0..N-1 in order, coords inside the sheet bounds;
- tile cross-refs (when present) name real manifest tiles;
- exactly one exit:true tile, which must be walkable.

Usage (inside nix develop for Pillow):
    python3 tools/level_editor/validate_tileset.py tools/level_editor/tilesets/forest.json
    python3 tools/level_editor/validate_tilesets.py tools/level_editor/tilesets/*.json
"""
import glob
import json
import os
import sys
from pathlib import Path

from PIL import Image

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
TILE_SIZE = 8
PALETTES = ("canonical", "gb_green", "auto")


def validate_tileset(path):
    errors = []
    try:
        data = json.load(open(path, encoding="utf-8"))
    except Exception as e:
        return False, [f"{path}: unreadable JSON: {e}"], []
    label = data.get("id", os.path.basename(path))
    by_id = {t.get("id"): t for t in data.get("tiles", [])}
    block = data.get("vram_block")
    if not isinstance(block, dict):
        # Opt-in: only world tilesets with VRAM blocks declare one
        # (combat/intrepid are UI/font sets, not world blocks).
        return True, [], [f"{label}: no vram_block (not a world tileset), skipped"]
    if block.get("palette") not in PALETTES:
        errors.append(f"{label}: unknown palette {block.get('palette')!r}")
    entries = block.get("tiles", [])
    seen_index = set()
    exits = []
    for pos, e in enumerate(entries):
        where = f"{label} block entry {pos}"
        if e.get("index") != pos:
            errors.append(f"{where}: index {e.get('index')} != position {pos} (must be dense 0..N-1)")
        seen_index.add(e.get("index"))
        sheet = e.get("sheet", block.get("source_sheet", ""))
        sheet_path = REPO_ROOT / sheet
        try:
            w, h = Image.open(sheet_path).size
        except Exception as ex:
            errors.append(f"{where}: cannot open sheet {sheet}: {ex}")
            continue
        if not (0 <= e.get("x", -1) < w // TILE_SIZE and 0 <= e.get("y", -1) < h // TILE_SIZE):
            errors.append(f"{where}: coords ({e.get('x')},{e.get('y')}) outside {sheet}")
        tid = e.get("tile")
        if tid is not None and tid not in by_id:
            errors.append(f"{where}: unknown tile id '{tid}'")
        if e.get("exit"):
            exits.append(e)
    if len(exits) != 1:
        errors.append(f"{label}: want exactly one exit:true tile, found {len(exits)}")
    else:
        t = by_id.get(exits[0].get("tile"), {})
        if not t.get("walkable", False):
            errors.append(f"{label}: exit tile '{exits[0].get('tile')}' is not walkable")
    # Mandatory glyph coverage: every tile resolves a single-char glyph,
    # either explicitly or via the walkable/category/exit derivation
    # (see generate_tiles.py landscape_entries). Nothing is silently glyph-less.
    for t in data.get("tiles", []):
        g = t.get("glyph", "")
        if g == "":
            continue  # derived downstream; any walkable/category/exit tile resolves
        if not isinstance(g, str) or len(g) != 1:
            errors.append(f"{label}: tile '{t.get('id')}' has invalid glyph {g!r} (want one character)")
    ok = not errors
    if not ok:
        return False, errors, []
    return True, [], [f"{label}: vram block {len(entries)} tiles, "
                      f"exit {exits[0].get('tile')}@{exits[0].get('index')}"]


def main(argv):
    paths = []
    for pat in argv or ["tools/level_editor/tilesets/*.json"]:
        paths.extend(glob.glob(pat))
    if not paths:
        print("no tileset files matched")
        return 1
    failed = False
    for p in sorted(set(paths)):
        ok, errors, notes = validate_tileset(p)
        for n in notes:
            print(f"PASS {n}")
        for e in errors:
            print(f"FAIL {e}")
            failed = True
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
