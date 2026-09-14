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
    base = [list(r) for r in forest]
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
    return anchors


SECTIONS = load_sections()
RAMPS = build_ramps(SECTIONS)
OBJ_RAMPS = build_obj_ramps(SECTIONS)
ANCHORS = build_anchors(SECTIONS)
