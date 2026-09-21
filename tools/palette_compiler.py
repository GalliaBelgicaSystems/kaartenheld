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
import re
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
    "card_frames": ("card_frames.png", "base"),
    "title": ("title-red.png", "title"),
}

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


def ui_color_slot(name):
    """Resolve a UI_COLOR_* macro in src/ui/ui.h to its integer slot,
    following alias #defines (UI_COLOR_ARROW -> UI_COLOR_FIELD -> 3).
    Returns None if the macro is missing or non-numeric.

    Single source for engine constants the host mapping must match: the
    runtime uses the same macro (UI_COLOR_ARROW in ui_battle_content.c),
    so encode and display cannot drift."""
    try:
        text = (REPO_ROOT / "src" / "ui" / "ui.h").read_text()
    except OSError:
        return None
    defines = dict(re.findall(r"#define\s+(UI_COLOR_\w+)\s+(\w+)", text))
    val = defines.get(name)
    seen = set()
    while val is not None and val in defines and val not in seen:
        seen.add(val)
        val = defines[val]
    try:
        return int(val)
    except (TypeError, ValueError):
        return None


def card_display_slots(skin, hud):
    """{cell coord -> UI_COLOR_* display slot} for every non-empty
    card_frames.png cell.

    SINGLE SOURCE OF TRUTH for the card display mapping: the encode ramp
    (this file's compile_sheet), the accounting doc (write_accounting)
    and verify_palette_manifest's encode==display check all read the
    generated/tiles/card_display_slots.json emitted from this function.
    The runtime paints these slots per src/ui/ui_battle_content.c.

    Defaults: frame borders / unlisted cells -> slot 0 (paper); the
    select-arrow cell -> UI_COLOR_ARROW from ui.h (the same engine
    constant the two runtime paint sites use); type + uses icons -> the
    type's color; element icons -> the element color; HUD icons + timer
    bar -> their hud-skin colors.
    """
    sys.path.insert(0, str(REPO_ROOT / "tools" / "screen_compiler"))
    from compose_card_frames import LAYOUT
    from battle_compile import SKIN_COLORS, DEFAULT_SKIN, DEFAULT_HUD

    def skin_slot(name):
        if name not in SKIN_COLORS:
            raise ValueError(f"skin color '{name}' is not a UI slot name")
        return SKIN_COLORS[name]

    coord_of = {}
    for y, row in enumerate(LAYOUT):
        for x, name in enumerate(row):
            if name is not None:
                coord_of[name] = (x, y)

    slots = {coord: 0 for coord in coord_of.values()}
    arrow_slot = ui_color_slot("UI_COLOR_ARROW")
    for _arrow in ("combat_arrow_pointing_up_light",
                   "combat_arrow_pointing_up_dark"):
        if arrow_slot is not None and _arrow in coord_of:
            slots[coord_of[_arrow]] = arrow_slot
    for tdef in (skin.get("types") or {}).values():
        s = skin_slot(tdef["color"])
        names = [tdef["icon"]] + list(tdef.get("uses_icons", []))
        names.append(tdef.get("uses_power_icon"))
        for name in names:
            if name is not None and name in coord_of:
                slots[coord_of[name]] = s
    for edef in (skin.get("elements") or {}).values():
        if edef["icon"] in coord_of:
            slots[coord_of[edef["icon"]]] = skin_slot(edef["color"])
    for key in ("hp", "ap", "deck"):
        hdef = hud.get(key) or {}
        if hdef.get("icon") in coord_of:
            slots[coord_of[hdef["icon"]]] = skin_slot(hdef["color"])
    bar = hud.get("bar") or {}
    for key in ("filled", "empty"):
        if bar.get(key) in coord_of:
            slots[coord_of[bar[key]]] = skin_slot(bar["color"])
    return slots


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

    # Battle art: {cellname: encoding ramp}. OAM-flagged sets (every battle
    # enemy except the boss, per Florent's model) encode with their OBJ ramp;
    # the sole BG-stamped set (boss) encodes with its fight ramp. The ROM
    # programs the matching CRAM side per path, so encoding and display
    # always agree; a mismatch row means OUR assignment is suspect, never
    # the artist's pixels (artist is always right).
    from compose_battle_sprites import TILE_COORDS as BATTLE_CELLS
    from compose_enemy_sprites import TILE_COORDS as ENEMY_CELLS
    from compose_hero_sprites import TILE_COORDS as HERO_CELLS
    sys.path.insert(0, str(REPO_ROOT / "tools" / "screen_compiler"))
    from battle_compile import DEFAULT_SKIN, DEFAULT_HUD
    battle_declared = {}
    battle_oam_names = set()
    for path in sorted((REPO_ROOT / "screens" / "combat_art").glob("*.json")):
        data = json.loads(path.read_text())
        pal = data.get("palette")
        if pal not in ramps:
            raise ValueError(f"combat_art/{path.name}: unknown palette ramp '{pal}'")
        obj = data.get("obj_palette")
        if obj is not None and obj not in ramps:
            raise ValueError(f"combat_art/{path.name}: unknown obj_palette ramp '{obj}'")
        enc = obj if data.get("oam") and obj else pal
        if data.get("oam") and not obj:
            raise ValueError(f"combat_art/{path.name}: oam set needs obj_palette")
        cells = list(data.get("frame0", []))
        f1 = data.get("frame1")
        cells += f1 if f1 is not None else data.get("frame0", [])
        for name in cells:
            if name is not None:
                battle_declared[name] = enc
                if data.get("oam"):
                    battle_oam_names.add(name)

    # Per-cell OBJ alt slots (combat_art obj_alt_palette/obj_alt_cells,
    # e.g. the spider eye): those cells encode AND display with the alt
    # artist ramp (second OBJ scratch slot at runtime), so they are
    # declared with it here.  The alt cell must fit the alt ramp EXACTLY
    # (no nearest fallback across slots); a violation fails loudly.
    battle_cells = sheet_cells(ASSETS_DIR / SHEETS["battle"][0])
    for path in sorted((REPO_ROOT / "screens" / "combat_art").glob("*.json")):
        data = json.loads(path.read_text())
        alt = data.get("obj_alt_palette")
        alt_cells = data.get("obj_alt_cells") or []
        if alt is None:
            continue
        if alt not in ramps:
            raise ValueError(f"combat_art/{path.name}: unknown obj_alt_palette ramp '{alt}'")
        if not data.get("oam"):
            raise ValueError(f"combat_art/{path.name}: obj_alt_palette needs oam")
        frame0 = list(data.get("frame0", []))
        w, h = data.get("width", 0), data.get("height", 0)
        if w * h > 8:
            raise ValueError(f"combat_art/{path.name}: obj_alt_cells needs width*height<=8")
        for idx in alt_cells:
            if not isinstance(idx, int) or not (0 <= idx < len(frame0)):
                raise ValueError(f"combat_art/{path.name}: obj_alt_cells entry {idx!r} out of frame0")
            name = frame0[idx]
            if name is None or name not in BATTLE_CELLS:
                raise ValueError(f"combat_art/{path.name}: obj_alt cell {idx} has no sheet coord")
            battle_declared[name] = alt
            battle_oam_names.add(name)
            coord = BATTLE_CELLS[name]
            pix = battle_cells.get(coord, [])
            bad = sorted(_hex(c) for c in pix if c not in ramps[alt])
            if bad:
                raise ValueError(
                    f"combat_art/{path.name}: obj_alt cell {idx} ({name}) "
                    f"has colors {bad} outside alt ramp '{alt}'; "
                    f"repaint the cell or drop the alt assignment")

    # Eye-glow overlay cells (combat_art glow block, e.g. boss eyes):
    # declared with the UNLIT ramp so exact-fit pixels stay silent in the
    # report. The overlay reuses the stamp's tile bytes through the
    # unlit/lit OBJ palettes (same shade indices, only index 1 remaps),
    # so this encoding equals the stamp encoding; battle_compile.py
    # enforces the ramp structure (shared 0/2/3). Deliberately NOT added
    # to battle_oam_names: these stay opaque stamp cells (no OAM
    # transparency handling applies to them).
    # Plus the glow-reference shape check: each <base>_2 curated PNG must
    # equal its unlit cell with red recoloured to orange ONLY (the
    # overlay assumes shape identity across the blink).
    from PIL import Image as _GlowImage
    _GLOW_RED = (139, 27, 27)      # COMBAT/heart_fire_enemy_eyes
    _GLOW_ORANGE = (245, 113, 55)  # COMBAT/eyes_glow
    _GLOW_PUB = REPO_ROOT / "tools" / "level_editor" / "public" / "tiles" / "combat"
    for path in sorted((REPO_ROOT / "screens" / "combat_art").glob("*.json")):
        data = json.loads(path.read_text())
        glow = data.get("glow")
        if glow is None:
            continue
        gunlit = glow.get("unlit")
        if gunlit not in ramps:
            raise ValueError(f"combat_art/{path.name}: unknown glow.unlit ramp '{gunlit}'")
        frame0 = list(data.get("frame0", []))
        for idx in glow.get("cells") or []:
            if not isinstance(idx, int) or not (0 <= idx < len(frame0)):
                raise ValueError(f"combat_art/{path.name}: glow cell {idx!r} out of frame0")
            name = frame0[idx]
            if name is None or name not in BATTLE_CELLS:
                raise ValueError(f"combat_art/{path.name}: glow cell {idx} has no sheet coord")
            battle_declared[name] = gunlit
            base_im = _GlowImage.open(_GLOW_PUB / (name + ".png")).convert("RGB")
            ref_im = _GlowImage.open(_GLOW_PUB / (name + "_2.png")).convert("RGB")
            if base_im.size != (8, 8) or ref_im.size != (8, 8):
                raise ValueError(f"combat_art/{path.name}: glow cell {idx} ({name}) PNGs must be 8x8")
            base_px = list(base_im.getdata())
            ref_px = list(ref_im.getdata())
            bad = [i for i, (a, b) in enumerate(zip(base_px, ref_px))
                   if a != b and not (a == _GLOW_RED and b == _GLOW_ORANGE)]
            if bad or base_px == ref_px:
                raise ValueError(
                    f"combat_art/{path.name}: glow cell {idx} ({name}_2) must equal "
                    f"{name} with red recoloured to orange ONLY")

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
    # Battle top-right OAM rider HUD (screens/rider_icons.json): explicit
    # per-cell OBJ ramps, same declaration channel as enemy overworld cells.
    rider_data = json.loads((REPO_ROOT / "screens" / "rider_icons.json").read_text())
    for cell, ramp in (rider_data.get("cells") or {}).items():
        if ramp not in ramps:
            raise ValueError(f"rider_icons.json: unknown ramp '{ramp}'")
        ow_declared[cell] = ramp

    GENERATED_DIR.mkdir(parents=True, exist_ok=True)
    SHADES_DIR.mkdir(parents=True, exist_ok=True)
    mismatches = []

    def compile_sheet(key, declared, require_slot=True, transparent=()):
        """Color every cell of a sheet with an existing ramp.

        declared: {coord: ramp} or a single ramp name for all cells.
        require_slot: declared ramps must hold a hardware slot in the
        sheet's set (world sheets: manifest slot mapping needs it).
        Battle art skips it (BG sets resolve via battle_compile, OAM sets
        program per battle; encoding only needs a known ramp).
        transparent: coords whose shade-0 is OAM transparency (off-ramp
        pixels must not fall back into index 0 there; pure white there is
        sheet background and maps straight to 0, so it is NOT reported).
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
        t0_cells = []
        for coord in sorted(cells):
            pixel_colors = cells[coord]
            want = declared.get(coord)
            # OAM transparency cells (plus whole obj sheets): pure white
            # is sheet background mapping straight to shade 0, never a
            # repaint todo.
            t0 = coord in transparent or setname == "obj"
            if want is not None and want not in ramps:
                raise ValueError(f"sheet '{key}' tile {coord}: unknown ramp '{want}'")
            if require_slot and want is not None and want not in [r for r, _ in cand]:
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
                off = sorted(_hex(c) for c in pixel_colors
                             if c not in quad and not (t0 and c == (255, 255, 255)))
                mismatches.append({"sheet": key, "tile": list(coord),
                                   "declared": None, "used_ramp": ramp,
                                   "reason": "no tag: nearest slotted ramp wins",
                                   "off_colors": off})
            else:
                ramp = want
                quad = ramps[ramp]
                off = sorted(_hex(c) for c in pixel_colors
                             if c not in quad and not (t0 and c == (255, 255, 255)))
                if off:
                    mismatches.append({"sheet": key, "tile": list(coord),
                                       "declared": want, "used_ramp": ramp,
                                       "reason": "tagged ramp stands; off-ramp pixels use nearest shade",
                                       "off_colors": off})
            used[coord] = ramp
            shades["%d,%d" % coord] = [_hex(c) for c in ramps[ramp]]
            if coord in transparent:
                t0_cells.append("%d,%d" % coord)
        # OBJ sheets: OAM color index 0 is transparent, so png2gb must not
        # let an off-ramp pixel fall back into shade 0 (see png2gb.py).
        (SHADES_DIR / f"{key}.json").write_text(json.dumps(
            {"sheet": key, "png": fname, "set": setname,
             "transparent_shade0": setname == "obj",
             "transparent_cells": sorted(t0_cells),
             "tiles": shades}, indent=1))
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
        "battle", {BATTLE_CELLS[n]: r for n, r in battle_declared.items() if n in BATTLE_CELLS},
        require_slot=False,
        transparent={BATTLE_CELLS[n] for n in battle_oam_names if n in BATTLE_CELLS})
    compile_sheet(
        "enemy_ow", {ENEMY_CELLS[n]: r for n, r in ow_declared.items()
                     if r is not None and n in ENEMY_CELLS})
    compile_sheet(
        "hero_ow", {HERO_CELLS[n]: hero_ramp for n in
                    (hero_data.get("overworld") or {}).get("cells", []) if n in HERO_CELLS})
    def slot_ramp(setname, slot):
        ramp = slotmap[setname][slot]
        return ramp

    # Card-frame sheet: every icon encodes with the ramp of the slot that
    # displays it.  One shared mapping (card_display_slots) is emitted to
    # generated/tiles/card_display_slots.json and consumed by the
    # accounting doc + verify_palette_manifest, so encode, doc and runtime
    # cannot drift apart.
    skin_path = REPO_ROOT / "screens" / "cards_skin.json"
    skin = json.loads(skin_path.read_text()) if skin_path.exists() else DEFAULT_SKIN
    hud_path = REPO_ROOT / "screens" / "battle_hud.json"
    hud = json.loads(hud_path.read_text()) if hud_path.exists() else DEFAULT_HUD
    display_slots = card_display_slots(skin, hud)
    (GENERATED_DIR / "card_display_slots.json").write_text(json.dumps(
        {"%d,%d" % c: s for c, s in sorted(display_slots.items())}, indent=1))
    card_declared = {c: slot_ramp("base", s) for c, s in display_slots.items()}

    compile_sheet("card_frames", card_declared)
    compile_sheet("title", "title")

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
    (GENERATED_DIR / "cgb_palettes.inc").write_text("\n".join(out))

    out = ["/* Generated by tools/palette_compiler.py -- DO NOT EDIT DIRECTLY */",
           "/* Overworld + town-NPC OBJ ramps (slots 0-7, verbatim artist colors). */", ""]
    out.append("static const palette_color_t cgb_obj_palettes[8][4] = {")
    for i in sorted(slotmap["obj"]):
        out.append(f"    /* {i} {slotmap['obj'][i]} */ {c_ramp(tables['obj'][i])},")
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
    from compose_card_frames import LAYOUT as CARD_LAYOUT

    _, ramps = parse_palette()
    slotmap = load_slotmap()
    slot_of = {}
    for setname, slots in slotmap.items():
        for i, r in sorted(slots.items()):
            slot_of.setdefault((setname, r), i)

    # Battle OAM ramps are programmed into one scratch OBJ slot at battle
    # entry (BATTLE_OBJ_SCRATCH, src/battle/battle.h), not through the
    # overworld `obj` slot map.  Read it so the battle table cannot print a
    # stale overworld slot number for a colliding ramp name.
    scratch = "scratch"
    try:
        m = re.search(r"#define\s+BATTLE_OBJ_SCRATCH\s+(\d+)",
                      (REPO_ROOT / "src" / "battle" / "battle.h").read_text())
        if m:
            scratch = int(m.group(1))
    except OSError:
        pass
    scratch2 = "scratch2"
    try:
        m2 = re.search(r"#define\s+BATTLE_OBJ_SCRATCH2\s+(\d+)",
                       (REPO_ROOT / "src" / "battle" / "battle.h").read_text())
        if m2:
            scratch2 = int(m2.group(1))
    except OSError:
        pass

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
             "(ramps win). ✓ = every pixel exists in the encode ramp; ≈ = "
             "nearest-shade fallback (repaint todo, see Repaint list).")
    L.append("> ✓ only asserts the pixels fit the ramp -- it does NOT assert "
             "the result is legible or artifact-free (e.g. the dim grey-out of "
             "spent/poisoned cards, or the low-contrast ice status label).")
    L.append("> Battle BG slots (`base` set, `tools/palette_slots.json`): "
             + ", ".join("%d %s" % (i, slotmap["base"][i])
                         for i in sorted(slotmap["base"])) + ".")
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

    L.append("## Battle sprites (display ramp + draw path)")
    L.append("")
    L.append("| set | size | path | display ramp (slot) | fit |")
    L.append("|---|---|---|---|---|")
    for path in sorted((REPO_ROOT / "screens" / "combat_art").glob("*.json")):
        data = json.loads(path.read_text())
        sid = data.get("id", path.stem)
        w, h = data.get("width", 0), data.get("height", 0)
        if data.get("oam"):
            pathstr = "OAM"
            ramp = data.get("obj_palette")
            slot = scratch
            alt = data.get("obj_alt_palette")
            if alt is not None:
                cells_alt = ",".join(str(c) for c in (data.get("obj_alt_cells") or []))
                ramp = "%s+%s[%s]" % (ramp, alt, cells_alt)
                slot = "%s+%s" % (scratch, scratch2)
        else:
            pathstr = "BG stamp"
            ramp = data.get("palette")
            slot = slot_of.get(("base", ramp), "?")
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
        L.append(f"| {sid} | {w}×{h} | {pathstr} | {ramp} ({slot}) | {fitcol} |")
    L.append("")
    L.append("`fit` is measured against the display ramp (the OBJ ramp for OAM "
             "sets, the BG ramp for the boss).  Every OAM ramp is programmed "
             "into the single battle OBJ scratch slot (%s) at entry "
             "(BATTLE_OBJ_SCRATCH), so the slot column shows %s, not the "
             "overworld `obj` slot map.  Sets with a second ramp "
             "(spider eye) program it into scratch slot %s "
             "(BATTLE_OBJ_SCRATCH2) for the listed frame-relative cells; "
             "their `fit` covers both ramps." % (scratch, scratch, scratch2))
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

    L.append("Town NPCs (guard, wizard, merchant, mayor) render as OAM sprites")
    L.append("with exact OBJ ramps (see Overworld sprites above); the old BG")
    L.append("overlay is deleted.")
    L.append("")

    L.append("## Card frames + icons (`card_frames.png`, skin display slots)")
    L.append("")
    L.append("| cell (x,y) | art | display slot → ramp | fit |")
    L.append("|---|---|---|---|")
    display_slots = json.loads(
        (GENERATED_DIR / "card_display_slots.json").read_text())
    sys.path.insert(0, str(REPO_ROOT / "tools" / "screen_compiler"))
    from battle_compile import SKIN_COLORS
    skin = json.loads((REPO_ROOT / "screens" / "cards_skin.json").read_text())
    type_slots = sorted({SKIN_COLORS[t["color"]]
                         for t in (skin.get("types") or {}).values()})
    frame_slot = display_slots.get("0,0", 0)
    arrow_slot = 0
    for _y, _row in enumerate(CARD_LAYOUT):
        for _x, _name in enumerate(_row):
            if _name in ("combat_arrow_pointing_up_light",
                         "combat_arrow_pointing_up_dark"):
                arrow_slot = display_slots.get("%d,%d" % (_x, _y), 0)
    for _y, _row in enumerate(CARD_LAYOUT):
        for _x, _name in enumerate(_row):
            if _name is None:
                continue
            slot = display_slots.get("%d,%d" % (_x, _y), 0)
            ramp = slotmap["base"][slot]
            mark, off = flag("card_frames", (_x, _y))
            fitcol = "✓" if mark == "✓" else f"{mark} " + ", ".join(off)
            L.append(f"| ({_x},{_y}) | {_name} | {slot} → {ramp} | {fitcol} |")
    L.append("")
    L.append("Frame borders use slot %d (%s); the select arrow slot %d (%s).  "
             "Weapon + uses icons and the power digit use the type's "
             "`weapon_color` slot(s) %s; the box is paper and the DIM grey-out "
             "override applies to the icon/digit cells too."
             % (frame_slot, slotmap["base"][frame_slot],
                arrow_slot, slotmap["base"][arrow_slot],
                ", ".join("%d %s" % (s, slotmap["base"][s]) for s in type_slots)))
    L.append("")

    L.append("## Title")
    L.append("")
    L.append("`title-red.png` → **title** (slot 1 programmed with the same reds "
             "directly by the title screen): exact by construction (see mismatch report).")
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
