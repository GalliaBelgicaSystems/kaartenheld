#pragma bank 11

#include "battle.h"
#include "banked.h"
#include "rpg/status.h"
#include "rpg/effects.h"
#include "rng.h"
#include "telemetry.h"
#include "telemetry_ring.h"
#include "game.h"

/* Burn tick from the inflicting hit's damage: 10% per round, min 1.
 * Subtract loop instead of / (which would lower to the fixed-bank div
 * library, unreachable while bank 11 is mapped; AGENTS.md 52.11.1).
 * Mirrors burn_tick() in status_content.c (bank 10). */
static uint8_t cu_burn_tick(uint8_t basis)
{
    uint8_t t = 0;
    while (basis >= 10) {
        basis = (uint8_t)(basis - 10);
        t++;
    }
    if (t == 0) t = 1;
    return t;
}

/* ── Banked post-swing counter rider (ROM bank 11) ─────────────────────
 * The dagger counter (battle_defend_counter, battle.c) stashes its
 * eval-scaled rider chance in g_bk_byte_e; the fixed caller dispatches
 * here after the swing resolves and the hand discards, so a poison
 * applied by the counter never confuses the swing it rode in on (only
 * pre-existing poison redirects -- see battle_defend_content.c).  Only
 * burn/poison riders reach here with a live victim (the fixed caller
 * gates on counter_hit; a killing counter never dispatches).
 *
 * Self-contained: WRAM only, plus direct calls into fixed-bank WR break
 * helpers and the header-inline ring writer (telemetry_ring.h, the
 * same pattern bank-10 bodies use; fixed code is always mapped, so a
 * banked->fixed call is safe, but a trampolined call would nest inside
 * this dispatch and resume on the wrong bank; AGENTS.md 52.11.1).  The
 * RNG rolls inline (xorshift on the WRAM stream, same single step
 * as rng_next_banked).  Stacking/refresh mirrors status_apply_banked
 * (bank 10); burn keeps the stronger tick.  Freeze riders cannot occur
 * here (loot legality: only bows roll freeze, never daggers), so no
 * grey-out path is needed.  Runs with interrupts enabled (timer ISR
 * saves/restores the live bank); no di/ei here (AGENTS.md 52.11.3). */
void battle_counter_rider_banked(void)
{
    Battle *b = &g_game.battle;
    uint8_t atk;
    uint8_t rid;
    uint8_t i;
    StatusSlots *slots;
    uint8_t max_stacks;
    uint8_t duration;
    if (b->attacking_enemy_idx < b->enemy_count)
        atk = b->attacking_enemy_idx;
    else
        atk = 0;
    rid = b->last_combo.cards[0].status_id;
    if (rid != STATUS_POISON && rid != STATUS_BURN) return;
    if (b->enemies[atk].hp == 0) return;
    if (rid == STATUS_POISON) {
        max_stacks = 5;
        duration = 2;
    } else {
        max_stacks = 3;
        duration = 3;
    }
    g_rng_state ^= (uint16_t)(g_rng_state << 7);
    g_rng_state ^= (uint16_t)(g_rng_state >> 9);
    g_rng_state ^= (uint16_t)(g_rng_state << 8);
    if ((uint8_t)g_rng_state >= g_bk_byte_e) {
        /* Resisted (docs/combo-system.md §19). */
        telemetry_ring_emit(EVENT_STATUS_RESISTED, rid, (uint8_t)(atk + 1),
                            0, 0);
        return;
    }
    slots = status_slots((uint8_t)(atk + 1));
    if (!slots) return;
    for (i = 0; i < slots->count; i++) {
        if (slots->slot[i].id == rid) {
            uint16_t total = (uint16_t)(slots->slot[i].stacks + 1);
            uint8_t nt;
            if (total > max_stacks) total = max_stacks;
            slots->slot[i].stacks = (uint8_t)total;
            slots->slot[i].duration = duration;
            if (rid == STATUS_BURN) {
                nt = cu_burn_tick(g_effect_last.amount);
                if (nt > slots->slot[i].tick) slots->slot[i].tick = nt;
            }
            telemetry_ring_emit(EVENT_STATUS_APPLIED, rid,
                                slots->slot[i].stacks, duration, 0);
            return;
        }
    }
    if (slots->count >= MAX_STATUSES_PER_COMBATANT) return;
    slots->slot[slots->count].id = rid;
    slots->slot[slots->count].stacks = 1;
    slots->slot[slots->count].duration = duration;
    slots->slot[slots->count].tick =
        (rid == STATUS_BURN) ? cu_burn_tick(g_effect_last.amount) : 0;
    slots->count++;
    telemetry_ring_emit(EVENT_STATUS_APPLIED, rid, 1, duration, 0);
}
