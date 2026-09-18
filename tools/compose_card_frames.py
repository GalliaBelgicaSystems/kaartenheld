"""Compose assets/card_frames.png (battle hand-card frame sheet).

Reads the CSV-slug editor PNGs (tools/level_editor/public/tiles/combat/)
and lays out 3 cols x 8 rows of 8x8 cells in VRAM-load order:

  row 0: card top border   (top_left_corner, top_middle, top_right_corner)
  row 1: card middle band  (left_side, center, right_side)
  row 2: card bottom border (bottom_left_corner, bottom_middle, bottom_right_corner)

png2gb converts the sheet to src/gfx/card_frame_tiles.h; the ROM loads all
24 tiles (9 frame tiles at UI_TILE_CARD_FRAME_BASE 118, bar segments,
HUD icons, select arrow, status icons, weapon icons -- VRAM block 1, battle enemy art
starts at 128) and the bank-3 renderer stamps 3x4 card boxes from them.

Exactness: curated art is copied byte-identically (RGBA transparency is
flattened onto white, entry 0 of the card ramps). NO quantization, NO
color snapping, NO resampling: sources must be 8x8; anything else fails
loudly. Off-ramp pixels fail later at sidecar validation
(tools/emit_sheet_sidecars.py), naming the cell and the offending color.
Repaint the pixel; do not adjust the tool.

Deterministic: rerunning reproduces the sheet byte-identically.
"""
from pathlib import Path
from PIL import Image

REPO_ROOT = Path(__file__).resolve().parent.parent
PUB = REPO_ROOT / 'tools' / 'level_editor' / 'public' / 'tiles' / 'combat'
LAYOUT = [
    ['combat_top_left_card_corner', 'combat_top_middle_card', 'combat_top_right_card_corner'],
    ['combat_left_card_side', 'combat_center_card', 'combat_right_card_side'],
    ['combat_bottom_left_card_corner', 'combat_bottom_middle_card', 'combat_bottom_right_card_corner'],
    # Turn-timer bar segments (HUD skin): filled / empty, then the HUD
    # icons from the combat tileset (combat-tileset-description.csv):
    # hp / ap / deck, the up-arrow select icon (replaces the '^'
    # caret on the battle marker/target rows), and the fire / ice /
    # poison status tiles (element riders).  VRAM: frames at
    # UI_TILE_CARD_FRAME_BASE (118-126), filled at UI_TIMER_FILLED (117),
    # empty at 127, HUD icons overwrite the atlas data at 113/114/116,
    # arrow at UI_TILE_SELECT_ARROW (96), status at 110/111/112.
    ['combat_timer_bar_filled', 'combat_timer_bar_empty', 'combat_hp_icon'],
    ['combat_ap_icon', 'combat_deck_icon', 'combat_arrow_pointing_up'],
    ['combat_fire_status', 'combat_ice_status', 'combat_poison_status'],
    # Card weapon icons (skin weapon_tile VRAM ids 104-108): sword,
    # shield, bow, dagger, ring.  The 6th cell stays blank (None); the
    # loader maps it to the unused VRAM 97 scratch tile.
    ['combat_sword_icon', 'combat_shield_icon', 'combat_bow_icon'],
    ['combat_dagger_icon', 'combat_ring_icon', None],
    # Limited-use arrow counters (bow floor row): remaining-uses glyphs
    # 0/1/2/3 plus the power-row nine icon.  VRAM: floor states at 98-101
    # (uses 0..3), power icon at 102 — free BG fetch slots (see
    # s_card_tile_vram_ids in src/ui/ui_battle_content.c).
    ['combat_0_arrows_left', 'combat_1_arrow_left', 'combat_2_arrows_left'],
    ['combat_3_arrows_left', 'combat_nine_icon', None],
]

# Sheet background (white): entry 0 of the card ramps.
BG = (255, 255, 255)


def main():
    rows = len(LAYOUT)
    sheet = Image.new('RGB', (24, rows * 8), BG)
    for y, row in enumerate(LAYOUT):
        for x, name in enumerate(row):
            if name is None:
                continue
            im = Image.open(PUB / ('%s.png' % name)).convert('RGBA')
            if im.size != (8, 8):
                raise ValueError(
                    'card sheet source %s.png is %dx%d, want 8x8 -- '
                    'resampling would invent colors' % (name, im.size[0], im.size[1]))
            im = Image.alpha_composite(Image.new('RGBA', im.size, BG + (255,)), im)
            sheet.paste(im.convert('RGB'), (x * 8, y * 8))
    sheet.save(REPO_ROOT / 'assets' / 'card_frames.png')
    print('wrote assets/card_frames.png')


if __name__ == '__main__':
    main()
