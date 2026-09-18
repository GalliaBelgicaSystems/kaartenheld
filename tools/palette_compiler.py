#!/usr/bin/env python3
"""palette_compiler.py -- Ramps win. Resolve artist ramps to hardware slots.

Inputs (single sources of truth):
  assets/palette.txt                        artist colors + ramp rows
  tools/palette_slots.json                  dev ramp -> slot numbers
  tools/level_editor/tilesets/*.json        per-tile ramp tags (world)
  screens/combat_art/*.json                 per-set BG ramp (battle art)
  screens/enemy_types/*.json, hero.json     per-enemy OW ramp (sprites)

Rule: every tile is ALWAYS colored with an EXISTING artist ramp, even if
wrong. The declared ramp (tag) wins when present; otherwise the nearest
slotted ramp in the sheet's set wins. The build stays green. Wrongness is
reported in generated/tiles/ramp_mismatches.json (the artist todo list).

There is no approximation, no quantization to invented colors, no
luminance guessing, no anchor pinning: pixels map to shades of the chosen
ramp only, by exact match or nearest shade within that ramp.

Outputs (all under generated/tiles/, regenerable, uncommitted):
  <world>.json        {palettes[{index,name,colors}], tile_ids[id|null...],
                       tile_palettes[slot...], tile_ramps[ramp...]} in VRAM-slot
                       order (editor + tile_palette.h)
  base|obj|title.json {slots: {i: {ramp, colors}}} (battle_compile + editor)
  cgb_palettes.inc    C definitions: 5 BG tables + NPC display slots (tiles_content.c)
  cgb_obj_palettes.inc C definition: 5 OBJ ramps (ui.c)
  shades/<sheet>.json per-cell ordered ramp hexes (png2gb --shade-map)
  ramp_mismatches.json non-exact tiles (sheet, cell, declared, used, off-colors)
"""

import sys
import json
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO_ROOT / "tools"))
from palette_parse import parse_palette

TILESETS_DIR = REPO_ROOT / "tools" / "level_editor" / "tilesets"
ASSETS_DIR = REPO_ROOT / "assets"
GENERATED_DIR = REPO_ROOT / "generated" / "tiles"
SHADES_DIR = GENERATED_DIR / "shades"

TILE_SIZE = 8

# Sheet inventory: key -> (png file, palette set).
SHEETS = {
    "forest": ("forest-tile.png", "forest"),
    "desolate_landscape": ("desolate-tile.png", "desolate_landscape"),
    "castle": ("castle-tile.png", "castle"),
    "village": ("village-tile.png", "village"),
    "battle": ("battle_sprites.png", "base"),
    "enemy_ow": ("enemy_sprites.png", "obj"),
    "hero_ow": ("hero_sprites.png", "obj"),
    "npc": ("npc_tiles.png", "village"),
    "card_frames": ("card_frames.png", "base"),
    "title": ("title-red.png", "title"),
}

# npc_tiles.png cell order (compose_npc_tiles.LAYOUT) -> BG display slot.
# Guard, wizard and dogs render with the FIELD-ish slot, merchant with
# WOOD, mayor with GOLD. The generated cgb_palettes.inc carries these
# numbers to C; this list is the single source.
NPC_DISPLAY_SLOTS = [3, 3, 5, 6, 3, 3]


def _hex(rgb):
    return "#%02x%02x%02x" % rgb


def _dist2(a, b):
    return (a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2 + (a[2] - b[2]) ** 2


def load_slotmap():
    """Returns {set: {slot: ramp}}. Shape errors fail loudly."""
    data = json.loads((REPO_ROOT / "tools" / "palette_slots.json").read_text())
    sets = data.get("sets")
    if not isinstance(sets, dict) or not sets:
        raise ValueError("palette_slots.json: want non-empty 'sets' object")
    out = {}
    for setname, slots in sets.items():
        if not isinstance(slots, dict):
            raise ValueError(f"palette_slots.json: set '{setname}' wants an object")
        norm = {}
        for k, v in slots.items():
            if k.startswith("_"):
                continue
            try:
                slot = int(k)
            except ValueError:
                raise ValueError(f"palette_slots.json: set '{setname}': bad slot '{k}'")
            if not (0 <= slot <= 7):
                raise ValueError(f"palette_slots.json: set '{setname}': slot {slot} out of 0-7")
            if slot in norm:
                raise ValueError(f"palette_slots.json: set '{setname}': duplicate slot {slot}")
            norm[slot] = v
        out[setname] = norm
    return out


def sheet_cells(png_path):
    """Returns {coord: [rgb...]} unique colors per 8x8 cell."""
    from PIL import Image
    img = Image.open(png_path).convert("RGB")
    w, h = img.size
    if w % TILE_SIZE or h % TILE_SIZE:
        raise ValueError(f"{png_path}: {w}x{h} not a multiple of 8x8")
    px = img.load()
    cells = {}
    for ty in range(h // TILE_SIZE):
        for tx in range(w // TILE_SIZE):
            ox, oy = tx * TILE_SIZE, ty * TILE_SIZE
            cells[(tx, ty)] = list({px[ox + x, oy + y]
                                    for y in range(TILE_SIZE)
                                    for x in range(TILE_SIZE)})
    return cells


def main():
    import PIL.Image  # noqa: F401 (clear error if Pillow is missing)
    _, ramps = parse_palette()
    slotmap = load_slotmap()

    for setname, slots in slotmap.items():
        for slot, ramp in slots.items():
            if ramp not in ramps:
                raise ValueError(
                    f"palette_slots.json: set '{setname}' slot {slot}: unknown ramp '{ramp}'")
    tables = {s: {i: ramps[r] for i, r in slots.items()} for s, slots in slotmap.items()}

    # World tileset tags: {tileset: ({tileid: ramp}, {tileid: coord})}.
    world_sets = ["forest", "castle", "desolate_landscape", "village"]
    tags = {}
    for ts in world_sets:
        ts_data = json.loads((TILESETS_DIR / f"{ts}.json").read_text())
        table, coords = {}, {}
        for t in ts_data.get("tiles", []):
            ramp = t.get("palette")
            if ramp is not None:
                if ramp not in ramps:
                    raise ValueError(
                        f"tilesets/{ts}.json: tile '{t.get('id')}': unknown ramp '{ramp}'")
                table[t.get("id")] = ramp
        for v in (ts_data.get("vram_block") or {}).get("tiles", []):
            if "tile" in v:
                coords[v["tile"]] = (v["x"], v["y"])
        tags[ts] = (ts_data, table, coords)

    # Battle art: {cellname: BG ramp}.
    from compose_battle_sprites import TILE_COORDS as BATTLE_CELLS
    from compose_enemy_sprites import TILE_COORDS as ENEMY_CELLS
    from compose_hero_sprites import TILE_COORDS as HERO_CELLS
    from compose_npc_tiles import LAYOUT as NPC_LAYOUT
    from compose_card_frames import LAYOUT as CARD_LAYOUT
    sys.path.insert(0, str(REPO_ROOT / "tools" / "screen_compiler"))
    from battle_compile import SKIN_COLORS, DEFAULT_SKIN, DEFAULT_HUD
    battle_declared = {}
    for path in sorted((REPO_ROOT / "screens" / "combat_art").glob("*.json")):
        data = json.loads(path.read_text())
        pal = data.get("palette")
        if pal not in ramps:
            raise ValueError(f"combat_art/{path.name}: unknown palette ramp '{pal}'")
        obj = data.get("obj_palette")
        if obj is not None and obj not in ramps:
            raise ValueError(f"combat_art/{path.name}: unknown obj_palette ramp '{obj}'")
        cells = list(data.get("frame0", []))
        f1 = data.get("frame1")
        cells += f1 if f1 is not None else data.get("frame0", [])
        for name in cells:
            if name is not None:
                battle_declared[name] = pal

    # Enemy/hero overworld: {cellname: ramp or None}.
    ow_declared = {}
    for path in sorted((REPO_ROOT / "screens" / "enemy_types").glob("*.json")):
        data = json.loads(path.read_text())
        ow = data.get("overworld") or {}
        pal = ow.get("palette", 0)
        ramp = pal if isinstance(pal, str) else None
        if ramp is not None and ramp not in ramps:
            raise ValueError(f"enemy_types/{path.name}: unknown ow ramp '{ramp}'")
        for cell in ow.get("cells", []):
            ow_declared[cell] = ramp
    hero_data = json.loads((REPO_ROOT / "screens" / "hero.json").read_text())
    hero_pal = (hero_data.get("overworld") or {}).get("palette", 0)
    hero_ramp = hero_pal if isinstance(hero_pal, str) else None
    if hero_ramp is not None and hero_ramp not in ramps:
        raise ValueError(f"hero.json: unknown ow ramp '{hero_ramp}'")

    GENERATED_DIR.mkdir(parents=True, exist_ok=True)
    SHADES_DIR.mkdir(parents=True, exist_ok=True)
    mismatches = []

    def compile_sheet(key, declared):
        """Color every cell of a sheet with an existing ramp.

        declared: {coord: ramp} or a single ramp name for all cells.
        Returns {coord: used_ramp}. Non-exact cells go to mismatches.
        """
        fname, setname = SHEETS[key]
        if setname not in tables:
            raise ValueError(f"sheet '{key}': unknown set '{setname}'")
        slots = tables[setname]
        slot_names = slotmap[setname]
        cand = []
        for i in sorted(slot_names):
            if slot_names[i] not in [c[0] for c in cand]:
                cand.append((slot_names[i], slots[i]))
        cells = sheet_cells(ASSETS_DIR / fname)
        if isinstance(declared, str):
            declared = {c: declared for c in cells}
        used, shades = {}, {}
        for coord in sorted(cells):
            pixel_colors = cells[coord]
            want = declared.get(coord)
            if want is not None and want not in [r for r, _ in cand]:
                mismatches.append({"sheet": key, "tile": list(coord),
                                   "declared": want, "used_ramp": None,
                                   "reason": f"tagged ramp has no slot in set '{setname}'; winner used instead",
                                   "off_colors": []})
                want = None
            if want is None:
                best, best_cost = None, None
                for name, quad in cand:
                    cost = sum(min(_dist2(c, s) for s in quad) for c in pixel_colors)
                    if best_cost is None or cost < best_cost:
                        best, best_cost = name, cost
                ramp = best
                quad = ramps[ramp]
                off = sorted(_hex(c) for c in pixel_colors if c not in quad)
                mismatches.append({"sheet": key, "tile": list(coord),
                                   "declared": None, "used_ramp": ramp,
                                   "reason": "no tag: nearest slotted ramp wins",
                                   "off_colors": off})
            else:
                ramp = want
                quad = ramps[ramp]
                off = sorted(_hex(c) for c in pixel_colors if c not in quad)
                if off:
                    mismatches.append({"sheet": key, "tile": list(coord),
                                       "declared": want, "used_ramp": ramp,
                                       "reason": "tagged ramp stands; off-ramp pixels use nearest shade",
                                       "off_colors": off})
            used[coord] = ramp
            shades["%d,%d" % coord] = [_hex(c) for c in ramps[ramp]]
        # OBJ sheets: OAM color index 0 is transparent, so png2gb must not
        # let an off-ramp pixel fall back into shade 0 (see png2gb.py).
        (SHADES_DIR / f"{key}.json").write_text(json.dumps(
            {"sheet": key, "png": fname, "set": setname,
             "transparent_shade0": setname == "obj", "tiles": shades}, indent=1))
        return used

    world_used = {}
    world_vram = {}
    for ts in world_sets:
        _, table, _ = tags[ts]
        ts_data = json.loads((TILESETS_DIR / f"{ts}.json").read_text())
        vram_id = {(v["x"], v["y"]): v["tile"]
                   for v in (ts_data.get("vram_block") or {}).get("tiles", []) if "tile" in v}
        world_vram[ts] = vram_id
        declared = {c: table[tid] for c, tid in vram_id.items() if tid in table}
        world_used[ts] = compile_sheet(ts, declared)

    battle_used = compile_sheet(
        "battle", {BATTLE_CELLS[n]: r for n, r in battle_declared.items() if n in BATTLE_CELLS})
    compile_sheet(
        "enemy_ow", {ENEMY_CELLS[n]: r for n, r in ow_declared.items()
                     if r is not None and n in ENEMY_CELLS})
    compile_sheet(
        "hero_ow", {HERO_CELLS[n]: hero_ramp for n in
                    (hero_data.get("overworld") or {}).get("cells", []) if n in HERO_CELLS})
    compile_sheet(
        "npc", {(x, 0): slotmap["village"][NPC_DISPLAY_SLOTS[x]] for x in range(len(NPC_LAYOUT))})
    def slot_ramp(setname, slot):
        ramp = slotmap[setname][slot]
        return ramp

    def skin_slot(color_name):
        if color_name not in SKIN_COLORS:
            raise ValueError(f"skin color '{color_name}' is not a UI slot name")
        return SKIN_COLORS[color_name]

    # Card-frame sheet: every icon encodes with the ramp of the slot that
    # displays it (skins name UI slots; battle UI stamps those slots).
    # Shared frame borders + select arrow encode with the canonical card
    # ramp fight1 (cards tint per type at stamp time, by engine design).
    skin_path = REPO_ROOT / "screens" / "cards_skin.json"
    skin = json.loads(skin_path.read_text()) if skin_path.exists() else DEFAULT_SKIN
    hud_path = REPO_ROOT / "screens" / "battle_hud.json"
    hud = json.loads(hud_path.read_text()) if hud_path.exists() else DEFAULT_HUD
    card_cells = {}
    for _y, _row in enumerate(CARD_LAYOUT):
        for _x, _name in enumerate(_row):
            if _name is not None:
                card_cells[_name] = (_x, _y)
    card_declared = {}
    for tkey, tdef in (skin.get("types") or {}).items():
        slot = skin_slot(tdef["color"])
        if tdef["icon"] in card_cells:
            card_declared[card_cells[tdef["icon"]]] = slot_ramp("base", slot)
        for uname in tdef.get("uses_icons", []) + [tdef.get("uses_power_icon")]:
            if uname in card_cells:
                card_declared[card_cells[uname]] = slot_ramp("base", slot)
    for ekey, edef in (skin.get("elements") or {}).items():
        slot = skin_slot(edef["color"])
        if edef["icon"] in card_cells:
            card_declared[card_cells[edef["icon"]]] = slot_ramp("base", slot)
    for hkey in ("hp", "ap", "deck"):
        hdef = hud.get(hkey) or {}
        if hdef.get("icon") in card_cells:
            card_declared[card_cells[hdef["icon"]]] = slot_ramp("base", skin_slot(hdef["color"]))
    bar = hud.get("bar") or {}
    for bkey in ("filled", "empty"):
        if bar.get(bkey) in card_cells:
            card_declared[card_cells[bar[bkey]]] = slot_ramp("base", skin_slot(bar["color"]))
    for _name, _coord in card_cells.items():
        card_declared.setdefault(_coord, "fight1")

    compile_sheet("card_frames", card_declared)
    compile_sheet("title", "title_logo")

    # World manifests in VRAM-slot order (the ROM indexes g_tile_pal_*
    # by VRAM slot). Forest/village/desolate pack vram index == scan
    # position; castle packs 8-wide against a 9-wide sheet, so its 16
    # slots follow vram-index order, not scan order (encoding matches:
    # see the castle gfx rule's --tile-coords).
    from PIL import Image as _Image
    set_of_ts = {"forest": "forest", "castle": "castle",
                 "desolate_landscape": "desolate_landscape", "village": "village"}
    for ts in world_sets:
        setname = set_of_ts[ts]
        fname, _ = SHEETS[ts]
        w, h = _Image.open(ASSETS_DIR / fname).size
        w //= TILE_SIZE
        vram_id = world_vram[ts]
        if ts == "castle":
            ts_data = json.loads((TILESETS_DIR / f"{ts}.json").read_text())
            # Sort by vram `index` (NOT by (x, y): that is column-major and
            # scrambles slots 1..14 against the row-major --tile-coords
            # the castle gfx rule encodes with).
            ordered = sorted((v["index"], (v["x"], v["y"]), v["tile"])
                             for v in (ts_data.get("vram_block") or {}).get("tiles", [])
                             if "tile" in v and v.get("index", 99) < 16)
            cells = [c for _, c, _ in ordered]
            ids = [t for _, _, t in ordered]
        else:
            ids = []
            cells = []
            n = {"forest": 48, "desolate_landscape": 48, "village": 48}[ts]
            for i in range(n):
                coord = (i % w, i // w)
                cells.append(coord)
                ids.append(vram_id.get(coord))
        slot_of_ramp = {}
        for i, r in sorted(slotmap[setname].items()):
            slot_of_ramp.setdefault(r, i)
        tile_ramps = [world_used[ts][c] for c in cells]
        tile_slots = [slot_of_ramp[r] for r in tile_ramps]
        palettes = [{"index": i, "name": slotmap[setname][i],
                     "colors": [_hex(c) for c in tables[setname][i]]}
                    for i in sorted(slotmap[setname])]
        (GENERATED_DIR / f"{ts}.json").write_text(json.dumps(
            {"tileset": ts, "palettes": palettes, "tile_ids": ids,
             "tile_palettes": tile_slots, "tile_ramps": tile_ramps}, indent=1))

    # Set manifests for battle_compile + the editor.
    for setname in ("base", "obj", "title"):
        (GENERATED_DIR / f"{setname}.json").write_text(json.dumps(
            {"set": setname,
             "slots": {str(i): {"ramp": slotmap[setname][i],
                                "colors": [_hex(c) for c in tables[setname][i]]}
                       for i in sorted(slotmap[setname])}}, indent=1))

    # C includes.
    def c_ramp(quad):
        return "{ %s }" % ", ".join("RGB8(%d,%d,%d)" % c for c in quad)

    names = {"base": "cgb_bg_palettes", "forest": "cgb_bg_palettes_forest",
             "desolate_landscape": "cgb_bg_palettes_desolate",
             "castle": "cgb_bg_palettes_castle", "village": "cgb_bg_palettes_village"}
    out = ["/* Generated by tools/palette_compiler.py -- DO NOT EDIT DIRECTLY */", ""]
    for setname in ("base", "forest", "desolate_landscape", "castle", "village"):
        out.append(f"const palette_color_t {names[setname]}[8][4] = {{")
        for i in sorted(slotmap[setname]):
            out.append(f"    /* {i} {slotmap[setname][i]} */ {c_ramp(tables[setname][i])},")
        out.append("};")
        out.append("")
    out.append("/* Village NPC overlay display slots (compose_npc_tiles.LAYOUT order). */")
    out.append("static const uint8_t g_npc_display_pals[6] = {%s};"
               % ", ".join(str(s) for s in NPC_DISPLAY_SLOTS))
    out.append("")
    (GENERATED_DIR / "cgb_palettes.inc").write_text("\n".join(out))

    out = ["/* Generated by tools/palette_compiler.py -- DO NOT EDIT DIRECTLY */",
           "/* Overworld OBJ ramps (slots 0-4, verbatim artist colors);", " * slots 5-7 stay neutral grey. */", ""]
    out.append("static const palette_color_t cgb_obj_palettes[8][4] = {")
    for i in sorted(slotmap["obj"]):
        out.append(f"    /* {i} {slotmap['obj'][i]} */ {c_ramp(tables['obj'][i])},")
    for i in (5, 6, 7):
        out.append(f"    /* {i} grey (unassigned) */ {c_ramp([(255, 255, 255), (170, 170, 170), (85, 85, 85), (0, 0, 0)])},")
    out.append("};")
    out.append("")
    (GENERATED_DIR / "cgb_obj_palettes.inc").write_text("\n".join(out))

    real = [m for m in mismatches if m["off_colors"] or m["used_ramp"] is None]
    (GENERATED_DIR / "ramp_mismatches.json").write_text(json.dumps(real, indent=1))

    write_accounting()

    slotted = {r for slots in slotmap.values() for r in slots.values()}
    spare = sorted(set(ramps) - slotted)
    print(f"palette_compiler: {len(ramps)} ramps, {len(slotted)} slotted, "
          f"{len(real)} reported tiles")
    if spare:
        print(f"  unslotted (no hardware slot, info only): {', '.join(spare)}")


def write_accounting():
    """Regenerate docs/accounting-tiles-sprites-vs-ramps.md from live inputs.

    Runs on every `make manifest`. Fully deterministic (sorted output, no
    timestamps): rerunning reproduces the file byte-identically.
    """
    from compose_battle_sprites import TILE_COORDS as BATTLE_CELLS
    from compose_enemy_sprites import TILE_COORDS as ENEMY_CELLS
    from compose_hero_sprites import TILE_COORDS as HERO_CELLS
    from compose_npc_tiles import LAYOUT as NPC_LAYOUT
    from compose_card_frames import LAYOUT as CARD_LAYOUT
    from battle_compile import SKIN_COLORS, DEFAULT_SKIN, DEFAULT_HUD

    _, ramps = parse_palette()
    slotmap = load_slotmap()
    slot_of = {}
    for setname, slots in slotmap.items():
        for i, r in sorted(slots.items()):
            slot_of.setdefault((setname, r), i)

    mis = json.loads((GENERATED_DIR / "ramp_mismatches.json").read_text())
    fit = {}
    for m in mis:
        key = (m["sheet"], "%d,%d" % tuple(m["tile"]))
        if m["used_ramp"] is None:
            fit[key] = ("⚠ no slot", [])
        else:
            fit[key] = ("≈", sorted(m["off_colors"]))

    def flag(sheet, coord):
        """(mark, off_colors) for a sheet cell; ✓ when exactly fitting."""
        return fit.get((sheet, "%d,%d" % tuple(coord)), ("✓", []))

    L = []
    L.append("# Tiles ↔ sprites ↔ ramps accounting")
    L.append("")
    L.append("> Generated by `make manifest` (`tools/palette_compiler.py`). "
             "DO NOT EDIT DIRECTLY.")
    L.append(">")
    L.append("> Rule: every tile is colored with an existing artist ramp "
             "(ramps win). ✓ = exact fit, ≈ = nearest-shade fallback "
             "(repaint todo, see Repaint list).")
    L.append("> Slot meanings (`UI_COLOR_*`, `src/ui/ui.h`, never move): "
             "0 NONE, 1 FIRE, 2 IRON, 3 FIELD, 4 POISON, 5 WOOD, 6 GOLD, 7 DIM.")
    L.append("> Artist sets: `sprites*` = RPG overworld, `fight1`–`fight7` = "
             "battle background, `fight`+enemy = battle sprites.")
    L.append("")

    L.append("## Slot map")
    L.append("")
    L.append("| set | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |")
    L.append("|---|---|---|---|---|---|---|---|---|")
    for setname in ("forest", "desolate_landscape", "castle", "village",
                    "base", "obj", "title"):
        row = [setname] + [slotmap[setname].get(i, "–") for i in range(8)]
        L.append("| " + " | ".join(row) + " |")
    L.append("")

    L.append("## World tiles")
    L.append("")
    for ts, setname in (("forest", "forest"), ("castle", "castle"),
                        ("desolate_landscape", "desolate_landscape"),
                        ("village", "village")):
        ts_data = json.loads((TILESETS_DIR / f"{ts}.json").read_text())
        vram_coord = {}
        for v in (ts_data.get("vram_block") or {}).get("tiles", []):
            if "tile" in v:
                vram_coord.setdefault(v["tile"], (v["x"], v["y"]))
        by_ramp = {}
        for t in ts_data.get("tiles", []):
            tid = t.get("id")
            ramp = t.get("palette")
            if ramp is None:
                ramp = "(untagged)"
            by_ramp.setdefault(ramp, []).append(tid)
        L.append(f"### {ts} (set `{setname}`)")
        L.append("")
        L.append("| ramp (slot) | tiles |")
        L.append("|---|---|")
        need_note = False
        for ramp in sorted(by_ramp):
            slot = slot_of.get((setname, ramp), "–")
            names = []
            for tid in sorted(by_ramp[ramp]):
                coord = vram_coord.get(tid)
                if coord is None:
                    need_note = True
                    names.append(f"{tid} (†)")
                else:
                    mark, off = flag(ts, coord)
                    if mark == "✓":
                        names.append(tid)
                    else:
                        names.append(f"{tid} ({mark} {', '.join(off)})")
            L.append(f"| {ramp} ({slot}) | {', '.join(names)} |")
        L.append("")
        if need_note:
            L.append("† = tile id has no vram_block cell on this sheet.")
            L.append("")

    L.append("## Battle sprites (BG stamp via `art_palette`)")
    L.append("")
    L.append("| set | size | BG ramp (slot) | fit | OAM obj ramp |")
    L.append("|---|---|---|---|---|")
    for path in sorted((REPO_ROOT / "screens" / "combat_art").glob("*.json")):
        data = json.loads(path.read_text())
        sid = data.get("id", path.stem)
        w, h = data.get("width", 0), data.get("height", 0)
        pal = data.get("palette")
        slot = slot_of.get(("base", pal), "?")
        cells = list(data.get("frame0", []))
        f1 = data.get("frame1")
        cells += f1 if f1 is not None else data.get("frame0", [])
        off = set()
        bad = False
        for name in cells:
            if name is None or name not in BATTLE_CELLS:
                continue
            mark, o = flag("battle", BATTLE_CELLS[name])
            if mark != "✓":
                bad = True
                off.update(o)
        fitcol = "✓" if not bad else "≈ " + ", ".join(sorted(off))
        obj = data.get("obj_palette") or "–"
        oam = "oam, " if data.get("oam") else ""
        L.append(f"| {sid} | {w}×{h} | {pal} ({slot}) | "
                 f"{fitcol} | {oam}{obj} |")
    L.append("")
    L.append("OAM obj ramps are declared content for future battle-OAM work; "
             "the ROM currently stamps every set as BG with its BG ramp.")
    L.append("")

    L.append("## Overworld sprites (OAM)")
    L.append("")
    L.append("| sprite | cells | ramp (slot) | fit |")
    L.append("|---|---|---|---|")
    entries = []
    for path in sorted((REPO_ROOT / "screens" / "enemy_types").glob("*.json")):
        entries.append(json.loads(path.read_text()))
    hero_data = json.loads((REPO_ROOT / "screens" / "hero.json").read_text())
    entries.append(hero_data)
    for data in entries:
        sid = data.get("id", "?")
        ow = data.get("overworld") or {}
        pal = ow.get("palette", 0)
        if isinstance(pal, str):
            ramp, slot = pal, slot_of.get(("obj", pal), "?")
        else:
            ramp = next((r for (s, r), i in slot_of.items()
                         if s == "obj" and i == pal), "?")
            slot = pal
        coords = ENEMY_CELLS if sid != "hero" else HERO_CELLS
        off = set()
        bad = False
        for name in ow.get("cells", []):
            if name not in coords:
                continue
            mark, o = flag("enemy_ow" if sid != "hero" else "hero_ow",
                           coords[name])
            if mark != "✓":
                bad = True
                off.update(o)
        fitcol = "✓" if not bad else "≈ " + ", ".join(sorted(off))
        L.append(f"| {sid} | {', '.join(ow.get('cells', []))} | "
                 f"{ramp} ({slot}) | {fitcol} |")
    L.append("")

    L.append("## NPC overlay (`npc_tiles.png`, fixed display slots)")
    L.append("")
    L.append("| cell | npc | display slot → ramp | fit |")
    L.append("|---|---|---|---|")
    for x, name in enumerate(NPC_LAYOUT):
        slot = NPC_DISPLAY_SLOTS[x]
        ramp = slotmap["village"][slot]
        short = name.replace("actors_", "").replace("_", " ")
        mark, off = flag("npc", (x, 0))
        fitcol = "✓" if mark == "✓" else f"{mark} " + ", ".join(off)
        L.append(f"| {x} | {short} | {slot} → {ramp} | {fitcol} |")
    L.append("")

    L.append("## Card frames + icons (`card_frames.png`, skin display slots)")
    L.append("")
    L.append("| cell (x,y) | art | display slot → ramp | fit |")
    L.append("|---|---|---|---|")
    skin_path = REPO_ROOT / "screens" / "cards_skin.json"
    skin = json.loads(skin_path.read_text()) if skin_path.exists() else DEFAULT_SKIN
    hud_path = REPO_ROOT / "screens" / "battle_hud.json"
    hud = json.loads(hud_path.read_text()) if hud_path.exists() else DEFAULT_HUD
    cell_slot = {}
    for tkey, tdef in (skin.get("types") or {}).items():
        s = SKIN_COLORS[tdef["color"]]
        cell_slot[tdef["icon"]] = s
        for uname in tdef.get("uses_icons", []) + [tdef.get("uses_power_icon")]:
            cell_slot[uname] = s
    for ekey, edef in (skin.get("elements") or {}).items():
        cell_slot[edef["icon"]] = SKIN_COLORS[edef["color"]]
    for hkey in ("hp", "ap", "deck"):
        hdef = hud.get(hkey) or {}
        if hdef.get("icon"):
            cell_slot[hdef["icon"]] = SKIN_COLORS[hdef["color"]]
    bar = hud.get("bar") or {}
    for bkey in ("filled", "empty"):
        if bar.get(bkey):
            cell_slot[bar[bkey]] = SKIN_COLORS[bar["color"]]
    for _y, _row in enumerate(CARD_LAYOUT):
        for _x, _name in enumerate(_row):
            if _name is None:
                continue
            if _name in cell_slot:
                slot = cell_slot[_name]
                ramp = slotmap["base"][slot]
            else:
                slot, ramp = 0, "fight1"
            mark, off = flag("card_frames", (_x, _y))
            fitcol = "✓" if mark == "✓" else f"{mark} " + ", ".join(off)
            L.append(f"| ({_x},{_y}) | {_name} | {slot} → {ramp} | {fitcol} |")
    L.append("")
    L.append("Frame borders + select arrow share `fight1` (slot 0): cards "
             "tint per type at stamp time by engine design.")
    L.append("")

    L.append("## Title")
    L.append("")
    L.append("`title-red.png` → **title_logo** (slot 1, programmed directly "
             "by the title screen): ✓ exact.")
    L.append("")

    L.append("## Repaint list (nearest-shade fallbacks)")
    L.append("")
    L.append("| sheet | cell | used ramp | off-ramp colors |")
    L.append("|---|---|---|---|")
    for m in mis:
        if m["used_ramp"] is None:
            L.append(f"| {m['sheet']} | {m['tile']} | ⚠ {m['declared']} "
                     f"has no slot | {m['reason']} |")
        else:
            L.append(f"| {m['sheet']} | {m['tile']} | {m['used_ramp']} | "
                     f"{', '.join(m['off_colors'])} |")
    L.append("")
    slotted = {r for slots in slotmap.values() for r in slots.values()}
    spare = sorted(set(ramps) - slotted)
    if spare:
        L.append(f"Unslotted ramps (parsed, no hardware slot, info only): "
                 f"{', '.join(spare)}.")
        L.append("")

    out_path = REPO_ROOT / "docs" / "accounting-tiles-sprites-vs-ramps.md"
    out_path.write_text("\n".join(L))


if __name__ == "__main__":
    main()
