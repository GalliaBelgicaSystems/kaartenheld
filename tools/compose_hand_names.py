"""Compose assets/hand_names.png (battle poker-hand name tiles).

Reads the letter-fragment cells from rows 6-7 of assets/combat-tile.png
(sliced directly by sheet coordinates, cross-checked against
assets/combat-tileset-description.csv labels like reload_boss_tiles.py)
plus the down arrow (combat_arrow_pointing_up_dark rotated 180 degrees,
per the HUD spec -- no down-arrow cell exists in the sheet).

Layout: 28x1 cells in sheet order (index = tile order):
  0: down arrow (rotated dark up-arrow)
  1-14: row-6 cells cols 1-14 (tw o pa ir sh hor rt st ra ig ht f lu sh)
  15-27: row-7 cells cols 0-12 (t hr ee ki nd fo ur fiv ve ul lh ou se)

The two "sh" cells (row 6, cols 5 and 14) are genuinely different art
(SHORT's SH vs FLUSH's SH): both are kept as distinct tiles.

palette_compiler encodes the sheet with the fight3 ramp (brown ink on
white paper); png2gb converts it to src/gfx/hand_name_tiles.h; the
bank-9 hand body streams the staged word on demand to the split window
UI_TILE_HAND_SLOTS (see ui.h).

Deterministic: rerunning reproduces the sheet byte-identically.
"""
import csv
import sys
from PIL import Image

REPO = __file__.rsplit("/tools/", 1)[0]
SHEET = REPO + "/assets/combat-tile.png"
CSV = REPO + "/assets/combat-tileset-description.csv"
PUB = REPO + "/tools/level_editor/public/tiles/combat"

# (sheet col, sheet row, expected CSV label)
CELLS = [
    (1, 6, "tw"), (2, 6, "o"), (3, 6, "pa"), (4, 6, "ir"),
    (5, 6, "sh"), (6, 6, "hor"), (7, 6, "rt"), (8, 6, "st"),
    (9, 6, "ra"), (10, 6, "ig"), (11, 6, "ht"), (12, 6, "f"),
    (13, 6, "lu"), (14, 6, "sh"),
    (0, 7, "t"), (1, 7, "hr"), (2, 7, "ee"), (3, 7, "ki"),
    (4, 7, "nd"), (5, 7, "fo"), (6, 7, "ur"), (7, 7, "fiv"),
    (8, 7, "ve"), (9, 7, "ul"), (10, 7, "lh"), (11, 7, "ou"),
    (12, 7, "se"),
]

# Transparent-background keys normalized to paper white (same keys as
# compose_card_frames.py): the hand cells are brown on white, and the
# arrow PNG carries the near-white key.
KEYS = ((255, 254, 255), (252, 252, 252))


def norm(im):
    px = im.load()
    for j in range(8):
        for i in range(8):
            if px[i, j] in KEYS:
                px[i, j] = (255, 255, 255)
    return im


def main():
    rows = list(csv.reader(open(CSV)))
    sheet = Image.open(SHEET).convert("RGB")
    out = Image.new("RGB", (28 * 8, 8), (255, 255, 255))
    for i, (c, r, want) in enumerate(CELLS):
        label = rows[r][c].strip()
        if label != want:
            sys.exit("ERROR: hand cell (%d,%d) label %r != %r "
                     "(sheet re-layout?)" % (c, r, label, want))
        tile = sheet.crop((c * 8, r * 8, c * 8 + 8, r * 8 + 8))
        out.paste(norm(tile), ((i + 1) * 8, 0))
    arrow = Image.open("%s/combat_arrow_pointing_up_dark.png" % PUB)
    arrow = arrow.convert("RGB").resize((8, 8), Image.NEAREST)
    out.paste(norm(arrow.transpose(Image.ROTATE_180)), (0, 0))
    out.save(REPO + "/assets/hand_names.png")
    print("wrote assets/hand_names.png")


if __name__ == "__main__":
    main()
