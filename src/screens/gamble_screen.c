#include "game.h"
#include "screen.h"
#include "banked.h"
#include "game/gamble.h"

/* Chuck-a-luck screen (friendly kobold dice game).  Fixed-bank minimal
 * stub (the fixed bank has ~50 B headroom): a single dispatch to the
 * bank-8 frame body, which owns entry, paint, input, dice, OAM, gold
 * and the replicated overworld exit (see gamble.h). */

void gamble_screen_update(Game *g)
{
    (void)g;
    g_bk_call_bank = 8;
    g_bk_call_target = (uint16_t)&gamble_frame_banked;
    banked_call_run();
}
