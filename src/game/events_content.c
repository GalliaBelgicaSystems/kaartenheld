#include "event.h"
#include "game_ids.h"
#include "audio.h"

/* ── Event table (game content, banked ROM) ────────────────────────
 * Compiled into MBC5 ROM bank GAME_EVENT_BANK (the `#pragma bank 10`
 * below) so the fixed bank 0 / _HOME area stays small.  The engine never
 * reads this table directly: core/event.c copies each row into a WRAM
 * scratch copy via banked_copy() before matching, so the banked layout is
 * transparent to the rest of the engine.
 *
 * INTERACT / MAP_ENTER resolve first-match in table order: more specific
 * conditions must precede the default fallback.  ACTOR_DEFEATED events are
 * all-match: BOSS_DEFEATED below does NOT suppress MONSTER_DEFEATED, so the
 * boss kill also counts toward the global MONSTERS_DEFEATED counter.
 * Never put SCENE_CHANGE and DIALOGUE actions in the same row: scene_load
 * invalidates the render cache without reseeding it, so a dialogue opened
 * on the new scene would misclassify as a dialogue exit (see screen.c).
 * (No row uses SCENE_CHANGE today; keep it that way or split the row.)
 *
 * Mayor quest (MONSTER HUNT), expressed as data, not game code:
 *   QUEST_EARLY_DONE : first meeting with 3+ kills already done
 *                    (quest NOT_STARTED) grants +1 AP (ENERGY_POOL) at
 *                    once via a new intro and jumps to COMPLETE.
 *   QUEST_START    : first meeting (quest NOT_STARTED, fewer than 3
 *                    kills) starts the quest: MAYOR_INTRO dialogue,
 *                    MET_MAYOR, quest=ACTIVE.
 *   QUEST_COMPLETE : quest ACTIVE and >= 3 monsters defeated -> reward
 *                    dialogue, +1 AP (ENERGY_POOL), quest=COMPLETE (given once).
 *   QUEST_ACTIVE   : quest ACTIVE (fewer than 3 defeated) -> "still working".
 *   QUEST_DONE     : quest COMPLETE -> already-rewarded dialogue.
 *   QUEST_NUDGE    : the exact 3rd kill with the quest ACTIVE pops the
 *                    "return to the mayor" notice (one-shot by exactness).
 *                    Kills before the accept stay silent.
 *   MONSTER_DEFEATED : every hostile defeat increments the global
 *                    MONSTERS_DEFEATED counter (no quest gating), so kills
 *                    before the quest starts still count.
 */
#pragma bank 10

const EventDefinition g_events[] = {
    {
        EVENT_ID_TOWN_ARRIVAL, EVENT_TRIGGER_MAP_ENTER, ENTITY_ID_NONE, MAP_TOWN,
        1,
        {{ EVENT_COND_FLAG, STORY_FLAG_ID_ARRIVED_TOWN, 0, false, false }},
        1,
        {{ EVENT_ACTION_SET_FLAG, STORY_FLAG_ID_ARRIVED_TOWN, 0, 0 }}
    },
    {
        /* Early turn-in (kills done before accepting): the hunt is
         * NOT_STARTED but 3+ monsters are already dead.  Fires a new
         * intro + the same +1 AP grant as QUEST_COMPLETE and jumps
         * straight to COMPLETE, so the reward is never missed and the
         * ordinary QUEST_START intro never plays.  Placed before
         * QUEST_START (first-match: START's bare quest==0 would
         * otherwise swallow this). */
        EVENT_ID_QUEST_EARLY_DONE, EVENT_TRIGGER_INTERACT, ENTITY_ID_MAYOR, EVENT_MAP_ANY,
        2,
        {
            { EVENT_COND_VARIABLE, VARIABLE_ID_QUEST_MONSTER_HUNT, 0, false, false },
            { EVENT_COND_VARIABLE, VARIABLE_ID_MONSTERS_DEFEATED, 3, false, true }
        },
        5,
        {
            { EVENT_ACTION_DIALOGUE, DIALOGUE_ID_MAYOR_EARLY, 0, 0 },
            { EVENT_ACTION_SET_FLAG, STORY_FLAG_ID_MET_MAYOR, 0, 0 },
            { EVENT_ACTION_ADD_VARIABLE, VARIABLE_ID_ENERGY_POOL, 1, 0 },
            { EVENT_ACTION_SET_VARIABLE, VARIABLE_ID_QUEST_MONSTER_HUNT, 2, 0 },
            { EVENT_ACTION_AUTOSAVE, 0, 0, 0 }
        }
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
        4,
        {
            { EVENT_ACTION_DIALOGUE, DIALOGUE_ID_QUEST_COMPLETE, 0, 0 },
            { EVENT_ACTION_ADD_VARIABLE, VARIABLE_ID_ENERGY_POOL, 1, 0 },
            { EVENT_ACTION_SET_VARIABLE, VARIABLE_ID_QUEST_MONSTER_HUNT, 2, 0 },
            { EVENT_ACTION_AUTOSAVE, 0, 0, 0 }
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
    /* ── Castle mimic reward ─────────────────────────────────────────
     * Felling the mimic (a solo miniboss with a persistent actor_id,
     * so this fires exactly once like the knight split) grants +1
     * battle AP (ENERGY_POOL), the same uniform ADD 1 as the
     * tutorial/chest/Monster Hunt milestones, and narrates the next step
     * (speakerless box, like FOUND SHOP KEYS: the dead mimic gives no
     * speech).  The grant is observable through VARIABLE_SET plus
     * the snapshot. */
    {
        EVENT_ID_MIMIC_REWARD, EVENT_TRIGGER_ACTOR_DEFEATED, ENTITY_ID_MIMIC, EVENT_MAP_ANY,
        0,
        {{ EVENT_COND_NONE, 0, 0, false, false }},
        3,
        {
            { EVENT_ACTION_ADD_VARIABLE, VARIABLE_ID_ENERGY_POOL, 1, 0 },
            { EVENT_ACTION_DIALOGUE, DIALOGUE_ID_MIMIC_REWARD, 0, 0 },
            { EVENT_ACTION_AUTOSAVE, 0, 0, 0 }
        }
    },
    {
        EVENT_ID_MONSTER_DEFEATED, EVENT_TRIGGER_ACTOR_DEFEATED, ENTITY_ID_NONE, EVENT_MAP_ANY,
        0,
        {{ EVENT_COND_NONE, 0, 0, false, false }},
        1,
        {{ EVENT_ACTION_ADD_VARIABLE, VARIABLE_ID_MONSTERS_DEFEATED, 1, 0 }}
    },
    /* ── 3-kill "return to the mayor" notice ─────────────────────────
     * The 3rd victorious battle (any hostile: ACTOR_DEFEATED is
     * all-match, and this row sits right after MONSTER_DEFEATED so the
     * counter already includes this kill) pops a speakerless next-step
     * box -- but ONLY when the quest was accepted first (quest ACTIVE).
     * Kills before the accept still count toward the counter, yet stay
     * silent: the first talk then takes the EARLY_DONE path above
     * instead of a stale "return" prompt.  Exact ==3 (not >=3) keeps
     * the 2-cond limit (quest + count) and is one-shot by
     * construction: the counter passes through 3 exactly once in real
     * gameplay (every defeat adds exactly 1).  Artificial staging that
     * skips 3 (debug descriptors, future +2 rewards) never fires this
     * -- that is the accepted cost of the 2-cond cap; widening
     * MAX_EVENT_CONDS would recover it.
     * STORY_FLAG_ID_HUNT_NOTIFIED is still set for observability.
     * Overlap note: a defeat that also carries its own reward dialogue
     * (Carl's speech, the mimic box) stages that text last, swallowing
     * this one -- grants and the flag are unaffected either way. */
    {
        EVENT_ID_QUEST_NUDGE, EVENT_TRIGGER_ACTOR_DEFEATED, ENTITY_ID_NONE, EVENT_MAP_ANY,
        2,
        {
            { EVENT_COND_VARIABLE, VARIABLE_ID_QUEST_MONSTER_HUNT, 1, false, false },
            { EVENT_COND_VARIABLE, VARIABLE_ID_MONSTERS_DEFEATED, 3, false, false }
        },
        2,
        {
            { EVENT_ACTION_DIALOGUE, DIALOGUE_ID_MONSTERS_SLAIN, 0, 0 },
            { EVENT_ACTION_SET_FLAG, STORY_FLAG_ID_HUNT_NOTIFIED, 0, 0 }
        }
    },
    /* ── Village-area gate guard ─────────────────────────────────────
     * Map-scoped override (first-match: placed before the generic
     * GUARD_AFTER_MAYOR / GUARD_GREETING fallbacks below): the east
     * guard (ENTITY_ID_GUARD) always gives the castle warning,
     * regardless of MET_MAYOR.  The west guard is ENTITY_ID_TOWN_GUARD
     * (no event rows match it), so its actor dialogue -- the kobold
     * warning -- plays.  Town / ridge guards keep the generic lines. */
    {
        EVENT_ID_VILLAGE_GUARDS, EVENT_TRIGGER_INTERACT, ENTITY_ID_GUARD, MAP_VILLAGE_AREA,
        0,
        {{ EVENT_COND_NONE, 0, 0, false, false }},
        1,
        {{ EVENT_ACTION_DIALOGUE, DIALOGUE_ID_VILLAGE_GUARDS, 0, 0 }}
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
     * 1 = ACTIVE (accepted from the merchant, or shop keys found
     * first), 2 = complete.  Loot state is STORY_FLAG_ID_SHOPKEYS_CHEST
     * (set atomically with the SHOP-KEYS grant, like FIELD_CHEST):
     * the chest sprite and the chest jingle key off the flag, never
     * the quest variable, so accepting early neither opens the chest
     * art nor plays the jingle.  DELIVER must precede INTRO (more
     * specific first): the first talk holding the shop keys delivers
     * them (thanks, +15 gold, keys removed, quest complete) before
     * any shop opens.  After delivery no merchant event matches, so
     * the interaction falls back to the merchant's SHOP interaction;
     * the same fallback is blocked mid-quest by REMINDER below.
     * Finding the shop keys is what unlocks the shop: pickup sets
     * QUEST=1, so no briefing detour stands between the find and
     * the delivery. */
    {
        EVENT_ID_MERCHANT_DELIVER, EVENT_TRIGGER_INTERACT, ENTITY_ID_MERCHANT, EVENT_MAP_ANY,
        2,
        {
            { EVENT_COND_ITEM_COUNT, CARD_SHOPKEYS, 1, false, true },
            { EVENT_COND_VARIABLE, VARIABLE_ID_MERCHANT_QUEST, 1, false, false }
        },
        5,
        {
            { EVENT_ACTION_DIALOGUE, DIALOGUE_ID_MERCHANT_THANKS, 0, 0 },
            { EVENT_ACTION_ADD_CURRENCY, CURRENCY_ID_GOLD, 15, 0 },
            { EVENT_ACTION_REMOVE_ITEM, CARD_SHOPKEYS, 1, 0 },
            { EVENT_ACTION_SET_VARIABLE, VARIABLE_ID_MERCHANT_QUEST, 2, 0 },
            { EVENT_ACTION_AUTOSAVE, 0, 0, 0 }
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
            { EVENT_COND_ITEM_COUNT, CARD_SHOPKEYS, 0, false, false }
        },
        1,
        {{ EVENT_ACTION_DIALOGUE, DIALOGUE_ID_MERCHANT_INTRO, 0, 0 }}
    },
    {
        EVENT_ID_SHOPKEYS_PICKUP, EVENT_TRIGGER_INTERACT, ENTITY_ID_SHOPKEYS, EVENT_MAP_ANY,
        1,
        {{ EVENT_COND_FLAG, STORY_FLAG_ID_SHOPKEYS_CHEST, 0, false, false }},
        4,
        {
            { EVENT_ACTION_DIALOGUE, DIALOGUE_ID_SHOPKEYS_FOUND, 0, 0 },
            { EVENT_ACTION_ADD_ITEM, CARD_SHOPKEYS, 1, 0 },
            { EVENT_ACTION_SET_VARIABLE, VARIABLE_ID_MERCHANT_QUEST, 1, 0 },
            { EVENT_ACTION_SET_FLAG, STORY_FLAG_ID_SHOPKEYS_CHEST, 0, 0 }
        }
    },
    /* ── Field tutorial spar (Carl, the friendly slime) ────────────
     * Carl is TWO co-located actors on one tile: a tutor hostile
     * (actor_id 1, AI_TUTOR, 10 HP, 0-power spar deck) and a dialogue
     * static (actor_id 0, never defeat-gated, carries the YES/NO spar
     * offer).  Both share ENTITY_ID_CARL; only the runtime hostile slot
     * can be defeated.  Fleeing a
     * finished lesson instead shows a farewell box (staged by the
     * banked tick through the post-tick opener, no event row needed)
     * and grants nothing.  The spar is gated on the field cache: talking or bumping the
     * tile while STORY_FLAG_ID_FIELD_CHEST is clear matches
     * CARL_CHEST_FIRST instead ("Open chest, then come back!"), which
     * returns ENGAGE_DIALOGUE without stashing the spar slot
     * (interaction.c syncs the slot only on the actor-dialogue path),
     * so closing it returns to the overworld with no fight -- and the
     * hero may enter the spar at pool 1 -- the key-locked field cache
     * (CHEST_LOCKED below) no longer gates the lesson, so no event
     * matches Carl and the ordinary offer path (offer + stashed spar
     * + auto-start on YES close) always runs.
     * Bumping or facing-talking the tile opens the static's offer
     * through the ordinary dialogue path while the spar slot stays
     * stashed (synced when the dialogue opens, interaction.c);
     * confirming YES auto-starts the spar via dialogue_screen's
     * stashed-slot test.  Stale-index safety is structural (every
     * gameplay writer stores NO or a live slot, dialogues are modal,
     * every battle exit clears it), not a slot-active guard -- the
     * full vetting call was measured at 136 B and does not fit the
     * debug fixed bank.  The static never
     * despawns by design: post-victory the spar is gone, so bumps
     * replay the offer as a remedial with no fight (locked by the
     * actor_defeat_initial scenario).  The grant is a uniform ADD 1:
     * every milestone (tutorial win, chest, Monster Hunt, mimic) adds
     * exactly one to the base-1 pool, reaching 5 in any completion
     * order (fleeing the lesson grants nothing).  Carl's defeat is
     * persistent (one-shot via GameState.world).  The lesson's +1 AP is
     * handed over when the spar STARTS (overworld encounter path,
     * one-shot via TUTORIAL_SKIPPED -- shared with the leave-the-field
     * skip, which hands over the same point), so the win row below keeps
     * only the graduation speech and every milestone (tutorial, chest,
     * Monster Hunt, mimic) still adds exactly one to the base-1 pool,
     * reaching 5 in any completion order.  The row also latches
     * TUTORIAL_SPAR_WON: the win increments the kill counter without
     * granting loot, and the latch tells the loot roll the counter's
     * single kill was the spar -- so the next real victory still earns
     * the first-victory bronze shield. */
    {
        /* Graduation speech only (no grant: the lesson's +1 AP was
         * already handed over at the spar start -- or by the
         * leave-the-field skip -- so a win after the skip still
         * graduates without a second point) plus the spar-won latch
         * for the first-victory loot exception. */
        EVENT_ID_TUTORIAL_COMPLETE, EVENT_TRIGGER_ACTOR_DEFEATED, ENTITY_ID_CARL, EVENT_MAP_ANY,
        0,
        {{ EVENT_COND_NONE, 0, 0, false, false }},
        2,
        {
            { EVENT_ACTION_DIALOGUE, DIALOGUE_ID_TUTORIAL_CARL_VICTORY, 0, 0 },
            { EVENT_ACTION_SET_FLAG, STORY_FLAG_ID_TUTORIAL_SPAR_WON, 0, 0 }
        }
    },
    /* ── Field cache (first poison dagger + AP unlock, key-locked) ───
     * A chest east of Carl holds one POISON_DAGGER (the first the
     * player can own: starter deck carries none; the Merchant sells
     * more) and a +1 AP pool grant (ADD, one of the four uniform +1
     * milestones from the base-1 pool reaching 5),
     * announced by its own dialogue.  The Old Key (traded from the
     * village dog for the skeleton's bone) unlocks it: without the key
     * CHEST_LOCKED below matches instead ("This chest is locked.").
     * One-shot via STORY_FLAG_ID_FIELD_CHEST; repeats fall through to
     * no interaction.  The chest is an OAM sprite: setting the flag
     * flips it to the open frame (and it stays open), no floor-paint
     * stamp. */
    {
        EVENT_ID_CHEST_LOCKED, EVENT_TRIGGER_INTERACT, ENTITY_ID_CHEST, EVENT_MAP_ANY,
        2,
        {
            { EVENT_COND_FLAG, STORY_FLAG_ID_FIELD_CHEST, 0, false, false },
            { EVENT_COND_ITEM_COUNT, CARD_OLD_KEY, 0, false, false }
        },
        1,
        {{ EVENT_ACTION_DIALOGUE, DIALOGUE_ID_CHEST_LOCKED, 0, 0 }}
    },
    {
        EVENT_ID_FIELD_CHEST, EVENT_TRIGGER_INTERACT, ENTITY_ID_CHEST, EVENT_MAP_ANY,
        2,
        {
            { EVENT_COND_FLAG, STORY_FLAG_ID_FIELD_CHEST, 0, false, false },
            { EVENT_COND_ITEM_COUNT, CARD_OLD_KEY, 1, false, true }
        },
        5,
        {
            { EVENT_ACTION_DIALOGUE, DIALOGUE_ID_FIELD_CHEST_AP, 0, 0 },
            { EVENT_ACTION_ADD_ITEM, CARD_POISON_DAGGER, 1, 0 },
            { EVENT_ACTION_ADD_VARIABLE, VARIABLE_ID_ENERGY_POOL, 1, 0 },
            { EVENT_ACTION_SET_FLAG, STORY_FLAG_ID_FIELD_CHEST, 0, 0 },
            { EVENT_ACTION_AUTOSAVE, 0, 0, 0 }
        }
    },
    /* ── Tunnel skeleton (bone for the village dog) ────────────────
     * An old skeleton rests in the south tunnel (Desolate Field to
     * South Ridge).  The first
     * bump only describes it; talking to the village dog first (its
     * hint sets MET_DOG) teaches the hero the bone's worth, and the
     * next bump offers "Take a bone from the skeleton?" (YES/NO).
     * YES resolves through choice_nav_banked.c code 9 (grant BONE +
     * BONE_TAKEN through the real item/flag APIs, then the taken
     * follow-up); NO closes plainly and the skeleton asks again.
     * DESC doubles as the actor fallback dialogue, so post-take
     * bumps (no event matches) describe it again. */
    {
        EVENT_ID_SKELETON_ASK, EVENT_TRIGGER_INTERACT, ENTITY_ID_SKELETON, EVENT_MAP_ANY,
        2,
        {
            { EVENT_COND_FLAG, STORY_FLAG_ID_MET_DOG, 0, true, false },
            { EVENT_COND_FLAG, STORY_FLAG_ID_BONE_TAKEN, 0, false, false }
        },
        1,
        {{ EVENT_ACTION_DIALOGUE, DIALOGUE_ID_SKELETON_ASK, 0, 0 }}
    },
    {
        EVENT_ID_SKELETON_DESC, EVENT_TRIGGER_INTERACT, ENTITY_ID_SKELETON, EVENT_MAP_ANY,
        2,
        {
            { EVENT_COND_FLAG, STORY_FLAG_ID_MET_DOG, 0, false, false },
            { EVENT_COND_FLAG, STORY_FLAG_ID_BONE_TAKEN, 0, false, false }
        },
        1,
        {{ EVENT_ACTION_DIALOGUE, DIALOGUE_ID_SKELETON_DESC, 0, 0 }}
    },
    /* ── Village dog (bone for the Old Key) ────────────────────────
     * The village dog holds the field cache's Old Key and won't give
     * it (HINT, one-shot via the dialogue's MET_DOG completion flag)
     * until the hero brings a bone from the tunnel skeleton.  HINT
     * also starts the DOG'S KEY quest (DOG_QUEST=1: the quest tab
     * lists it from the first talk, like the merchant quest starts
     * on its intro).  ASK
     * offers "Give Bone to dog?" (YES/NO): YES resolves through
     * choice_nav_banked.c code 10 (remove BONE, grant OLD_KEY, set
     * DOG_KEY through the real APIs, then the DOG_THANKS follow-up)
     * and clears the dog's ? mark; NO closes plainly.  BUSY replays
     * on later bumps once the key is traded ("The dog is busy
     * gnawing the bone."): the thanks line belongs to the handover
     * moment only.  Scoped to the quest entity (VILLAGE_DOG): the
     * wild dog and the decor dog keep their greeting/silence. */
    {
        EVENT_ID_DOG_HINT, EVENT_TRIGGER_INTERACT, ENTITY_ID_VILLAGE_DOG, EVENT_MAP_ANY,
        2,
        {
            { EVENT_COND_FLAG, STORY_FLAG_ID_DOG_KEY, 0, false, false },
            { EVENT_COND_ITEM_COUNT, CARD_BONE, 0, false, false }
        },
        2,
        {
            { EVENT_ACTION_DIALOGUE, DIALOGUE_ID_DOG_HINT, 0, 0 },
            { EVENT_ACTION_SET_VARIABLE, VARIABLE_ID_DOG_QUEST, 1, 0 }
        }
    },
    {
        EVENT_ID_DOG_ASK, EVENT_TRIGGER_INTERACT, ENTITY_ID_VILLAGE_DOG, EVENT_MAP_ANY,
        2,
        {
            { EVENT_COND_ITEM_COUNT, CARD_BONE, 1, false, true },
            { EVENT_COND_FLAG, STORY_FLAG_ID_DOG_KEY, 0, false, false }
        },
        1,
        {{ EVENT_ACTION_DIALOGUE, DIALOGUE_ID_DOG_ASK, 0, 0 }}
    },
    {
        EVENT_ID_DOG_THANKS, EVENT_TRIGGER_INTERACT, ENTITY_ID_VILLAGE_DOG, EVENT_MAP_ANY,
        1,
        {{ EVENT_COND_FLAG, STORY_FLAG_ID_DOG_KEY, 0, true, false }},
        1,
        {{ EVENT_ACTION_DIALOGUE, DIALOGUE_ID_DOG_BUSY, 0, 0 }}
    },
    /* ── Slime-knight armor split ──────────────────────────────────
     * Felling the armor (ACTOR_DEFEATED, all-match: MONSTER_DEFEATED
     * still counts the kill) reveals the crew inside: the full
     * sentence in a dialogue box (no banner truncation), then the
     * knight's runtime slot becomes a slime-trio encounter in place
     * (EVENT_ACTION_SPAWN_SLIME_TRIO, arg0 = slime ow_type 9 --
     * drift-checked by validate.py --spawn-refs, never hand-synced).
     * NOTE: this is the g_enemy_types TABLE index (plain sorted order),
     * not the blob tile -- the table has one row per type even when two
     * types share art (heal mage shares the wizard tile but keeps its
     * own red-ramp row), so table indices still shift on new types
     * while blob tiles stay put.
     * One-shot by construction (the knight's defeat is persistent;
     * trio kills never re-match this row).  The trio is dynamic
     * (actor_id 0, untracked): it despawns on reload like all runtime
     * state (AGENTS.md §53.1). */
    {
        EVENT_ID_KNIGHT_SPLIT, EVENT_TRIGGER_ACTOR_DEFEATED, ENTITY_ID_SLIME_KNIGHT, EVENT_MAP_ANY,
        0,
        {{ EVENT_COND_NONE, 0, 0, false, false }},
        2,
        {
            { EVENT_ACTION_DIALOGUE, DIALOGUE_ID_KNIGHT_SPLIT, 0, 0 },
            { EVENT_ACTION_SPAWN_SLIME_TRIO, 9, 0, 0 }
        }
    },
    /* ── Village Heal Mage (one free heal, YES/NO ask) ───────────────
     * The mage asks once ("do you want me to heal your wounds?") with a
     * YES/NO choice prompt (DIALOGUE_CHOICE_YES_NO, last line "> YES   NO").
     * The dialogue screen dispatches the game-owned bank-5 body
     * (src/game/heal_mage_banked.c), which owns what the answer means:
     * YES heals the whole party, stages the jingle + telemetry the
     * fixed wrapper owes, and sets HEAL_MAGE_USED; NO stages the
     * "come back when you're hurt" follow-up and sets nothing, so the
     * mage asks again next time.  Once used, talks match REFUSED first
     * (more specific first): the "another heal?" line, no heal, no
     * jingle.  The jingle is the stock MUSIC_CHEST one-shot and restores
     * the town track through the normal bank-8 clock, exactly like a
     * chest loot. */
    /* -- Heal Mage ring lesson (one-shot tutorial) ------------------
     * First talk while owning the Iron Ring: the mage asks whether the
     * hero wants to learn how rings work (YES/NO choice).  YES opens
     * the tactics lesson below (attack joker + defense overblock heal)
     * and its completion flag retires the row, so later talks fall
     * through to the ASK/REFUSED flow; NO falls through to the heal
     * ASK while the free heal is still available (USED unset), else
     * closes plainly.  Placed before both (first-match). */
    {
        EVENT_ID_HEAL_MAGE_RING_LESSON, EVENT_TRIGGER_INTERACT, ENTITY_ID_HEAL_MAGE, EVENT_MAP_ANY,
        2,
        {
            { EVENT_COND_ITEM_COUNT, CARD_IRON_RING, 1, false, true },
            { EVENT_COND_FLAG, STORY_FLAG_ID_RING_LESSON, 0, false, false }
        },
        1,
        {{ EVENT_ACTION_DIALOGUE, DIALOGUE_ID_HEAL_MAGE_RINGS_ASK, 0, 0 }}
    },
    {
        EVENT_ID_HEAL_MAGE_REFUSED, EVENT_TRIGGER_INTERACT, ENTITY_ID_HEAL_MAGE, EVENT_MAP_ANY,
        1,
        {{ EVENT_COND_FLAG, STORY_FLAG_ID_HEAL_MAGE_USED, 0, true, false }},
        1,
        {{ EVENT_ACTION_DIALOGUE, DIALOGUE_ID_HEAL_MAGE_REFUSED, 0, 0 }}
    },
    {
        EVENT_ID_HEAL_MAGE, EVENT_TRIGGER_INTERACT, ENTITY_ID_HEAL_MAGE, EVENT_MAP_ANY,
        1,
        {{ EVENT_COND_FLAG, STORY_FLAG_ID_HEAL_MAGE_USED, 0, false, false }},
        1,
        {{ EVENT_ACTION_DIALOGUE, DIALOGUE_ID_HEAL_MAGE_ASK, 0, 0 }}
    }
};

/* Keep the register-time count in game_ids.h in sync with this table. */
typedef char event_table_count_ok[
    sizeof(g_events) / sizeof(g_events[0]) == GAME_EVENT_COUNT ? 1 : -1
];
