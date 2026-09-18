"""Compose assets/battle_sprites.png (canonical battle-art source sheet).

Reads the CSV-slug editor PNGs (tools/level_editor/public/tiles/combat/,
sliced fresh from assets/combat-tile.png via import_tileset.py --tileset-id
combat), downscales NEAREST to 8x8, and lays out 3 cols x N rows of cells:

  row 0: slime top_*       row 6: boss torso_*
  row 1: slime bottom_*    row 7: mimic top_*
  row 2: bat top_*         row 8: mimic bottom_*
  row 3: bat bottom_*      row 9-10: kobold top_*/bottom_*
  row 4: boss horns_*      row 11-12: spider top_*/bottom_*
  row 5: boss head_*       row 13: blank

The slime second anim frame was removed by the artist (single-frame
slime); its curated PNGs are gone, so no row is reserved for it.

The boss glow-eyes cells (combat_*_boss_2) are intentionally excluded:
they use 5 colors, over the 4-color 2bpp tile budget (make gfx fails).
They ship when the art is reduced to 4 colors.

Deterministic: rerunning reproduces the sheet byte-identically.
"""
import sys
from PIL import Image

PUB = 'tools/level_editor/public/tiles/combat'
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
    ['combat_top_left_kobold', 'combat_top_middle_kobold', 'combat_top_right_kobold'],
    ['combat_bottom_left_kobold', 'combat_bottom_middle_kobold', 'combat_bottom_right_kobold'],
    ['combat_top_left_spider', 'combat_top_middle_spider', 'combat_top_right_spider'],
    ['combat_bottom_left_spider', 'combat_bottom_middle_spider', 'combat_bottom_right_spider'],
    [None, None, None],
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

# The all-white cell (last row) pads partial art sets.
BLANK_COORD = (0, len(LAYOUT) - 1)


def main():
    rows = len(LAYOUT)
    sheet = Image.new('RGB', (24, rows * 8), (255, 255, 255))
    for y, row in enumerate(LAYOUT):
        for x, name in enumerate(row):
            if name is None:
                continue
            im = Image.open('%s/%s.png' % (PUB, name)).convert('RGB')
            sheet.paste(im.resize((8, 8), Image.NEAREST), (x * 8, y * 8))
    sheet.save('assets/battle_sprites.png')
    print('wrote assets/battle_sprites.png')


if __name__ == '__main__':
    main()
