"""Compose assets/enemy_sprites.png (canonical shared overworld enemy sheet).

Reads the curated 8x8 enemy PNGs and lays out one row of cells. New
enemies append cells at the end (blob offsets must stay stable, see
battle_compile.py --ow-coords).

Exactness: curated art is copied byte-identically (RGBA transparency is
flattened onto SPRITES/background yellow, which is entry 0 -- hence
transparent -- of every OBJ ramp). NO quantization, NO color snapping:
any pixel the artist did not paint in an exact ramp color fails loudly
later at sidecar validation (tools/emit_sheet_sidecars.py), naming the
cell and the offending color. Repaint the pixel; do not adjust the tool.

Deterministic: rerunning reproduces the sheet byte-identically.
"""
from PIL import Image

PUB = 'tools/level_editor/public/tiles/enemies'
LAYOUT = [
    ['slime_f0', 'slime_f1', 'bat_f0', 'bat_f1'],
    ['boss_ow_tl', 'boss_ow_tr', 'boss_ow_bl', 'boss_ow_br'],
    ['kobold_f0', 'kobold_f1', 'spider_f0', 'spider_f1'],
    ['kobold_idle', 'mimic_f0', 'mimic_f1', None],
    # Friendly "real NPC" art (dogs, fires): appends at the end — blob
    # offsets of earlier rows must never renumber (battle_compile.py
    # --ow-coords).  Rendered by static (non-hostile) actors through the
    # SPRITE_KIND_ENEMY path (sprite_tile_for reads g_enemy_types).
    ['dog_f0', 'dog_f1', 'fire_f0', 'fire_f1'],
]

# Tile-name -> sheet (x, y): the single source of truth for enemy
# overworld cell addressing.  battle_compile.py imports this (no side
# effects) to resolve screens/enemy_types overworld.cells.
TILE_COORDS = {}
for _y, _row in enumerate(LAYOUT):
    for _x, _name in enumerate(_row):
        if _name is not None:
            TILE_COORDS[_name] = (_x, _y)


# SPRITES/background: entry 0 (transparent) of every OBJ ramp. Opaque
# source pixels are preserved byte-identically; transparent ones become
# background (transparent on hardware).
BG = (241, 235, 3)


def main():
    sheet = Image.new('RGB', (32, len(LAYOUT) * 8), BG)
    for y, row in enumerate(LAYOUT):
        for x, name in enumerate(row):
            if name is None:
                continue
            im = Image.open('%s/%s.png' % (PUB, name)).convert('RGBA')
            im = Image.alpha_composite(Image.new('RGBA', im.size, BG + (255,)), im)
            sheet.paste(im.convert('RGB'), (x * 8, y * 8))
    sheet.save('assets/enemy_sprites.png')
    print('wrote assets/enemy_sprites.png')


if __name__ == '__main__':
    main()
