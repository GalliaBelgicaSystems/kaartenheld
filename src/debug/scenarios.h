#ifndef SCENARIOS_H
#define SCENARIOS_H

#include <stdint.h>
#include "game.h"

extern volatile uint8_t g_scen_load;
extern volatile uint8_t g_scen_load_state;
extern volatile uint8_t g_debug_action[6];
extern volatile uint8_t g_debug_action_pending;

void scenario_check_and_load(void);

/* Debug-action ids: host wire protocol (tools/emulator.py
 * session.DBG_ACT_* writes g_debug_action[0]).  Never renumber without
 * updating the host map. */
enum {
    DBG_ACT_ADD_ITEM = 1,
    DBG_ACT_REMOVE_ITEM = 2,
    DBG_ACT_ADD_CURRENCY = 3,
    DBG_ACT_ADD_PROGRESS = 4,
    DBG_ACT_BUY_ITEM = 5,
    DBG_ACT_USE_ITEM = 6,
    DBG_ACT_EQUIP_ITEM = 7,
    DBG_ACT_SAVE = 8,
    DBG_ACT_LOAD = 9,
    DBG_ACT_SET_HAND_CARD = 10,
    DBG_ACT_SET_FILTER = 11,
    DBG_ACT_SET_SORT = 12,
    DBG_ACT_DECK_ADD = 13,
    DBG_ACT_SET_HAND_CARD_META = 14,
    DBG_ACT_START_BATTLE = 15,
    DBG_ACT_DECK_REMOVE = 16,
    DBG_ACT_SET_HAND_CARD_STATUS = 17,
    DBG_ACT_SET_ENEMY_HP = 18,
    DBG_ACT_APPLY_STATUS = 19,
    DBG_ACT_SET_HAND_RING = 20
};

#endif /* SCENARIOS_H */
