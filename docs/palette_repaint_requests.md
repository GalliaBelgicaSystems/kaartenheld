# Tiles that need repainting

 Florent — the game can only show exact colors from a fixed list per
 tile (the Game Boy holds 8 background palettes + 8 sprite palettes,
 all full, so no new colors can be added). A few tiles still use colors
 outside their list. Everything below is: open the PNG, find the tile
 by column/row (count from 0, top-left is (0,0)), repaint the named
 pixels using ONLY the listed hex codes (eyedropper them in), keep the
 background as-is (white for battle art, yellow `#f1eb03` for sprites,
 tan `#b6a27e` for village people). Max 4 colors per tile. Dev
 re-checks everything with one command after your pass.

 One dev-side change needs your eyes: the boss overworld bottom-right
 tile had 6 stray brown pixels along its left edge (seam bleed from the
 neighboring tile). Dev snapped them to gray `#565656` — confirm, or
 repaint that edge yourself.

## `assets/combat-tile.png` (16 columns x 5 rows)

 | Tile (col,row) | What's wrong | Allowed colors (first = background) |
 |---|---|---|
 | Bat, all 6: (0,2),(1,2),(2,2),(3,2),(4,2),(5,2) | wings `#3d3044`, one tile has red eyes `#c34e1b` | `f1eb03, 8b1b1b, 565656, 000000` — e.g. gray wings, black outlines, red eyes |
 | Boss middle two: (9,2),(10,2) | red details `#8b1b1b` | `ffffff, 8d754a, 565656, 000000` |
 | Spider top-middle: (7,3) | orange eye `#c34e1b` | `ffffff, 8f8f8f, 5a4a3d, 000000` |
 | Kobold top-left (0,3), top-middle (1,3), bottom-middle (4,3) | white eyes `#ffffff` | `f1eb03, 844f4f, 4a2727, 4a2727` — no white in the list. Either repaint the eyes brown, or tell dev and he will swap the duplicated dark entry for white |
 | Mimic top-middle (13,3) | white eyes `#ffffff` (2 px) | `f1eb03, 8d754a, 6f5a34, 3f3017` — same choice: repaint eyes or tell dev |
 | Sword icon (0,0) | outline `#755930` | `ffffff, dfbd8d, e3ae63, b09266` |
 | Shield icon (2,0) | outline `#755930` | `ffffff, 8d754a, 6f5a34, 3f3017` |
 | Bow icon (1,0) | outline `#755930` | `ffffff, 8f8f8f, 565656, 000000` |
 | Dagger icon (6,4) | outline `#755930` | `ffffff, dfbd8d, b09266, 7136c1` |
 | Ring icon (7,4) | outline `#755930`, gem `#7ae3f3` | `ffffff, 89cc5e, 5c903a, 000000` |
 | Fire icon (0,1) | card back `#b09266, #dfbd8d` around the flame | `ffffff, c34e1b, 844f4f, 4a2727` |
 | Ice icon (15,2) | ice blue `#7ae3f3` | `ffffff, dfbd8d, e3ae63, b09266` — no ice blue exists in any list; pick from the list or tell dev |
 | AP icon (3,1), deck icon (4,1), arrow up (1,1), arrow counters (8,4),(11,4),(10,4),(9,4), nine (13,4) | outline `#755930` | `ffffff, dfbd8d, e3ae63, b09266` |

 The brown outline `#755930` appears in no list anywhere — every icon
 using it needs re-outlining in its tile's colors.

## `assets/sprites.png` (12 columns x 3 rows)

 Village people are drawn in sprite colors but shown through village
 palettes, so they need repainting into tans/browns:

 | Tile (col,row) | What's wrong | Allowed colors (first = background) |
 |---|---|---|
 | Guard (0,1) | dark outline `#3f3017` | `b6a27e, 8d754a, 79643e, 645233` |
 | Wizard (1,1) | outline `#3f3017`, robe `#475ca8`, beard `#b6b6b6` | same `town1` list as guard |
 | Merchant (2,1) | outline `#3f3017`, coin `#8f591f` | same `town1` list |
 | Mayor (3,1) | outline `#3f3017`, red `#8b1b1b`, beard `#b6b6b6` | `b6a27e, f1cf91, ccaa6c, 645233` |
 | Dog frames (4,1),(5,1) | red `#8b1b1b` | same `town1` list as guard |

## `tools/level_editor/public/tiles/enemies/` (no master sheet — paint these files directly)

 | File | What's wrong | Allowed colors (first = background) |
 |---|---|---|
 | `dog_f0.png`, `dog_f1.png` | body `#645233`, accent `#8b1b1b` | `f1eb03, ac9d23, 8d754a, 6f5a34` |
 | `fire_f0.png`, `fire_f1.png` | `#26232e, #d7a726, #edc214` | same list — no fire colors exist in any sprite list. If flames can't work in these golds/browns, tell dev instead of forcing it; the alternative is redesigning a palette slot |
