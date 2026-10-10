#include "event.h"
#include "game_ids.h"

/* ── Event table (game content, banked ROM) ────────────────────────
 * Compiled into MBC5 ROM bank GAME_CONTENT_BANK (the `#pragma bank 2`
 * below) so the fixed bank 0 / _HOME area stays small.  The engine never
 * reads this table directly: core/event.c copies each row into a WRAM
 * scratch copy via banked_copy() before matching, so the banked layout is
 * transparent to the rest of the engine.
 *
 * INTERACT / MAP_ENTER resolve first-match in table order: more specific
 * conditions must precede the default fallback.  ACTOR_DEFEATED events are
 * all-match: BOSS_DEFEATED below does NOT suppress MONSTER_DEFEATED, so the
 * boss kill also counts toward the global MONSTERS_DEFEATED counter.
 *
 * Mayor quest (MONSTER HUNT), expressed as data, not game code:
 *   QUEST_START    : first meeting (quest NOT_STARTED) starts the quest:
 *                    MAYOR_INTRO dialogue, MET_MAYOR, quest=ACTIVE.
 *   QUEST_COMPLETE : quest ACTIVE and >= 3 monsters defeated -> reward
 *                    dialogue, give SWORD, quest=COMPLETE (given once).
 *   QUEST_ACTIVE   : quest ACTIVE (fewer than 3 defeated) -> "still working".
 *   QUEST_DONE     : quest COMPLETE -> already-rewarded dialogue.
 *   MONSTER_DEFEATED : every hostile defeat increments the global
 *                    MONSTERS_DEFEATED counter (no quest gating), so kills
 *                    before the quest starts still count.
 */
#pragma bank 2

const EventDefinition g_events[] = {
    {
        EVENT_ID_TOWN_ARRIVAL, EVENT_TRIGGER_MAP_ENTER, ENTITY_ID_NONE, MAP_TOWN,
        1,
        {{ EVENT_COND_FLAG, STORY_FLAG_ID_ARRIVED_TOWN, 0, false, false }},
        1,
        {{ EVENT_ACTION_SET_FLAG, STORY_FLAG_ID_ARRIVED_TOWN, 0, 0 }}
    },
    {
        EVENT_ID_QUEST_START, EVENT_TRIGGER_INTERACT, ENTITY_ID_MAYOR, EVENT_MAP_ANY,
        1,
        {{ EVENT_COND_VARIABLE, VARIABLE_ID_QUEST_MONSTER_HUNT, 0, false, false }},
        3,
        {
            { EVENT_ACTION_DIALOGUE, DIALOGUE_ID_MAYOR_INTRO, 0, 0 },
            { EVENT_ACTION_SET_FLAG, STORY_FLAG_ID_MET_MAYOR, 0, 0 },
            { EVENT_ACTION_SET_VARIABLE, VARIABLE_ID_QUEST_MONSTER_HUNT, 1, 0 }
        }
    },
    {
        EVENT_ID_QUEST_COMPLETE, EVENT_TRIGGER_INTERACT, ENTITY_ID_MAYOR, EVENT_MAP_ANY,
        2,
        {
            { EVENT_COND_VARIABLE, VARIABLE_ID_QUEST_MONSTER_HUNT, 1, false, false },
            { EVENT_COND_VARIABLE, VARIABLE_ID_MONSTERS_DEFEATED, 3, false, true }
        },
        3,
        {
            { EVENT_ACTION_DIALOGUE, DIALOGUE_ID_QUEST_COMPLETE, 0, 0 },
            { EVENT_ACTION_ADD_ITEM, CARD_IRON_SWORD, 1, 0 },
            { EVENT_ACTION_SET_VARIABLE, VARIABLE_ID_QUEST_MONSTER_HUNT, 2, 0 }
        }
    },
    {
        EVENT_ID_QUEST_ACTIVE, EVENT_TRIGGER_INTERACT, ENTITY_ID_MAYOR, EVENT_MAP_ANY,
        1,
        {{ EVENT_COND_VARIABLE, VARIABLE_ID_QUEST_MONSTER_HUNT, 1, false, false }},
        1,
        {{ EVENT_ACTION_DIALOGUE, DIALOGUE_ID_QUEST_ACTIVE, 0, 0 }}
    },
    {
        EVENT_ID_QUEST_DONE, EVENT_TRIGGER_INTERACT, ENTITY_ID_MAYOR, EVENT_MAP_ANY,
        1,
        {{ EVENT_COND_VARIABLE, VARIABLE_ID_QUEST_MONSTER_HUNT, 2, false, false }},
        1,
        {{ EVENT_ACTION_DIALOGUE, DIALOGUE_ID_QUEST_DONE, 0, 0 }}
    },
    {
        EVENT_ID_BOSS_DEFEATED, EVENT_TRIGGER_ACTOR_DEFEATED, ENTITY_ID_SLIME_LORD, EVENT_MAP_ANY,
        0,
        {{ EVENT_COND_NONE, 0, 0, false, false }},
        1,
        {{ EVENT_ACTION_SET_VARIABLE, VARIABLE_ID_ENDING_SHOWN, 1, 0 }}
    },
    {
        EVENT_ID_MONSTER_DEFEATED, EVENT_TRIGGER_ACTOR_DEFEATED, ENTITY_ID_NONE, EVENT_MAP_ANY,
        0,
        {{ EVENT_COND_NONE, 0, 0, false, false }},
        1,
        {{ EVENT_ACTION_ADD_VARIABLE, VARIABLE_ID_MONSTERS_DEFEATED, 1, 0 }}
    },
    {
        EVENT_ID_GUARD_AFTER_MAYOR, EVENT_TRIGGER_INTERACT, ENTITY_ID_GUARD, EVENT_MAP_ANY,
        1,
        {{ EVENT_COND_FLAG, STORY_FLAG_ID_MET_MAYOR, 0, true, false }},
        1,
        {{ EVENT_ACTION_DIALOGUE, DIALOGUE_ID_GUARD_AFTER_MAYOR, 0, 0 }}
    },
    {
        EVENT_ID_GUARD_GREETING, EVENT_TRIGGER_INTERACT, ENTITY_ID_GUARD, EVENT_MAP_ANY,
        1,
        {{ EVENT_COND_FLAG, STORY_FLAG_ID_MET_MAYOR, 0, false, false }},
        1,
        {{ EVENT_ACTION_DIALOGUE, DIALOGUE_ID_GUARD_GREETING, 0, 0 }}
    },
    /* ── Lost Merchant quest (data-driven fetch/deliver) ─────────────
     * Quest state is the MERCHANT_QUEST variable: 0 = not started,
     * 1 = amulet found (ACTIVE), 2 = complete.  DELIVER must precede INTRO
     * (more specific first).  After delivery no merchant event matches, so
     * the interaction falls back to the merchant's SHOP interaction. */
    {
        EVENT_ID_MERCHANT_DELIVER, EVENT_TRIGGER_INTERACT, ENTITY_ID_MERCHANT, EVENT_MAP_ANY,
        2,
        {
            { EVENT_COND_ITEM_COUNT, CARD_AMULET, 1, false, true },
            { EVENT_COND_VARIABLE, VARIABLE_ID_MERCHANT_QUEST, 1, false, false }
        },
        4,
        {
            { EVENT_ACTION_DIALOGUE, DIALOGUE_ID_MERCHANT_THANKS, 0, 0 },
            { EVENT_ACTION_ADD_CURRENCY, CURRENCY_ID_GOLD, 15, 0 },
            { EVENT_ACTION_REMOVE_ITEM, CARD_AMULET, 1, 0 },
            { EVENT_ACTION_SET_VARIABLE, VARIABLE_ID_MERCHANT_QUEST, 2, 0 }
        }
    },
    {
        EVENT_ID_MERCHANT_INTRO, EVENT_TRIGGER_INTERACT, ENTITY_ID_MERCHANT, EVENT_MAP_ANY,
        1,
        {{ EVENT_COND_VARIABLE, VARIABLE_ID_MERCHANT_QUEST, 0, false, false }},
        1,
        {{ EVENT_ACTION_DIALOGUE, DIALOGUE_ID_MERCHANT_INTRO, 0, 0 }}
    },
    {
        EVENT_ID_AMULET_PICKUP, EVENT_TRIGGER_INTERACT, ENTITY_ID_AMULET, EVENT_MAP_ANY,
        1,
        {{ EVENT_COND_VARIABLE, VARIABLE_ID_MERCHANT_QUEST, 0, false, false }},
        3,
        {
            { EVENT_ACTION_DIALOGUE, DIALOGUE_ID_AMULET_FOUND, 0, 0 },
            { EVENT_ACTION_ADD_ITEM, CARD_AMULET, 1, 0 },
            { EVENT_ACTION_SET_VARIABLE, VARIABLE_ID_MERCHANT_QUEST, 1, 0 }
        }
    },
    /* ── Field tutorial spar (Carl, the friendly slime) ────────────
     * Carl teaches through two existing paths, no combat-code changes:
     * his friendly side is an INTERACTION_DIALOGUE static whose lesson
     * plays through the ordinary fallback dialogue path, and his spar
     * side is a solo BATTLE_CARL hostile on the same tile (movement
     * resolves hostiles first, so bumps fight while A-presses talk).
     * TUTORIAL_COMPLETE is all-match ACTOR_DEFEATED like
     * MONSTER_DEFEATED.  Carl owns ENTITY_ID_CARL (no other actor
     * carries it), so only his defeat grants the pool: the pool==0
     * condition makes the +1 AP unlock one-shot, and the victory
     * dialogue shows deferred at the battle exit (see
     * battle_screen.c). */
    {
        EVENT_ID_TUTORIAL_COMPLETE, EVENT_TRIGGER_ACTOR_DEFEATED, ENTITY_ID_CARL, EVENT_MAP_ANY,
        1,
        {{ EVENT_COND_VARIABLE, VARIABLE_ID_ENERGY_POOL, 0, false, false }},
        2,
        {
            { EVENT_ACTION_SET_VARIABLE, VARIABLE_ID_ENERGY_POOL, 3, 0 },
            { EVENT_ACTION_DIALOGUE, DIALOGUE_ID_TUTORIAL_CARL_VICTORY, 0, 0 }
        }
    },
    /* ── Field cache (first poison dagger) ─────────────────────────
     * A chest east of Carl holds one POISON_DAGGER: the first the
     * player can own (starter deck carries none; the Merchant sells
     * more).  One-shot via STORY_FLAG_ID_FIELD_CHEST; repeats fall
     * through to no interaction.  Silent grant (no dialogue row):
     * Carl's lesson points at the cache, and the CARDS tab shows it. */
    {
        EVENT_ID_FIELD_CHEST, EVENT_TRIGGER_INTERACT, ENTITY_ID_CHEST, EVENT_MAP_ANY,
        1,
        {{ EVENT_COND_FLAG, STORY_FLAG_ID_FIELD_CHEST, 0, false, false }},
        2,
        {
            { EVENT_ACTION_ADD_ITEM, CARD_POISON_DAGGER, 1, 0 },
            { EVENT_ACTION_SET_FLAG, STORY_FLAG_ID_FIELD_CHEST, 0, 0 }
        }
    }
};

/* Keep the register-time count in game_ids.h in sync with this table. */
typedef char event_table_count_ok[
    sizeof(g_events) / sizeof(g_events[0]) == GAME_EVENT_COUNT ? 1 : -1
];
