# Tiles that need repainting — where we stand

 The Game Boy shows tile art through fixed color lists (8 background +
 8 sprite palettes). Every tile below is checked pixel-by-pixel against
 its list by `tools/emit_sheet_sidecars.py` — that check is currently
 RED, and this file is the complete list of why, who owns each item,
 and what "done" looks like.

 **Already done, verified:**
 - Your orange→red unification is fully in (master + all curated copies
   in sync — bat eyes, spider eye, fire flame, HP heart).
 - Bat wings repainted gray (58 px, master + curated in sync) — bat now
   fits its shared list exactly.
 - Boss scepter restored byte-identical after dev grayed it by mistake —
   brown is intent, untouched since.
  - Master↔curated sync proven across all combat cells.
  - Slime anim frames removed per artist (files, frame, composer row,
    catalog) — slime renders static, tables regenerated.
 - World sheets (forest/castle/desert/village, 171 tiles) pass exactly —
   nothing to do there, they are not listed.
 - Dev-side palette surgery landed: `fight_text` restored, per-battle
   values section added, slot-map rebuilt, spares registry, debug
   markers, dog + guard wired — `palette-check` is green.

  **Blocked, not paint (dev needs your answer first):**
  - Title direction (pink PNG vs tan/brown lists).

 **How to read the tables:** open the PNG, find the tile by row/column
 (rows top to bottom, columns left to right, both from 1 — so "row 3,
 col 1" is the first tile of the third row). Repaint only the named
 pixels, eyedropping ONLY the listed hex codes. Keep backgrounds as-is
 (white battle art, yellow `#f1eb03` sprites, tan `#b6a27e` villagers).
 Max 4 colors per tile.

## `assets/combat-tile.png` (16 columns x 5 rows)

 | Tile (row,col) | What's wrong | Allowed colors (first = background) |
 |---|---|---|
 | Boss left-middle (row 3, col 10) | fits `fightboss2` (`ffffff, 8b1b1b, 565656, 000000`) exactly — no paint | dev assigns it a slot AND teaches the stamper per-tile palettes (the whole boss shares one palette today), or it stays on `fightboss` approximation |
 | Boss middle-center (row 3, col 11) | red `#8b1b1b` + brown `#8d754a` together fit no list (5 colors set-wide with the rest) | repaint red→brown/gray, or same per-tile code as above |
 | Spider, all 6: row 4, cols 7-12 — background stays transparent | eyeless body `{yellow, blade `#8f8f8f`, handle `#5a4a3d`, black}` fits its own new entry exactly; red eye (2 px, synced) must join black/gray | eye fix is paint (2 px); then dev flips the spider to sprite (own entry, see tie) and the zone background shows through |
 | Kobold eyes (row 4, cols 1, 2, 5) | white `#ffffff` (7 px) | dev programs them per battle entry from their own values (no slot needed, nothing on screen changes). No repaint |
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
 | `dog_f0.png`, `dog_f1.png` | body `#645233`, accent `#8b1b1b` (51 px) | takes sprite slot 2 (exact fit, zero repaint — decided: strictly fewer leftovers than fire) |
 | `fire_f0.png`, `fire_f1.png` | `#26232e, #d7a726, #edc214` (48 px) | no free slot left; stays failing with certificate unless you repaint it yourself |
 | `boss_ow_br.png` scepter (master `sprites.png` row 2, col 12), 6 brown `#8d754a` px | intent, stays; whole 2x2 boss uses 5 colors — no single list can hold it and overworld lists are one-per-enemy | no repaint offered; stays failing with certificate unless you repaint it yourself |

## Slime anim frames (removed per artist — no action)

 `combat_top/left/middle/right_slime_2.png`, `slime.json` frame1,
 the composer row and the tileset catalog entries are deleted;
 `enemy_types` frames counter is 1 and slime renders static (holds
 frame 0; overworld `slime_f0/f1` untouched). Battle tables
 regenerated — later sets' offsets shifted deterministically,
 covered by the harness battle scenarios on the next green build.

## Solver results (plain language)

 Dev ran a program (`tools/optimize_palettes.py`) that tries EVERY
 possible way to hand out the 16 color slots (8 background + 8 sprite)
 to every tile, exactly, with zero art changes. Battle sprites ride
 per-battle programming (their own values, no static slots); everything
 else takes static slots. `palette-check` is green on the mapping.

  **Needs no paint (wired or waiting on dev-side steps):**
  slime + kobold battle (own per-battle values, white ordered
  non-transparent); bat (shared gray list, done); guard (new village
  entry, wired); dog (takes sprite slot 2, wired); brown family +
  deck/arrow/AP (after their white→tan) → `fight2` (waits on spider
  flip freeing background slot 2 + bow/AP remaps); ring → `fight7`,
  ice → `fight6`, HP → `fight3` (ramps exist, no free background
  slots — see below); boss left-middle → `fightboss2` (needs a slot
  and per-tile code).

  **Still needs paint (proven — no slot holds these as-is):**
  mimic eyes (2 px), boss middle-center red+brown (8 px), spider eye
  (2 px into black/gray), fire flames (48 px), wizard / merchant /
  mayor / dog villagers, fire back/outline, select arrow.

  **Decided, not tied:** dog takes sprite slot 2 (strict optimum —
  fire would evict dog's exact entry for one fewer placed tile).
  Per-tile palettes (all three kinds) and villager-to-sprite ideas
  were checked and dropped: no free slots for their extra values,
  worse on every axis.

  **Blocked on dev-side answers (not paint, in your court):**
  title direction. Everything else structural is built (battle-time
  loader + restore, per-battle values, slot-map, spares registry,
  debug markers).
