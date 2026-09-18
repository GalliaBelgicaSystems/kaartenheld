#!/usr/bin/env python3
"""verify_palette_manifest.py -- emulator-free check of the world tile palettes.

Runs after palette_compiler.py (wired into `make manifest`, so every build
and CI job executes it). It recomputes, independently of the compiler's own
ordering code, what palette each VRAM slot SHOULD get and compares that with
generated/tiles/<tileset>.json (which becomes g_tile_pal_* in tile_palette.h,
indexed by VRAM slot at runtime: g_active_tile_palette[i] = pal_src[i]).

Checks, per world tileset (forest, desolate_landscape, castle, village):

  1. Length equals the C array size declared in src/gfx/rpg_tile_lookup.h.
  2. VRAM slot i sits at the sheet cell the gfx rule encodes into slot i:
     row-major over the sheet width, except castle (9-wide sheet, 8-wide
     VRAM packing: slot i = cell (i % 8, i // 8), see the Makefile
     --tile-coords). A tileset whose vram_block index disagrees fails.
  3. tile_ids[i] is the tile the vram_block puts at index i.
  4. tile_palettes[i] is a slot whose colors equal the shade-map ramp that
     png2gb encodes slot i's bytes with (alias slots holding the same colors
     are accepted; a different ramp is a scrambled table).
  5. No world tile uses UI_COLOR_PAPER (slot 4): ui_draw_dialogue
     re-programs CRAM slot 4 to a white/black ramp while a dialogue box is
     open, so any world tile on slot 4 would flip to paper colors
     (src/ui/ui.h, AGENTS.md 52.11).

Exits non-zero with an actionable message per violation.
"""

import json
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
GEN = REPO_ROOT / "generated" / "tiles"
TILESETS = REPO_ROOT / "tools" / "level_editor" / "tilesets"
ASSETS = REPO_ROOT / "assets"
LOOKUP_H = REPO_ROOT / "src" / "gfx" / "rpg_tile_lookup.h"

PAPER_SLOT = 4  # UI_COLOR_PAPER in src/ui/ui.h

# tileset -> (sheet png, C array suffix in rpg_tile_lookup.h, VRAM pack width)
# pack width None = the sheet's own width (forest/desolate/village scan order).
WORLD = {
    "forest": ("forest-tile.png", "forest", None),
    "desolate_landscape": ("desolate-tile.png", "desolate", None),
    "castle": ("castle-tile.png", "castle", 8),
    "village": ("village-tile.png", "village", None),
}


def declared_lengths():
    text = LOOKUP_H.read_text()
    return {m.group(1): int(m.group(2))
            for m in re.finditer(r"g_tile_pal_(\w+)\[(\d+)\]", text)}


def main():
    from PIL import Image

    errors = []
    lengths = declared_lengths()
    total = 0

    for ts, (png, c_suffix, pack_w) in WORLD.items():
        try:
            man = json.loads((GEN / f"{ts}.json").read_text())
            shades = json.loads((GEN / "shades" / f"{ts}.json").read_text())["tiles"]
            ts_data = json.loads((TILESETS / f"{ts}.json").read_text())
        except (OSError, ValueError, KeyError) as e:
            errors.append(f"{ts}: cannot load manifest inputs ({e}); run `make manifest`")
            continue

        pal = man["tile_palettes"]
        ids = man["tile_ids"]
        n = len(pal)
        total += n

        want_n = lengths.get(c_suffix)
        if want_n is None:
            errors.append(f"{ts}: g_tile_pal_{c_suffix} not declared in {LOOKUP_H.name}")
        elif want_n != n:
            errors.append(f"{ts}: manifest has {n} palette entries but "
                          f"g_tile_pal_{c_suffix}[{want_n}] is declared in {LOOKUP_H.name}")

        sheet_w = Image.open(ASSETS / png).size[0] // 8
        w = pack_w or sheet_w
        vram = {v["index"]: v for v in (ts_data.get("vram_block") or {}).get("tiles", [])
                if "tile" in v and "index" in v}
        slot_colors = {p["index"]: p["colors"] for p in man["palettes"]}

        for i in range(n):
            v = vram.get(i)
            if v is None:
                errors.append(f"{ts}: VRAM slot {i} has no vram_block entry")
                continue
            cell = (v["x"], v["y"])
            enc_cell = (i % w, i // w)
            if cell != enc_cell:
                errors.append(
                    f"{ts}: VRAM slot {i} ('{v['tile']}') is vram_block cell {cell}, but the "
                    f"gfx rule encodes cell {enc_cell} into slot {i} (row-major, width {w})")
                continue
            if ids[i] != v["tile"]:
                errors.append(
                    f"{ts}: tile_ids[{i}] = '{ids[i]}' but vram_block index {i} is '{v['tile']}' "
                    f"(manifest not in VRAM-slot order)")
            ramp_hex = shades.get("%d,%d" % cell)
            if ramp_hex is None:
                errors.append(f"{ts}: no shade-map entry for cell {cell} (slot {i})")
                continue
            ok_slots = sorted(s for s, cols in slot_colors.items() if cols == ramp_hex)
            if pal[i] not in ok_slots:
                errors.append(
                    f"{ts}: slot {i} ('{v['tile']}') is encoded with ramp {ramp_hex} "
                    f"(palette slot(s) {ok_slots or 'NONE'}) but tile_palettes[{i}] = {pal[i]}")
            if pal[i] == PAPER_SLOT:
                errors.append(
                    f"{ts}: slot {i} ('{v['tile']}') uses palette slot {PAPER_SLOT} = UI_COLOR_PAPER; "
                    f"ui_draw_dialogue re-programs it to white/black, so this tile would flip colors "
                    f"during dialogue. Move its ramp to another slot in tools/palette_slots.json.")

    if errors:
        print("verify_palette_manifest: FAILED", file=sys.stderr)
        for e in errors:
            print("  - " + e, file=sys.stderr)
        return 1
    print(f"verify_palette_manifest: OK ({len(WORLD)} tilesets, {total} VRAM slots)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
