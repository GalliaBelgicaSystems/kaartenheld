# Dialogue Boxes — Layout and Constraints

This document explains how the dialogue system's size limits work today,
why they exist, and records the design decision to raise the dialogue-box
height in a later stage.

## 1. How it works today

All dialogue strings are staged into fixed-size WRAM buffers at start time.
The Game Boy has no heap, so every buffer is compile-time sized somewhere —
the dialogue system sizes them here (`src/core/dialogue.h`):

| Constant / buffer | Value | Purpose |
|---|---|---|
| `MAX_DIALOGUE_LINES` | 8 | Hard cap on lines per dialogue |
| line staging | 20 chars + NUL | `g_dlg_lines[8][21]` in WRAM |
| speaker staging | 11 chars + NUL | `g_dlg_speaker[12]` in WRAM |

Flow when a dialogue starts (`dialogue_start_def`):

1. The fixed-bank dispatcher stages `DialogueState*` + `DialogueId` and
   runs the bank-2 body (`src/core/dialogue_banked.c`, AGENTS.md 52.11.1).
2. The banked body scans the registered content table directly (same ROM
   bank) and copies speaker + lines **byte-wise into the WRAM buffers** —
   fixed code can never read bank-2 rodata after returning.
3. `DialogueState.lines[i]` pointers target those WRAM buffers, never
   bank-2 addresses.
4. Display: two lines at a time in a 4-row box at screen rows 14-17 —
   only horizontal bars (no side borders): top carries the speaker
   (`+--SPEAKER-----+` on row 14), the current pair on rows 15-16 as
   full-width text (**20 visible characters**, 19 beside the chevron,
   via `ui_draw_dialogue_line`, `src/screens/dialogue_screen.c`).
   Pressing A turns to the next pair (`dialogue_next` steps
   `current_line` by 2, so a 2-line greeting reads in one page and an
   8-line lesson in four).  The far bottom right (col 19, row 16) shows
   a down-chevron — the intrepid `>` glyph rotated 90 degrees clockwise
   (VRAM 109, `UI_TILE_DIALOGUE_CONTINUE`, `v` in the semantic buffer) —
   whenever another page follows; the last page shows none.  Full width
   gives two extra characters per row, so the chevron never eats text.

## 2. Why the limits exist

- **No heap**: an N-line dialogue needs N fixed buffers somewhere; the cap
  is where "somewhere" is.
- **WRAM budget**: the staging block costs ~168 bytes today.  Every raise
  spends real WRAM (see §4).
- **UX**: an N-line dialogue costs N presses of 18-character fragments.
  Longer text does not become better text; it becomes more button-mashing.
  Current content has never needed more than 8 lines.

## 3. Decision: raise the height later

The battle-system signpost (`DIALOGUE_ID_SIGNPOST`, the "?" in the starting
area) ships as 8 dense telegraph-style lines describing trios, poker tiers,
the suited bonus, the 2-energy pool and poison ticks.  A richer explanation
in fuller sentences wants more than 8 lines.

**Recorded decision**: lift `MAX_DIALOGUE_LINES` beyond 8 (target range
12–16; exact number to be picked at implementation time against the WRAM
budget).  Deferred because current content fits comfortably and the memory
cost, while small, should be spent deliberately.

## 4. Implementation checklist (deferred work)

1. Bump `MAX_DIALOGUE_LINES`; clean rebuild (§52.2 — no header dependency
   tracking).
2. Confirm the WRAM delta with `make memmap`.
3. Re-run all `dialogue_*` scenarios plus `make verify-vram` /
   `make vram-dialogue` ground-truth checks.
4. Audit code that indexes lines or iterates to `MAX_DIALOGUE_LINES`
   (`dialogue_banked.c`, `dialogue_screen.c`) — no hard-coded 8s expected,
   but verify.
5. Update the authoring table in §1 with the new numbers.
