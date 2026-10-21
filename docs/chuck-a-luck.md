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

* Entry: talk to (or bump into) Nis, the gambler kobold. Town
  (`levels/town.json` `town_gambler`, east corridor) and the frozen
  harness fixture (`tools/scenarios/fixtures/levels/test_gamble.json`
  `test_gamble_gambler`, next to spawn) each carry one as an ordinary
  `INTERACTION_DIALOGUE` actor (`dialogue:
  DIALOGUE_ID_KOBOLD_INVITE`). Nis explains the rules in a YES/NO
  dialogue (the game-owned bank-8 choice nav handles the cursor); YES
  is handed to `SCREEN_GAMBLE` by a small fixed dispatch on the staged
  action code, NO advances plainly and the dialogue closes back to the
  overworld. No separate INTERACTION_GAMBLE engage path exists -- the
  dialogue owns the invitation.
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
  dialogue closed and walks away with gold untouched; B anywhere walks
  away too.

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
the script loudly.

## Fixed-bank budget (read before touching)

The fixed bank had ~50 B headroom when this feature landed. The fixed
side is exactly: the `screen.c` update dispatch case, the update stub
(dispatch only), and the dialogue YES handoff in
`dialogue_screen_update` (a staged-action-code branch, same pattern as
the heal-mage 3/6 codes). The direct `INTERACTION_GAMBLE` engage
branches were removed to pay for the handoff. There is deliberately NO
render function (the screen renders during update; `screen_render`
falls through to `default`), NO start/settle helpers, NO SFX, and NO
exit call -- the banked frame body even replicates `screen_change`'s
overworld-target effects with WRAM writes. Any new fixed byte must be
paid for by moving an old one to a bank (AGENTS.md 52.18 family).
`make memmap` is the gate; the debug build is the tight one.

## Observability contract

* Snapshot: gold deltas are visible in the extended snapshot currency
  section; the `screen` field reads `GAMBLE`; dice digits live in the
  semantic screen buffer (row 12 centers), OAM tiles 128-133 hold the
  kobold art (entries 1-6).
* Telemetry v1 gap (documented, not accidental): gold moves by direct
  WRAM writes and the exit replicates `screen_change`, so no
  `CURRENCY_SPENT/ADDED` or `SCREEN_CHANGED` events fire for gamble
  transitions. Scenarios assert gold values + screen rows instead.
  Follow-up: stage amounts/sfx in `g_bk_*` and emit from a fixed stub
  once the fixed budget allows (~30 B).
* RNG: the shared stream is primed from DIV by `game_frame_tick_banked`
  (AGENTS.md 52.27 -- without it the release ROM booted the xorshift
  at 0 and every roll was [1,1,1]). Harness scenarios seed explicitly,
  so the primer never fires there.
* Other v1 gaps: silent (no SFX), no CRAM re-check beyond entry (the
  entry programs the base set itself).

## Scenarios (all deterministic, seeded RNG)

`tools/scenarios/tests/gamble_*.json` (16): invite (rules + YES/NO
prompt), invite-decline (NO walks away), invite-accept (YES opens the
table), enter, lose, 1:1 / 2:1 / 3:1 wins (seeds 12345 / 21 / 579 pin
dice [2,6,3] / [1,4,1] / [1,1,1]), bet cycling, high-stakes win
(bet 10 -> +10), clamp-to-gold, exit, roundtrip (dice clear to '?' +
LEAVEE-tail regression), two-rolls (stream advance), OAM placement. The ×2/×3 seeds were found by replicating the ROM xorshift
(`rng_next_banked` step + 252-reject + subtraction fold) in Python and
are pinned by the assertions -- same determinism contract as combat.
The RNG stream is proven clean: no consumer sits between scenario start
and the roll (the seeded dice match the replica exactly).

## Review shots

`screenshots/review/gamble-table.png` (CHOOSE screen) and
`screenshots/review/gamble-roll.png` (result screen) via
`tools/capture_gamble_shot.py` (release ROM, real path: walk to the
town kobold, bump in, roll). Registered in `review/manifest.json`.
