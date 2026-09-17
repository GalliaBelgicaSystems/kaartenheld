"""Compose assets/enemy_sprites.png (canonical shared overworld enemy sheet).

Reads the curated 8x8 enemy PNGs (transparent background: the sheet cell
background maps to GB shade 0, which OAM renders transparent on every
world) and lays out one row of cells.  New enemies append cells at the
end (blob offsets must stay stable, see battle_compile.py --ow-coords).

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


TONGUE_RED = (139, 27, 27)
TONGUE_INDEX = 10
# Dark wood dog body (TOWN/wood_outlines_rubble): must survive quantization
# exactly so it keeps sorting darker than the tongue (shade 3 vs 2).
DARK_WOOD = (100, 82, 51)
DARK_WOOD_INDEX = 11
PRESERVED = {TONGUE_RED: TONGUE_INDEX, DARK_WOOD: DARK_WOOD_INDEX}


def main():
    ref = Image.open('assets/desolate-tile.png')
    sheet = Image.new('P', (32, len(LAYOUT) * 8))
    pal = list(ref.getpalette())
    # Exact tongue red (SPRITES/dogtongue_ribbon): the desolate reference
    # has no red, so plain quantization snaps dog tongues to brown.  The
    # sheet has 256 palette entries with only 10 used -- pin the red at
    # index 10 so png2gb reads the authored color back exactly.
    pal[TONGUE_INDEX * 3:TONGUE_INDEX * 3 + 3] = list(TONGUE_RED)
    pal[DARK_WOOD_INDEX * 3:DARK_WOOD_INDEX * 3 + 3] = list(DARK_WOOD)
    sheet.putpalette(pal)
    # OAM transparent background: the curated enemy tiles use this light
    # gray as their (opaque) background, which the per-tile auto shade map
    # in png2gb maps to shade 0 = OAM transparent.  RGBA source tiles with
    # real alpha are composited onto it so transparent pixels keep the
    # sheet convention instead of snapping to black on convert('RGB').
    BG = (147, 141, 161)
    for y, row in enumerate(LAYOUT):
        for x, name in enumerate(row):
            if name is None:
                continue
            # Quantize to the reference palette (no dither): curated PNGs
            # may be P, RGB, or RGBA; colors snap to the sheet ramps.
            im = Image.open('%s/%s.png' % (PUB, name)).convert('RGBA')
            im = Image.alpha_composite(Image.new('RGBA', im.size, BG + (255,)), im)
            rgb = im.convert('RGB')
            im = rgb.quantize(palette=ref, dither=Image.Dither.NONE)
            sheet.paste(im, (x * 8, y * 8))
            # Restore exact preserved pixels (dog body/tongue): opaque
            # source pixels survive the composite byte-identically, so an
            # exact match is safe and deterministic.
            px = rgb.load()
            for dy in range(8):
                for dx in range(8):
                    if px[dx, dy] in PRESERVED:
                        sheet.putpixel((x * 8 + dx, y * 8 + dy),
                                       PRESERVED[px[dx, dy]])
    sheet.save('assets/enemy_sprites.png')
    print('wrote assets/enemy_sprites.png')


if __name__ == '__main__':
    main()
