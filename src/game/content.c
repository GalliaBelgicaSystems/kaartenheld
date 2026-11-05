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

#define HERO_START_HP    15
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

    /* Cross-session outro state is NOT GameState (module WRAM) and would
     * otherwise leak across a LOAD/RESET mid-session (AGENTS.md §20):
     * a fresh game never starts demo-completed, mid-despair, or
     * partway through the THANKS hold.  BSS zero covers boot; this
     * covers scenario_load_state() and every new-game path. */
    g_demo_completed = 0;
    g_slime_despair = 0;

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
    /* Base energy pool: every milestone (tutorial, chest, Monster Hunt,
     * mimic) grants a uniform ADD 1 from here, reaching 5 in any
     * completion order.
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

void game_slime_despair_outro(void)
{
    /* The battle screen guarantees a live game; the single global Game
     * instance is always the battle owner (main.c drives everything from
     * &g_game).  Wakes in the demo-end wake scene (vast empty
     * underground -- the finale backdrop, reachable only through this
     * teleport: the wake dialogue opens immediately and closes straight
     * to the ENDING screen, so the player never walks here).  The defeat
     * whites out first (banked palette fade on the result screen), then
     * the teleport lands and the wake dialogue's first-enter fades the
     * area back in -- no LCD change here (the dialogue entry performs
     * the full LCD-off redraw after the frame's vsync(); turning the LCD
     * off in update context hangs vsync() -- no VBlank while the LCD is
     * off -- stranding the release ROM on a white screen with the area
     * track playing, a hang the harness never sees (it skips vsync)). */
    g_bk_call_bank = 10;
    g_bk_call_target = (uint16_t)&game_despair_fade_out_banked;
    banked_call_run();
#ifdef TEST_LEVELS
    scene_load(&g_game, SCENE_TEST_FOREST, 12, 10);
#else
    scene_load(&g_game, SCENE_DEMO_END_WAKE_SCENE, 12, 10);
#endif
    /* scene_load leaves facing untouched, so route it explicitly to the
     * authored spawn facing (UP) instead of the engage direction. */
    g_game.world.player.facing = DIRECTION_UP;
    dialogue_start_def(&g_game.dialogue, (DialogueId)DIALOGUE_ID_STRANGE_WAKE);
    screen_change(&g_game, SCREEN_DIALOGUE);
}
