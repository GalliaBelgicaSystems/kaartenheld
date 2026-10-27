# Tiny repaint job — two sprites are losing a colour

**Total work: 34 pixels.** Two colours in `assets/sprites.png` are not in the
sprite palette they are drawn with, so the game snaps them to the nearest colour
it does have. Result: the dog's red collar renders brown, and the AP-cost pips
render grey-brown instead of tan.

Nothing else is wrong — no shape, layout or size changes are needed.

---

## Background (30 seconds)

Game Boy sprites can show **3 colours + transparent**. Each group of sprites
shares one 3-colour palette ("ramp"). Two of our groups currently need a
**4th** colour, and the hardware slot they share has no room, so the extra
colour gets replaced by the closest one available.

There is a code-only way to fix this (give each sprite its own palette slot),
but it touches screen-transition palette handling. A 34-pixel repaint is much
safer, so that's what this job is.

---

## Job 1 — dog collar (3 pixels)

**File:** `assets/sprites.png`

The dog is two frames, 8×8 each:

| Frame | Cell (col, row) | Pixels to change |
|---|---|---|
| dog frame 1 | (4, 1) | `(33, 12)` |
| dog frame 2 | (5, 1) | `(41, 12)`, `(41, 13)` |

**Change:** `#8b1b1b` (red) → **`#8f591f`** (orange-brown, already used by the
merchant's coin).

`#8f591f` keeps the collar warm and distinct from the tan body (`#8d754a`).
If you prefer it more muted, `#3f3017` (dark brown) also works.

---

## Job 2 — AP-cost pips (31 pixels)

**File:** `assets/sprites.png`

Three small icons, 8×8 each (the 1 / 2 / 3 energy-cost dots on battle cards):

| Icon | Cell (col, row) | Pixels to change |
|---|---|---|
| "1" pip | (10, 2) | `(84,18) (83,19) (84,19) (84,20) (87,20) (84,21) (84,22)` |
| "2" pip | (11, 2) | `(90,18) (91,18) (92,18) (92,19) (90,20) (91,20) (92,20) (95,20) (90,21) (90,22) (91,22) (92,22)` |
| "3" pip | (0, 3) | `(2,26) (3,26) (4,26) (4,27) (2,28) (3,28) (4,28) (7,28) (4,29) (2,30) (3,30) (4,30)` |

**Change:** `#b09266` (tan) → **`#565656`** (mid grey, already used by the
spider).

Grey matches the current pip's brightness best. If you'd rather the cost read
as "hot", `#8b1b1b` (red) is also exact.

---

## Please don't change

- anything outside the pixel lists above;
- the sheet size (`96×32`) or the 8×8 grid;
- the transparent key colour (`#f1eb03`).

---

## When you've saved the PNG

```bash
python3 tools/level_editor/reload_boss_tiles.py   # refresh the derived slices
make gfx                                          # re-encode + verify palettes
```

What "done" looks like:

- `python3 tools/level_editor/reload_boss_tiles.py --check` prints
  `clean (24 PNGs + swatches in sync)`.
- `generated/tiles/ramp_mismatches.json` is an empty list — the compiler's
  to-do list is cleared.
- In game: the dog has a lighter orange-brown collar; AP pips are grey.

If either command complains, it will name the tile and the offending colour —
that's the build telling you a pixel was missed.
