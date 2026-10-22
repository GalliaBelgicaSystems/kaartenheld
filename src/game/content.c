#include "content.h"
#include "game_ids.h"
#include "story.h"
#include "event.h"
#include "dialogue.h"
#include "actor.h"
#include "scene.h"
#include "rpg/party.h"
#include "rpg/deck.h"
#include "rpg/loot.h"
#include "core/game.h"
#include "banked.h"
#include "battle_data.h"
#include "audio.h"

#define HERO_START_HP    10
#define HERO_START_GOLD  20

/* ── Victory loot drop (docs/loot.md §17/§34.5) ─────────────────────
 * Thin fixed-bank wrapper: the whole decision (profile pick, roll,
 * encode) runs as ONE bank-9 body (loot_drop_banked.c); only this
 * staging call stays fixed.  Returns the derived CardId (always drops
 * -- quality varies, not presence). */
uint8_t game_loot_drop(uint8_t battle_type)
{
    g_bk_call_bank = 9;
    g_bk_call_target = (uint16_t)&game_loot_drop_banked;
    g_bk_byte_a = battle_type;
    banked_call_run();
    return g_loot_id;
}

void game_content_init(void)
{
    story_init(STORY_FLAG_ID_COUNT);
    game_events_register();
    game_dialogue_register();
    game_actors_register();
    game_cards_register();
}

void game_new_game(GameState *state)
{
    if (!state) return;
    game_state_zero(state);

    /* Player start is compiled from the wake-grove spawn (the single source of
     * truth).  scene_spawn() runs the banked table read; the fallback for
     * bad map ids lives in that banked body, so this fixed-bank call site
     * stays branch-free (fixed-bank _CODE budget).  Harness builds start
     * in the frozen fixture field, never the real content. */
#ifdef TEST_LEVELS
    state->scene.scene_id = SCENE_TEST_FIELD;
    scene_spawn(MAP_TEST_FIELD);
#else
    state->scene.scene_id = SCENE_WAKE_GROVE;
    scene_spawn(MAP_WAKE_GROVE);
#endif
    state->scene.player_x = g_bk_byte_b;
    state->scene.player_y = g_bk_byte_c;
    state->scene.player_facing = g_bk_byte_d;

    state->party.count = 1;
    state->party.members[0].id = CHARACTER_HERO;
    state->party.members[0].hp = HERO_START_HP;
    state->party.members[0].max_hp = HERO_START_HP;

    state->variables.values[VARIABLE_ID_CHAPTER - 1] = 1;
    /* Base energy pool: every milestone (tutorial, chest, Monster Hunt) grants
     * a uniform ADD 1 from here, reaching 4 in any completion order.
     * Initializing (not relying on the stager's < 1 fallback) keeps the
     * variable equal to the visible pool, so staged values, snapshots
     * and the debugger never disagree about what a fight will stage. */
    state->variables.values[VARIABLE_ID_ENERGY_POOL - 1] = 1;
    state->currency.amount[CURRENCY_ID_GOLD - 1] = HERO_START_GOLD;

    /* Starter deck (docs/deck-management.md §1), granted from the generated
     * hero table (screens/hero.json via battle_compile.py) so the editor
     * owns the contents: each entry is granted once to the collection and
     * once to the draw pile, preserving exact draw order.  The bank-2
     * table is staged one byte at a time (no large stack or WRAM scratch
     * needed); deck_add_card/deck_collection_add enforce max_copies. */
    {
        uint8_t n = 0;
        uint8_t i;
        CardId id = CARD_NONE;
        banked_copy(2, &n, &g_hero_starter_deck_count, 1);
        if (n > MAX_DECK_CARDS) n = MAX_DECK_CARDS;
        for (i = 0; i < n; i++) {
            banked_copy(2, &id, &g_hero_starter_deck_ids[i], 1);
            deck_collection_add(&state->cards, id, 1);
        }
        for (i = 0; i < n; i++) {
            banked_copy(2, &id, &g_hero_starter_deck_ids[i], 1);
            deck_add_card(&state->cards, id);
        }
    }
}

void game_on_level_up(GameState *state, ProgressionTarget target,
                      const ProgressionAddResult *result)
{
    CharacterState *hero;
    uint8_t gained;

    if (!state || !result || !result->crossed) return;
    gained = (uint8_t)(result->level_after - result->level_before);
    if (gained == 0) return;

    if (target.type == PROG_TYPE_HERO) {
        hero = party_get_member(&state->party, CHARACTER_HERO);
        if (hero) {
            hero->max_hp = (uint8_t)(hero->max_hp + (gained << 1));
            hero->hp = hero->max_hp;
        }
    }
}

ScreenId game_screen_after_victory(const Game *g)
{
    if (!g) return SCREEN_OVERWORLD;
    if (game_variable_get(&g->state, VARIABLE_ID_ENDING_SHOWN) != 0) {
        return SCREEN_ENDING;
    }
    return SCREEN_OVERWORLD;
}
