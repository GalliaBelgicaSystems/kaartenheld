#ifndef GAME_GEMS_H
#define GAME_GEMS_H

#include <stdint.h>
#include "game_ids.h"

/* Gem registry (game content).  Gems are passive skills: owning a gem
 * does nothing; equipping it (manual, A on the GEM tab) unlocks its
 * skill.  Owned/equipped state lives in the generic variables
 * VARIABLE_ID_GEM_OWNED / VARIABLE_ID_GEM_EQUIPPED (bit i = gem bit i),
 * so snapshots, descriptors, saves and the roundtrip cover gems with no
 * transport churn.  The table lives in src/game/gems_content.c (ROM
 * bank 8, shared with the bank-8 GEM-tab renderer in
 * src/screens/item_gem_banked.c); a banked body must share its tables'
 * bank (AGENTS.md 52.11.1). */

typedef struct {
    uint8_t bit;        /* GEM_* bit in the OWNED/EQUIPPED masks */
    char name[13];      /* display label, e.g. "STRAIGHT GEM" */
    char skill[16];     /* what it unlocks, e.g. "SHORT STRAIGHT" */
} GemDefinition;

#define GAME_GEM_COUNT 1

extern const GemDefinition g_gems[];

#endif /* GAME_GEMS_H */
