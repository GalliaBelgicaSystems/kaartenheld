#ifndef GAME_IDS_H
#define GAME_IDS_H

#include "entity.h"
#include "event.h"
#include "dialogue.h"
#include "rpg/state.h"

/* Game-specific semantic IDs for this RPG's content.  The engine exposes
 * generic slots (GameState flags/variables/currency indexed by 1-based
 * numeric ids) and the shared identity vocabulary (entity/event/dialogue/
 * item types).  The game layer names the ones this game uses: entity/event/
 * dialogue/item ids live in the per-game range starting at the engine's
 * *_FIRST_GAME bases, so a different RPG built on the same engine defines
 * its own ids here without ever touching the engine headers. */

/* ── Entity types (engine range: NONE=0, PLAYER=1; game range >= 0x80) ──
 * Values come from the generated entity_ids_generated.h (single source of
 * truth: the entity-type JSON registries in screens/enemy_types and
 * screens/entity_types, emitted by
 * tools/screen_compiler/entity_compile.py).  Humans add entity types in
 * the editor — never hand-allocate ids here. */
#include "entity_ids_generated.h"

/* ── Cards (engine range: NONE=0; game range >= CARD_FIRST_GAME) ── */
#define CARD_IRON_SWORD    (CARD_FIRST_GAME + 0)
#define CARD_WOODEN_SHIELD (CARD_FIRST_GAME + 1)
#define CARD_WOOD_RING     (CARD_FIRST_GAME + 2)   /* curated shop ring (docs/loot.md §34.6) */
#define CARD_FIRE_SWORD    (CARD_FIRST_GAME + 3)
#define CARD_POISON_DAGGER (CARD_FIRST_GAME + 4)
#define CARD_AMULET        (CARD_FIRST_GAME + 5)
#define CARD_BOW_9        (CARD_FIRST_GAME + 6)
#define GAME_CARD_COUNT    7

/* ── Events (engine range: NONE=0; game range >= 0x80) ── */
#define EVENT_ID_TOWN_ARRIVAL    (EVENT_ID_FIRST_GAME + 0)
#define EVENT_ID_QUEST_START     (EVENT_ID_FIRST_GAME + 1)
#define EVENT_ID_QUEST_ACTIVE    (EVENT_ID_FIRST_GAME + 2)
#define EVENT_ID_QUEST_COMPLETE  (EVENT_ID_FIRST_GAME + 3)
#define EVENT_ID_QUEST_DONE      (EVENT_ID_FIRST_GAME + 4)
#define EVENT_ID_GUARD_AFTER_MAYOR (EVENT_ID_FIRST_GAME + 5)
#define EVENT_ID_GUARD_GREETING  (EVENT_ID_FIRST_GAME + 6)
#define EVENT_ID_MONSTER_DEFEATED (EVENT_ID_FIRST_GAME + 7)
#define EVENT_ID_BOSS_DEFEATED   (EVENT_ID_FIRST_GAME + 8)
#define EVENT_ID_MERCHANT_INTRO  (EVENT_ID_FIRST_GAME + 9)
#define EVENT_ID_MERCHANT_DELIVER (EVENT_ID_FIRST_GAME + 10)
#define EVENT_ID_AMULET_PICKUP   (EVENT_ID_FIRST_GAME + 11)

/* ── Dialogues (engine range: NONE=0; game range >= 0x80) ──
 * Values come from the generated dialogue_ids_generated.h (single source
 * of truth: the screens/dialogue JSON files, emitted by
 * tools/screen_compiler/dialogue_compile.py).  Humans author dialogue
 * text in the editor — never hand-allocate ids here. */
#include "dialogue_ids_generated.h"

/* Story flags.  Each maps to bit (id-1) of GameState.flags.bytes[].
 * STORY_FLAG_ID_COUNT is the exclusive upper bound passed to story_init(). */
typedef enum {
    STORY_FLAG_ID_ARRIVED_TOWN = 1,
    STORY_FLAG_ID_MET_MAYOR    = 2,
    STORY_FLAG_ID_COUNT        = 3
} StoryFlagId;

/* Named variables.  VARIABLE_ID_x - 1 indexes VariableState.values[]. */
typedef enum {
    VARIABLE_ID_CHAPTER            = 1,
    VARIABLE_ID_MONSTERS_DEFEATED  = 2,   /* global total (all kills count) */
    VARIABLE_ID_QUEST_MONSTER_HUNT = 3,   /* 0 = NOT_STARTED, 1 = ACTIVE, 2 = COMPLETE */
    VARIABLE_ID_ENDING_SHOWN       = 4,   /* set when the final boss is defeated */
    VARIABLE_ID_MERCHANT_QUEST     = 5    /* 0 = not started, 1 = amulet found, 2 = complete */
} VariableIdNamed;

/* Named currencies.  CURRENCY_ID_x - 1 indexes CurrencyState.amount[]. */
typedef enum {
    CURRENCY_ID_GOLD = 1
} CurrencyIdNamed;

/* ── Banked content ────────────────────────────────────────────────
 * The event and dialogue content tables are compiled into MBC5 ROM bank
 * GAME_CONTENT_BANK (see events_content.c / dialogue_content.c, which use
 * `#pragma bank 2`).  Bank 2 is used because the project links with -yo8:
 * bank 0 legitimately spans the full 32 KB (file 0x0000-0x7FFF), so bank 1
 * (file 0x4000-0x7FFF) overlaps bank 0's second half and sdldgb's
 * "Multiple write" overwrites the tables with bank-0 code.  Bank 2
 * (file 0x8000-0xBFFF) is clear.  The engine reads the tables through WRAM
 * scratch copies (core/event.c, core/dialogue.c, banked_copy in crt0.s), so
 * no gameplay code ever reads banked data directly.  The *_COUNT values are
 * compile-time-asserted against the tables in the content files. */
#define GAME_CONTENT_BANK 2
#define GAME_EVENT_COUNT 12
/* GAME_DIALOGUE_COUNT lives in the generated dialogue_ids_generated.h
 * (emitted by dialogue_compile.py from the screens/dialogue files). */

/* Frozen harness-test content (TEST_LEVELS, debug build only): the test
 * scenes/actors compile into ROM bank 4 (battle-art bank, which has the
 * headroom) so the release budgets are untouched, the debug link stays
 * at 8 banks (-yo8), and the fixtures can diverge freely. */
#define GAME_TEST_CONTENT_BANK 4

/* Per-scene actor tables (actors_content.c) live in bank 4 in BOTH builds:
 * the debug build already links the fixture actor tables there
 * (GAME_TEST_CONTENT_BANK) and the release build moved there to free the
 * tight bank 2 for the other content tables (dialogue/events/cards/...).
 * actor_load_banked.c (#pragma bank 4) reads them in-bank. */
#define GAME_ACTOR_BANK 4

#endif /* GAME_IDS_H */
