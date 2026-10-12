#include "combo.h"
#include "rpg/cards.h"
#include "banked.h"

/* Pack-range guards (see COMBO_PACK in combo.h): the staging byte carries
 * the phase in bit 0 and the effect id in bits 1..7.  Tested against the
 * COUNT sentinel so a future effect outgrows the byte loudly. */
static const int g_combo_pack_phase_ok[(COMBO_PHASE_DEFEND <= 1) ? 1 : 0];
static const int g_combo_pack_effect_ok[(CARD_EFFECT_COUNT <= 128) ? 1 : 0];

/* combo_resolve() lived here as a fixed-bank staging wrapper around the
 * banked body in src/battle/combo_content.c (ROM bank 3).  Its staging is
 * now inlined at its only call site (battle_play_hand in
 * src/battle/battle.c) to save one nesting level on the deepest battle
 * chain (AGENTS.md 52.24).  The pack guards below stay: they pin the
 * staging-byte format both sides share. */

