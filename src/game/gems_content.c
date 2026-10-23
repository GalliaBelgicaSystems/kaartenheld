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

/* Registry length is GAME_GEM_COUNT == GEM_COUNT by construction (see
 * gems.h).  GEM rows are 2 tall from row 3 and the message sits at row
 * 16, so more than 7 gems would collide with it -- fail loudly instead
 * of painting over the hints. */
#if GEM_COUNT > 7
#error "gem registry outgrew the GEM-tab layout"
#endif
