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

 Your red unification landed cleanly (dev synced the 4 curated copies
 that still carried the old orange: bat eyes, spider eye, fire flame,
 HP heart — 58 px total). Scepter restored untouched (see below).

 Boss scepter (`assets/sprites.png` row 2, col 12 + overworld
 `boss_ow_br.png`, 6 brown `#8d754a` pixels): dev briefly grayed it by
 mistake and has reverted it byte-identical. The brown is intent and
 stays. The whole 2x2 boss uses 5 colors, which no single 4-color list
 can hold — and overworld palettes are one-per-enemy, so the scepter
 cell can't take a slot of its own either. No repaint is offered for
 it; it stays failing with a certificate unless you repaint it
 yourself.

## `assets/combat-tile.png` (16 columns x 5 rows)

 | Tile (row,col) | What's wrong | Allowed colors (first = background) |
 |---|---|---|
 | Boss left-middle (row 3, col 10) | fits `fightboss2` (`ffffff, 8b1b1b, 565656, 000000`) exactly — no paint | dev assigns it a slot AND teaches the stamper per-tile palettes (the whole boss shares one palette today), or it stays on `fightboss` approximation |
 | Boss middle-center (row 3, col 11) | red `#8b1b1b` + brown `#8d754a` together fit no list (5 colors set-wide with the rest) | repaint red→brown/gray, or same per-tile code as above |
 | Spider, all 6: row 4, cols 7-12 — background stays transparent | eyeless body `{yellow, blade `#8f8f8f`, handle `#5a4a3d`, black}` fits its own new entry exactly; red eye (2 px, synced) must join black/gray | eye fix is paint (2 px); then dev flips the spider to sprite (own entry, see tie) and the zone background shows through |
 | Kobold eyes (row 4, cols 1, 2, 5) | white `#ffffff` (7 px); its list was removed from the file — dev restores it (waiting your palette answers), then adds white to the unused spare entry (proven: no tile uses it, nothing on screen changes) | no repaint unless you'd rather paint the eyes brown: `844f4f` |
 | Mimic top-middle (row 4, col 14) | white eyes `#ffffff` (2 px); teeth verified brown, no action there | repaint the 2 pixels brown (`8d754a`/`6f5a34`/`3f3017`) — no way to keep white, proven |
 | Sword (row 1, col 1), shield (row 1, col 3), bow (row 1, col 2), dagger (row 5, col 7), arrows (row 5, cols 9-12), nine (row 5, col 14) | brown `#755930` — fits `fight2` (`dfbd8d, e3ae63, b09266, 755930`) exactly, zero repaint | dev assigns it a slot (needs the spider flip to free one — see below) |
 | Deck (row 2, col 5), arrow up (row 2, col 2), AP (row 2, col 4) | white backgrounds; rest fits `fight2` | repaint white→card-tan `#dfbd8d` (few px each), then same `fight2` slot as above |
 | Ring (row 5, col 8) | fits `fight7` (`dfbd8d, 7ae3f3, 755930`) exactly, zero repaint | dev assigns it a slot (none free today) |
 | Fire (row 2, col 1) | flame now red (synced); card back `#b09266, #dfbd8d` around it fit no red list | repaint back/outline into `fightgoblin` hues, or tell dev |
 | Ice (row 3, col 16) | fits `fight6` (`ffffff, dfbd8d, b09266, 7ae3f3`) exactly, zero repaint | dev assigns it a slot (none free today) |
 | HP heart (row 2, col 3) | heart now red (synced); fits `fight3` (`ffffff, b09266, 8b1b1b, 755930`) exactly, zero repaint | dev assigns it a slot (none free today) |
 | Select arrow (marker row) | single brown pixel art `#755930` | repaint into `fight1` brown `b09266` (1 color) |

## `assets/sprites.png` (12 columns x 3 rows)

 Village people are drawn in sprite colors but shown through village
 palettes:

 | Tile (row,col) | What's wrong | Allowed colors (first = background) |
 |---|---|---|
 | Guard (row 2, col 1) | dark outline `#3f3017` — no repaint needed, dev wires a new entry for it | `b6a27e, 3f3017` (new entry, exact fit) |
 | Wizard (row 2, col 2) | outline `#3f3017`, robe `#475ca8`, beard `#b6b6b6` | `b6a27e, 8d754a, 79643e, 645233` |
 | Merchant (row 2, col 3) | outline `#3f3017`, coin `#8f591f` | `b6a27e, 8d754a, 79643e, 645233` |
 | Mayor (row 2, col 4) | outline `#3f3017`, red `#8b1b1b`, beard `#b6b6b6` | `b6a27e, f1cf91, ccaa6c, 645233` |
 | Dog frames (row 2, cols 5-6) | red `#8b1b1b` | `b6a27e, 8d754a, 79643e, 645233` |

## `tools/level_editor/public/tiles/enemies/` (no master sheet — paint these files directly)

 | File | What's wrong | Allowed colors (first = background) |
 |---|---|---|
 | `dog_f0.png`, `dog_f1.png` | body `#645233`, accent `#8b1b1b` (51 px) | own new entry `f1eb03, 645233, 8b1b1b` — exact fit, zero repaint, IF the dog wins the free-slot tie (see solver results). Otherwise body → gray `#565656` (shares bat+boss slot, verified to fit) |
 | `fire_f0.png`, `fire_f1.png` | `#26232e, #d7a726, #edc214` (48 px) | own new entry `f1eb03, edc214, d7a726, 26232e` — exact fit, zero repaint, IF the fire wins the tie. Otherwise flames → mimic golds/browns (shares mimic slot, verified to fit) |

## Curated-only slime frames (dev question, no paint asked)

 `combat_top/left/middle/right_slime_2.png` hold live animation art
 (fits `fightslime` exactly, used in battle) but their master cells
 (`combat-tile.png` row 2, cols 14-16) are blank — in both the old and
 the new master, so nothing was deleted. Either paint that art into
 the master blanks or confirm curated-only is fine; dev changes nothing
 either way.

## Solver results (plain language)

 Dev ran a program (`tools/optimize_palettes.py`) that tries EVERY
 possible way to hand out the 16 color slots (8 background + 8 sprite)
 to every tile, exactly, with zero art changes. (Re-run blocked until
 the palette file parses again — deltas below are hand-derived from
 its last exact run plus the verified fits above.)

  **Needs no paint (ramps exist, dev assigns slots as they free up):**
  brown family + deck/arrow/AP (after their white→tan) → `fight2`;
  ring → `fight7`; ice → `fight6`; HP → `fight3`; guard → new village
  entry; kobold eyes → restored spare entry; bat wings+eyes (done,
  gray + red now exact in its shared list). Boss left-middle →
  `fightboss2` (needs per-tile code too — the whole boss shares one
  list today).

  **Still needs paint (proven — no slot holds these as-is):**
  mimic eyes (2 px), boss middle-center red+brown (8 px), spider eye
  (2 px into black/gray), dog (51 px) or fire (48 px) — see tie,
  wizard / merchant / mayor / dog villagers, fire back/outline,
  select arrow.

  **The free-sprite-slot tie (three-way):** dog-own-entry, fire-own-
  entry, and spider-eyeless-own-entry each fit exactly and only one
  slot exists. Leftover repaint math: spider wins → dog 51 + fire 48
  get painted (99 px); dog wins → spider ~150 (full rework into shared
  hues) + fire 48; fire wins → spider ~150 + dog 51. Spider also
  unlocks the brown family (its flip frees the background slot
  `fight2` needs). Your call — math favors spider, then dog.
  The boss scepter is NOT in this tie (5 colors can't take any slot;
  separate entry above).

  **Blocked on dev-side answers (not paint, in your court):**
  palette restores, slot-map rebuild, title direction — then dev
  re-runs the solver gate and the counts above get re-proven.
