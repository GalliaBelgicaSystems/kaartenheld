#include "dialogue.h"
#include "game_ids.h"

/* ── Dialogue table registration (game layer) ──────────────────────
 * The dialogue content table itself lives in a banked ROM region
 * (dialogue_content.c, `#pragma bank 7`).  This file stays
 * in the fixed bank and hands the table + count + bank to the engine; the
 * engine copies the matching row + its text into WRAM staging in
 * dialogue_start_def(), so the banked layout is invisible everywhere else.
 * GAME_DIALOGUE_COUNT is asserted against the table in
 * dialogue_content.c. */

extern const DialogueDefinition g_dialogues[];

void game_dialogue_register(void)
{
    dialogue_register(g_dialogues, GAME_DIALOGUE_COUNT, GAME_CONTENT_BANK);
}
