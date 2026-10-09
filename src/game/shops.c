#include "shops.h"
#include "game_ids.h"
#include "banked.h"
#include <stddef.h>

extern const ShopDefinition g_shops[];
extern const uint8_t g_shop_count;

static ShopDefinition s_shop_scratch;
static uint8_t s_shop_count;

const ShopDefinition *game_shop_for_id(uint8_t id)
{
    uint8_t i;

    /* The table (and g_shop_count) live in bank 2; this is fixed-bank
     * code, so the count must be copied out through the banked window
     * before it can be trusted.  Shops are few and small, so a linear
     * scan is fine.  (Reading g_shop_count directly here silently used
     * whatever bank happened to be mapped — a bug the debug scenarios
     * masked but the release walkthrough's shop purchase caught.) */
    banked_copy(GAME_CONTENT_BANK, &s_shop_count, &g_shop_count, 1);
    for (i = 0; i < s_shop_count; i++) {
        banked_copy(GAME_CONTENT_BANK, &s_shop_scratch, &g_shops[i],
                    sizeof(ShopDefinition));
        if (s_shop_scratch.id == id) return &s_shop_scratch;
    }
    return NULL;
}
