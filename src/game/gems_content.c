#pragma bank 8

#include "gems.h"
#include "game_ids.h"

/* ── Gem registry (game content, banked ROM) ─────────────────────
 * Only gem 0 is winnable for now (chuck-a-luck net +20 in one sitting,
 * docs/chuck-a-luck.md); the OWNED/EQUIPPED bitmasks and
 * MAX_EQUIPPED_GEMS already make room for more.  Name/skill widths are
 * fixed for the GEM-tab row layout (name at col 2, ON/OFF at col 15,
 * skill on the line below). */

const GemDefinition g_gems[] = {
    { GEM_STRAIGHT, "STRAIGHT GEM", "SHORT STRAIGHT" },
};

/* Keep the registry length in sync with GEM_COUNT (game_ids.h).  NOTE: a
 * typedef negative-size check does NOT fire under this SDCC (it silently
 * accepts char[-1]), so this is documentation only -- the GEM tab
 * iterates GEM_COUNT and would read past a short table. */
typedef char gem_table_count_ok[
    sizeof(g_gems) / sizeof(g_gems[0]) == GAME_GEM_COUNT ? 1 : -1
];
