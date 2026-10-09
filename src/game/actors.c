#include "actor.h"
#include "game_ids.h"

/* ── Actor registration (game layer) ───────────────────────────────
 * The actor content tables live in bank GAME_ACTOR_BANK (actors_content.c,
 * bank 4).  This file stays in the fixed bank and registers the table
 * pointer + bank with the engine.  The real table COUNT is emitted by
 * compile.py (g_actor_table_count) and read by the bank-4 loader body
 * directly (same bank), so the fixed bank carries no count logic and a
 * stale object here can never desync it (AGENTS.md 52.2/52.19). */

extern const WorldActorTable g_actor_tables[];
#define GAME_ACTOR_TABLE_COUNT 6   /* legacy hint; loader uses g_actor_table_count */

void game_actors_register(void)
{
    actor_register_tables(g_actor_tables, GAME_ACTOR_TABLE_COUNT, GAME_ACTOR_BANK);
}
