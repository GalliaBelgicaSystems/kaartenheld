# Palette tutorial (for the artist)

You own every color in the game. All of it lives in one file:
`assets/palette.txt`. If a color on screen doesn't come from this file,
that's a bug — report it.

## The file has two parts

1. **Color dictionary** (named `SECTION`s): `name: #hex`. One entry per
   color you use.
2. **Ramp rows**: `name: SECTION/ref, SECTION/ref, SECTION/ref,
   SECTION/ref`. Four colors per ramp, light to dark. `UNUSED` means
   "repeat the previous shade" (only as the last entry).

Lines starting with `#` are comments. That's the whole contract — there
are no slot tables, anchors, or options in this file.

## Ramp sets (Florent's rule)

- `sprites*` → RPG mode (overworld sprites).
- `fight1`–`fight7` → BATTLE background (cards, UI, icons).
- `fight`+enemy name (`fightslime`, `fightgoblin`, `fightmimic`,
  `fightspider`, `fightbat`, `fightboss`…) → BATTLE sprites (enemy art).
- `field*` / `desolate*` / `castle*` / `town*` → world backgrounds
  (forest / desolate / castle / village).

## Ramps always win

Every tile is colored with an existing ramp, even if it's the wrong one
— the build never fails on color, never invents one. What doesn't fit
exactly is listed in `generated/tiles/ramp_mismatches.json` (run `make
manifest` to refresh): sheet, tile, which ramp was used, which pixels
don't fit. That file is your repaint todo list.

## Quick start

1. Change a hex or a ramp row in `assets/palette.txt`.
2. Run `make manifest && make palette-check`: typos and bad references
   fail loudly here; mismatches print as a report (green, not an error).
3. Run `make gfx && make screenshots` and look at the frames.
4. To use a new ramp in-game, tell dev: hardware slots
   (`tools/palette_slots.json`) are dev-owned. Unslotted ramps are
   reported, never silently dropped.
