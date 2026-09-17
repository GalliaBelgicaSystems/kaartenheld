"""Compose assets/hero_sprites.png (canonical shared overworld hero sheet).

Reads the curated 8x8 hero PNGs (transparent background) and lays out
1 row of cells: hero_f0, hero_f1.

Deterministic: rerunning reproduces the sheet byte-identically.
"""
from PIL import Image

PUB = 'tools/level_editor/public/tiles/hero'
LAYOUT = [
    ['hero_f0', 'hero_f1'],
]

# Tile-name -> sheet (x, y): single source of truth for hero cell addressing.
# battle_compile.py imports this (no side effects) to resolve hero.json cells.
TILE_COORDS = {}
for _y, _row in enumerate(LAYOUT):
    for _x, _name in enumerate(_row):
        if _name is not None:
            TILE_COORDS[_name] = (_x, _y)


def main():
    ref = Image.open('assets/desolate-tile.png')
    sheet = Image.new('P', (16, 8))
    sheet.putpalette(ref.getpalette())
    # OAM transparent background: the curated hero tiles use this light
    # gray as their background, which the per-tile auto shade map
    # in png2gb maps to shade 0 = OAM transparent.
    BG = (147, 141, 161)
    for y, row in enumerate(LAYOUT):
        for x, name in enumerate(row):
            if name is None:
                continue
            im = Image.open('%s/%s.png' % (PUB, name)).convert('RGBA')
            im = Image.alpha_composite(Image.new('RGBA', im.size, BG + (255,)), im)
            im = im.convert('RGB').quantize(palette=ref, dither=Image.Dither.NONE)
            sheet.paste(im, (x * 8, y * 8))
    sheet.save('assets/hero_sprites.png')
    print('wrote assets/hero_sprites.png')


if __name__ == '__main__':
    main()