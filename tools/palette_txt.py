#!/usr/bin/env python3
"""palette_txt.py -- Single source of truth for authored colors.

`assets/palette.txt` holds the artist-authored hex colors per asset family
(COMMON, FOREST, DESOLATE, CASTLE, TOWN, SPRITES, COMBAT, TITLE).  This module
parses it and exposes the canonical CGB ramp tables derived from it by
*direct replacement*: the 8x4 ramp structure per tileset is kept, and each
slot whose authored counterpart exists in palette.txt uses that exact hex.
Slots with no authored counterpart (legacy fire/iron/dim gameplay ramps)
keep their values and are marked as such below.

Consumers:
  tools/palette_compiler.py  FIXED_PALETTES / ANCHOR_COLORS (imported)
  tools/palette_check.py     drift check vs tiles_content.c / ui.c / Makefile
  docs: each ramp slot cites its palette.txt (section, name) or LEGACY.
"""

from pathlib import Path
from typing import Dict, Tuple

REPO_ROOT = Path(__file__).resolve().parent.parent
PALETTE_TXT = REPO_ROOT / "assets" / "palette.txt"

RGB = Tuple[int, int, int]


def _hex_to_rgb(h: str) -> RGB:
    h = h.strip().lstrip("#")
    if len(h) != 6:
        raise ValueError(f"expected 6-digit hex, got '{h}'")
    try:
        return (int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16))
    except ValueError:
        raise ValueError(f"invalid hex color '{h}'")


def load_sections(path: Path = PALETTE_TXT) -> Dict[str, Dict[str, RGB]]:
    """Parse palette.txt -> {SECTION: {name: (r,g,b)}}.

    Section headers are bare uppercase words; entries are `name: #hex`.
    Fails loudly on empty/non-hex values (e.g. a bare '#').
    """
    sections: Dict[str, Dict[str, RGB]] = {}
    current: str = ""
    for lineno, raw in enumerate(path.read_text().splitlines(), 1):
        line = raw.strip()
        if not line:
            continue
        if ":" not in line:
            current = line.upper()
            if current in sections:
                raise ValueError(f"{path}:{lineno}: duplicate section '{current}'")
            sections[current] = {}
            continue
        if not current:
            raise ValueError(f"{path}:{lineno}: entry before any section: '{raw}'")
        name, _, value = line.partition(":")
        name = name.strip()
        rgb = _hex_to_rgb(value.strip())
        if name in sections[current]:
            raise ValueError(f"{path}:{lineno}: duplicate '{name}' in [{current}]")
        sections[current][name] = rgb
    return sections


def _c(sections: Dict[str, Dict[str, RGB]], section: str, name: str) -> RGB:
    try:
        return sections[section][name]
    except KeyError:
        raise KeyError(f"palette.txt: [{section}] '{name}' not defined")


def build_ramps(sections: Dict[str, Dict[str, RGB]]) -> Dict[str, list]:
    """Canonical 8x4 CGB BG ramps per tileset, fully resolved RGB tuples.

    Provenance per slot: `palette.txt [SECTION] name` or LEGACY (no authored
    counterpart; gameplay ramp kept as-is).
    """
    white = _c(sections, "COMMON", "white")
    black = _c(sections, "FOREST", "void_holes")
    slime_light = _c(sections, "COMBAT", "slime_combat")
    slime_dark = _c(sections, "COMBAT", "slime_outline_combat")
    grass = _c(sections, "FOREST", "grass")
    tree_big = _c(sections, "FOREST", "tree_leaves_terrain_outline_big_grass")
    tree_dark = _c(sections, "FOREST", "dark_tree_leaves")
    rocks_outline = _c(sections, "FOREST", "rocks_outline")
    slate = _c(sections, "DESOLATE", "ground")
    castle_ground = _c(sections, "CASTLE", "ground")
    castle_gold = _c(sections, "CASTLE", "gold")
    curtain_light = _c(sections, "CASTLE", "curtain_light")
    curtain_dark = _c(sections, "CASTLE", "curtain_dark")
    wall_light = _c(sections, "CASTLE", "wall_light")
    wall_dark = _c(sections, "CASTLE", "wall_dark")
    window_frame = _c(sections, "CASTLE", "window_frame")
    castle_wood_light = _c(sections, "CASTLE", "wood_light")
    castle_wood_dark = _c(sections, "CASTLE", "wood_dark")
    dirt = _c(sections, "TOWN", "ground")
    house_wall = _c(sections, "TOWN", "house_wall")
    roof_shade1 = _c(sections, "TOWN", "house_roof_shade1")
    wood_outlines = _c(sections, "TOWN", "wood_outlines")
    barrel_wood = _c(sections, "TOWN", "barrel_wood_fence")
    # Legacy gameplay ramps with no palette.txt counterpart (kept as-is).
    fire = [(255, 255, 224), (255, 140, 40), (220, 50, 20), (100, 10, 0)]
    iron = [(235, 242, 250), (140, 180, 214), (70, 105, 138), (27, 43, 58)]
    mauve = [(190, 140, 200), (140, 80, 160), (60, 30, 80)]
    mauve_light_forest = (250, 240, 250)
    gray_mid = [(170, 170, 170), (85, 85, 85)]

    forest = [
        [white, *gray_mid, black],                                  # 0 gray
        list(fire),                                                 # 1 fire (LEGACY)
        list(iron),                                                 # 2 iron/ice (LEGACY)
        [grass, tree_big, tree_dark, black],                       # 3 field
        [mauve_light_forest, *mauve],                               # 4 poison/mauve (LEGACY hue)
        [grass, (196, 138, 72), (138, 82, 34), (61, 32, 10)],      # 5 wood (LEGACY browns)
        [(255, 252, 224), castle_gold, (200, 140, 8), (90, 58, 0)],# 6 gold (slot1=authored)
        [(200, 200, 200), (150, 150, 150), (90, 90, 90), rocks_outline],  # 7 dim (slot3=authored)
    ]
    # Base battle/UI set: same as forest but wood slot0 stays beige (no scene
    # anchor in battle context) and dim slot3 stays neutral gray.
    # Slot 3 is the SLIME ramp, not field: its only consumers are slime
    # battle art (art_palette 3; art pixels are exactly these authored
    # colors) and heal-card spans (slime tint reads green, accepted).
    # Slot 0 white blends into the surrounding UI_COLOR_NONE backdrop.
    base = [list(r) for r in forest]
    base[3] = [white, slime_light, slime_dark, black]
    base[5][0] = (245, 230, 210)
    base[7][3] = (40, 40, 40)

    desolate = [
        [white, *gray_mid, black],                                  # 0 gray
        [slate, (237, 194, 20), (215, 80, 20), (80, 10, 0)],       # 1 campfire (LEGACY flames)
        [slate, *iron[1:]],                                         # 2 iron/ice (LEGACY)
        [slate, (116, 111, 128), (63, 58, 74), (38, 35, 46)],      # 3 flora (LEGACY)
        [slate, *mauve],                                            # 4 poison/mauve (LEGACY hue)
        [slate, castle_wood_light, castle_wood_dark, (38, 35, 46)],# 5 deadwood (authored)
        [slate, castle_gold, castle_wood_light, (50, 30, 10)],     # 6 gold (authored)
        [slate, (131, 123, 150), (63, 58, 74), (38, 35, 46)],      # 7 slate rock (authored)
    ]
    castle = [
        [castle_ground, (179, 176, 176), wall_light, window_frame],# 0 stone (slots2-3 authored)
        [castle_ground, curtain_light, curtain_dark, (30, 0, 0)],  # 1 curtain (authored)
        [castle_ground, (140, 160, 180), (70, 90, 110), (30, 40, 50)],  # 2 iron (LEGACY)
        [castle_ground, (90, 140, 80), (40, 80, 30), (10, 30, 10)],# 3 moss (LEGACY)
        [castle_ground, *mauve],                                    # 4 poison/mauve (LEGACY hue)
        [castle_ground, (158, 142, 113), castle_wood_dark, (40, 25, 10)],  # 5 wood furn
        [castle_ground, castle_gold, (162, 146, 113), (60, 40, 10)],# 6 gold (slot1 authored)
        [castle_ground, wall_light, wall_dark, (35, 35, 35)],      # 7 dim shadow (authored)
    ]
    village = [
        [dirt, (200, 200, 200), (125, 125, 125), (30, 30, 30)],    # 0 gray (LEGACY shades)
        [dirt, (255, 196, 96), (220, 110, 32), (90, 40, 10)],      # 1 fire (LEGACY)
        [dirt, (150, 160, 180), (85, 105, 130), (35, 45, 60)],     # 2 iron (LEGACY)
        [dirt, (140, 120, 88), (96, 78, 52), (48, 36, 24)],        # 3 dirt floor (LEGACY shades)
        [dirt, *mauve],                                             # 4 mauve (LEGACY hue; ROM truth)
        [dirt, barrel_wood, wood_outlines, (38, 24, 10)],          # 5 wood (authored)
        [dirt, house_wall, roof_shade1, wood_outlines],            # 6 cream (authored)
        [dirt, (158, 148, 128), (100, 88, 66), (42, 36, 26)],      # 7 dim (LEGACY shades)
    ]
    return {
        "base": base,
        "forest": forest,
        "desolate_landscape": desolate,
        "castle": castle,
        "village": village,
    }


def build_obj_ramps(sections: Dict[str, Dict[str, RGB]]) -> Dict[str, list]:
    """Canonical 4-entry CGB OBJ ramps (src/ui/ui.c)."""
    white = _c(sections, "COMMON", "white")
    black = _c(sections, "FOREST", "void_holes")
    wood_dog = _c(sections, "SPRITES", "wood_dog")
    chest_outline = _c(sections, "SPRITES", "chest_outline")
    npc = _c(sections, "SPRITES", "npc")
    slime_light = _c(sections, "SPRITES", "slime_light")
    slime_dark = _c(sections, "SPRITES", "slime_dark")
    glow = _c(sections, "SPRITES", "boss_eyes_scepter_glow")
    heart_fire = _c(sections, "COMBAT", "heart_fire_enemy_eyes")
    scepter = _c(sections, "SPRITES", "boss_eyes_scepter")
    return {
        # Slot 0 white is COMMON white; mid grays have no authored
        # counterpart (kept).
        "grey": [white, (170, 170, 170), (85, 85, 85), black],
        # Kobold/orange remapped to the authored scepter-glow ramp.
        "orange": [white, glow, heart_fire, scepter],
        "brown": [white, wood_dog, chest_outline, npc],
        # Slot 1 has no authored counterpart (kept); slots 2-3 are slime art.
        "green": [white, (180, 245, 120), slime_light, slime_dark],
    }


def build_anchors(sections: Dict[str, Dict[str, RGB]]) -> Dict[str, str]:
    """png2gb --anchor-color per sheet, as '#rrggbb'."""
    def hx(section: str, name: str) -> str:
        r, g, b = _c(sections, section, name)
        return f"#{r:02x}{g:02x}{b:02x}"

    anchors = {
        "forest": hx("FOREST", "grass"),
        "desolate_landscape": hx("DESOLATE", "ground"),
        "castle": hx("CASTLE", "ground"),
        "village": hx("TOWN", "ground"),
        "title": hx("COMMON", "white"),
    }
    # npc_tiles is a chroma-key exception, NOT an authored color: the sheet
    # background is compositor yellow #f1eb03 (tools/compose_npc_tiles.py BG)
    # and the anchor must equal those pixels so they land on shade 0.
    anchors["npc_tiles"] = "#f1eb03"
    # enemy_ow pins the sheet's documented transparent convention
    # (tools/compose_enemy_sprites.py BG): without it, cells whose art is
    # brighter than the slate bg (fire, mimic gold) map the bg to a solid
    # shade instead of transparent shade 0.
    anchors["enemy_ow"] = hx("DESOLATE", "ground")
    return anchors


SECTIONS = load_sections()
RAMPS = build_ramps(SECTIONS)
OBJ_RAMPS = build_obj_ramps(SECTIONS)
ANCHORS = build_anchors(SECTIONS)

# Semantic ramp names per set (also used for palette_compiler manifests).
RAMP_NAMES = {
    "base": ["gray", "fire", "iron_ice", "slime", "poison", "wood",
             "gold", "dim"],
    "forest": ["gray", "fire", "iron_ice", "field", "poison", "wood",
               "gold", "dim"],
    "desolate_landscape": ["gray", "campfire", "iron_ice", "flora",
                           "poison", "deadwood", "gold", "slate_rock"],
    "castle": ["stone", "curtain", "iron", "moss_green", "poison",
               "wood_furn", "gold", "dim_shadow"],
    "village": ["gray", "fire", "iron", "dirt_floor", "mauve", "wood",
                "cream", "dim"],
}

# What each ramp serves (battle/UI set: card colors + combat art slots;
# overworld sets: terrain roles + UI_COLOR_* indices + NPC overlays).
RAMP_USES = {
    "base": ["UI text/backdrop, spider art", "burn cards, kobold art",
             "sword/freeze cards", "slime art, heal cards",
             "poison/dagger cards, dialogue paper", "shield cards, mimic art",
             "bow cards", "grey-out, bat/boss art"],
    "forest": ["misc gray", "campfire tiles", "iron accents",
               "canopy + grass (UI_COLOR_FIELD)", "poison accents",
               "trunks/stumps, merchant NPC (UI_COLOR_WOOD)",
               "gold accents, mayor NPC", "rocks (UI_COLOR_DIM)"],
    "desolate_landscape": ["misc gray", "campfire tiles", "iron accents",
                           "flora accents", "poison accents",
                           "dead trees (UI_COLOR_WOOD)",
                           "gold accents, treasure chest",
                           "slate ground (UI_COLOR_DIM)"],
    "castle": ["stone floors/walls (UI_COLOR_NONE)", "curtains",
               "iron accents", "moss accents", "poison accents",
               "furniture (UI_COLOR_WOOD)", "gold accents, chest",
               "shading (UI_COLOR_DIM)"],
    "village": ["stonework (UI_COLOR_NONE)", "braziers/torches",
                "iron accents", "dirt ground (UI_COLOR_FIELD)",
                "mauve accents", "houses/barrels, merchant NPC (UI_COLOR_WOOD)",
                "walls/roofs, mayor NPC", "shading (UI_COLOR_DIM)"],
}

# Provenance per slot: "palette.txt [SECTION] name" or "LEGACY".
# Shape must mirror RAMPS exactly (checked by render_doc()).
_PROV = {
    "base": [
        ["[COMMON] white", "LEGACY", "LEGACY", "[FOREST] void_holes"],
        ["LEGACY", "LEGACY", "LEGACY", "LEGACY"],
        ["LEGACY", "LEGACY", "LEGACY", "LEGACY"],
        ["[COMMON] white", "[COMBAT] slime_combat",
         "[COMBAT] slime_outline_combat", "[FOREST] void_holes"],
        ["LEGACY", "LEGACY", "LEGACY", "LEGACY"],
        ["LEGACY", "LEGACY", "LEGACY", "LEGACY"],
        ["LEGACY", "[CASTLE] gold", "LEGACY", "LEGACY"],
        ["LEGACY", "LEGACY", "LEGACY", "LEGACY"],
    ],
    "forest": [
        ["[COMMON] white", "LEGACY", "LEGACY", "[FOREST] void_holes"],
        ["LEGACY", "LEGACY", "LEGACY", "LEGACY"],
        ["LEGACY", "LEGACY", "LEGACY", "LEGACY"],
        ["[FOREST] grass",
         "[FOREST] tree_leaves_terrain_outline_big_grass",
         "[FOREST] dark_tree_leaves", "[FOREST] void_holes"],
        ["LEGACY", "LEGACY", "LEGACY", "LEGACY"],
        ["[FOREST] grass", "LEGACY", "LEGACY", "LEGACY"],
        ["LEGACY", "[CASTLE] gold", "LEGACY", "LEGACY"],
        ["LEGACY", "LEGACY", "LEGACY", "[FOREST] rocks_outline"],
    ],
    "desolate_landscape": [
        ["[COMMON] white", "LEGACY", "LEGACY", "[FOREST] void_holes"],
        ["[DESOLATE] ground", "LEGACY", "LEGACY", "LEGACY"],
        ["[DESOLATE] ground", "LEGACY", "LEGACY", "LEGACY"],
        ["[DESOLATE] ground", "LEGACY", "[DESOLATE] terrain_side",
         "[DESOLATE] tree_item_outlines_cracks"],
        ["[DESOLATE] ground", "LEGACY", "LEGACY", "LEGACY"],
        ["[DESOLATE] ground", "[CASTLE] wood_light", "[CASTLE] wood_dark",
         "[DESOLATE] tree_item_outlines_cracks"],
        ["[DESOLATE] ground", "[CASTLE] gold", "[CASTLE] wood_light",
         "LEGACY"],
        ["[DESOLATE] ground", "[DESOLATE] rocks_stone_light",
         "[DESOLATE] terrain_side",
         "[DESOLATE] tree_item_outlines_cracks"],
    ],
    "castle": [
        ["[CASTLE] ground", "LEGACY", "[CASTLE] wall_light",
         "[CASTLE] window_frame"],
        ["[CASTLE] ground", "[CASTLE] curtain_light",
         "[CASTLE] curtain_dark", "LEGACY"],
        ["[CASTLE] ground", "LEGACY", "LEGACY", "LEGACY"],
        ["[CASTLE] ground", "LEGACY", "LEGACY", "LEGACY"],
        ["[CASTLE] ground", "LEGACY", "LEGACY", "LEGACY"],
        ["[CASTLE] ground", "LEGACY", "[CASTLE] wood_dark", "LEGACY"],
        ["[CASTLE] ground", "[CASTLE] gold", "LEGACY", "LEGACY"],
        ["[CASTLE] ground", "[CASTLE] wall_light", "[CASTLE] wall_dark",
         "LEGACY"],
    ],
    "village": [
        ["[TOWN] ground", "LEGACY", "LEGACY", "LEGACY"],
        ["[TOWN] ground", "LEGACY", "LEGACY", "LEGACY"],
        ["[TOWN] ground", "LEGACY", "LEGACY", "LEGACY"],
        ["[TOWN] ground", "LEGACY", "LEGACY", "LEGACY"],
        ["[TOWN] ground", "LEGACY", "LEGACY", "LEGACY"],
        ["[TOWN] ground", "[TOWN] barrel_wood_fence",
         "[TOWN] wood_outlines", "LEGACY"],
        ["[TOWN] ground", "[TOWN] house_wall",
         "[TOWN] house_roof_shade1", "[TOWN] wood_outlines"],
        ["[TOWN] ground", "LEGACY", "LEGACY", "LEGACY"],
    ],
}

_OBJ_PROV = {
    "grey": ["[COMMON] white", "LEGACY", "LEGACY", "[FOREST] void_holes"],
    "orange": ["[COMMON] white", "[SPRITES] boss_eyes_scepter_glow",
               "[COMBAT] heart_fire_enemy_eyes",
               "[SPRITES] boss_eyes_scepter"],
    "brown": ["[COMMON] white", "[SPRITES] wood_dog",
              "[SPRITES] chest_outline", "[SPRITES] npc"],
    "green": ["[COMMON] white", "LEGACY", "[SPRITES] slime_light",
              "[SPRITES] slime_dark"],
}

_OBJ_USES = {
    "grey": "player, bats, UI sprites (OBJ 0)",
    "orange": "town braziers, kobolds (OBJ 1)",
    "brown": "hero, kobold bodies, chests (OBJ 2)",
    "green": "overworld slimes (OBJ 3)",
}

_ANCHOR_PROV = {
    "forest": "[FOREST] grass",
    "desolate_landscape": "[DESOLATE] ground",
    "castle": "[CASTLE] ground",
    "village": "[TOWN] ground",
    "title": "[COMMON] white",
    "npc_tiles": "chroma-key #f1eb03 (compose_npc_tiles.py, not a color)",
    "enemy_ow": "[DESOLATE] ground (sheet transparent convention)",
}


def _hx(rgb) -> str:
    return f"#{rgb[0]:02x}{rgb[1]:02x}{rgb[2]:02x}"


def render_doc() -> str:
    """Render assets/palettes.md from the canonical tables."""
    for ts, ramps in RAMPS.items():
        prov = _PROV.get(ts)
        if prov is None or len(ramps) != 8 or len(prov) != 8:
            raise ValueError(f"palette doc: provenance shape wrong for {ts}")
        for ramp, pr in zip(ramps, prov):
            if len(ramp) != 4 or len(pr) != 4:
                raise ValueError(
                    f"palette doc: slot shape wrong for {ts}")
    L = []
    L.append("# Defined palettes (generated — do not edit by hand)")
    L.append("")
    L.append("Source of truth: `assets/palette.txt`, read by")
    L.append("`tools/palette_txt.py`.  Regenerate with `make manifest`")
    L.append("(the `palette-check` gate fails on drift).  Slots marked")
    L.append("LEGACY have no authored counterpart and keep their gameplay")
    L.append("values; every other slot cites its `palette.txt` entry.")
    L.append("")
    titles = {
        "base": "Base — battle / card UI (`cgb_bg_palettes`)",
        "forest": "Forest / field (`cgb_bg_palettes_forest`)",
        "desolate_landscape":
            "Desolate (`cgb_bg_palettes_desolate`)",
        "castle": "Castle (`cgb_bg_palettes_castle`)",
        "village": "Village / town (`cgb_bg_palettes_village`)",
    }
    for ts in ("base", "forest", "desolate_landscape", "castle",
               "village"):
        L.append(f"## {titles[ts]}")
        L.append("")
        L.append("| # | Name | S0 | S1 | S2 | S3 | Serves | Provenance |")
        L.append("|---|------|----|----|----|----|--------|------------|")
        for i, ramp in enumerate(RAMPS[ts]):
            hexes = " | ".join(f"`{_hx(c)}`" for c in ramp)
            prov = ", ".join(p for p in _PROV[ts][i] if p != "LEGACY")
            L.append(f"| {i} | {RAMP_NAMES[ts][i]} | {hexes} | "
                     f"{RAMP_USES[ts][i]} | {prov or 'LEGACY'} |")
        L.append("")
    L.append("## OBJ sprite ramps (`src/ui/ui.c`, slot 0 = COMMON white)")
    L.append("")
    L.append("| Name | S0 | S1 | S2 | S3 | Serves | Provenance |")
    L.append("|------|----|----|----|----|--------|------------|")
    for key, ramp in OBJ_RAMPS.items():
        hexes = " | ".join(f"`{_hx(c)}`" for c in ramp)
        prov = ", ".join(p for p in _OBJ_PROV[key] if p != "LEGACY")
        L.append(f"| {key} | {hexes} | {_OBJ_USES[key]} | "
                 f"{prov or 'LEGACY'} |")
    L.append("")
    L.append("## Sheet anchors (`png2gb --anchor-color` → shade 0)")
    L.append("")
    L.append("| Sheet | Anchor | Source |")
    L.append("|-------|--------|--------|")
    sheet_of = {"forest": "forest-tile", "desolate_landscape": "desolate",
                "castle": "castle-tile", "village": "village-tile",
                "title": "title-red", "npc_tiles": "npc_tiles",
                "enemy_ow": "enemy_sprites"}
    for key, anchor in ANCHORS.items():
        L.append(f"| `{sheet_of[key]}` | `{anchor}` | {_ANCHOR_PROV[key]} |")
    L.append("")
    return "\n".join(L)


DOC_PATH = REPO_ROOT / "assets" / "palettes.md"


def main(argv=None) -> int:
    import argparse
    ap = argparse.ArgumentParser(description="Render assets/palettes.md")
    ap.add_argument("--write-doc", action="store_true",
                    help="write assets/palettes.md")
    ap.add_argument("--check-doc", action="store_true",
                    help="fail if assets/palettes.md is stale")
    args = ap.parse_args(argv)
    if args.check_doc:
        want = render_doc() + "\n"
        got = DOC_PATH.read_text() if DOC_PATH.exists() else ""
        if got != want:
            print("palette doc: assets/palettes.md is stale "
                  "(run make manifest)")
            return 1
        print("palette doc: OK")
        return 0
    if args.write_doc:
        DOC_PATH.write_text(render_doc() + "\n")
        print(f"wrote {DOC_PATH.relative_to(REPO_ROOT)}")
        return 0
    ap.print_help()
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
