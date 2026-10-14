#include "quest.h"
#include <stddef.h>

/* ── Quest engine ──────────────────────────────────────────────────
 * The quest table is game content, registered at boot via quest_init().
 * The engine only counts the table; status derivation lives in the bank-2
 * item renderer (ic_quest_status), which reads the table directly. */
static const QuestDefinition *g_quests = NULL;
static uint8_t g_quest_count = 0;

void quest_init(const QuestDefinition *table, uint8_t count, uint8_t bank)
{
    (void)bank;
    g_quests = table;
    g_quest_count = count;
}

uint8_t quest_count(void)
{
    return g_quest_count;
}
