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
 *   QUEST_COMPLETE : quest ACTIVE and >= 5 monsters defeated -> reward
 *                    dialogue, +1 AP (ENERGY_POOL), quest=COMPLETE (given once).
 *   QUEST_ACTIVE   : quest ACTIVE (fewer than 5 defeated) -> "still working".
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
            { EVENT_COND_VARIABLE, VARIABLE_ID_MONSTERS_DEFEATED, 5, false, true }
        },
        3,
        {
            { EVENT_ACTION_DIALOGUE, DIALOGUE_ID_QUEST_COMPLETE, 0, 0 },
            { EVENT_ACTION_ADD_VARIABLE, VARIABLE_ID_ENERGY_POOL, 1, 0 },
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
     * 1 = ACTIVE (accepted from the merchant, or amulet found first),
     * 2 = complete.  Loot state is STORY_FLAG_ID_AMULET_CHEST (set
     * atomically with the AMULET grant, like FIELD_CHEST): the chest
     * sprite and the chest jingle key off the flag, never the quest
     * variable, so accepting early neither opens the chest art nor
     * plays the jingle.  DELIVER must precede INTRO (more specific
     * first).  After delivery no merchant event matches, so the
     * interaction falls back to the merchant's SHOP interaction; the
     * same fallback is blocked mid-quest by REMINDER below. */
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
        2,
        {
            { EVENT_ACTION_DIALOGUE, DIALOGUE_ID_MERCHANT_INTRO, 0, 0 },
            { EVENT_ACTION_SET_VARIABLE, VARIABLE_ID_MERCHANT_QUEST, 1, 0 }
        }
    },
    {
        /* Mid-quest talks re-brief (same INTRO text, distinct telemetry
         * id) instead of falling through to the locked shop. */
        EVENT_ID_MERCHANT_REMINDER, EVENT_TRIGGER_INTERACT, ENTITY_ID_MERCHANT, EVENT_MAP_ANY,
        2,
        {
            { EVENT_COND_VARIABLE, VARIABLE_ID_MERCHANT_QUEST, 1, false, false },
            { EVENT_COND_ITEM_COUNT, CARD_AMULET, 0, false, false }
        },
        1,
        {{ EVENT_ACTION_DIALOGUE, DIALOGUE_ID_MERCHANT_INTRO, 0, 0 }}
    },
    {
        EVENT_ID_AMULET_PICKUP, EVENT_TRIGGER_INTERACT, ENTITY_ID_AMULET, EVENT_MAP_ANY,
        1,
        {{ EVENT_COND_VARIABLE, VARIABLE_ID_MERCHANT_QUEST, 0, false, false }},
        4,
        {
            { EVENT_ACTION_DIALOGUE, DIALOGUE_ID_AMULET_FOUND, 0, 0 },
            { EVENT_ACTION_ADD_ITEM, CARD_AMULET, 1, 0 },
            { EVENT_ACTION_SET_VARIABLE, VARIABLE_ID_MERCHANT_QUEST, 1, 0 },
            { EVENT_ACTION_SET_FLAG, STORY_FLAG_ID_AMULET_CHEST, 0, 0 }
        }
    },
    {
        EVENT_ID_AMULET_PICKUP_ACCEPTED, EVENT_TRIGGER_INTERACT, ENTITY_ID_AMULET, EVENT_MAP_ANY,
        2,
        {
            { EVENT_COND_VARIABLE, VARIABLE_ID_MERCHANT_QUEST, 1, false, false },
            { EVENT_COND_ITEM_COUNT, CARD_AMULET, 0, false, false }
        },
        4,
        {
            { EVENT_ACTION_DIALOGUE, DIALOGUE_ID_AMULET_FOUND, 0, 0 },
            { EVENT_ACTION_ADD_ITEM, CARD_AMULET, 1, 0 },
            { EVENT_ACTION_SET_VARIABLE, VARIABLE_ID_MERCHANT_QUEST, 1, 0 },
            { EVENT_ACTION_SET_FLAG, STORY_FLAG_ID_AMULET_CHEST, 0, 0 }
        }
    },
    /* ── Field tutorial spar (Carl, the friendly slime) ────────────
     * Carl is TWO co-located actors on one tile: a tutor hostile
     * (actor_id 1, AI_TUTOR, 15 HP, 0-power spar deck) and a dialogue
     * static (actor_id 0, never defeat-gated, carries
     * DIALOGUE_ID_TUTORIAL_CARL_INTRO).  Both share ENTITY_ID_CARL;
     * only the runtime hostile slot can be defeated, so
     * TUTORIAL_COMPLETE below fires exactly once on the spar win.
     * Bumping or facing-talking the tile opens the static's lesson
     * through the ordinary dialogue path while the spar slot stays
     * stashed (synced when the dialogue opens, interaction.c);
     * closing the lesson auto-starts the spar via dialogue_screen's
     * stashed-slot test.  Stale-index safety is structural (every
     * gameplay writer stores NO or a live slot, dialogues are modal,
     * every battle exit clears it), not a slot-active guard -- the
     * full vetting call was measured at 136 B and does not fit the
     * debug fixed bank.  The static never
     * despawns by design: post-victory the spar is gone, so bumps
     * replay the lesson as a remedial with no fight (locked by the
     * actor_defeat_initial scenario).  The SET is
     * unconditional (no pool==0 gate): the field cache SETs the same
     * value, so whichever comes first wins and the second is a harmless
     * no-op -- opening the chest first must not eat the victory speech.
     * The pool write is idempotent either way, and Carl's defeat is
     * persistent (one-shot via GameState.world), so the reward fires
     * exactly once. */
    {
        EVENT_ID_TUTORIAL_COMPLETE, EVENT_TRIGGER_ACTOR_DEFEATED, ENTITY_ID_CARL, EVENT_MAP_ANY,
        0,
        {{ EVENT_COND_NONE, 0, 0, false, false }},
        2,
        {
            { EVENT_ACTION_SET_VARIABLE, VARIABLE_ID_ENERGY_POOL, 3, 0 },
            { EVENT_ACTION_DIALOGUE, DIALOGUE_ID_TUTORIAL_CARL_VICTORY, 0, 0 }
        }
    },
    /* ── Field cache (first poison dagger + AP unlock) ───────────────
     * A chest east of Carl holds one POISON_DAGGER (the first the
     * player can own: starter deck carries none; the Merchant sells
     * more) and the +1 AP pool unlock, announced by its own dialogue.
     * One-shot via STORY_FLAG_ID_FIELD_CHEST; repeats fall through to
     * no interaction.  The pool SET mirrors TUTORIAL_COMPLETE (same
     * value, order-safe in both directions).  The chest is an OAM
     * sprite: setting the flag flips it to the open frame (and it
     * stays open), no floor-paint stamp. */
    {
        EVENT_ID_FIELD_CHEST, EVENT_TRIGGER_INTERACT, ENTITY_ID_CHEST, EVENT_MAP_ANY,
        1,
        {{ EVENT_COND_FLAG, STORY_FLAG_ID_FIELD_CHEST, 0, false, false }},
        4,
        {
            { EVENT_ACTION_DIALOGUE, DIALOGUE_ID_FIELD_CHEST_AP, 0, 0 },
            { EVENT_ACTION_ADD_ITEM, CARD_POISON_DAGGER, 1, 0 },
            { EVENT_ACTION_SET_VARIABLE, VARIABLE_ID_ENERGY_POOL, 3, 0 },
            { EVENT_ACTION_SET_FLAG, STORY_FLAG_ID_FIELD_CHEST, 0, 0 }
        }
    }
};

/* Keep the register-time count in game_ids.h in sync with this table. */
typedef char event_table_count_ok[
    sizeof(g_events) / sizeof(g_events[0]) == GAME_EVENT_COUNT ? 1 : -1
];
