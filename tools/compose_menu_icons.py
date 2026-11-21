"""Compose the card-menu tile sheets from the new combat cells.

Reads the combat-tileset cells (tools/level_editor/public/tiles/combat/,
sliced from assets/combat-tile.png by reload_boss_tiles.py MENU_MAP) and
writes two small sheets:

  assets/menu_riders.png (4 cells, 32x8)
    cell 0: fire rider icon    (combat_fire_icon_menu)
    cell 1: ice rider icon     (combat_ice_icon_menu)
    cell 2: poison rider icon  (combat_poison_icon_menu)
    cell 3: health rider icon  (combat_health_icon_menu)

  assets/menu_cursor.png (2 cells, 16x8)
    cell 0: finger cursor pointing right
            (combat_finger_pointing_right_cursor_menu)
    cell 1: the same finger rotated 90 degrees counter-clockwise, so it
            points UP (replaces the '^' active-tab caret)Two sheets, not one, because the two halves live in different ROM
banks: the menu render bodies (bank 2) stream the rider icons on
every menu entry, while the title body (bank 9, first screen of
every boot) patches the font's '>' / '^' BG glyph bitmaps with the
cursor cells.  One combined sheet would duplicate the other half
into both banks.

palette_compiler encodes each cell with the ramp of the hardware slot it
displays on (element ramps for the riders, white/brown for the finger);
png2gb converts each sheet to src/gfx/menu_rider_tiles.h and
src/gfx/menu_cursor_tiles.h.

Deterministic: rerunning reproduces both sheets byte-identically.
"""
from PIL import Image

PUB = 'tools/level_editor/public/tiles/combat'
RIDERS = [
    'combat_fire_icon_menu',
    'combat_ice_icon_menu',
    'combat_poison_icon_menu',
    'combat_health_icon_menu',
]
CURSOR = 'combat_finger_pointing_right_cursor_menu'


def _load(name):
    return Image.open('%s/%s.png' % (PUB, name)).convert('RGB').resize(
        (8, 8), Image.NEAREST)


def compose(sheet_path, cells):
    sheet = Image.new('RGB', (len(cells) * 8, 8), (255, 255, 255))
    for i, im in enumerate(cells):
        sheet.paste(im, (i * 8, 0))
    sheet.save(sheet_path)
    print('wrote %s' % sheet_path)


def main():
    compose('assets/menu_riders.png', [_load(n) for n in RIDERS])
    finger = _load(CURSOR)
    # ROTATE_90 turns 90 degrees counter-clockwise: right -> up.
    compose('assets/menu_cursor.png',
            [finger, finger.transpose(Image.ROTATE_90)])


if __name__ == '__main__':
    main()
