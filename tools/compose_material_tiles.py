"""Compose assets/material_tiles.png (card-menu material OAM icons).

Reads the curated editor PNGs (tools/level_editor/public/tiles/combat/,
sliced from assets/combat-tile.png by reload_boss_tiles.py) and lays out
4 8x8 cells in VRAM-stream order:

  x=0: material icon art, encoded with inventory1 (bronze)
  x=1: material icon art, encoded with inventory2 (iron + steel)
  x=2: material icon art, encoded with inventory3 (mithril)
  x=3: wood icon art,     encoded with inventory4 (wood)

The tinting happens at encode time (palette_compiler.py maps each cell
through its inventory ramp); the three copies of the same art become
three distinct 2bpp tiles.  The runtime streams the sheet to OAM
119-122 on ITEM ticks (marks_banked.c) and programs OBJ slots 0/1/6/5
with the same four ramps, so each card row shows its material's icon in
its material's colors (slots from tools/palette_slots.json obj aliases).

Deterministic: rerunning reproduces the sheet byte-identically.
"""
from PIL import Image

PUB = 'tools/level_editor/public/tiles/combat'
CELLS = [
    'combat_material_icon',
    'combat_material_icon',
    'combat_material_icon',
    'combat_wood_material_icon',
]


def main():
    sheet = Image.new('RGB', (8 * len(CELLS), 8), (255, 255, 255))
    for x, name in enumerate(CELLS):
        im = Image.open('%s/%s.png' % (PUB, name)).convert('RGB')
        sheet.paste(im.resize((8, 8), Image.NEAREST), (x * 8, 0))
    sheet.save('assets/material_tiles.png')
    print('wrote assets/material_tiles.png')


if __name__ == '__main__':
    main()
