#include "rpg/cards.h"
#include "game_ids.h"

extern const CardDefinition g_cards[];

/* The registered catalog must stay in ROM bank 2: bank-2 code reads the
 * table bank-locally and directly (src/battle/battle_init_content.c), so
 * a registration from any other bank would silently read the wrong ROM
 * window.  Compile-time tripwire in the game layer, where both the
 * catalog define (game_ids.h) and the registration meet. */
typedef char g_card_catalog_bank_ok[GAME_CONTENT_BANK == 2 ? 1 : -1];

void game_cards_register(void)
{
    card_register_defs(g_cards, GAME_CARD_COUNT, GAME_CONTENT_BANK);
}
