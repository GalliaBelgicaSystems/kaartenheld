#pragma bank 11

#include "battle.h"
#include "banked.h"
#include "rpg/status.h"
#include "rng.h"
#include "telemetry.h"
#include "telemetry_ring.h"
#include "game.h"

/* ── Banked post-swing counter poison rider (ROM bank 11) ────────────
 * The dagger counter (battle_defend_counter, battle.c) stashes its
 * eval-scaled poison chance in g_bk_byte_e and returns 2; the fixed
 * caller dispatches here AFTER the swing resolves and the hand
 * discards (only while the hero still stands), so a poison applied by
 * the counter never confuses the swing it rode in on (only
 * pre-existing poison redirects -- see battle_defend_content.c).
 *
 * Poison-only: burn/freeze riders run immediately in
 * battle_defend_counter via battle_leader_rider() (deferring burn past
 * battle_defend_resolve would also lose g_effect_last.amount if an
 * enemy's organic rider ran status_apply_banked in between).  A killing
 * or despair-floor (0-HP) counter returns 1, so it never dispatches
 * here or reads stale g_bk_byte_e.
 *
 * Self-contained: WRAM only (`g_game.battle`, `s_battle_status`,
 * `g_rng_state`, `g_bk_byte_e`) plus the header-inline ring writer
 * (telemetry_ring.h, same pattern bank-10 bodies use); zero fixed-bank
 * calls (never calls status_slots() while banked; AGENTS.md 52.11.1).
 * The RNG rolls inline (xorshift on the WRAM stream, same single step
 * as rng_next_banked).  Stacking/refresh mirrors status_apply_banked
 * via shared STATUS_POISON_MAX_STACKS / STATUS_POISON_DURATION
 * constants.  Runs with interrupts enabled (timer ISR saves/restores
 * the live bank); no di/ei here (AGENTS.md 52.11.3). */
void battle_counter_rider_banked(void)
{
    Battle *b = &g_game.battle;
    uint8_t atk;
    uint8_t rid;
    uint8_t i;
    StatusSlots *slots;
    if (b->player.hp == 0) return;
    if (b->attacking_enemy_idx < b->enemy_count)
        atk = b->attacking_enemy_idx;
    else
        atk = 0;
    rid = b->last_combo.cards[0].status_id;
    if (rid != STATUS_POISON) return;
    if (b->enemies[atk].hp == 0) return;
    if ((uint8_t)(atk + 1) >= STATUS_ROUND_SLOTS) return;
    g_rng_state ^= (uint16_t)(g_rng_state << 7);
    g_rng_state ^= (uint16_t)(g_rng_state >> 9);
    g_rng_state ^= (uint16_t)(g_rng_state << 8);
    if ((uint8_t)g_rng_state >= g_bk_byte_e) {
        /* Resisted (docs/combo-system.md §19). */
        telemetry_ring_emit(EVENT_STATUS_RESISTED, STATUS_POISON,
                            (uint8_t)(atk + 1), 0, 0);
        return;
    }
    slots = &s_battle_status[(uint8_t)(atk + 1)];
    for (i = 0; i < slots->count; i++) {
        if (slots->slot[i].id == STATUS_POISON) {
            uint16_t total = (uint16_t)(slots->slot[i].stacks + 1);
            if (total > STATUS_POISON_MAX_STACKS) {
                total = STATUS_POISON_MAX_STACKS;
            }
            slots->slot[i].stacks = (uint8_t)total;
            slots->slot[i].duration = STATUS_POISON_DURATION;
            telemetry_ring_emit(EVENT_STATUS_APPLIED, STATUS_POISON,
                                slots->slot[i].stacks,
                                STATUS_POISON_DURATION, 0);
            return;
        }
    }
    if (slots->count >= MAX_STATUSES_PER_COMBATANT) return;
    slots->slot[slots->count].id = STATUS_POISON;
    slots->slot[slots->count].stacks = 1;
    slots->slot[slots->count].duration = STATUS_POISON_DURATION;
    slots->slot[slots->count].tick = 0;
    slots->count++;
    telemetry_ring_emit(EVENT_STATUS_APPLIED, STATUS_POISON, 1,
                        STATUS_POISON_DURATION, 0);
}
