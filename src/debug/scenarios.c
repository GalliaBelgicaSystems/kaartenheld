#include "scenarios.h"
#include "rng.h"
#include "telemetry.h"
#include "audio.h"
#include "screen.h"
#include "interaction.h"
#include "event.h"
#include "dialogue.h"
#include "actor.h"
#include "scene.h"
#include "rpg/deck.h"
#include "rpg/cards.h"
#include "rpg/status.h"
#include "rpg/currency.h"
#include "rpg/progression.h"
#include "rpg/save.h"
#include "content.h"
#include "game_ids.h"
#include "ui.h"
#include "battle.h"
#include "banked.h"

extern Game g_game;

void scenario_load_apply_banked(void);

volatile uint8_t g_scen_load;  /* BSS zero (AGENTS.md 52.27) */
volatile uint8_t g_scen_load_state;  /* BSS zero (AGENTS.md 52.27) */
volatile uint8_t g_debug_action[6];
volatile uint8_t g_debug_action_pending;  /* BSS zero (AGENTS.md 52.27) */
uint8_t g_scen_state_buf[STATE_LOAD_DESC_SIZE];

static void scenario_begin(uint16_t seed)
{
    g_game.frame = 0;
    g_game.game_over_choice = 0;
    rng_set_seed(seed);
#ifdef DEBUG_BUILD
    input_reset();
#endif
    telemetry_init();
    telemetry_set_frame_ptr(&g_game.frame);
    /* Baseline music is the loaded scene's track (runs after
     * world_load_map), so TOWN/CASTLE boots start on their own area theme
     * instead of a hardcoded overworld track. */
    {
        const SceneDefinition *def = scene_definition_for_map(g_game.world.map_id);
        audio_play_music(def ? def->music : MUSIC_OVERWORLD);
    }
}

#define snap_read16(p) (*(const uint16_t *)(p))

static void scenario_load_state(void)
{
    const uint8_t *b = g_scen_state_buf;
    SceneId scene;
    MapId map;
    uint8_t x, y, facing, screen, dialogue_id, start_battle, i;
    uint16_t seed;

    if (b[0] != STATE_LOAD_DESC_VERSION) return;

    screen = b[STATE_LOAD_DESC_SCREEN_OFF];
    scene = (SceneId)b[STATE_LOAD_DESC_SCENE_OFF];
    x = b[STATE_LOAD_DESC_PLAYER_X_OFF];
    y = b[STATE_LOAD_DESC_PLAYER_Y_OFF];
    facing = b[STATE_LOAD_DESC_PLAYER_FACING_OFF];
    seed = snap_read16(b + STATE_LOAD_DESC_SEED_OFF);
    dialogue_id = b[STATE_LOAD_DESC_DIALOGUE_ID_OFF];
    start_battle = b[STATE_LOAD_DESC_START_BATTLE_OFF];
    map = scene_id_to_map(scene);

    game_new_game(&g_game.state);

    /* Descriptor GameState sections (variables, currency, party,
     * collection, deck, world, progression) apply in the bank-5 body
     * so the fixed bank keeps headroom under 0x8000
     * (docs/fixed-bank-move-out.md).  Pure WRAM writes, no fixed
     * calls, no telemetry -- setup semantics per AGENTS.md 53.3. */
    g_bk_call_bank = 5;
    g_bk_call_target = (uint16_t)&scenario_load_apply_banked;
    banked_call_run();

    g_game.state.scene.scene_id = scene;
    g_game.state.scene.player_x = x;
    g_game.state.scene.player_y = y;
    g_game.state.scene.player_facing = facing;
    world_init(&g_game.world, &g_game.state);
    world_load_map(&g_game.world, map, &g_game.state);
    g_game.world.player.position.x = x;
    g_game.world.player.position.y = y;
    g_game.world.player.facing = (Direction)facing;
    g_game.world.encounter_actor_index = NO_ACTOR_INDEX;
    dialogue_init(&g_game.dialogue);

    scenario_begin(seed);

    for (i = 0; i < STATE_LOAD_DESC_FLAGS_SIZE; i++) {
        g_game.state.flags.bytes[i] = b[STATE_LOAD_DESC_FLAGS_OFF + i];
    }
    g_game.game_over_choice = b[STATE_LOAD_DESC_GAME_OVER_CHOICE_OFF];
    g_game.shop_id = 0;
    g_game.prev_screen = SCREEN_OVERWORLD;
    g_game.screen = SCREEN_OVERWORLD;

    if (b[STATE_LOAD_DESC_FONT_TEST_OFF]) {
        ui_draw_font_test();
    }
    if (dialogue_id != DIALOGUE_ID_NONE) {
        dialogue_start_def(&g_game.dialogue, (DialogueId)dialogue_id);
        /* Tell the truth about what was rendered: this scenario boots
         * straight into dialogue without ever rendering the overworld,
         * so prev must read DIALOGUE (not OVERWORLD) or the entry takes
         * the LCD-on overlay over uninitialized VRAM.  Normal NPC
         * entries set prev via screen_change from the live current
         * screen and are unaffected. */
        g_game.prev_screen = SCREEN_DIALOGUE;
        g_game.screen = SCREEN_DIALOGUE;
    }
    if (start_battle) {
        /* Engage the last spawned hostile.  Scenes list their headline
         * hostile last (e.g. CASTLE: wandering BAT, then the gated boss),
         * so this picks the intended encounter without an adjacency scan
         * (fixed-bank budget). */
        for (i = 0; i < MAX_WORLD_ACTORS; i++) {
            if (g_game.world.actors[i].active) {
                g_game.world.encounter_actor_index = i;
            }
        }
        start_battle_from_world(&g_game);
        /* Apply initial status effects (frozen mask) AFTER battle_start()
         * status_reset_battle() which clears g_status_frozen_mask. */
        g_status_frozen_mask = b[STATE_LOAD_DESC_STATUS_OFF];
    }
    if (screen == SCREEN_GAME_OVER || screen == SCREEN_THANKS || screen == SCREEN_ENDING) {
        g_game.screen = (ScreenId)screen;
    }
    if (screen == SCREEN_TITLE) {
        /* Boot into the real title screen so title-menu flow scenarios
         * exercise the actual menu path (INPUT -> index 3 TUTORIAL). */
        g_game.screen = SCREEN_TITLE;
        g_game.prev_screen = SCREEN_TITLE;
        g_game.title_menu_showing = 0;
        g_game.title_menu_index = 0;
        audio_play_music(MUSIC_TITLE);
    }
    if (screen == SCREEN_SPLASH) {
        /* Studio splash boot state (auto-advance / skip scenarios). */
        g_game.screen = SCREEN_SPLASH;
        g_game.prev_screen = SCREEN_SPLASH;
        audio_play_music(MUSIC_TITLE);
    }
    /* NOTE: no SCREEN_GAMBLE boot branch (fixed budget, 52.18):
     * gamble scenarios reach the table through the real path (walk into
     * the fixture kobold, press A through the invite, answer YES), so no
     * loader support is needed.  The banked frame body auto-initializes
     * on entry.  The farewell dialogue is exercised by pressing B. */

    game_render_reset(&g_game);

    debug_snapshot();
}

/* Pure state-write leaf cases live in the bank-5 body
 * (dbg_action_banked.c) so the fixed bank stays under 0x8000
 * (AGENTS.md §52.11.1/§52.18). */
void debug_action_leaf_banked(void);

static void debug_run_action(void)
{
    uint8_t action = g_debug_action[0];
    uint8_t a0 = g_debug_action[1];
    int16_t a1 = (int16_t)snap_read16((const uint8_t *)&g_debug_action[2]);
    uint8_t a2 = g_debug_action[4];
    uint8_t i;
    ProgressionTarget target;
    ProgressionAddResult pres;

    g_bk_call_bank = 5;
    g_bk_call_target = (uint16_t)&debug_action_leaf_banked;
    banked_call_run();
    if (g_bk_byte_a) {
        debug_snapshot();
        return;
    }

    switch (action) {
        case DBG_ACT_ADD_ITEM:
            deck_collection_add(&g_game.state.cards, (CardId)a0, a2);
            break;
        case DBG_ACT_REMOVE_ITEM:
            deck_collection_remove(&g_game.state.cards, (CardId)a0, a2);
            break;
        case DBG_ACT_ADD_CURRENCY:
            currency_add(&g_game.state, (CurrencyId)a0, a1);
            break;
        case DBG_ACT_ADD_PROGRESS:
            target.type = a0;
            target.id = (uint16_t)a1;
            if (progression_add(&g_game.state, a0, (uint16_t)a1, a2, &pres)) {
                game_on_level_up(&g_game.state, target, &pres);
            }
            break;
        case DBG_ACT_BUY_ITEM:
            {
                const CardDefinition *def = card_get_def((CardId)a0);
                if (def && def->price != 0 &&
                    currency_get(&g_game.state, CURRENCY_ID_GOLD) >= def->price) {
                    if (deck_collection_add(&g_game.state.cards, (CardId)a0, 1)) {
                        currency_add(&g_game.state, CURRENCY_ID_GOLD, -(int16_t)def->price);
                        telemetry_emit(EVENT_CARD_PURCHASED, a0, (uint8_t)def->price, 0, 0);
                    } else {
                        telemetry_emit(EVENT_CARD_PURCHASE_FAILED, a0, 2, 0, 0);
                    }
                } else {
                    telemetry_emit(EVENT_CARD_PURCHASE_FAILED, a0, 1, 0, 0);
                }
            }
            break;
        case DBG_ACT_SAVE:
            save_game_slot(a0 < SAVE_SLOT_COUNT ? a0 : 0, &g_game.state);
            break;
        case DBG_ACT_LOAD:
            if (load_game_slot(a0 < SAVE_SLOT_COUNT ? a0 : 0, &g_game.state)) {
                scene_load(&g_game, g_game.state.scene.scene_id,
                           g_game.state.scene.player_x, g_game.state.scene.player_y);
                g_game.world.player.facing = (Direction)g_game.state.scene.player_facing;
            }
            break;
        case DBG_ACT_APPLY_STATUS:
            /* Apply a status through the real mechanic (status_apply,
             * STATUS_APPLIED telemetry included): a0 = combatant slot
             * (0 = player, 1..n = enemy), a1 = StatusId, a2 = duration
             * (stacks 1).  Reaches targets no card rider can hit today
             * (e.g. freezing the PLAYER).  Poison also triggers its grey-out. */
            if (a0 < STATUS_ROUND_SLOTS && a1 != STATUS_NONE) {
                bool ok = status_apply(status_slots(a0), a0, a1, 1, a2);
                telemetry_emit(EVENT_STATUS_RESISTED, 0xEE, a0, ok ? 1 : 0,
                               g_status_frozen_mask); /* PROBE */
                if (ok && a1 == STATUS_POISON) {
                    uint8_t pool = (a0 == 0) ? BATTLE_HAND_SIZE
                                             : g_game.battle.enemy_deck.count;
                    status_grey_apply(a0, pool);
                }
                /* Repaint the tinted row: on hardware every apply is
                 * followed by a phase change (DIRTY_ALL), but debug-action
                 * applies land mid-phase with no widely-dirty redraw. */
                if (ok) {
                    g_game.battle.dirty |= (a0 == 0) ? BATTLE_DIRTY_HERO
                                                     : BATTLE_DIRTY_ENEMIES;
                }
            }
            break;

        case DBG_ACT_DECK_ADD:
            /* Real mechanic call: all deck_add_card validations apply
             * (ownership, SPECIAL exclusion, max_copies, size). */
#ifdef DEBUG_BUILD
            if (deck_add_card(&g_game.state.cards, (CardId)a0)) {
                telemetry_emit(EVENT_CARD_ADDED_TO_DECK, a0, 0, 0, 0);
            }
#else
            (void)deck_add_card(&g_game.state.cards, (CardId)a0);
#endif
            break;
        case DBG_ACT_DECK_REMOVE:
            /* Real mechanic call: membership + DECK_MIN_CARDS floor apply.
             * Both emit on success only (mirroring the UI caller), so
             * scenarios assert acceptance/rejection via event counts. */
#ifdef DEBUG_BUILD
            if (deck_remove_card(&g_game.state.cards, (CardId)a0)) {
                telemetry_emit(EVENT_CARD_REMOVED_FROM_DECK, a0, 0, 0, 0);
            }
#else
            (void)deck_remove_card(&g_game.state.cards, (CardId)a0);
#endif
            break;
        case DBG_ACT_START_BATTLE:
            /* Re-arm an encounter against the first living active actor
             * (mirrors the loader's start_battle path) and enter through the
             * real start_battle_from_world mechanic. */
            for (i = 0; i < MAX_WORLD_ACTORS; i++) {
                if (g_game.world.actors[i].active &&
                    g_game.world.actors[i].hp > 0) {
                    g_game.world.encounter_actor_index = i;
                    break;
                }
            }
            start_battle_from_world(&g_game);
            break;
        /* SET_HAND_CARD / SET_HAND_CARD_META / SET_HAND_CARD_STATUS /
         * SET_HAND_RING / SET_ENEMY_HP / SET_FILTER / SET_SORT are pure
         * state writes handled by the bank-5 leaf body above; no case
         * here (the body's out code short-circuits before the switch). */
        default:
            break;
    }
    debug_snapshot();
}

void scenario_check_and_load(void)
{
    if (g_scen_load_state) {
        g_scen_load_state = 0;
        scenario_load_state();
    }
    if (g_debug_action_pending) {
        g_debug_action_pending = 0;
        debug_run_action();
    }
    g_scen_load = 0;
}
