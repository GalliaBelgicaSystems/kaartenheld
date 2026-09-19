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

Plus the battle card/icon check:

  6. Every card_frames.png cell is encoded with the ramp of the slot the
     runtime paints it on (encode == display: skins name UI slots and the
     hand/HUD renderer stamps those slots per src/ui/ui_battle_content.c),
     and every pixel color in the cell exists in that display ramp
     (a pixel absent from the display ramp renders in a wrong shade even
     when the encode ramp matches, e.g. white pixels on a white-less
     fight2 box, or brown pixels on a brown-less fight5 dagger).

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


def check_card_frames(errors):
    """Check 6: encode ramp == display ramp per card_frames.png cell, and
    every pixel color exists in the display ramp.

    Reads the generated display mapping (generated/tiles/card_display_slots.json,
    emitted by palette_compiler.card_display_slots) -- the same source the
    encoder and the accounting doc use -- so this check cannot drift from
    them.  The runtime paints those slots per src/ui/ui_battle_content.c.
    Returns the number of sheet cells checked.
    """
    from compose_card_frames import LAYOUT
    from palette_compiler import sheet_cells

    try:
        display = json.loads((GEN / "card_display_slots.json").read_text())
        shades = json.loads((GEN / "shades" / "card_frames.json").read_text())["tiles"]
        base = json.loads((GEN / "base.json").read_text())["slots"]
    except (OSError, ValueError, KeyError) as e:
        errors.append(f"card_frames: cannot load generated manifest inputs ({e}); run `make manifest`")
        return 0
    names = {}
    for y, row in enumerate(LAYOUT):
        for x, name in enumerate(row):
            if name is not None:
                names["%d,%d" % (x, y)] = name
    slot_colors = {int(i): v["colors"] for i, v in base.items()}
    try:
        cells = sheet_cells(ASSETS / "card_frames.png")
    except (OSError, ValueError) as e:
        errors.append(f"card_frames: cannot read sheet pixels ({e})")
        return 0

    n = 0
    for key in sorted(display):
        n += 1
        slot = display[key]
        name = names.get(key, "?")
        x, y = (int(v) for v in key.split(","))
        if slot not in slot_colors:
            errors.append(
                f"card_frames: cell ({x},{y}) '{name}' displays on slot {slot}, "
                f"which has no ramp in set 'base' (tools/palette_slots.json)")
            continue
        want = list(slot_colors[slot])
        got = shades.get(key)
        if got is None:
            errors.append(f"card_frames: cell ({x},{y}) '{name}' has no shade-map entry; run `make manifest`")
            continue
        if list(got) != want:
            errors.append(
                f"card_frames: cell ({x},{y}) '{name}' is encoded with {got} but "
                f"displays on slot {slot} ({want}); point the skin color at the "
                f"display slot so encode == display")
        pix = sorted("#%02x%02x%02x" % tuple(c) for c in cells.get((x, y), []))
        off = [c for c in pix if c not in want]
        if off:
            errors.append(
                f"card_frames: cell ({x},{y}) '{name}' has pixel colors {off} absent "
                f"from display slot {slot} ({want}); repaint the cell into the "
                f"display ramp or rebind the slot")
    return n


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
    card_n = check_card_frames(errors)
    if errors:
        print("verify_palette_manifest: FAILED", file=sys.stderr)
        for e in errors:
            print("  - " + e, file=sys.stderr)
        return 1
    print(f"verify_palette_manifest: OK ({len(WORLD)} tilesets, {total} VRAM slots, {card_n} card cells)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
