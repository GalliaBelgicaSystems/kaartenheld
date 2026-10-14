#ifndef QUEST_H
#define QUEST_H

#include <stdint.h>
#include <stdbool.h>
#include "rpg/state.h"

/* Generic quest engine.  The engine owns the quest vocabulary and the
 * status derivation; the game layer supplies its quest table at boot via
 * quest_init() (see src/game/quests.c).  A quest tracks its progress in ONE
 * status variable with thresholds declared per quest; the state itself is
 * mutated by the generic event system (quests are events, not engine code). */

typedef enum {
    QUEST_STATUS_NOT_STARTED = 0,
    QUEST_STATUS_ACTIVE = 1,
    QUEST_STATUS_COMPLETE = 2
} QuestStatus;

typedef struct {
    uint8_t id;                 /* stable quest id */
    char name[14];              /* display label, e.g. "MONSTER HUNT" */
    VariableId status_variable; /* quest status variable (see QuestStatus) */
    int16_t status_active;      /* var == this -> ACTIVE */
    int16_t status_complete;    /* var == this -> COMPLETE */
    VariableId progress_variable;  /* 0 = none; counter shown when ACTIVE */
    int16_t progress_target;
    char progress_label[10];    /* e.g. "monsters" -> "monsters: X/3" */
    char complete_note[10];     /* reward shown as "complete - <note>" */
} QuestDefinition;

/* Register the game's quest table.  The engine is generic; the game layer
 * supplies its content table at boot. */
void quest_init(const QuestDefinition *table, uint8_t count, uint8_t bank);

/* Iterate the registered quest table. */
uint8_t quest_count(void);

#endif /* QUEST_H */
