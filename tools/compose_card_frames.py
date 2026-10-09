"""Compose assets/card_frames.png (battle hand-card frame sheet).

Reads the CSV-slug editor PNGs (tools/level_editor/public/tiles/combat/)
and lays out 3 cols x 11 rows of 8x8 cells in VRAM-load order:

  row 0: card top border   (top_left_corner, top_middle, top_right_corner)
  row 1: card middle band  (left_side, center, right_side)
  row 2: card bottom border (bottom_left_corner, bottom_middle, bottom_right_corner)

png2gb converts the sheet to src/gfx/card_frame_tiles.h; the ROM loads all
31 tiles (9 frame tiles at UI_TILE_CARD_FRAME_BASE 118, bar segments,
HUD icons, select arrow, status icons, weapon icons -- VRAM block 1, battle enemy art
starts at 128) and the bank-3 renderer stamps 3x4 card boxes from them.

Deterministic: rerunning reproduces the sheet byte-identically.
"""
from PIL import Image

PUB = 'tools/level_editor/public/tiles/combat'
LAYOUT = [
    ['combat_top_left_card_corner', 'combat_top_middle_card', 'combat_top_right_card_corner'],
    ['combat_left_card_side', 'combat_center_card', 'combat_right_card_side'],
    ['combat_bottom_left_card_corner', 'combat_bottom_middle_card', 'combat_bottom_right_card_corner'],
    # Turn-timer bar segments (HUD skin): filled / empty, then the HUD
    # icons from the combat tileset (combat-tileset-description.csv):
    # hp / ap / deck, the light/dark up-arrow select icons (light = cursor,
    # dark = cursor on an already-selected card; both replace the '^'
    # caret on the battle marker/target rows).
    # VRAM: frames at
    # UI_TILE_CARD_FRAME_BASE (118-126), filled at UI_TIMER_FILLED (117),
    # empty at 127, HUD icons overwrite the atlas data at 113/114/116,
    # light arrow at UI_TILE_SELECT_ARROW (96), dark arrow at
    # UI_TILE_SELECT_ARROW_DARK (110, overwrites the atlas Flame Spire).
    ['combat_timer_bar_filled', 'combat_timer_bar_empty', 'combat_hp_icon'],
    ['combat_ap_icon', 'combat_deck_icon', 'combat_arrow_pointing_up_light'],
    ['combat_arrow_pointing_up_dark', None, None],
    # Card weapon icons (skin weapon_tile VRAM ids 104-108): sword,
    # shield, bow, dagger, ring.  The 6th cell stays blank (None); the
    # loader maps it to the unused VRAM 97 scratch tile.
    ['combat_sword_icon', 'combat_shield_icon', 'combat_bow_icon'],
    ['combat_dagger_icon', 'combat_ring_icon', None],
    # Limited-use arrow counters (bow floor row): remaining-uses glyphs
    # 0-4 plus the power-row nine icon.  VRAM: floor states at 98-101
    # (uses 0..3) and 97 (uses 4), power icon at 102 — free BG fetch
    # slots (see s_card_tile_vram_ids in src/ui/ui_battle_content.c).
    # The row-7 blank still maps to 97 first, but the appended 4-arrows
    # cell overwrites it right after (nothing reads blank-97).
    ['combat_0_arrows_left', 'combat_1_arrow_left', 'combat_2_arrows_left'],
    ['combat_3_arrows_left', 'combat_nine_icon', None],
    ['combat_4_arrows_left', None, None],
]


def main():
    rows = len(LAYOUT)
    sheet = Image.new('RGB', (24, rows * 8), (255, 255, 255))
    for y, row in enumerate(LAYOUT):
        for x, name in enumerate(row):
            if name is None:
                continue
            im = Image.open('%s/%s.png' % (PUB, name)).convert('RGB')
            sheet.paste(im.resize((8, 8), Image.NEAREST), (x * 8, y * 8))
    sheet.save('assets/card_frames.png')
    print('wrote assets/card_frames.png')


if __name__ == '__main__':
    main()
