# Chuck-a-Luck (friendly kobold dice game)

A friendly kobold in Oakhaven runs a chuck-a-luck table. The hero picks
a number 1-6 and a bet (1 / 5 / 10 gold); the kobold rolls three dice.
Each matching die pays its face stake back plus a fixed profit:

| matches | payout           | example (bet 10)      |
|---------|------------------|-----------------------|
| 0       | lose the bet     | -10                   |
| 1       | 1:1 (profit = bet)   | +10 (stake back + 10) |
| 2       | 2:1 (profit = 2x)    | +20                   |
| 3       | 3:1 (profit = 3x)    | +30                   |

The kobold's bank is infinite: payouts never run out, and the house
never refuses a covered bet.

## Where it lives

* Entry: talk to (or bump into) Nis, the gambler kobold. He stands in
  the Village Area (`levels/village_area.json` `village_area_gambler`,
  open floor east of the merchant) and in the frozen harness fixture
  (`tools/scenarios/fixtures/levels/test_gamble.json`
  `test_gamble_gambler`, next to spawn). Both are ordinary
  `INTERACTION_DIALOGUE` actors (`dialogue:
  DIALOGUE_ID_KOBOLD_INVITE`). Nis explains the rules in a YES/NO
  dialogue (the game-owned bank-8 choice nav handles the cursor). On
  YES the nav closes the dialogue and raises `g_gamble_enter`; the
  per-frame tick opens `SCREEN_GAMBLE` itself, so the fixed bank pays
  nothing for the handoff beyond recording the choice. NO advances
  plainly and the dialogue closes back to the overworld. No separate
  INTERACTION_GAMBLE engage path exists -- the dialogue owns the
  invitation.
* Screen: `SCREEN_GAMBLE` (14). It is deliberately NOT a battle mode:
  the engine stays generic (AGENTS.md 55.1) and the game owns the dice
  rules in `src/game/gamble_content.c` (ROM bank 8) behind the single
  banked frame body `gamble_frame_banked()`.
* Look: kobold combat art (OAM, entries 1-6, battle-art block base 128),
  title banner row 0, identity row 2, outcome row 5 (result
  announcement directly below the kobold), `CHOOSE BET` / `CHOOSE
  NUMBER` / `GOLD OWNED` rows 7-9 (replacing the battle HP/AP rows),
  match-detail row 10, three card-frame dice boxes (rows 11-14, cols
  2/8/14), controls row 16. The `>` caret marks the active choose row
  (UP/DOWN switches BET/NUMBER, LEFT/RIGHT changes the value: number
  wraps 1-6, bet cycles 1/5/10 clamped to owned gold), A rolls (SELECT
  also works), B leaves.
* Flow: bump Nis -> his rules dialogue (3 pages, YES/NO choice,
  default YES) -> CHOOSE on YES (A to confirm) -> RESULT (A for another
  round with fresh hidden dice, B to leave). NO (DOWN + A) advances the
  dialogue closed and walks away with gold untouched.
* Leaving the table (B) puts Nis's goodbye box on screen -- `Nis: Come
  back anytime!` (`screens/dialogue/kobold_farewell.json`) -- over the
  overworld; A closes it and returns control. The banked body cannot
  call `dialogue_start_def`, so it stages the id in the generic
  `g_bk_dlg_id` request and the fixed post-tick hook in `game_update()`
  starts the dialogue + `screen_change(SCREEN_DIALOGUE)`; that path
  keeps `DIALOGUE_STARTED`/`DIALOGUE_ENDED` telemetry honest. The hook
  was funded by deleting the fixed-bank `huge_music_stop()` twin (the
  bank-8 resolver already stops the previous song inline).

## Dice art (reusable battle chrome)

Digit tiles 0-9 (`assets/combat-tile.png` last column + row-0 1-4) are
sliced to `tools/level_editor/public/tiles/combat/combat_<n>_icon.png`,
registered in `tools/level_editor/tilesets/combat.json`, and composed
by `tools/compose_dice.py` into `assets/dice.png` (digit order 0-9).
`palette_compiler.py` encodes the sheet with the wood ramp; `png2gb`
emits `src/gfx/dice_tiles.h`. The bank-8 dice body streams the 10 tiles
to `UI_TILE_DICE_BASE` (246-255, high end of the battle-art block,
clear of the 128+ enemy-art growth) on gamble entry. Battle renderers
can reference the same VRAM ids later -- the digits are shared chrome,
not gamble-only.

The kobold art + base CRAM bundle (`src/gfx/gamble_art.h`) is generated
by `tools/gamble_art_compile.py` from the live battle sources every
build (blob bytes + fightgoblin OBJ ramp + 8 base BG slots), so the
artist can never silently desync the bank-8 copy: geometry changes fail
the script loudly.  (The fightgoblin ramp itself repeats its shade at
indices 2 and 3, so nearest-shade ties in the encoder all resolve to the
same colour -- the battle pipeline uses the identical ramp.)

## Fixed-bank budget (read before touching)

There is **no `screen.c` dispatch case** for `SCREEN_GAMBLE`: the whole
table (entry paint, input, dice, OAM, gold, both screen switches) runs in
the bank-8 per-frame tick body. What stays in the fixed bank is only:

* the dialogue YES branch in `dialogue_screen_update` (records the
  choice, ends the dialogue, returns -- the tick opens the table);
* the post-tick hook in `game_update()` that opens a staged dialogue id
  (`g_bk_dlg_id`, used for Nis's farewell) and the casino-music code;
* the one-shot SFX pull (the shared `byte_c == 2` slot).

Everything else is banked. The direct `INTERACTION_GAMBLE` /
`ENGAGE_GAMBLE` engage paths were removed, `gamble_frame_banked()` is a
plain same-bank call from the tick, and the fixed `huge_music_stop()`
twin was deleted (the bank-8 resolver already stops inline) to fund the
farewell hook. Any new fixed byte must be paid for by moving an old one
to a bank (AGENTS.md 52.18 family). `make memmap` is the gate; the debug
build is the tight one (~50 B).

## Observability contract

* Snapshot: gold deltas are visible in the extended snapshot currency
  section; the `screen` field reads `GAMBLE`; dice digits live in the
  semantic screen buffer (row 12 centers), OAM tiles 128-133 hold the
  kobold art (entries 1-6).
* Dialogue telemetry is complete (`DIALOGUE_STARTED` / `CHOICE` /
  `ENDED`): both the invite accept and the farewell run through the real
  `dialogue_start_def` / `dialogue_end` APIs from the fixed side.
* Telemetry v1 gap (documented, not accidental): gold moves by direct
  WRAM writes and the table's own screen switches are banked, so no
  `CURRENCY_SPENT/ADDED` or `SCREEN_CHANGED` events fire for them.
  Scenarios assert gold values + screen rows instead.
* RNG (AGENTS.md 52.27): the shared stream is primed from DIV by
  `game_frame_tick_banked` and re-mixed once on the first real input, so
  two sessions do not share a stream. The release-ROM cold-boot
  non-zero check lives in `verify-walkthrough` (no harness scenario can
  see it: they pre-seed, and the harness skips the re-mix).
* Known gap: no CRAM re-check beyond entry (the entry programs the base
  set itself; the dialogue render that follows the farewell reprograms
  the village set through `ui_draw_world_full`).

## Audio

* **Music**: `MUSIC_CASINO` (`assets/music/casino.uge`) plays while the
  table is up; the scene track is restored on exit. The track's data is
  ~3.3 KB and bank 7 has only ~640 B free in the release layout (~1.5 KB
  in debug), so the song lives in **bank 9** next to its own driver copy
  (`hUGEDriver_b9.o`, ~1.9 KB, renamed `_b9` exports): the hUGE driver
  reads song bytes through the mapped ROM
  window, so driver + song must share a bank. `huge_music.c` gained a
  third dispatch leg (bank 9) for init / dosound / mute; the two mute
  entry points share one `huge_mute_dispatch()` ladder to keep the
  fixed-bank cost down.
* **SFX**: `SFX_CURSOR` blips on every caret / bet / number move,
  `SFX_SELECT` on the roll, `SFX_BLOCK` (the defend thump) on a loss and
  `SFX_WIN` on a win.  Both result cues are SFX, so the casino theme
  keeps playing across a result: only the cue's audio channel is muted
  for its duration, then restored (the same mechanism as the loss
  thump).  `SFX_WIN` is the chest-opening arpeggio (`assets/music/chest
  opening.uge`) transcribed by the SFX pipeline (`screens/sfx.json` maps
  the `WIN` id to it; the transcriber needs only CH1, and the jingle is
  3 tone steps / 55 ticks).  Using the `MUSIC_CHEST` *track* here
  instead stops the table theme, plays the jingle and restarts the theme
  from the top -- which is why the win cue is an SFX.
  A result cue replaces the roll's `SFX_SELECT` in the same frame, so
  the roll blip is not heard on a scoring roll.
* **Staging (no fixed bytes added)**: a banked body cannot call the
  audio API (AGENTS.md 52.11.1), so requests ride WRAM out to
  `game_update()`: `g_gamble_sfx` (id + 1; 0 = none, BSS-safe because
  the custom CRT0 never copies `_INITIALIZER`) folds into the existing
  one-shot SFX slot (`g_bk_byte_c == 2`, id in `g_bk_byte_d` -- the
  same slot the pursuit alert uses), the casino theme is the one new
  code (`g_bk_byte_b == 4`), and leaving the table reuses the existing
  scene-track restore code (`g_bk_byte_b == 2`).
* No result starts a music track, so there is no jingle-restore override
  at the table: the theme runs from table entry until the player
  leaves.
* `huge_music_stop()` (the fixed-bank stop twin) was deleted: the bank-8
  resolver already calls `huge_stop_inline()` before every track
  resolution, including the NONE path, so the fixed copy was dead code.
  Its bytes fund the post-tick dialogue hook.

## Scenarios (all deterministic, seeded RNG)

`tools/scenarios/tests/gamble_*.json` (18): invite (rules + YES/NO
prompt), invite-decline (NO walks away), invite-accept (YES opens the
table), enter, lose, 1:1 / 2:1 / 3:1 wins (seeds 12345 / 21 / 579 pin
dice [2,6,3] / [1,4,1] / [1,1,1]), bet cycling, high-stakes win
(bet 10 -> +10), clamp-to-gold, rich-bet (260 gold: the bet keeps
cycling 1 -> 5 -> 10; regression for the old uint8 clamp), exit (Nis's
farewell box), farewell-close (box closes back to the overworld with the
scene track), roundtrip (dice clear to '?' + LEAVEE-tail regression),
two-rolls (stream advance), OAM placement. The ×2/×3 seeds were found by replicating the ROM xorshift
(`rng_next_banked` step + 252-reject + subtraction fold) in Python and
are pinned by the assertions -- same determinism contract as combat.
The RNG stream is proven clean: no consumer sits between scenario start
and the roll (the seeded dice match the replica exactly).

## Review shots

`screenshots/review/gamble-table.png` (CHOOSE screen),
`screenshots/review/gamble-roll.png` (result screen) and
`screenshots/review/gamble-farewell.png` (Nis's goodbye box) via
`tools/capture_gamble_shot.py` / `tools/capture_farewell_shot.py`
(release ROM, real path: walk to Nis in the Village Area, bump in, say
YES, roll / leave). Registered in `review/manifest.json`.
