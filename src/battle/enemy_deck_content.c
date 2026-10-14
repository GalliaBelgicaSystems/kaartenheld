#pragma bank 2
#pragma disable_warning 110

#include "deck.h"
#include "banked.h"
#include "rpg/status.h"

/* ── Enemy deck tables (banked ROM) ─────────────────────────────
 * Small decks for each enemy type.  Each enemy draws one card per
 * attack turn; the card's power is the incoming damage the player
 * must defend against.  Decks reshuffle when exhausted.
 * Fixed order (no startup shuffle) — the starter deck follows the
 * same pattern (see deck_init.c). */

static const EnemyCard s_slime_deck[] = {
    { BATTLE_CARD_TYPE_SWORD, 2, STATUS_NONE, 0 },
    { BATTLE_CARD_TYPE_SWORD, 3, STATUS_NONE, 0 },
    { BATTLE_CARD_TYPE_SWORD, 3, STATUS_NONE, 0 },
    { BATTLE_CARD_TYPE_HEAL,  2, STATUS_NONE, 0 },
};
static const EnemyCard s_bat_deck[] = {
    { BATTLE_CARD_TYPE_BOW,   2, STATUS_NONE, 0 },
    { BATTLE_CARD_TYPE_BOW,   3, STATUS_NONE, 0 },
    { BATTLE_CARD_TYPE_SWORD, 4, STATUS_NONE, 0 },
    { BATTLE_CARD_TYPE_SWORD, 3, STATUS_NONE, 0 },
    /* Organic poison rider (Phase D): this swing carries a poison rider at
     * ~60/255 chance; when it lands on the player it also greys her hand. */
    { BATTLE_CARD_TYPE_BOW,   3, STATUS_POISON, 60 },
};
static const EnemyCard s_slime_trio_deck[] = {
    { BATTLE_CARD_TYPE_SWORD, 2, STATUS_NONE, 0 },
    { BATTLE_CARD_TYPE_SWORD, 2, STATUS_NONE, 0 },
    { BATTLE_CARD_TYPE_SWORD, 3, STATUS_NONE, 0 },
    { BATTLE_CARD_TYPE_SWORD, 3, STATUS_NONE, 0 },
    { BATTLE_CARD_TYPE_HEAL,  2, STATUS_NONE, 0 },
};
static const EnemyCard s_kobold_deck[] = {
    { BATTLE_CARD_TYPE_SWORD, 2, STATUS_NONE, 0 },
    { BATTLE_CARD_TYPE_SWORD, 2, STATUS_NONE, 0 },
    { BATTLE_CARD_TYPE_SWORD, 3, STATUS_NONE, 0 },
    { BATTLE_CARD_TYPE_HEAL,  2, STATUS_NONE, 0 },
};
static const EnemyCard s_mimic_deck[] = {
    { BATTLE_CARD_TYPE_SWORD, 3, STATUS_NONE, 0 },
    { BATTLE_CARD_TYPE_SWORD, 3, STATUS_NONE, 0 },
    { BATTLE_CARD_TYPE_SWORD, 4, STATUS_NONE, 0 },
    { BATTLE_CARD_TYPE_SWORD, 2, STATUS_NONE, 0 },
    { BATTLE_CARD_TYPE_HEAL,  3, STATUS_NONE, 0 },
};
static const EnemyCard s_spider_deck[] = {
    { BATTLE_CARD_TYPE_BOW,   2, STATUS_NONE, 0 },
    { BATTLE_CARD_TYPE_SWORD, 3, STATUS_NONE, 0 },
    { BATTLE_CARD_TYPE_SWORD, 3, STATUS_NONE, 0 },
    { BATTLE_CARD_TYPE_HEAL,  2, STATUS_NONE, 0 },
    /* Webbing strike: poison rider at ~70/255; landing it also greys the
     * player's hand (Phase D), the spider's signature threat. */
    { BATTLE_CARD_TYPE_BOW,   3, STATUS_POISON, 70 },
};
/* Carl's practice deck: a single 0-power swing, drawn every round.  The
 * tutorial spar is harmless by construction (no defend needed to
 * survive it); the telegraphed 0 is the visible proof. */
static const EnemyCard s_carl_deck[] = {
    { BATTLE_CARD_TYPE_SWORD, 0, STATUS_NONE, 0 },
};

void enemy_deck_setup_banked(void)
{
    EnemyCompactDeck *ed = (EnemyCompactDeck *)g_bk_ptr_a;
    uint8_t battle_id = g_bk_byte_a;
    const EnemyCard *src = (const EnemyCard *)0;
    uint8_t count = 0, i;

    if (!ed) return;

    switch (battle_id) {
    case 1:  src = s_slime_deck;      count = 4; break;
    case 2:  src = s_bat_deck;        count = 5; break;
    case 3:  src = s_slime_trio_deck; count = 5; break;
    /* BATTLE_MIMIC = 4, BATTLE_KOBOLD = 5, BATTLE_SPIDER = 6 (numeric
     * cases match the BattleId enum; see actor.h). */
    case 4:  src = s_mimic_deck;      count = 5; break;
    case 5:  src = s_kobold_deck;     count = 4; break;
    case 6:  src = s_spider_deck;     count = 5; break;
    case 7:  src = s_carl_deck;       count = 1; break;
    }

    if (!src) return;
    if (count == 0 || count > ENEMY_DECK_SIZE) return;

    for (i = 0; i < count; i++) {
        ed->cards[i].type = src[i].type;
        ed->cards[i].value = src[i].value;
        ed->cards[i].status_id = src[i].status_id;
        ed->cards[i].status_chance = src[i].status_chance;
    }
    ed->count = count;
    ed->draw_idx = 0;
}
