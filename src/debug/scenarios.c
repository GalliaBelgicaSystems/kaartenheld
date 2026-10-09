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

extern Game g_game;

volatile uint8_t g_scen_load = 0;
volatile uint8_t g_scen_load_state = 0;
volatile uint8_t g_debug_action[6];
volatile uint8_t g_debug_action_pending = 0;
uint8_t g_scen_state_buf[STATE_LOAD_DESC_SIZE];

enum {
    DBG_ACT_ADD_ITEM = 1,
    DBG_ACT_REMOVE_ITEM = 2,
    DBG_ACT_ADD_CURRENCY = 3,
    DBG_ACT_ADD_PROGRESS = 4,
    DBG_ACT_BUY_ITEM = 5,
    DBG_ACT_USE_ITEM = 6,
    DBG_ACT_EQUIP_ITEM = 7,
    DBG_ACT_SAVE = 8,
    DBG_ACT_LOAD = 9,
    DBG_ACT_SET_HAND_CARD = 10,
    DBG_ACT_SET_FILTER = 11,
    DBG_ACT_SET_SORT = 12,
    DBG_ACT_DECK_ADD = 13,
    DBG_ACT_SET_HAND_CARD_META = 14,
    DBG_ACT_START_BATTLE = 15,
    DBG_ACT_DECK_REMOVE = 16,
    DBG_ACT_SET_HAND_CARD_STATUS = 17,
    DBG_ACT_SET_ENEMY_HP = 18,
    DBG_ACT_APPLY_STATUS = 19,
    DBG_ACT_SET_HAND_RING = 20
};

static void scenario_begin(uint16_t seed)
{
    g_game.frame = 0;
    g_game.game_over_choice = 0;
    rng_set_seed(seed);
    input_reset();
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
    const uint8_t *p;
    uint8_t *dst;
    SceneId scene;
    MapId map;
    uint8_t x, y, facing, screen, dialogue_id, start_battle, i, n;
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

    p = b + STATE_LOAD_DESC_VARIABLES_ENTRY_OFF;
    for (i = 0; i < b[STATE_LOAD_DESC_VARIABLES_COUNT_OFF]; i++) {
        uint8_t vid = *p++;
        int16_t val = (int16_t)snap_read16(p);
        p += 2;
        if (vid >= 1 && vid <= MAX_STATE_VARIABLES) {
            g_game.state.variables.values[vid - 1] = val;
        }
    }

    p = b + STATE_LOAD_DESC_CURRENCY_ENTRY_OFF;
    for (i = 0; i < b[STATE_LOAD_DESC_CURRENCY_COUNT_OFF]; i++) {
        uint8_t cid = *p++;
        int16_t amt = (int16_t)snap_read16(p);
        p += 2;
        if (cid >= 1 && cid <= MAX_CURRENCIES) {
            g_game.state.currency.amount[cid - 1] = amt;
        }
    }

    p = b + STATE_LOAD_DESC_PARTY_ENTRY_OFF;
    for (i = 0; i < b[STATE_LOAD_DESC_PARTY_COUNT_OFF]; i++) {
        g_game.state.party.members[i].id = (CharacterId)*p++;
        g_game.state.party.members[i].hp = *p++;
        g_game.state.party.members[i].max_hp = *p++;
        g_game.state.party.count = (uint8_t)(i + 1);
    }

    p = b + STATE_LOAD_DESC_INVENTORY_ENTRY_OFF;
    for (i = 0; i < b[STATE_LOAD_DESC_INVENTORY_COUNT_OFF]; i++) {
        deck_collection_add(&g_game.state.cards, (CardId)*p, *(p + 1));
        p += 2;
    }

    /* Deck section: only applied when the host marks it present, so
     * scenarios without "deck" keep the starter default.  Direct setup
     * writes (no mechanic calls, no telemetry) per AGENTS.md 53.3.
     * An explicit empty deck (count 0) is allowed: it is how scenarios
     * reach battle_start's packed fallback-deck path now that the CARDS
     * menu enforces DECK_MIN_CARDS. */
    if (b[STATE_LOAD_DESC_DECK_PRESENT_OFF]) {
        n = b[STATE_LOAD_DESC_DECK_COUNT_OFF];
        if (n > MAX_DECK_CARDS) n = MAX_DECK_CARDS;
        p = b + STATE_LOAD_DESC_DECK_ENTRY_OFF;
        dst = g_game.state.cards.deck.cards;
        i = n;
        while (i) {
            *dst++ = *p++;
            i--;
        }
        g_game.state.cards.deck.count = n;
    }

    p = b + STATE_LOAD_DESC_WORLD_ENTRY_OFF;
    for (i = 0; i < b[STATE_LOAD_DESC_WORLD_COUNT_OFF]; i++) {
        g_game.state.world.actors[i].actor_id = (ActorId)snap_read16(p);
        p += 2;
        g_game.state.world.actors[i].state = *p++;
        g_game.state.world.count = (uint8_t)(i + 1);
    }

    p = b + STATE_LOAD_DESC_PROGRESSION_ENTRY_OFF;
    for (i = 0; i < b[STATE_LOAD_DESC_PROGRESSION_COUNT_OFF]; i++) {
        uint8_t t_type = *p++;
        uint16_t t_id = snap_read16(p);
        p += 2;
        progression_ensure(&g_game.state, t_type, t_id, *p, snap_read16(p + 1));
        p += 3;
    }
    /* equipment field removed; skipped */

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
    g_game.shop_id = 1;
    g_game.prev_screen = SCREEN_OVERWORLD;
    g_game.screen = SCREEN_OVERWORLD;

    if (b[STATE_LOAD_DESC_FONT_TEST_OFF]) {
        ui_draw_font_test();
    }
    if (dialogue_id != DIALOGUE_ID_NONE) {
        dialogue_start_def(&g_game.dialogue, (DialogueId)dialogue_id);
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

    game_render_reset(&g_game);

    if (dialogue_id != DIALOGUE_ID_NONE) {
        g_game.render_cache.prev_screen = SCREEN_DIALOGUE;
    }
    debug_snapshot();
}

static void debug_run_action(void)
{
    uint8_t action = g_debug_action[0];
    uint8_t a0 = g_debug_action[1];
    int16_t a1 = (int16_t)snap_read16((const uint8_t *)&g_debug_action[2]);
    uint8_t a2 = g_debug_action[4];
    uint8_t i;
    ProgressionTarget target;
    ProgressionAddResult pres;

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
        case DBG_ACT_SET_HAND_CARD:
            if (a0 < BATTLE_HAND_SIZE) {
                uint8_t t = (uint8_t)((uint16_t)a1 & 0xFF);
                uint8_t fx = CARD_EFFECT_DAMAGE_TARGET;
                /* Injected cards bypass CardDefinition lookup; give them
                 * the default effect for their type so effect resolution
                 * sees a complete card.  Debug-only mapping (this file is
                 * excluded from the release ROM). */
                if (t == BATTLE_CARD_TYPE_SHIELD) {
                    fx = CARD_EFFECT_BLOCK_DAMAGE;
                } else if (t == BATTLE_CARD_TYPE_HEAL) {
                    fx = CARD_EFFECT_HEAL_HP;
                }
                g_game.battle.hand[a0].type = (BattleCardType)t;
                g_game.battle.hand[a0].value = a2;
                g_game.battle.hand[a0].effect = fx;
                /* Injected cards carry no status rider; reset both fields
                 * so a previously injected rider cannot leak into a slot
                 * that was played earlier in the same battle. */
                g_game.battle.hand[a0].status_id = STATUS_NONE;
                g_game.battle.hand[a0].status_chance = 0;
                g_game.battle.hand[a0].ring = 0;
            }
            break;

        case DBG_ACT_SET_HAND_CARD_STATUS:
            /* Pin the injected hand card's on-hit status rider (Phase C
             * scenarios).  a1 = StatusId, a2 = roll chance (1/255). */
            if (a0 < BATTLE_HAND_SIZE) {
                g_game.battle.hand[a0].status_id = a1;
                g_game.battle.hand[a0].status_chance = a2;
            }
            break;
        case DBG_ACT_SET_ENEMY_HP:
            /* Pin an enemy combatant's current HP (battle must be active).
             * Direct state write, no telemetry: scenario setup semantics
             * (AGENTS.md §53.3). */
            if (g_game.battle.enemy_count > a0) {
                g_game.battle.enemies[a0].hp = a1;
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
            }
            break;

        case DBG_ACT_SET_HAND_RING:
            /* Mark an injected hand card as a loot RING (docs/loot.md
             * §34.3) so joker/gate scenarios don't depend on the dealt
             * deck contents. */
            if (a0 < BATTLE_HAND_SIZE) {
                g_game.battle.hand[a0].ring = 1;
            }
            break;

        case DBG_ACT_DECK_ADD:
            /* Real mechanic call: all deck_add_card validations apply
             * (ownership, SPECIAL exclusion, max_copies, size). */
            if (deck_add_card(&g_game.state.cards, (CardId)a0)) {
                telemetry_emit(EVENT_CARD_ADDED_TO_DECK, a0, 0, 0, 0);
            }
            break;
        case DBG_ACT_DECK_REMOVE:
            /* Real mechanic call: membership + DECK_MIN_CARDS floor apply.
             * Both emit on success only (mirroring the UI caller), so
             * scenarios assert acceptance/rejection via event counts. */
            if (deck_remove_card(&g_game.state.cards, (CardId)a0)) {
                telemetry_emit(EVENT_CARD_REMOVED_FROM_DECK, a0, 0, 0, 0);
            }
            break;
        case DBG_ACT_SET_HAND_CARD_META:
            /* Companion to SET_HAND_CARD: pins the injected hand card's
             * energy cost and remaining uses so combo/energy scenarios are
             * independent of the dealt deck contents. */
            if (a0 < BATTLE_HAND_SIZE) {
                g_game.battle.hand[a0].cost = a1;
                g_game.battle.hand[a0].uses_remaining = a2;
            }
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
        case DBG_ACT_SET_FILTER:
            g_game.item_menu_filter = a0;
            g_game.render_cache.valid = false;
            break;
        case DBG_ACT_SET_SORT:
            g_game.item_menu_sort = a0;
            g_game.render_cache.valid = false;
            break;
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
