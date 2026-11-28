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
#define CARD_IRON_RING      (CARD_FIRST_GAME + 2)   /* curated shop ring (docs/loot.md §34.6) */
#define CARD_BRONZE_SWORD  (CARD_FIRST_GAME + 3)
#define CARD_POISON_DAGGER (CARD_FIRST_GAME + 4)
#define CARD_SHOPKEYS        (CARD_FIRST_GAME + 5)
#define CARD_BOW_9        (CARD_FIRST_GAME + 6)
#define CARD_WOOD_SWORD   (CARD_FIRST_GAME + 7)
#define CARD_STEEL_SHIELD (CARD_FIRST_GAME + 8)   /* steel SH6 (docs/enemies_ranking.md §5.2):
                                                   * the merchant-stocked answer to the
                                                   * Lord's flat-6 swing (blocks alone) */
#define CARD_BONE         (CARD_FIRST_GAME + 9)   /* quest bone (skeleton -> dog trade) */
#define CARD_OLD_KEY      (CARD_FIRST_GAME + 10)  /* quest key (the dog's field-cache key) */
#define GAME_CARD_COUNT    11

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
#define EVENT_ID_SHOPKEYS_PICKUP   (EVENT_ID_FIRST_GAME + 11)
#define EVENT_ID_TUTORIAL_COMPLETE (EVENT_ID_FIRST_GAME + 12)
#define EVENT_ID_FIELD_CHEST     (EVENT_ID_FIRST_GAME + 13)
#define EVENT_ID_MERCHANT_REMINDER (EVENT_ID_FIRST_GAME + 14)
#define EVENT_ID_KNIGHT_SPLIT    (EVENT_ID_FIRST_GAME + 15)
#define EVENT_ID_HEAL_MAGE       (EVENT_ID_FIRST_GAME + 16)
#define EVENT_ID_HEAL_MAGE_REFUSED (EVENT_ID_FIRST_GAME + 17)
/* 18 reused for EVENT_ID_CHEST_LOCKED below: the Carl chest gate was
 * removed when the key quest landed (talking to Carl no longer needs
 * the cache), so the slot now names the locked-chest bump. */
#define EVENT_ID_CHEST_LOCKED   (EVENT_ID_FIRST_GAME + 18) /* field cache bump without the dog's key */
#define EVENT_ID_MIMIC_REWARD    (EVENT_ID_FIRST_GAME + 19)
#define EVENT_ID_QUEST_NUDGE     (EVENT_ID_FIRST_GAME + 20)
#define EVENT_ID_HEAL_MAGE_RING_LESSON (EVENT_ID_FIRST_GAME + 21)
#define EVENT_ID_QUEST_EARLY_DONE (EVENT_ID_FIRST_GAME + 22)
#define EVENT_ID_VILLAGE_GUARDS (EVENT_ID_FIRST_GAME + 23)
#define EVENT_ID_SKELETON_ASK   (EVENT_ID_FIRST_GAME + 24)
#define EVENT_ID_SKELETON_DESC  (EVENT_ID_FIRST_GAME + 25)
#define EVENT_ID_DOG_HINT       (EVENT_ID_FIRST_GAME + 26)
#define EVENT_ID_DOG_ASK        (EVENT_ID_FIRST_GAME + 27)
#define EVENT_ID_DOG_THANKS     (EVENT_ID_FIRST_GAME + 28)
#ifdef TRAVEL_DEMO
#define EVENT_ID_TRAVEL_DEMO    (EVENT_ID_FIRST_GAME + 29)
#define EVENT_ID_TRAVEL_SUPPLY  (EVENT_ID_FIRST_GAME + 30)
#define EVENT_ID_RUINS_CHEST    (EVENT_ID_FIRST_GAME + 31)
#define EVENT_ID_RUINS_EXIT     (EVENT_ID_FIRST_GAME + 32)
#define EVENT_ID_RUINS_CHEST_EMPTY (EVENT_ID_FIRST_GAME + 33)
#define EVENT_ID_TRAVEL_DEMO_ACT1 (EVENT_ID_FIRST_GAME + 34)
#define EVENT_ID_TRAVEL_RETURN  (EVENT_ID_FIRST_GAME + 35)
#define EVENT_ID_TRAVEL_DESOLATE_RETURN (EVENT_ID_FIRST_GAME + 37)
#define EVENT_ID_WAKE_CHEST     (EVENT_ID_FIRST_GAME + 36)
#else
#define EVENT_ID_WAKE_CHEST     (EVENT_ID_FIRST_GAME + 29)
#endif

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
    STORY_FLAG_ID_FIELD_CHEST  = 3,
    STORY_FLAG_ID_SHOPKEYS_CHEST = 4,   /* mountain shop-keys chest opened (loot
                                       * state, like FIELD_CHEST -- set
                                       * atomically with the SHOP-KEYS grant) */
    STORY_FLAG_ID_HEAL_MAGE_USED = 5, /* the village Heal Mage already gave
                                       * its one free heal (later talks get
                                       * the refusal, no heal, no jingle) */
    STORY_FLAG_ID_HUNT_NOTIFIED = 6, /* the 3-kill "return to the mayor"
                                       * notice already showed (one-shot) */
    STORY_FLAG_ID_RING_LESSON = 7, /* the Heal Mage explained ring tactics
                                       * (shown once, on first talk while
                                       * owning the Iron Ring) */
    STORY_FLAG_ID_TUTORIAL_SKIPPED = 8, /* lesson AP granted (spar start
                                         * or leave-the-field skip),
                                         * one-shot: read as
                                         * TUTORIAL_AP_GRANTED (kept name:
                                         * scenarios, saves and the host
                                         * flag maps key on it).  The
                                         * offer never repeats and the
                                         * victory grants nothing more */
    STORY_FLAG_ID_MET_DOG      = 9, /* heard the village dog's key hint
                                       * (gates the skeleton's bone offer:
                                       * the first bump only describes it) */
    STORY_FLAG_ID_BONE_TAKEN   = 10, /* took the bone from the tunnel
                                       * skeleton (one-shot; later bumps
                                       * only describe it) */
    STORY_FLAG_ID_DOG_KEY      = 11, /* the village dog traded its Old Key
                                       * for the bone (? mark gone, later
                                       * bumps replay the thanks) */
    STORY_FLAG_ID_TUTORIAL_SPAR_WON = 12, /* Carl's spar was won (set by the
                                       * victory event, never cleared):
                                       * with exactly one kill on the
                                       * counter this means the only kill
                                       * was the spar, so the next real
                                       * victory still earns the
                                       * first-victory bronze shield */
    STORY_FLAG_ID_SLIME_LORD_INTRO = 13, /* Lord of Slimes throne-room intro
                                          * dialogue played (advancing 5 rows) */
    STORY_FLAG_ID_WAKE_CHEST = 14, /* act-2 wake-cache AP chest opened
                                    * (one-shot loot state, like FIELD_CHEST) */
#ifdef TRAVEL_DEMO
    STORY_FLAG_ID_RUINS_CHEST = 15, /* sunken-ruins AP chest (demo only) */
    STORY_FLAG_ID_COUNT        = 16
#else
    STORY_FLAG_ID_COUNT        = 15
#endif
} StoryFlagId;

/* Named variables.  VARIABLE_ID_x - 1 indexes VariableState.values[]. */
typedef enum {
    VARIABLE_ID_CHAPTER            = 1,
    VARIABLE_ID_MONSTERS_DEFEATED  = 2,   /* global total (all kills count) */
    VARIABLE_ID_QUEST_MONSTER_HUNT = 3,   /* 0 = NOT_STARTED, 1 = ACTIVE, 2 = COMPLETE */
    VARIABLE_ID_ENDING_SHOWN       = 4,   /* set when the final boss is defeated */
    VARIABLE_ID_MERCHANT_QUEST     = 5,   /* 0 = not started, 1 = ACTIVE (accepted or found), 2 = complete */
    VARIABLE_ID_ENERGY_POOL        = 6,   /* battle energy pool: story events raise it, no cap */
    VARIABLE_ID_GEM_OWNED          = 7,   /* owned-gem bitmask (bit i = gem i won) */
    VARIABLE_ID_GEM_EQUIPPED       = 8,   /* equipped-gem bitmask (subset of owned, max MAX_EQUIPPED_GEMS bits) */
    VARIABLE_ID_DOG_QUEST          = 9,   /* 0 = not started, 1 = ACTIVE (heard the hint), 2 = complete (key traded) */
    VARIABLE_ID_DAY                = 10,  /* Convergence countdown (days left, starts at CONVERGENCE_DAYS) */
    VARIABLE_ID_LORD_POWER         = 11,  /* Lord of Slimes strength (0 = normal, 1 = converged/supreme) */
    VARIABLE_ID_PROVISIONS        = 12,  /* journey provisions (3 burned per travel day) */
    VARIABLE_ID_WATER_RETIRED      = 13   /* retired: water folded into provisions (never reuse
                                        * without a save-format audit; gameplay must not read it) */
} VariableIdNamed;

/* Named currencies.  CURRENCY_ID_x - 1 indexes CurrencyState.amount[].
 * ARROWS is the shared bow-ammo pool (limited-arrows): every bow play
 * spends 1, victory grants 1-3 (capped at ARROWS_MAX), the shop sells
 * 1 arrow for 1 gold.  Cards never leave the deck for lack of arrows;
 * they are just unplayable (NO ARROWS) and never drawn empty. */
typedef enum {
    CURRENCY_ID_GOLD = 1,
    CURRENCY_ID_ARROWS = 2
} CurrencyIdNamed;

/* Convergence countdown (docs/travel-system.md §35): days left until the
 * Lord of Slimes peaks.  Travel steps and road events burn days; at zero
 * the Lord goes supreme (VARIABLE_ID_LORD_POWER) and late fights differ. */
#define CONVERGENCE_DAYS 180

/* Shared arrow pool tuning (limited-arrows). */
#define ARROWS_MAX         30
#define ARROW_PRICE_GOLD   1
#define HERO_START_ARROWS  3

/* ── Gems (quick-screen GEM tab, docs/short-straight.md) ──────────
 * Passive skills: owning a gem does nothing; equipping it (manual, A on
 * the GEM tab) unlocks its skill.  State lives in the generic variables
 * above (VARIABLE_ID_GEM_OWNED / _EQUIPPED bitmasks) so snapshots,
 * descriptors, saves and the roundtrip cover gems with no transport
 * churn; the registry below names this game's gems.  Only gem 0 is
 * winnable for now (chuck-a-luck net +20 in one sitting); the bitmasks
 * and the equip cap already make room for more. */
#define GEM_STRAIGHT        0   /* short-straight combo tier (docs/short-straight.md) */
#define GEM_COUNT             1   /* gems defined by this game (bits 0..GEM_COUNT-1) */
#define MAX_EQUIPPED_GEMS     3   /* at most this many gems equipped at once */

/* ── Banked content ────────────────────────────────────────────────
 * The event/card/shop content tables are compiled into MBC5 ROM bank
 * GAME_CONTENT_BANK (see events_content.c / cards_content.c, which use
 * `#pragma bank 2`).  Bank 2 is used because the project links with -yo8:
 * bank 0 legitimately spans the full 32 KB (file 0x0000-0x7FFF), so bank 1
 * (file 0x4000-0x7FFF) overlaps bank 0's second half and sdldgb's
 * "Multiple write" overwrites the tables with bank-0 code.  Bank 2
 * (file 0x8000-0xBFFF) is clear.  The engine reads the tables through WRAM
 * scratch copies (core/event.c, core/dialogue.c, banked_copy in crt0.s), so
 * no gameplay code ever reads banked data directly.  The *_COUNT values are
 * compile-time-asserted against the tables in the content files. */
#define GAME_CONTENT_BANK 2

/* Bank holding the event table (src/game/events_content.c).  The engine
 * reads rows through banked_copy(), so the table is free to live in any
 * bank: bank 10 hosts it because bank 2 (cards catalog + the DECK view
 * build) is full.  Keep in sync with events_content.c's `#pragma bank`. */
/* The travel-demo event table (loop legs) outgrew bank 10, so the
 * travel build keeps the table in the bank-12 spill bank instead.
 * Main ROMs keep bank 10 (byte-identical objects).  The slime-trio
 * spawn body moves with it (it scans the table in-bank). */
#ifdef TRAVEL_DEMO
#define GAME_EVENT_BANK  12
#else
#define GAME_EVENT_BANK  10
#endif
#ifdef TRAVEL_DEMO
#define GAME_EVENT_COUNT 38
#else
#define GAME_EVENT_COUNT 30
#endif
/* GAME_DIALOGUE_COUNT lives in the generated dialogue_ids_generated.h
 * (emitted by dialogue_compile.py from the screens/dialogue files). */

/* Frozen harness-test content (TEST_LEVELS, debug build only): the test
 * scenes compile into ROM bank 4 and the test actor tables into ROM
 * bank 9 (GAME_TEST_ACTOR_BANK), so the release budgets are untouched,
 * the debug link keeps the bank-4 sprite body's same-bank plain call
 * to the chest resolver (which stays in the bank-4 scenes file via
 * --chest-in-scenes), and the fixtures can diverge freely. */
#define GAME_TEST_CONTENT_BANK 4
/* Test actor-table bank (debug build only): debug bank 4 is full
 * (battle art + actor tables + sprite bodies share it), while the
 * loader body must share its tables' bank (direct reads, §52.11.1).
 * Bank 9 is pristine (MBC5 low byte reaches banks 8-15 with no asm
 * change, §55.5).  Mirrors scene_load.c's TEST_LEVELS bank split. */
#define GAME_TEST_ACTOR_BANK 9

/* Per-scene actor tables (actors_content.c) live in bank 4 in BOTH builds:
 * the debug build already links the fixture actor tables there
 * (GAME_TEST_CONTENT_BANK) and the release build moved there to free the
 * tight bank 2 for the other content tables (dialogue/events/cards/...).
 * actor_load_banked.c (#pragma bank 4) reads them in-bank. */
#define GAME_ACTOR_BANK 4

#endif /* GAME_IDS_H */
