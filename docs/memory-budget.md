# Memory Budget

Reproducible numbers from `make memmap` (parses both the debug and release
linker maps and fails on a `_HOME` >= 0x8000 violation).

## Current budget (2026-10-22, `make memmap` on both maps)

`make memmap` runs both link maps.  The debug build is much larger because
it carries the harness/scenario tables; the release build is what ships.

### Debug

```text
Game Boy ROM memory budget
Map: kaartenheld_debug.map
--------------------------
_CODE (fixed bank code/data) :  32090 B  @ 0x0200-0x7F5A
_HOME (non-bankable)          :    125 B  @ 0x7F5A-0x7FDE  headroom to 0x8000: 34 B  [OK]
_DATA (WRAM)                  :   4250 B  @ 0xC940-0xD9DA  headroom to 0xE000: 1574 B

Switchable ROM bank size checks (16 KB each)
_CODE_2 : 15763 B  headroom:  621 B  [OK]
_CODE_3 : 15802 B  headroom:  582 B  [OK]
_CODE_4 : 14808 B  headroom: 1576 B  [OK]
_CODE_5 : 12858 B  headroom: 3526 B  [OK]
_CODE_6 : 15352 B  headroom: 1032 B  [OK]
_CODE_7 : 14882 B  headroom: 1502 B  [OK]
_CODE_8 : 12390 B  headroom: 3994 B  [OK]
_CODE_9 :  8218 B  headroom: 8166 B  [OK]
```

### Release

```text
Game Boy ROM memory budget
Map: kaartenheld.map
--------------------------
_CODE (fixed bank code/data) :  27245 B  @ 0x0200-0x6C6D
_HOME (non-bankable)          :    125 B  @ 0x6C6D-0x6CED  headroom to 0x8000: 4883 B  [OK]
_DATA (WRAM)                  :   3310 B  @ 0xC940-0xD62E  headroom to 0xE000: 2514 B

Switchable ROM bank size checks (16 KB each)
_CODE_2 : 15806 B  headroom:  578 B  [OK]
_CODE_3 : 15948 B  headroom:  436 B  [OK]
_CODE_4 : 16171 B  headroom:  213 B  [OK]
_CODE_5 : 14564 B  headroom: 1820 B  [OK]
_CODE_6 : 16110 B  headroom:  274 B  [OK]
_CODE_7 : 15745 B  headroom:  639 B  [OK]
_CODE_8 : 12124 B  headroom: 4260 B  [OK]
_CODE_9 :  5700 B  headroom: 10684 B  [OK]
```

> The **debug** fixed bank is the tight one (34 B to `0x8000`; the castle
> 36-slot mapping costs 12 B net via a 16-byte slot table); release has
> 4,883 B.  Any fixed-bank addition still risks the silent bank-1→2
> overflow (§52.18 — the Makefile link rules fail loudly on it).
> In the **release** map, bank 4 is now the tightest switchable bank
> (213 B), ahead of bank 6 (274 B) and bank 2 (578 B).  Moving the loot
> drop body to bank 9 restored bank 3 from 79 B to 436 B of release
> headroom (the shared `LOOT_POWER` closed-form macro saved another ~60 B
> there and ~320 B in debug bank 2 versus per-file compare chains).
> Plan new banked content against both maps, and prefer bank 8
> (4,260 B), bank 9 (10,684 B) or bank 5 (1,820 B).

## Where the bytes go

### ROM (256 KB MBC5, 16 banks)

* **`_CODE`** (~32 KB): game + engine code.  The `static const` content
  tables have all moved to banked ROM (scenes → bank 5, actors → bank 4,
  dialogue/events/shops/quests/cards → bank 2, title → bank 4, terrain
  overflow → banks 6/7).  `_CODE` grows with every feature.
* **`_HOME`** (125 B): boot-critical code that must stay below `0x8000` —
  `_joypad`, `_set_bkg_data`/`_set_tile_data` and other non-bankable
  library routines.  The debug map has only 34 B of headroom; release
  has 4,883 B.  Keep the debug build there; do not grow it casually.
* Banked ROM (banks 1-9): scenes, actors, dialogue, events, shops,
  quests, cards, title, terrain overflow, the hUGE driver + songs
  (banks 6/7/9), SFX tables, and ~30 banked logic bodies live
  here (see `make memmap` banked-target validation).  Release free
  room: bank 2 578 B, bank 3 436 B, bank 4 213 B, bank 5 1820 B,
  bank 6 274 B, bank 7 639 B, bank 8 4260 B, bank 9 10684 B.
  Release's tightest bank is bank 4 (213 B), then bank 6 (274 B),
  bank 3 (436 B) and bank 2 (578 B).

### WRAM (8 KB, `0xC000-0xDFFF`)

* `_DATA` (debug: 4250 B @ `0xC940-0xD9DA`; release: 3310 B): GBDK
  globals + the fixed-size runtime structs + the WRAM-resident timer ISR
  (`0xC900`) and banked-call trampolines.  Debug has ~1574 B headroom to
  `0xE000` (stack base) — keep new
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
