# Fixed-bank move-out procedure

The debug fixed bank (`_CODE`/`_HOME`, everything linked below CPU
`0x8000`) is the project's hardest budget. Status (Nov 2026,
`make memmap`):

* debug `_HOME` headroom: **2 B** (every debug build links within
  bytes of the ceiling -- see AGENTS.md §52.18);
* release `_HOME` headroom: **~3.9 KB** (release-only pressure is low;
  the debug build is the gate);
* companion pressure: bank 3 thin in both builds, bank 5 has room,
  bank 11 nearly full in debug (133 B) -- check `make memmap` before
  placing a new banked body.

## When a move-out is needed

Any new fixed-bank code (a branch, a call site, a string literal)
that overflows the debug link (`Possible overflow from Bank 1 into
Bank 2`, guest spin per §52.18) must move behind the banked-call
trampoline instead of being trimmed blindly. Golfing buys bytes once;
structure buys them permanently.

## Recipe (AGENTS.md §52.11.1)

1. Move the logic to a no-arg `xxx_banked(void)` body in a file with
   `#pragma bank N` (prefer bank 5; verify headroom first). The body
   reads inputs only from the `g_bk_*` staging globals (plus its own
   bank-local const data) and writes outputs only through a staged
   pointer or staging bytes. It must be pure: WRAM + regs + same-bank
   calls only -- never a fixed-bank call, never `di`/`ei`
   (§52.11.3), never unconditional bulk LCD-on writes (use
   `BANKED_VRAM_WRITE`).
2. Keep a thin fixed-bank wrapper with the original signature: stage
   bank + target + args into `g_bk_call_*`, then `banked_call_run()`.
   A wrapper costs ~10 B; the moved body typically saves 3-10x that.
3. A new per-frame fixed helper must live in its own tiny static
   frame with one call site: inlining it into `game_update`'s huge
   frame cost ~2x via register spillover (`game_consume_sfx` is the
   template -- the inlined form alone overflowed the bank).
4. Re-run `make memmap` (fails on violation), `make lint`
   (no `di`/`ei` in `#pragma bank` files), the three sentinels
   (§52.19), and the full harness -- WRAM layout shifts flip latent
   SDCC miscompiles (§52.19) and stack-budget margins (§52.24).

## Constraints (do not work around)

* The SFX mailbox (`g_bk_sfx_req`, §52.28) must be drained in normal
  user context (`game_update`), never in the 256 Hz timer ISR: the
  drain calls `audio_play_sfx`, which does MBC5 switches and (in
  debug) a banked copy -- both illegal from inside the ISR, same
  family as §52.11.3. Saving the 3 B call this way would corrupt
  the interrupted bank.
* New WRAM staging bytes cost one byte of stack-budget headroom
  each and shift every symbol above them: after adding one, the
  sentinels + `make stack-budget` + full harness are mandatory,
  not optional.
* Keep the engine/game split (§55.1): moved bodies stay generic,
  game content stays in `src/game/`.
