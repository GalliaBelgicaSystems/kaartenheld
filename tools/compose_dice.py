"""Compose assets/dice.png (chuck-a-luck / battle dice digits).

Reads the CSV-slug editor PNGs (tools/level_editor/public/tiles/combat/
combat_<name>_icon.png, sliced from assets/combat-tile.png) and lays out
10 tiles in digit order 0-9 (single row of 10 cells, 80x8):

  dice.png cell i = digit i (i.e. combat_zero_icon .. combat_nine_icon).

palette_compiler encodes the sheet with the wood ramp (brown on tan,
like the arrow counters); png2gb converts it to src/gfx/dice_tiles.h;
the bank-8 dice body (src/game/dice_content.c, same bank as the data)
streams the bytes on demand to UI_TILE_DICE_BASE (246-255, high end of
the battle-art block, clear of the 128+ enemy-art growth).  Battle +
gamble renderers stamp dice by VRAM id, so the digits are reusable
battle chrome, not gamble-only.

Deterministic: rerunning reproduces the sheet byte-identically.
"""
from PIL import Image

PUB = 'tools/level_editor/public/tiles/combat'
ORDER = ['zero', 'one', 'two', 'three', 'four',
         'five', 'six', 'seven', 'eight', 'nine']


def main():
    sheet = Image.new('RGB', (10 * 8, 8), (255, 255, 255))
    for i, name in enumerate(ORDER):
        im = Image.open('%s/combat_%s_icon.png' % (PUB, name)).convert('RGB')
        sheet.paste(im.resize((8, 8), Image.NEAREST), (i * 8, 0))
    sheet.save('assets/dice.png')
    print('wrote assets/dice.png')


if __name__ == '__main__':
    main()
