#include "rpg/progression.h"
#include "telemetry.h"
#include <stddef.h>

/* ── Static progression definitions (rules), separate from state ─────
 * Fixed lookup tables (Game Boy friendly).  Threshold entries are the
 * progress needed to advance FROM level N to level N+1. */
static const uint16_t g_hero_thresholds[] = {100, 250, 450, 700, 1000, 1400, 1900, 2500, 3200, 4000};

static const ProgressionDefinition g_progression_defs[] = {
    { 11, g_hero_thresholds, 10 }
};

const ProgressionDefinition *progression_get_def(uint8_t target_type)
{
    if (target_type >= 1 && target_type <= 1) {
        return &g_progression_defs[target_type - 1];
    }
    return NULL;
}

ProgressionState *progression_get(GameState *state, uint8_t target_type, uint16_t target_id)
{
    uint8_t i;
    if (!state || target_type == PROG_TYPE_NONE) return NULL;
    for (i = 0; i < state->progression.count; i++) {
        if (state->progression.entries[i].target.type == target_type &&
            state->progression.entries[i].target.id == target_id) {
            return &state->progression.entries[i].state;
        }
    }
    return NULL;
}

static ProgressionState *prog_get_or_add(GameState *state, uint8_t target_type, uint16_t target_id)
{
    ProgressionState *ps;
    if (!state || target_type == PROG_TYPE_NONE) return NULL;
    ps = progression_get(state, target_type, target_id);
    if (!ps) {
        if (state->progression.count >= MAX_PROGRESSION_TARGETS) return NULL;
        state->progression.entries[state->progression.count].target.type = target_type;
        state->progression.entries[state->progression.count].target.id = target_id;
        ps = &state->progression.entries[state->progression.count++].state;
        ps->level = 1;
        ps->progress = 0;
    }
    return ps;
}

bool progression_add(GameState *state, uint8_t target_type, uint16_t target_id,
                     uint16_t amount, ProgressionAddResult *out_result)
{
    const ProgressionDefinition *def;
    ProgressionState *ps;
    uint8_t i, level_before;

    if (out_result) {
        out_result->level_before = 0;
        out_result->level_after = 0;
        out_result->crossed = false;
    }
    def = progression_get_def(target_type);
    if (!def) return false;

    ps = prog_get_or_add(state, target_type, target_id);
    if (!ps) return false;

    level_before = ps->level;
    ps->progress = (uint16_t)(ps->progress + amount);
    telemetry_emit(EVENT_PROGRESSION_GAINED, target_type,
                   (uint8_t)target_id, (uint8_t)amount, ps->level);

    for (i = ps->level; i < def->max_level && i <= def->threshold_count; i++) {
        uint16_t th = def->thresholds[i - 1];
        if (ps->progress < th) break;
        ps->progress = (uint16_t)(ps->progress - th);
        ps->level++;
        telemetry_emit(EVENT_LEVEL_UP, target_type, (uint8_t)target_id, ps->level, 0);
    }

    if (out_result) {
        out_result->level_before = level_before;
        out_result->level_after = ps->level;
        out_result->crossed = (ps->level != level_before);
    }
    return (ps->level != level_before);
}
