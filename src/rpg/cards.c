#include "rpg/cards.h"
#include "rpg/loot.h"
#include "banked.h"
#include <stddef.h>

/* The card catalog is game content, registered at boot via
 * card_register_defs() (see src/game/cards.c). */
const CardDefinition *g_card_defs = NULL;
uint8_t g_card_defs_count = 0;
/* BSS: card_register_defs() (via game_content_init() at every boot) sets
 * the home bank before any def is read. */
static uint8_t g_card_bank;
CardDefinition g_card_scratch;

void card_register_defs(const CardDefinition *defs, uint8_t count,
                        uint8_t bank)
{
    g_card_defs = defs;
    g_card_defs_count = count;
    g_card_bank = bank;
}

const CardDefinition *card_get_def(CardId id)
{
    uint8_t i;
    if (!g_card_defs) return NULL;

    /* Loot-range ids synthesize from the material/effect/weapon tables
     * (docs/loot.md §34): bank-3 body fills the shared scratch. */
    if (loot_is_loot_id(id)) {
        g_bk_call_bank = 3;
        g_bk_call_target = (uint16_t)&loot_synth_banked;
        g_bk_byte_a = id;
        banked_call_run();
        return &g_card_scratch;
    }

    for (i = 0; i < g_card_defs_count; i++) {
        banked_copy(g_card_bank, &g_card_scratch, &g_card_defs[i],
                    sizeof(CardDefinition));
        if (g_card_scratch.id == id) {
            return &g_card_scratch;
        }
    }
    return NULL;
}
