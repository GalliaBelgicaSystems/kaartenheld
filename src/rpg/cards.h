#ifndef RPG_CARDS_H
#define RPG_CARDS_H

#include <stdint.h>
#include <stdbool.h>

/* Card identity.  The engine defines CARD_NONE and the start of the
 * per-game content range; the game names its cards in game_ids.h. */
typedef uint8_t CardId;

#define CARD_NONE       0
#define CARD_FIRST_GAME 0x40

/* Broad card categories. */
typedef enum {
    CARD_TYPE_ATTACK  = 0,
    CARD_TYPE_DEFENSE = 1,
    CARD_TYPE_HEAL    = 2,
    CARD_TYPE_STATUS  = 3,
    CARD_TYPE_UTILITY = 4,
    CARD_TYPE_SPECIAL = 5    /* quest-only card: no battle use, not deckable */
} CardType;

#define CARD_TYPE_COUNT 6

/* What happens when a card is played. */
typedef enum {
    CARD_EFFECT_NONE          = 0,
    CARD_EFFECT_DAMAGE_TARGET = 1,
    CARD_EFFECT_BLOCK_DAMAGE   = 2,
    CARD_EFFECT_HEAL_HP        = 3,
    /* Sentinel: one past the last valid effect, never a real effect.
     * Implicit (no explicit value) so it always stays one past the last
     * enumerator -- adding an effect cannot silently collide with it.
     * Range guards (e.g. the combo staging pack) must test against this,
     * not against the current last enumerator. */
    CARD_EFFECT_COUNT
} CardEffectType;

/* Static card definition.  Registered at boot via card_register_defs().
 * name is a short display string (4-8 chars) stored INLINE so the whole row
 * travels through banked_copy() into WRAM; a banked pointer would be
 * unreadable from the fixed bank (see the quest engine's identical pattern). */
typedef struct {
    CardId id;
    uint8_t type;       /* CardType */
    uint8_t power;      /* numerical value */
    uint8_t cost;       /* energy cost to play (0 = free) */
    uint8_t uses_per_battle; /* 0 = unlimited (never depletes in battle) */
    uint8_t max_copies;   /* 0 = unlimited in collection & deck */
    uint8_t effect;     /* CardEffectType */
    uint8_t battle_type; /* BattleCardType for combat deck mapping; unused for SPECIAL */
    uint8_t price;      /* shop price in gold (0 = not sold) */
    /* On-hit status rider (docs/combo-system.md §13): battle rolls the
     * deterministic RNG and applies status_id when the roll passes.
     * chance is in 1/255 units (e.g. 160 ~= 63%); 0 = no rider. */
    uint8_t status_id;    /* StatusId (rpg/status.h); 0 = none */
    uint8_t status_chance; /* roll threshold, 1/255 units */
    /* 11 chars max incl NUL so a synthesized loot name fits:
     * "M P DA" — 1-letter material + 1-letter effect + 2-letter weapon
     * (docs/loot.md §34.1). */
    char name[12];       /* 4-8 char display name + NUL */
} CardDefinition;

/* Register the game's card catalog (content, banked ROM).
 * mechanics below are generic over whatever catalog is registered. */
void card_register_defs(const CardDefinition *defs, uint8_t count,
                        uint8_t bank);

/* Registered catalog location (ROM banked!).  Fixed-bank code must NOT
 * dereference g_card_defs -- read rows through card_get_def()'s WRAM
 * scratch instead.  Bank-2 code may index the array directly while its
 * bank is mapped (see battle_init_content.c); this contract is enforced
 * at registration time by a compile-time check in the game layer
 * (src/game/cards.c): the catalog bank must be 2. */
extern const CardDefinition *g_card_defs;
extern uint8_t g_card_defs_count;

/* Look up a card definition by id.  Returns a pointer to a scratch copy
 * (valid until the next call).  Returns NULL for unknown ids.
 * Loot-range ids (>= LOOT_ID_BASE) synthesize from the loot identity
 * tables (docs/loot.md §34). */
const CardDefinition *card_get_def(CardId id);

/* Shared definition scratch (WRAM): card_get_def's return target.  The
 * bank-3 synth body (loot_synth_banked) writes it directly, so it must
 * be visible project-wide rather than file-static. */
extern CardDefinition g_card_scratch;

#endif /* RPG_CARDS_H */
