#include "event.h"
#include "game_ids.h"

/* ── Event table registration (game layer) ─────────────────────────
 * The event content table itself lives in a banked ROM region
 * (events_content.c, `#pragma bank GAME_EVENT_BANK`).  This file stays
 * in the fixed bank and hands the table + count + bank to the engine; the
 * engine copies each row into WRAM scratch before matching, so the banked
 * layout is invisible everywhere else.  GAME_EVENT_COUNT is asserted
 * against the table in events_content.c. */

extern const EventDefinition g_events[];

void game_events_register(void)
{
    event_init(g_events, GAME_EVENT_COUNT, GAME_EVENT_BANK);
}
