"""Compose assets/battle_sprites.png (canonical battle-art source sheet).

Reads the CSV-slug editor PNGs (tools/level_editor/public/tiles/combat/,
sliced fresh from assets/combat-tile.png via import_tileset.py --tileset-id
combat), and lays out 3 cols x N rows of 8x8 cells:

  row 0: slime top_*       row 5: boss head_*
  row 1: slime bottom_*    row 6: boss torso_*
  row 2: bat top_*         row 7: mimic top_*
  row 3: bat bottom_*      row 8: mimic bottom_*
  row 4: boss horns_*      row 9: blank
  row 10-11: kobold top_*/bottom_*
  row 12-13: spider top_*/bottom_*

(Slime anim frame removed per artist: single-frame slime holds frame 0.)

The boss glow-eyes cells (combat_*_boss_2) are intentionally excluded:
they use 5 colors, over the 4-color 2bpp tile budget (make gfx fails).
They ship when the art is reduced to 4 colors.

Exactness: curated art is copied byte-identically (RGBA transparency is
flattened onto the cell background, see below). NO quantization, NO color
snapping, NO resampling: sources must be 8x8; anything else fails loudly.
Off-ramp pixels fail later at sidecar validation
(tools/emit_sheet_sidecars.py), naming the set and the offending color.
Repaint the pixel; do not adjust the tool.

Cell background is explicit per combat-art set (screens/combat_art/*.json
`oam` flag): OAM sets flatten onto SPRITES/background yellow (entry 0 --
hence transparent -- of every OBJ ramp); BG-stamped sets (boss, spider)
flatten onto white (entry 0 of every BASE ramp).

Deterministic: rerunning reproduces the sheet byte-identically.
"""
import json
from pathlib import Path
from PIL import Image

REPO_ROOT = Path(__file__).resolve().parent.parent
PUB = REPO_ROOT / 'tools' / 'level_editor' / 'public' / 'tiles' / 'combat'
ART_DIR = REPO_ROOT / 'screens' / 'combat_art'

LAYOUT = [
    ['combat_top_left_slime', 'combat_top_middle_slime', 'combat_top_right_slime'],
    ['combat_bottom_left_slime', 'combat_bottom_middle_slime', 'combat_bottom_right_slime'],
    ['combat_top_left_bat', 'combat_top_middle_bat', 'combat_top_right_bat'],
    ['combat_bottom_left_bat', 'combat_bottom_middle_bat', 'combat_bottom_right_bat'],
    ['combat_top_left_boss', 'combat_top_middle_boss', 'combat_top_right_boss'],
    ['combat_left_middle_boss', 'combat_middle_center_boss', 'combat_middle_right_boss'],
    ['combat_bottom_left_boss', 'combat_bottom_middle_boss', 'combat_bottom_right_boss'],
    ['combat_top_left_mimic', 'combat_top_middle_mimic', 'combat_top_right_mimic'],
    ['combat_bottom_left_mimic', 'combat_bottom_middle_mimic', 'combat_bottom_right_mimic'],
    [None, None, None],
    ['combat_top_left_kobold', 'combat_top_middle_kobold', 'combat_top_right_kobold'],
    ['combat_bottom_left_kobold', 'combat_bottom_middle_kobold', 'combat_bottom_right_kobold'],
    ['combat_top_left_spider', 'combat_top_middle_spider', 'combat_top_right_spider'],
    ['combat_bottom_left_spider', 'combat_bottom_middle_spider', 'combat_bottom_right_spider'],
]

# Tile-name -> sheet (x, y): the single source of truth for combat-art
# cell addressing.  battle_compile.py imports this (no side effects) to
# resolve screens/combat_art/*.json cells; new tiles are added here plus
# their curated PNGs (Phase 3 meta-tile composer extends this table).
TILE_COORDS = {}
for _y, _row in enumerate(LAYOUT):
    for _x, _name in enumerate(_row):
        if _name is not None:
            TILE_COORDS[_name] = (_x, _y)

# The all-white cell (row 9) pads partial art referenced as null in
# combat-art JSONs.
BLANK_COORD = (0, 9)

# Sheet backgrounds: OAM yellow (transparent on hardware) vs BG white.
OAM_BG = (241, 235, 3)
BG_BG = (255, 255, 255)


def oam_cells():
    """Tile names belonging to OAM sets (from combat_art JSON `oam` flags)."""
    out = set()
    for path in sorted(ART_DIR.glob('*.json')):
        data = json.loads(path.read_text())
        if not data.get('oam'):
            continue
        for frame in ('frame0', 'frame1'):
            for name in data.get(frame) or []:
                if name is not None:
                    out.add(name)
    return out


def main():
    oam = oam_cells()
    rows = len(LAYOUT)
    sheet = Image.new('RGB', (24, rows * 8), BG_BG)
    for y, row in enumerate(LAYOUT):
        for x, name in enumerate(row):
            if name is None:
                continue
            bg = OAM_BG if name in oam else BG_BG
            im = Image.open(PUB / ('%s.png' % name)).convert('RGBA')
            if im.size != (8, 8):
                raise ValueError(
                    'battle sheet source %s.png is %dx%d, want 8x8 -- '
                    'resampling would invent colors' % (name, im.size[0], im.size[1]))
            im = Image.alpha_composite(Image.new('RGBA', im.size, bg + (255,)), im)
            sheet.paste(im.convert('RGB'), (x * 8, y * 8))
    sheet.save(REPO_ROOT / 'assets' / 'battle_sprites.png')
    print('wrote assets/battle_sprites.png')


if __name__ == '__main__':
    main()
