# Memory Budget

Reproducible numbers from `make memmap` (parses the debug linker map and fails
on a `_HOME` >= 0x8000 violation).

## Current budget (2026-09-27, `make memmap` on the debug map)

```text
Game Boy ROM memory budget
--------------------------
_CODE (fixed bank code/data) :  32047 B  @ 0x0200-0x7F2F
_HOME (non-bankable)          :    125 B  @ 0x7F2F-0x7FF1  headroom to 0x8000: 15 B  [OK]
  (includes 69 B _INITIALIZER table after _HOME)
_DATA (WRAM)                  :   5613 B  @ 0xC940-0xDF2D  headroom to 0xE000: 211 B

Switchable ROM bank size checks (16 KB each)
_CODE_2 : 16080 B  headroom:  304 B  [OK]
_CODE_3 : 15654 B  headroom:  730 B  [OK]
_CODE_4 : 16213 B  headroom:  171 B  [OK]
_CODE_5 : 15888 B  headroom:  496 B  [OK]
_CODE_6 : 15352 B  headroom: 1032 B  [OK]
_CODE_7 : 15534 B  headroom:  850 B  [OK]
```

> Fixed bank 0 is essentially full (15 B to `0x8000`): any fixed-bank
> addition risks the silent bank-1→2 overflow (§52.18 — the Makefile
> link rules fail loudly on it). The remaining fixed `_CODE` is almost
> entirely code — the content-table migration waves already moved scenes
> (bank 5), actors (bank 4), dialogue/events/shops/quests/cards (bank 2),
> title (bank 4), terrain overflow (banks 6/7) out. No mult/div/mod
> library routines link into fixed (only `___memcpy`); flag math is
> shift/mask. The next substantial feature must plan banked placement
> (§52.11.1) up front — bank 6 (1032 B) and bank 7 (850 B) have the most
> room; bank 4 (171 B) fits almost nothing more.

## Where the bytes go

### ROM (128 KB MBC5, 8 banks)

* **`_CODE`** (~32 KB): game + engine code.  The `static const` content
  tables have all moved to banked ROM (scenes → bank 5, actors → bank 4,
  dialogue/events/shops/quests/cards → bank 2, title → bank 4, terrain
  overflow → banks 6/7).  `_CODE` grows with every feature.
* **`_HOME`** (125 B): boot-critical code that must stay below `0x8000` —
  `_joypad`, `_set_bkg_data`/`_set_tile_data` and other non-bankable
  library routines.  This is the tightest budget:
  only 15 B of headroom.  Keep it there; do not grow it casually.
* Banked ROM (banks 1-7): scenes, actors, dialogue, events, shops,
  quests, cards, title, terrain overflow, the hUGE driver + songs
  (bank 6 + bank-7 copy), SFX tables, and ~30 banked logic bodies live
  here (see `make memmap` banked-target validation).  Free room:
  bank 2 ~304 B, bank 3 ~730 B, bank 4 ~171 B, bank 5 ~496 B,
  bank 6 ~1032 B, bank 7 ~850 B.

### WRAM (8 KB, `0xC000-0xDFFF`)

* `_DATA` (5613 B @ `0xC940-0xDFFF`): GBDK globals + the fixed-size
  runtime structs + the WRAM-resident timer ISR (`0xC900`) and banked-call
  trampolines.  Only ~211 B headroom to `0xE000` (stack base) — keep new
  WRAM statics tiny (an 800 B staging static once flipped hostile
  spawning under the harness, §52.19 family).  The big fixed consumers
  (approximate):
  * `GameState` ~ 200 B (party 13, inventory 33, variables 32, world 49,
    progression 49, flags 8, currency 8, scene 4, equipment 1)
  * `World` ~ 300 B (20x12 tile map = 240 B + player + 4 actor slots)
  * `Game` aggregates the above plus `Battle`, `DialogueState`,
    `RenderCache`, telemetry buffers.
* The remaining WRAM (`0xDF2D-0xE000`, ~211 B) is the stack headroom —
  see AGENTS.md §52.14 (harness `SP = 0xFFFE` leaves ~254 B before I/O
  mirrors; no stack locals > ~64 B in harness-exercised code).

### HRAM / VRAM / stack

* HRAM: only the timer ISR uses it transiently.
* VRAM: currently holds the console font (ASCII prototype).  The graphics
  pipeline (see `docs/graphics.md`) will consume the 8 KB BG tilemap +
  tilesets + OAM (40 sprites) budgets.
* Stack: GBDK default; fits comfortably in the free WRAM.

## Rules

* Run `make memmap` after any substantial feature; it exits non-zero on a
  `_HOME` violation.
* Keep `_CODE` (bank 0) as small as practical — prefer banked content when it
  grows.
* Every substantial feature should note its memory impact in
  `docs/roadmap.md`.

## Largest consumers to watch

1. Fixed bank 0 headroom (15 B) — the binding constraint on every change.
2. Bank-4 fixture/content budget (171 B) — new test fixtures must fit here.
3. `GameState` — the save unit; its size defines the SRAM save size.
