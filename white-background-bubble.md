# White Background for `!` and `?` Overworld Marks (`assets/sprites.png`)

## Executive Summary

**Yes — there is a clean spot to put `#ffffff` on an OBJ sprite ramp without touching a single line of C code.**

In fact, the artist sheet `assets/sprites.png` at cells `(1, 3)` (`exclamation point`) and `(2, 3)` (`question mark`) **already has a pure white (`#ffffff`) bubble background** and black (`#000000`) glyph over the yellow chroma-key surround (`#f1eb03`):
- `(1, 3) exclamation point`: `35 px #ffffff` (bubble), `5 px #000000` (`!`), `24 px #f1eb03` (transparent)
- `(2, 3) question mark`: `33 px #ffffff` (bubble), `7 px #000000` (`?`), `24 px #f1eb03` (transparent)

In commit `77e939a5`, the derived slices (`tools/level_editor/public/tiles/actors/actors_exclamation_point.png` and `actors_question_mark.png`) were manually recolored to `#b6b6b6` (beard-gray) + `#3f3017` (dark brown) to fit `sprites7` (OBJ Slot 5), leaving `reload_boss_tiles.py --check` out of sync with `assets/sprites.png`.

Below is:
1. Why `tools/png2gb.py` turned `#ffffff` transparent previously (and why adding `#ffffff` to the ramp fixes it automatically).
2. A complete audit of all **8 CGB hardware OBJ palette slots (`0..7`)** and all `sprites*` ramps in `assets/palette.txt`.
3. Ranked recommendations for adding `#ffffff`, with exact step-by-step instructions.

---

## 1. How `tools/png2gb.py` Handles `#ffffff` on Sprite Sheets

In `tools/png2gb.py` (`nearest_shade_index`, lines 142–157):

```python
def nearest_shade_index(
    color: tuple[int, int, int],
    shades: list[tuple[int, int, int]],
    transparent0: bool = False,
) -> int:
    for i, s in enumerate(shades):
        if color == s:
            return i
    if transparent0 and color == (255, 255, 255):
        return 0
```

- When `#ffffff` is **not** in the target `sprites*` ramp, the exact-match loop misses and `if transparent0 and color == (255, 255, 255): return 0` maps white to **shade 0 (transparent)**, making the bubble invisible.
- When `#ffffff` **is** in the target `sprites*` ramp (at shade 1, 2, or 3), the exact-match loop (`if color == s: return i`) runs **before** the `transparent0` fallback and maps `#ffffff` cleanly to that opaque shade index with zero mismatch.

---

## 2. Complete Audit of All 8 CGB OBJ Palette Slots (`tools/palette_slots.json`)

Every 2bpp CGB sprite has 4 shade indices:
- **Shade 0**: Hardware transparent (`SPRITES/background` = `#f1eb03`).
- **Shades 1, 2, 3**: 3 opaque colors per slot.

For the `!` and `?` marks to render a **white bubble** (`#ffffff`) and a **dark glyph** (`#000000` or `#3f3017`), their OBJ slot's ramp must dedicate **2 of its 3 opaque shades** to white and black/dark-brown, leaving **1 opaque shade** for any other sprite sharing that slot.

| OBJ Slot | Ramp Name | Shade 1 | Shade 2 | Shade 3 | All Sprites Using This Slot | Can It Hold White (`#ffffff`) + Dark Glyph? |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Slot 0** | `sprites` | `#673c3c` (`kobold`) | `#565656` (`hero`) | `#000000` (`black`) | `hero` (`#565656, #000000`), `spider` (`#565656, #000000`), `bat` (`#000000`), `kobold` (`#673c3c`) | **No (0 free shades).** All 3 opaque shades are actively used on overworld maps (`hero` + `kobold`). |
| **Slot 1** | `sprites5` | `#ac9d23` (`chest_gold`) | `#8d754a` (`chest_light_wood`) | `#6f5a34` (`chest_dark_wood`) | `treasure_chest`, `mimic` | **No (0 free shades).** Closed chest uses all 3 shades + lacks a dark/black glyph color. |
| **Slot 2** | `sprites12` | `#b6b6b6` (`npc_beard`) | `#7ae3f3` (`ice_icon`) | `#7136c1` (`poison_icon`) | `rider_ice` (`#b6b6b6` 5px, `#7ae3f3` 20px), `rider_poison` (`#7136c1` 18px) | **Only with compromise or C change.** Has no dark shade (`#7ae3f3` cyan and `#7136c1` purple are both needed by battle/item riders). However, **Slot 2 is 100% unused in the overworld** (see Option 3). |
| **Slot 3** | `sprites11` | `#77e331` (`slime_light`) | `#5c903a` (`slime_dark`) | `UNUSED` (repeats `#5c903a`) | `slime`, `slime_knight`, and `RIDER_OBJ_HEALTH` (`src/battle/card.h:30`) | **No.** Although shade 3 is marked `UNUSED` in `assets/palette.txt`, two things prevent using it for marks: (1) `RIDER_OBJ_HEALTH` draws the 1bpp `'+'` font tile (`RIDER_TILE_HEALTH = 11`), whose foreground pixels are encoded as **shade 3 (`0b11`)** — putting `#ffffff` in shade 3 turns the green `'+'` heal icon on cards **white**; and (2) `sprites11` only has bright/medium green (`#77e331`, `#5c903a`) and no black/brown shade for the `!`/`?` glyph. |
| **Slot 4** | `sprites2` | `#8b1b1b` (`slime_lord`) | `#565656` (`hero`) | `#000000` (`black`) | `slime_lord` (`#8b1b1b, #565656, #000000`), `rider_fire` (`#8b1b1b`), `ap_1`/`ap_2` | **Only if `slime_lord` highlight changes.** `slime_lord` (`boss_ow_*`) uses `#565656` for 10–13 crown/highlight pixels per tile; replacing `#565656` with `#ffffff` turns those pixels white. |
| **Slot 5** | `sprites7` | `#b6b6b6` (`npc_beard`) | `#475ca8` (`mage_cloth`) | `#3f3017` (`npc`) | **`npc_wizard` (in `town`) and `mark_icons` (`!` / `?`) ONLY** | **YES — Best Fit (Option 1 & Option 2).** Only `npc_wizard` shares this slot! |
| **Slot 6** | `sprites9` | `#8d754a` (`chest_light_wood`) | `#8f591f` (`npc_cloth2`) | `#3f3017` (`npc`) | `npc_merchant` (all 3 shades), `dog` (`#8d754a` + off-ramp `#8b1b1b`); also `BATTLE_OBJ_SCRATCH2` in battle | **No (0 free shades).** `npc_merchant` uses all 3 shades. |
| **Slot 7** | `sprites8` | `#b6b6b6` (`npc_beard`) | `#8b1b1b` (`slime_lord`) | `#3f3017` (`npc`) | `npc_mayor` (all 3 shades), `npc_guard`, `npc_heal_mage`; also `BATTLE_OBJ_SCRATCH` in battle | **No (0 free shades).** `npc_mayor` uses all 3 shades. |

*(Note: `sprites6` and `sprites10` in `assets/palette.txt` are not assigned to any hardware OBJ slot in `tools/palette_slots.json`; `sprites6` is loaded dynamically into Slot 7 during Mimic battles.)*

---

## 3. Recommended Solutions

### Option 1 (Recommended — Simplest, Zero C Changes): Change Shade 1 of `sprites7` (OBJ Slot 5) from `#b6b6b6` to `#ffffff`

**Why this works so well:**
- `screens/mark_icons.json` is **already wired to `sprites7` (OBJ Slot 5)**.
- The **only** other user of `sprites7` (Slot 5) in the entire game is `npc_wizard` (`screens/enemy_types/npc_wizard.json`, placed in `town`).
- `npc_wizard` uses:
  - Shade 1 (`#b6b6b6`): 8 beard pixels
  - Shade 2 (`#475ca8`): 24 blue robe pixels
  - Shade 3 (`#3f3017`): 12 dark-brown face/staff pixels
- Meanwhile, `npc_mayor` (who also uses `SPRITES/npc_beard` `#b6b6b6`) is on `sprites8` (Slot 7), and `rider_ice` is on `sprites12` (Slot 2) — so adding a `white: #ffffff` token to `SPRITES` and using it in `sprites7` leaves `npc_mayor` and `rider_ice` completely untouched, and simply gives the town wizard a **white beard** (`#ffffff` instead of `#b6b6b6`) while giving `!` and `?` a **pure white bubble (`#ffffff`)** and dark-brown (`#3f3017`) glyph!
- Slot 5 is also **never overwritten by battle** (battle only overwrites Slots 6 and 7 via `BATTLE_OBJ_SCRATCH2` and `BATTLE_OBJ_SCRATCH`, and `ui_load_tileset_banked` restores Slots 5–7 on tileset load).

**Exact steps for Option 1:**
1. **`assets/palette.txt`**:
   - Under `SPRITES`, add:
     ```text
     white: #ffffff
     ```
   - Update `sprites7` from:
     ```text
     sprites7: SPRITES/background, SPRITES/npc_beard, SPRITES/mage_cloth, SPRITES/npc
     ```
     to:
     ```text
     sprites7: SPRITES/background, SPRITES/white, SPRITES/mage_cloth, SPRITES/npc
     ```
2. **`assets/sprites.png`** (to make the asset pipeline 100% lossless with `--max-mismatches 0`):
   - At cell `(1, 3)` (`exclamation point`) and cell `(2, 3)` (`question mark`), the bubble fill is already `#ffffff`. Change the 5-pixel `!` and 7-pixel `?` glyph from `#000000` to `#3f3017` (`SPRITES/npc`, dark brown) so it matches shade 3 of `sprites7` with 0 mismatches — OR, if you want the glyph to be pure `#000000` black, change shade 3 of `sprites7` to `SPRITES/black` (`#000000`) and change the wizard's 8 beard pixels at `(1, 1)` to `#ffffff` and 12 face/staff pixels to `#000000`.
   - At cell `(1, 1)` (`npc_wizard`), change the 8 beard pixels from `#b6b6b6` to `#ffffff` so `npc_wizard.json` also compiles with `0` mismatches.
3. **Regenerate actor slices & ROM headers**:
   - Run `python3 tools/level_editor/reload_boss_tiles.py` (which copies `(1, 3)` and `(2, 3)` from `assets/sprites.png` directly into `tools/level_editor/public/tiles/actors/actors_exclamation_point.png` and `actors_question_mark.png` and regenerates `assets/enemy_sprites.png`).
   - Run `make debug && make release`.

---

### Option 2 (Zero C Changes, Zero Change to `npc_wizard`'s Gray Beard): Move `npc_wizard` to `sprites8` (Slot 7) and Dedicate `sprites7` (Slot 5) to Marks

If you want `!` and `?` to be **exact `#ffffff` white + `#000000` black** as currently painted in `assets/sprites.png` `(1, 3)` and `(2, 3)` without changing a single pixel on `npc_wizard`:
- Notice that `npc_heal_mage` (`screens/enemy_types/npc_heal_mage.json`) already renders the wizard sprite using `sprites8` (Slot 7). Or if `npc_wizard` in `town` is given a red robe (`sprites8`) / brown robe (`sprites9`), **nothing else in the game uses `sprites7` (Slot 5)**.
- Then `sprites7` (Slot 5) can be defined as:
  ```text
  sprites7: SPRITES/background, SPRITES/white, SPRITES/black, UNUSED
  ```
- With that ramp, `assets/sprites.png` cells `(1, 3)` and `(2, 3)` (`#f1eb03`, `#ffffff`, `#000000`) compile with **0 mismatches** as-is via `reload_boss_tiles.py`!

---

### Option 3 (Minimal C Change, Zero Visual Changes to Any Existing Sprite): Time-Share OBJ Slot 2 (`sprites12`) or Slot 5 (`sprites7`)

Because `!` and `?` marks are **only drawn in the overworld** (`src/ui/ui_world_sprite_banked.c:45-51`), while `rider_ice` and `rider_poison` are **only drawn in the battle and item screens**:
- **OBJ Slot 2 (`sprites12`) is completely unused in the overworld.**
- **OBJ Slot 5 (`sprites7`) is completely unused in battle/item screens.**
- You could keep `npc_wizard` on `sprites7` untouched, define a dedicated `sprites_marks: SPRITES/background, SPRITES/white, SPRITES/black, UNUSED` ramp in `assets/palette.txt`, load `sprites_marks` into OBJ Slot 2 in `ui_load_tileset_banked()` (overworld), and restore `sprites12` into OBJ Slot 2 when entering battle/item screens (just like Slots 6 and 7 are already swapped for battle in `src/game/marks_banked.c`).
- However, this requires updating C palette-loading code and the static manifest verifier (`tools/verify_palette_manifest.py`), whereas **Option 1** requires **zero C or Python tooling changes** and works within the existing static slot architecture.

---

## Summary Recommendation

Go with **Option 1**:
1. Add `white: #ffffff` to `SPRITES` in `assets/palette.txt` and set `sprites7` (OBJ Slot 5) to `SPRITES/background, SPRITES/white, SPRITES/mage_cloth, SPRITES/npc` (or `SPRITES/black` as shade 3 if you prefer a pure black `!`/`?` glyph and wizard outline).
2. Sync `assets/sprites.png` cells `(1, 3)`, `(2, 3)`, and `(1, 1)` so both the marks and the wizard beard use `#ffffff`, then run `python3 tools/level_editor/reload_boss_tiles.py`.
3. This eliminates the current `reload_boss_tiles.py --check` drift, requires **no C or tooling code changes**, and gives `!` and `?` a crisp `#ffffff` white background in-game.
