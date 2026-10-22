#include "rpg/loot.h"

/* Fixed-bank home for the loot module's shared WRAM state only.
 *
 * Everything else about drops lives in bank 3
 * (src/game/loot_drop_banked.c -- gate/profile/roll) or behind the
 * synth trampoline dispatched by card_get_def() (src/rpg/loot_banked.c
 * -- identity tables).  The fixed bank sits hard against the 0x8000
 * boundary (AGENTS.md 55.5): overflowing it corrupts bank 3 and hangs
 * the guest, so the drop path deliberately costs almost no fixed code.
 */

/* Derived-id result byte: written by the bank-3 drop/synth bodies,
 * read by the fixed-bank wrappers. */
uint8_t g_loot_id;

/* Isolated loot RNG state.  Lives in fixed-bank _DATA (WRAM, always
 * mapped -- but NOTE the custom CRT0 clears WRAM and never copies
 * _INITIALIZER, and the harness skips CRT0 entirely, so the = 0x1234
 * initializer never takes effect at boot: both streams prime lazily
 * from the divider in game_frame_tick_banked (AGENTS.md 52.x).  Both
 * bank-3 bodies advance one deterministic stream inline, exactly like
 * g_rng_state (src/debug/rng.h).  Harness scenarios reseed from the
 * main seed via rng_set_seed() (src/debug/rng.c); victory drops must
 * NOT consume the shared game RNG (docs/loot.md §34.5). */
uint16_t g_loot_rng_state;  /* BSS zero; primed with the main stream
                             * (AGENTS.md 52.27: no non-zero initializers) */
