#ifndef RPG_PROGRESSION_H
#define RPG_PROGRESSION_H

#include <stdint.h>
#include <stdbool.h>
#include "rpg/state.h"

/* Generic progression mechanic.  A progression target is anything in the
 * game that can progress (hero, weapon, card, companion, ...).  The engine
 * does NOT know what a target type means; it only maps a target to a static
 * ProgressionDefinition (max level + thresholds) and mutates its
 * ProgressionState.  Consequences of a level-up are handled by the caller
 * (the game-specific layer), not by this engine. */

typedef struct {
    uint8_t max_level;
    const uint16_t *thresholds;  /* thresholds[level-1] = progress needed to reach level+1 */
    uint8_t threshold_count;
} ProgressionDefinition;

typedef struct {
    uint8_t level_before;
    uint8_t level_after;
    bool crossed;                /* a level was gained */
} ProgressionAddResult;

const ProgressionDefinition *progression_get_def(uint8_t target_type);

ProgressionState *progression_get(GameState *state, uint8_t target_type, uint16_t target_id);

bool progression_add(GameState *state, uint8_t target_type, uint16_t target_id,
                     uint16_t amount, ProgressionAddResult *out_result);

#endif /* RPG_PROGRESSION_H */
