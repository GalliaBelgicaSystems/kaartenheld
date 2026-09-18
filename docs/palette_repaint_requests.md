# Tiles that need repainting

 Florent — the game can only show exact colors from a fixed list per
 tile (the Game Boy holds 8 background palettes + 8 sprite palettes,
 all full, so no new colors can be added). A few tiles still use colors
 outside their list. Everything below is: open the PNG, find the tile
 by row/column (rows 1-5 top to bottom, columns left to right, both
 starting at 1 — so "row 3, col 1" is the first tile of the third
 row), repaint the named pixels using ONLY the listed hex codes
 (eyedropper them in), keep the background as-is (white for battle
 art, yellow `#f1eb03` for sprites, tan `#b6a27e` for village
 people). Max 4 colors per tile. Dev re-checks everything with one
 command after your pass.

 One dev-side change needs your eyes: the boss overworld bottom-right
 tile had 6 stray brown pixels along its left edge (seam bleed from the
 neighboring tile). Dev snapped them to gray `#565656` — confirm, or
 repaint that edge yourself.

## `assets/combat-tile.png` (16 columns x 5 rows)

 | Tile (row,col) | What's wrong | Allowed colors (first = background) |
 |---|---|---|
 | Bat, all 6: row 3, cols 1-6 | 5 of the 6 tiles already use just 3 colors, so count is fine — the wing color itself matches nothing: wings `#3d3044` | `f1eb03, 8b1b1b, 565656, 000000` — repaint only the wings to gray `#565656`; the 2 red eye pixels (top-middle tile) already match and stay |
 | Boss middle two: row 3, cols 10-11 | red details `#8b1b1b` | `ffffff, 8d754a, 565656, 000000` |
 | Spider, all 6: row 4, cols 7-12 — background stays transparent | 5 colors across the set: blade `#8f8f8f`, handle `#5a4a3d`, black, orange eye `#c34e1b` | sprite palette, slot 2 is free — but a sprite palette holds 4 colors max, so the eye must join an existing color (e.g. black or blade gray). Tell dev when done; he flips the spider from background stamp to sprite and the zone background will show through behind it |
 | Kobold top-left (row 4, col 1), top-middle (row 4, col 2), bottom-middle (row 4, col 5) | white eyes `#ffffff` | `f1eb03, 844f4f, 4a2727, 4a2727` — no white in the list. Either repaint the eyes brown, or tell dev and he will swap the duplicated dark entry for white |
 | Mimic top-middle (row 4, col 14) | white eyes `#ffffff` (2 px) | `f1eb03, 8d754a, 6f5a34, 3f3017` — same choice: repaint eyes or tell dev |
 | Sword icon (row 1, col 1) | outline `#755930` | `ffffff, dfbd8d, e3ae63, b09266` |
 | Shield icon (row 1, col 3) | outline `#755930` | `ffffff, 8d754a, 6f5a34, 3f3017` |
 | Bow icon (row 1, col 2) | outline `#755930` | `ffffff, 8f8f8f, 565656, 000000` |
 | Dagger icon (row 5, col 7) | outline `#755930` | `ffffff, dfbd8d, b09266, 7136c1` |
 | Ring icon (row 5, col 8) | outline `#755930`, gem `#7ae3f3` | `ffffff, 89cc5e, 5c903a, 000000` |
 | Fire icon (row 2, col 1) | card back `#b09266, #dfbd8d` around the flame | `ffffff, c34e1b, 844f4f, 4a2727` |
 | Ice icon (row 3, col 16) | ice blue `#7ae3f3` | `ffffff, dfbd8d, e3ae63, b09266` — no ice blue exists in any list; pick from the list or tell dev |
 | AP icon (row 2, col 4), deck icon (row 2, col 5), arrow up (row 2, col 2), arrow counters (row 5, cols 9-12), nine (row 5, col 14) | outline `#755930` | `ffffff, dfbd8d, e3ae63, b09266` |

 The brown outline `#755930` appears in no list anywhere — every icon
 using it needs re-outlining in its tile's colors.

## `assets/sprites.png` (12 columns x 3 rows)

 Village people are drawn in sprite colors but shown through village
 palettes, so they need repainting into tans/browns:

 | Tile (row,col) | What's wrong | Allowed colors (first = background) |
 |---|---|---|
 | Guard (row 2, col 1) | dark outline `#3f3017` | `b6a27e, 8d754a, 79643e, 645233` |
 | Wizard (row 2, col 2) | outline `#3f3017`, robe `#475ca8`, beard `#b6b6b6` | same `town1` list as guard |
 | Merchant (row 2, col 3) | outline `#3f3017`, coin `#8f591f` | same `town1` list |
 | Mayor (row 2, col 4) | outline `#3f3017`, red `#8b1b1b`, beard `#b6b6b6` | `b6a27e, f1cf91, ccaa6c, 645233` |
 | Dog frames (row 2, cols 5-6) | red `#8b1b1b` | same `town1` list as guard |

## `tools/level_editor/public/tiles/enemies/` (no master sheet — paint these files directly)

 | File | What's wrong | Allowed colors (first = background) |
 |---|---|---|
 | `dog_f0.png`, `dog_f1.png` | body `#645233`, accent `#8b1b1b` | `f1eb03, ac9d23, 8d754a, 6f5a34` |
 | `fire_f0.png`, `fire_f1.png` | `#26232e, #d7a726, #edc214` | same list — no fire colors exist in any sprite list. If flames can't work in these golds/browns, tell dev instead of forcing it; the alternative is redesigning a palette slot |
