#pragma bank 10
#pragma disable_warning 110

#include "battle.h"
#include "banked.h"
#include "rpg/status.h"
#include "telemetry.h"

/* ── Enemy telegraph card draw (ROM bank 10) ─────────────────────────
 * Hosts battle_update()'s ANIM/SHUFFLE-tail telegraph block out of the
 * fixed bank (which sits hard against 0x8000, AGENTS.md §52.18).  The
 * fixed wrapper in battle.c stages the Battle* through the WRAM
 * trampoline; the SFX tail (SFX_HIT) and the delay stay in the fixed
 * wrapper.
 *
 * Self-contained (AGENTS.md §52.11.1): reads/writes only the staged
 * Battle* plus battle-scoped WRAM (grey masks, frozen mask, shake
 * timers), never calls fixed-bank functions.  Telemetry appends run
 * inline on the ring buffer (debug builds only) -- a banked body must
 * not dispatch another banked body (banked.h non-reentrancy), so the
 * bank-2 append body is replicated field-for-field here instead of
 * called; keep in sync with telemetry_emit_banked.c.  Field-wise
 * EnemyCard copies only: SDCC lowers struct assignment to the
 * RAM-resident __memcpy helper, unreachable while a bank is mapped
 * (see battle_turn_draw_banked.c).
 *
 * Poison grey-out (Phase D): a greyed deck position never plays.  The
 * body still peeks the greyed card into enemy_played_card (the foe
 * "selects" it) and announces it via ENEMY_CARD_PLAYED, then the swing
 * fails: incoming 0, TURN_SKIPPED (cause POISON), the attacker judders
 * like a hit victim (shake mask + 12-frame timer, read by the bank-3
 * BG judder and the bank-5 OAM judder), and msg 5 stages the
 * "FOE IS POISONED!" beat the bank-3 banner/combo painters read.  The
 * draw position does NOT advance, so an enemy whose lead card is greyed
 * stays locked out for the grey-out duration (docs/combo-system.md).
 * Freeze keeps its silent skip (no card, no message). */

#ifdef DEBUG_BUILD
static void telegraph_emit(uint8_t type, uint8_t d0, uint8_t d1,
                           uint8_t d2, uint8_t d3)
{
    GameEvent *ev = &g_telemetry_buffer[g_telemetry_head];
    ev->seq = g_telemetry_seq++;
    ev->frame = g_telemetry_frame_ptr ? *g_telemetry_frame_ptr : 0;
    ev->type = type;
    ev->data[0] = d0;
    ev->data[1] = d1;
    ev->data[2] = d2;
    ev->data[3] = d3;

    g_telemetry_head = (uint8_t)((g_telemetry_head + 1) & (MAX_TELEMETRY_EVENTS - 1));
    if (g_telemetry_count < MAX_TELEMETRY_EVENTS) {
        g_telemetry_count++;
    }
}
#else
#define telegraph_emit(type, d0, d1, d2, d3) ((void)0)
#endif

void battle_telegraph_banked(void)
{
    /* Volatile access throughout (same SDCC pointer-cache workaround as
     * battle_update in battle.c): the optimizer reuses cached
     * &b->field stack slots across the branches below. */
    volatile Battle *b = (volatile Battle *)g_bk_ptr_a;
    uint8_t grey_mask = 0;
    uint8_t gi;
    uint8_t atk;
    uint8_t di;

    if (!b) return;
    b->phase = BATTLE_PHASE_ENEMY_TELEGRAPH;
    b->turn = BATTLE_TURN_ENEMY;
    /* gi doubles as the advance counter (stack discipline). */
    gi = 0;
    while (gi < b->enemy_count && b->enemies[b->attacking_enemy_idx].hp == 0) {
        /* Wrap instead of % -- keeps the SDCC int-mod library out. */
        b->attacking_enemy_idx++;
        if (b->attacking_enemy_idx >= b->enemy_count) {
            b->attacking_enemy_idx = 0;
        }
        gi++;
    }
    atk = b->attacking_enemy_idx;
    /* The enemy deck and its draw position are SHARED by the whole
     * hostile party (every encounter engages as a trio).  A poisoned
     * victim's grey-out therefore applies to the shared deck, not to
     * one attacker's slot: a greyed deck position never plays, no
     * matter which enemy is up.  OR the per-victim masks (they carry
     * independent durations, so the union is only the currently
     * active grey set). */
    for (gi = 1; gi <= b->enemy_count; gi++) {
        grey_mask |= s_grey_mask[gi];
    }
    /* STATUS_FREEZE (docs/combo-system.md §12): the frozen-mask
     * bit is TESTED here but never consumed -- status_apply sets
     * it and status_tick_banked clears it each round and re-sets
     * it while a freeze instance remains.  Mask bits follow status
     * SLOTS (player = 0, enemies = 1..n).  A frozen attacker skips
     * its swing: no enemy card, nothing incoming this cycle. */
    if ((g_status_frozen_mask &
         (uint8_t)(1u << (atk + 1))) != 0 &&
        b->enemies[atk].hp != 0) {
        b->enemy_incoming_dmg = 0;
        telegraph_emit(EVENT_TURN_SKIPPED,
                       (uint8_t)(atk + 1),
                       STATUS_FREEZE, 0, 0);
    } else if (b->enemy_deck.count > 0 &&
               (grey_mask &
                (uint8_t)(1u << b->enemy_deck.draw_idx)) != 0 &&
               b->enemies[atk].hp != 0) {
        /* Poison-wracked swing: the foe selects its (greyed) card but
         * cannot play it -- no advance, nothing incoming.  The peek
         * announces the selection, the shake judders the attacker, and
         * msg 5 stages the failed-swing beat. */
        di = b->enemy_deck.draw_idx;
        b->enemy_played_card.type = b->enemy_deck.cards[di].type;
        b->enemy_played_card.value = b->enemy_deck.cards[di].value;
        b->enemy_played_card.status_id = b->enemy_deck.cards[di].status_id;
        b->enemy_played_card.status_chance = b->enemy_deck.cards[di].status_chance;
        b->enemy_incoming_dmg = 0;
        telegraph_emit(EVENT_ENEMY_CARD_PLAYED,
                       di,
                       b->enemy_played_card.type,
                       b->enemy_played_card.value, 0);
        telegraph_emit(EVENT_TURN_SKIPPED,
                       (uint8_t)(atk + 1),
                       STATUS_POISON, 0, 0);
        b->msg_id = BATTLE_MSG_POISONED_FOE;
        b->msg_ttl = 0;
        g_battle_shake_mask |= (uint8_t)(1u << atk);
        g_battle_shake_timer = 12;
    } else if (b->enemy_deck.count > 0) {
        di = b->enemy_deck.draw_idx;
        b->enemy_played_card.type = b->enemy_deck.cards[di].type;
        b->enemy_played_card.value = b->enemy_deck.cards[di].value;
        b->enemy_played_card.status_id = b->enemy_deck.cards[di].status_id;
        b->enemy_played_card.status_chance = b->enemy_deck.cards[di].status_chance;
        b->enemy_incoming_dmg = b->enemy_played_card.value;
        telegraph_emit(EVENT_ENEMY_CARD_PLAYED,
                       di,
                       b->enemy_played_card.type,
                       b->enemy_played_card.value, 0);
        b->enemy_deck.draw_idx++;
        if (b->enemy_deck.draw_idx >= b->enemy_deck.count) {
            b->enemy_deck.draw_idx = 0;
        }
    } else {
        b->enemy_incoming_dmg = 6;
        /* No enemy card: the fallback flat 6-damage swipe still
         * marks an inbound strike.  Only deckless battlers (the
         * Lord of Slimes) reach this: three SH2 shields (3 x 2 = 6)
         * or one steel shield (power 6, stocked by the merchant)
         * blocks it exactly (docs/enemies_ranking.md §5.2). */
    }
}
