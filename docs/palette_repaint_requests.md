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
 | Bat, all 6: row 3, cols 1-6 | wings `#3d3044` (58 px) + red eyes `#c34e1b` (2 px, top-middle tile) | own new entry `f1eb03, c34e1b, 3d3044, 000000` — exact fit, zero repaint, IF the bat wins the free-slot tie (see solver results). Otherwise wings → gray `#565656`, eyes → red `#8b1b1b` of the shared sprite list |
 | Boss middle two: row 3, cols 10-11 | red details `#8b1b1b` (8 px) live in no list | `ffffff, 8d754a, 565656, 000000` — repaint the 8 red pixels into one of these four (or tell dev: per-tile palettes could keep the red at game-code cost) |
 | Spider, all 6: row 4, cols 7-12 — background stays transparent | 5 colors across the set: blade `#8f8f8f`, handle `#5a4a3d`, black, orange eye `#c34e1b` | a sprite entry of its own — but the only free slot is contested (see tie below), so the eye must join an existing color regardless. Tell dev when done; he flips the spider from background stamp to sprite and the zone background will show through behind it |
 | Kobold top-left (row 4, col 1), top-middle (row 4, col 2), bottom-middle (row 4, col 5) | white eyes `#ffffff` (7 px) | dev adds white to kobold's unused spare entry — solver proved no tile uses it, so nothing on screen changes. No repaint needed unless you'd rather paint the eyes brown: `844f4f` |
 | Mimic top-middle (row 4, col 14) | white eyes `#ffffff` (2 px), and its list is full with no spare entry | repaint the 2 pixels brown (`8d754a`/`6f5a34`/`3f3017`) — no way to keep white, proven |
 | Sword icon (row 1, col 1) | outline `#755930` | `ffffff, dfbd8d, e3ae63, b09266` |
 | Shield icon (row 1, col 3) | outline `#755930` | `ffffff, 8d754a, 6f5a34, 3f3017` |
 | Bow icon (row 1, col 2) | outline `#755930` | `ffffff, 8f8f8f, 565656, 000000` |
 | Dagger icon (row 5, col 7) | outline `#755930` | `ffffff, dfbd8d, b09266, 7136c1` |
 | Ring icon (row 5, col 8) | outline `#755930`, gem `#7ae3f3` | `ffffff, 89cc5e, 5c903a, 000000` |
 | Fire icon (row 2, col 1) | card back `#b09266, #dfbd8d` around the flame | `ffffff, c34e1b, 844f4f, 4a2727` |
 | Ice icon (row 3, col 16) | ice blue `#7ae3f3` | `ffffff, dfbd8d, e3ae63, b09266` — no ice blue exists in any list; pick from the list or tell dev |
 | AP icon (row 2, col 4), deck icon (row 2, col 5), arrow up (row 2, col 2), arrow counters (row 5, cols 9-12), nine (row 5, col 14) | outline `#755930` | `ffffff, dfbd8d, e3ae63, b09266` |

  The brown outline `#755930` appears in no list anywhere — every icon
  using it needs re-outlining in its tile's colors. Note: the lists
  above assume each icon keeps its own slot (option A in the solver
  results); today icons inherit their card's box color, under which no
  repaint can ever match — the design choice comes first, paint second.

## `assets/sprites.png` (12 columns x 3 rows)

 Village people are drawn in sprite colors but shown through village
 palettes, so they need repainting into tans/browns:

 | Tile (row,col) | What's wrong | Allowed colors (first = background) |
 |---|---|---|
 | Guard (row 2, col 1) | dark outline `#3f3017` — GOOD NEWS, see solver results below: no repaint needed, dev wires a new entry for it | `b6a27e, 3f3017` (new entry, exact fit) |
 | Wizard (row 2, col 2) | outline `#3f3017`, robe `#475ca8`, beard `#b6b6b6` | `b6a27e, 8d754a, 79643e, 645233` |
 | Merchant (row 2, col 3) | outline `#3f3017`, coin `#8f591f` | `b6a27e, 8d754a, 79643e, 645233` |
 | Mayor (row 2, col 4) | outline `#3f3017`, red `#8b1b1b`, beard `#b6b6b6` | `b6a27e, f1cf91, ccaa6c, 645233` |
 | Dog frames (row 2, cols 5-6) | red `#8b1b1b` | `b6a27e, 8d754a, 79643e, 645233` |

## `tools/level_editor/public/tiles/enemies/` (no master sheet — paint these files directly)

 | File | What's wrong | Allowed colors (first = background) |
 |---|---|---|
 | `dog_f0.png`, `dog_f1.png` | body `#645233`, accent `#8b1b1b` (51 px) | own new entry `f1eb03, 645233, 8b1b1b` — exact fit, zero repaint, IF the dog wins the free-slot tie (see solver results). Otherwise body → town brown |
 | `fire_f0.png`, `fire_f1.png` | `#26232e, #d7a726, #edc214` (48 px) | own new entry `f1eb03, edc214, d7a726, 26232e` — exact fit, zero repaint, IF the fire wins the tie. Otherwise flames → golds/browns of an existing list |

## Solver results (plain language)

 Dev ran a program (`tools/optimize_palettes.py`) that tries EVERY
 possible way to hand out the 16 color slots (8 background + 8 sprite)
 to every tile, exactly, with zero art changes. It proves what fits
 and what can't — nothing below is a guess.

 **Fits with zero art change (nothing for Florent to do):**
 slime (battle + overworld), bat/spider/kobold/hero overworld, mimic
 and boss overworld, kobold battle (dev adds white to an unused spare
 entry — its other tiles don't use that entry, so nothing on screen
 changes), guard villager (dev adds one new village entry with its
 exact 2 colors), HP heart and poison icons (already fit their slots).

 **Needs repainting (proven — no slot on earth holds these as-is):**
 mimic eyes (2 px), boss red details (8 px), spider eye (2 px),
 overworld dog body+accent (51 px) or fire flames (48 px) or battle
 bat wings+eyes (60 px) — see tie below, wizard / merchant / mayor /
 dog villagers, card icons except HP heart and poison (see card note).

 **The one tie the math can't break:** a single free sprite slot is
 claimed by three tiles that each fit it exactly: battle bat wings,
 overworld dog, overworld fire. Only one fits; the other two get
 repainted. Dev recommends the bat (leftover repaints: 99 px vs 108
 vs 111), but it's your call — all three are zero-change for the
 winner.

 **Ideas the solver checked and rejected:** turning villagers into
 sprites (needs game-code surgery, still leaves 3 villagers homeless,
 and breaks kobold's free fix — worse on every axis); per-tile battle
 palettes for the boss reds (possible with game-code cost, parked
 unless you prefer code over 8 repainted pixels); kicking any color
 out of a used slot (rejected — that's someone else's look).

 **Card icons need a design choice, not just paint:** each icon sits
 on cards whose whole-box color changes (red sword, green ring...),
 but one icon tile can only ever match ONE slot. Three honest
 options: (A) frames always use their brown slot and only the insides
 get tinted — needs a small game-code change, keeps the tinted-card
 look; (B) redraw all icons as black-on-white ink (exact in every
 slot, but cards lose their tinted frames); (C) leave icons on the
 old approximate mapping while everything else goes exact. Dev
 recommends A. HP heart and poison need nothing under any option.

 **Dev-side proposals waiting on your approval (no art touched):**
 new `battle_bat` sprite entry (if bat wins the tie), white added to
 kobold's spare entry, new guard village entry, and the matching
 one-line JSON assignments. Nothing here changes a single art pixel.
