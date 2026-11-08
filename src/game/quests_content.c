#pragma bank 8

#include "quest.h"
#include "game_ids.h"
#include <stddef.h>

/* ── Quest registry (game content, banked ROM) ─────────────────────
 * Progress counters render as a single digit ('0' + value in
 * item_quest_banked.c): keep every target ≤ 9.  The #error below fails
 * the build otherwise -- extend it if the registry grows. */
#define QUEST_MONSTERS_TARGET 3
#define QUEST_SHOPKEYS_TARGET   0  /* inert: SHOP KEYS has no counter */

const QuestDefinition g_quests[] = {
    {
        1, "MONSTER HUNT", "MAYOR IN TOWN", "KILL 3 MONSTERS",
        VARIABLE_ID_QUEST_MONSTER_HUNT, 1, 2,
        VARIABLE_ID_MONSTERS_DEFEATED, QUEST_MONSTERS_TARGET, "monsters", "+1 AP", "+1 AP"
    },
    {
        2, "SHOP KEYS", "MERCHANT IN TOWN", "FIND THE SHOP KEYS",
        VARIABLE_ID_MERCHANT_QUEST, 1, 2,
        0, QUEST_SHOPKEYS_TARGET, "", "MERCHANT", "SHOP OPENED"
    },
    {
        3, "DOG'S KEY", "DOG IN VILLAGE", "TRADE BONE FOR KEY",
        VARIABLE_ID_DOG_QUEST, 1, 2,
        0, 0, "", "OLD KEY", "OLD KEY"
    }
};

/* Keep the count in quest.h in sync with this table. */
typedef char quest_table_count_ok[
    sizeof(g_quests) / sizeof(g_quests[0]) == GAME_QUEST_COUNT ? 1 : -1
];

/* Progress counters render as a single digit ('0' + value in
 * item_quest_banked.c).  NOTE: the typedef-negative-size idiom does NOT
 * fire under this SDCC (it silently accepts char[-1]), so the bound
 * uses #error, which the preprocessor enforces.  Extend the #if if the
 * registry grows. */
#if QUEST_MONSTERS_TARGET > 9 || QUEST_SHOPKEYS_TARGET > 9
#error "quest progress_target exceeds the single-digit renderer"
#endif
