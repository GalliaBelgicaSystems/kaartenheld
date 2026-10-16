#ifndef QUEST_H
#define QUEST_H

#include <stdint.h>
#include <stdbool.h>
#include "rpg/state.h"

/* Quest content types.  Quests are data: the registry lives in
 * src/game/quests_content.c (bank 5, shared with the bank-5 quest-tab
 * renderer in src/screens/item_quest_banked.c); quest state itself is
 * mutated by the generic event system (quests are events, not engine
 * code).  A quest tracks its progress in ONE status variable with
 * thresholds declared per quest. */

/* Quest count (length of g_quests[] in quests_content.c).  Kept in sync by
 * the compile-time assert next to the table. */

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

#define GAME_QUEST_COUNT 2

#endif /* QUEST_H */
