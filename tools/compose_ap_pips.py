"""Compose assets/ap_pips_bg.png (menu AP pip BG tiles).

Reads the BG-ready pip PNGs (tools/level_editor/public/tiles/combat/
combat_ap_pip_1..5.png, derived from the actors_one_ap..five_ap pixels
with the card_frames paper/brown palette) and lays out 5 tiles in cost
order 1-5 (single row, 40x8):

  ap_pips_bg.png cell i = cost i+1 pip.

palette_compiler encodes the sheet with the fightspider ramp (menu text black-on-white,
base slot 6 = UI_COLOR_GOLD); png2gb converts it
to src/gfx/ap_pip_tiles.h; the bank-8 menu body streams the bytes on
demand to UI_TILE_AP_PIP_BASE (236-240, high block-1 window clear of
enemy art and dice). Menu renderer stamps pips by VRAM id.

Deterministic: rerunning reproduces the sheet byte-identically.
"""
from PIL import Image

PUB = 'tools/level_editor/public/tiles/combat'
ORDER = ['combat_ap_pip_1', 'combat_ap_pip_2', 'combat_ap_pip_3',
         'combat_ap_pip_4', 'combat_ap_pip_5']


def main():
    sheet = Image.new('RGB', (5 * 8, 8), (255, 255, 255))
    for i, name in enumerate(ORDER):
        im = Image.open('%s/%s.png' % (PUB, name)).convert('RGB')
        sheet.paste(im.resize((8, 8), Image.NEAREST), (i * 8, 0))
    sheet.save('assets/ap_pips_bg.png')
    print('wrote assets/ap_pips_bg.png')


if __name__ == '__main__':
    main()
