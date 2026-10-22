# Memory Budget

Reproducible numbers from `make memmap` (parses the debug linker map and fails
on a `_HOME` >= 0x8000 violation).

## Current budget (2026-10-22, `make memmap` on the debug map)

```text
Game Boy ROM memory budget
--------------------------
_CODE (fixed bank code/data) :  32078 B  @ 0x0200-0x7F4E
_HOME (non-bankable)          :    125 B  @ 0x7F4E-0x7FD2  headroom to 0x8000: 46 B  [OK]
  (includes 7 B _INITIALIZER table after _HOME)
_DATA (WRAM)                  :   4250 B  @ 0xC940-0xD9DA  headroom to 0xE000: 1574 B

Switchable ROM bank size checks (16 KB each)
_CODE_2 : 15603 B  headroom:  781 B  [OK]
_CODE_3 : 16157 B  headroom:  227 B  [OK]
_CODE_4 : 14808 B  headroom: 1576 B  [OK]
_CODE_5 : 12707 B  headroom: 3677 B  [OK]
_CODE_6 : 15352 B  headroom: 1032 B  [OK]
_CODE_7 : 14882 B  headroom: 1502 B  [OK]
_CODE_8 : 12390 B  headroom: 3994 B  [OK]
_CODE_9 :  7866 B  headroom: 8518 B  [OK]
```

> Fixed bank 0 is essentially full (46 B to `0x8000`): any fixed-bank
> addition risks the silent bank-1→2 overflow (§52.18 — the Makefile
> link rules fail loudly on it). The remaining fixed `_CODE` is almost
> entirely code — the content-table migration waves already moved scenes
> (bank 5), actors (bank 4), dialogue/events/shops/quests/cards (bank 2),
> title (bank 4), terrain overflow (banks 6/7) out. No mult/div/mod
> library routines link into fixed (only `___memcpy`); flag math is
> shift/mask. The next substantial feature must plan banked placement
> (§52.11.1) up front — bank 3 is now the tightest (227 B); bank 8
> (3994 B), bank 9 (8518 B), bank 5 (3677 B) and bank 4 (1576 B) have the
> most room.

## Where the bytes go

### ROM (256 KB MBC5, 16 banks)

* **`_CODE`** (~32 KB): game + engine code.  The `static const` content
  tables have all moved to banked ROM (scenes → bank 5, actors → bank 4,
  dialogue/events/shops/quests/cards → bank 2, title → bank 4, terrain
  overflow → banks 6/7).  `_CODE` grows with every feature.
* **`_HOME`** (125 B): boot-critical code that must stay below `0x8000` —
  `_joypad`, `_set_bkg_data`/`_set_tile_data` and other non-bankable
  library routines.  This is the tightest budget:
  only 46 B of headroom.  Keep it there; do not grow it casually.
* Banked ROM (banks 1-9): scenes, actors, dialogue, events, shops,
  quests, cards, title, terrain overflow, the hUGE driver + songs
  (banks 6/7/9), SFX tables, and ~30 banked logic bodies live
  here (see `make memmap` banked-target validation).  Free room:
  bank 2 ~781 B, bank 3 ~227 B, bank 4 ~1576 B, bank 5 ~3677 B,
  bank 6 ~1032 B, bank 7 ~1502 B, bank 8 ~3994 B, bank 9 ~8518 B.

### WRAM (8 KB, `0xC000-0xDFFF`)

* `_DATA` (4250 B @ `0xC940-0xD9DA`): GBDK globals + the fixed-size
  runtime structs + the WRAM-resident timer ISR (`0xC900`) and banked-call
  trampolines.  Only ~1574 B headroom to `0xE000` (stack base) — keep new
  WRAM statics tiny (an 800 B staging static once flipped hostile
  spawning under the harness, §52.19 family).  The big fixed consumers
  (approximate):
  * `GameState` ~ 200 B (party 13, inventory 33, variables 32, world 49,
    progression 49, flags 8, currency 8, scene 4, equipment 1)
  * `World` ~ 300 B (20x12 tile map = 240 B + player + 4 actor slots)
  * `Game` aggregates the above plus `Battle`, `DialogueState`,
    `RenderCache`, telemetry buffers.
* The remaining WRAM (`0xD9DA-0xE000`, ~1574 B) is the stack headroom —
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
