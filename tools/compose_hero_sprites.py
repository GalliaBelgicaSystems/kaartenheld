"""Compose assets/hero_sprites.png (canonical shared overworld hero sheet).

Reads the curated 8x8 hero PNGs and lays out 1 row of cells:
hero_f0, hero_f1.

Exactness: curated art is copied byte-identically (RGBA transparency is
flattened onto SPRITES/background yellow, which is entry 0 -- hence
transparent -- of every OBJ ramp). NO quantization, NO color snapping:
any pixel the artist did not paint in an exact ramp color fails loudly
later at sidecar validation (tools/emit_sheet_sidecars.py), naming the
cell and the offending color. Repaint the pixel; do not adjust the tool.

Deterministic: rerunning reproduces the sheet byte-identically.
"""
from pathlib import Path
from PIL import Image

REPO_ROOT = Path(__file__).resolve().parent.parent
PUB = REPO_ROOT / 'tools' / 'level_editor' / 'public' / 'tiles' / 'hero'

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

# SPRITES/background: entry 0 (transparent) of every OBJ ramp.
BG = (241, 235, 3)


def main():
    sheet = Image.new('RGB', (16, 8), BG)
    for y, row in enumerate(LAYOUT):
        for x, name in enumerate(row):
            if name is None:
                continue
            im = Image.open(PUB / ('%s.png' % name)).convert('RGBA')
            if im.size != (8, 8):
                raise ValueError(
                    'hero sheet source %s.png is %dx%d, want 8x8 -- '
                    'resampling would invent colors' % (name, im.size[0], im.size[1]))
            im = Image.alpha_composite(Image.new('RGBA', im.size, BG + (255,)), im)
            sheet.paste(im.convert('RGB'), (x * 8, y * 8))
    sheet.save(REPO_ROOT / 'assets' / 'hero_sprites.png')
    print('wrote assets/hero_sprites.png')


if __name__ == '__main__':
    main()
