#pragma bank 5

#include "scenarios.h"
#include "battle.h"
#include "rpg/cards.h"
#include "rpg/status.h"
#include "banked.h"

/* ── Debug-action leaf cases, bank-5 body ──────────────────────────
 * Dispatched from debug_run_action() (fixed-bank cost: staging + call
 * -- the fixed bank sits hard against 0x8000, AGENTS.md §52.18).  Only
 * the pure state-write cases live here: they touch WRAM + ROM consts
 * only and call no fixed-bank function, so they are safe behind the
 * trampoline (AGENTS.md §52.11.1).  Cases that run real mechanics
 * (deck/currency/progression/save/status/battle entry) stay in the
 * fixed switch: they call fixed-bank functions a banked body cannot
 * reach.  Reports 1 when the action was handled, 0 otherwise (the
 * fixed switch keeps the remaining cases).
 *
 * Self-contained: reads g_debug_action / g_game directly (WRAM, always
 * mapped), no staging, no fixed calls.  Action ids are the host wire
 * protocol (tools/emulator.py session.DBG_ACT_*): never renumber. */

static uint16_t leaf_read16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

void debug_action_leaf_banked(void)
{
    uint8_t action;
    uint8_t a0;
    int16_t a1;
    uint8_t a2;

    action = g_debug_action[0];
    a0 = g_debug_action[1];
    a1 = (int16_t)leaf_read16((const uint8_t *)&g_debug_action[2]);
    a2 = g_debug_action[4];

    switch (action) {
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

        case DBG_ACT_SET_HAND_RING:
            /* Mark an injected hand card as a loot RING (docs/loot.md
             * §34.3) so joker/gate scenarios don't depend on the dealt
             * deck contents. */
            if (a0 < BATTLE_HAND_SIZE) {
                g_game.battle.hand[a0].ring = 1;
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
        case DBG_ACT_SET_FILTER:
            g_game.item_menu_filter = a0;
            g_game.render_cache.valid = false;
            break;
        case DBG_ACT_SET_SORT:
            g_game.item_menu_sort = a0;
            g_game.render_cache.valid = false;
            break;
        default:
            g_bk_byte_a = 0;
            return;
    }
    g_bk_byte_a = 1;
}
