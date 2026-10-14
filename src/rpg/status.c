#include "status.h"
#include "banked.h"
#include "telemetry.h"

/* Fixed-bank wrappers around the bank-3 bodies (src/rpg/status_content.c),
 * keeping the stacking/ticking logic out of the fixed-bank budget.  The
 * trampoline exposes two pointers + two bytes; the apply call packs
 * stacks/duration into byte_b (both <= 15).  Telemetry is emitted HERE,
 * after the banked body returns -- banked code cannot emit events.
 *
 * Both bodies report through small shared WRAM structs (below); battles
 * resolve sequentially, so single slots are safe. */

/* Written by status_apply_banked(): what landed on the actor. */
StatusInstance g_status_applied;

/* Written by status_tick_banked(); declared in status.h. */
StatusTickResult g_status_tick;

/* Frozen-combatant bitmask (status.h); cleared at battle start. */
uint8_t g_status_frozen_mask;

/* Battle-scoped storage (see status.h).  Zeroed at battle start.
 * Non-static so the bank-3 battle HUD can color names by active status. */
StatusSlots s_battle_status[STATUS_ROUND_SLOTS];

/* Poison grey-out (status.h): mask/duration per actor slot.  Battle-scoped
 * runtime state, cleared at battle start; written by status_grey_apply
 * (wrapper + bank-3 pick body), read by battle gating and the bank-3 HUD. */
uint8_t s_grey_mask[STATUS_ROUND_SLOTS];
uint8_t s_grey_dur[STATUS_ROUND_SLOTS];

void status_reset_battle(void)
{
    uint8_t i;
    for (i = 0; i < STATUS_ROUND_SLOTS; i++) {
        s_battle_status[i].count = 0;
        s_grey_mask[i] = 0;
        s_grey_dur[i] = 0;
    }
    g_status_frozen_mask = 0;
}

StatusSlots *status_slots(uint8_t actor_slot)
{
    if (actor_slot >= STATUS_ROUND_SLOTS) return (StatusSlots *)0;
    return &s_battle_status[actor_slot];
}

bool status_apply(StatusSlots *slots, uint8_t actor_slot, uint8_t id,
                  uint8_t stacks, uint8_t duration)
{
    g_status_applied.id = STATUS_NONE;

    g_bk_call_bank = 3;
    g_bk_call_target = (uint16_t)&status_apply_banked;
    g_bk_ptr_a = (void *)slots;
    g_bk_byte_a = id;
    g_bk_byte_b = (uint8_t)((stacks << 4) | duration);
    banked_call_run();

    if (g_status_applied.id == STATUS_NONE) {
        /* Rejected (e.g. slots full): nothing landed, so the frozen
         * mask must stay untouched -- a rejected FREEZE must not skip
         * anyone's turn. */
        return false;
    }

    /* FREEZE takes effect immediately: the next attack by this
     * combatant is skipped; the tick body re-arms the bit while an
     * instance remains alive. */
    if (id == STATUS_FREEZE && actor_slot < STATUS_ROUND_SLOTS) {
        g_status_frozen_mask |= (uint8_t)(1 << actor_slot);
    }

    telemetry_emit(EVENT_STATUS_APPLIED, g_status_applied.id,
                   g_status_applied.stacks, g_status_applied.duration, 0);
    return true;
}

uint8_t status_tick(StatusSlots *slots, uint8_t actor_slot)
{
    g_bk_call_bank = 3;
    g_bk_call_target = (uint16_t)&status_tick_banked;
    g_bk_ptr_a = (void *)slots;
    g_bk_byte_a = actor_slot;
    banked_call_run();

    /* One combined event: damage taken + expiry summary (d2=count,
     * d3=first expired id), plus one dedicated STATUS_EXPIRED per tick
     * reporting the first removed instance (docs/combo-system.md §19;
     * MAX_STATUSES_PER_COMBATANT bounds simultaneity, and multi-expiry
     * rounds remain fully described by TICKED's d2 count). */
#ifdef DEBUG_BUILD
    if (g_status_tick.damage != 0 || g_status_tick.expired_count != 0) {
        telemetry_emit(EVENT_STATUS_TICKED, g_status_tick.damage,
                       actor_slot, g_status_tick.expired_count,
                       g_status_tick.expired_ids[0]);
        if (g_status_tick.expired_count != 0) {
            telemetry_emit(EVENT_STATUS_EXPIRED,
                           g_status_tick.expired_ids[0], actor_slot, 0, 0);
        }
    }
#endif
    return g_status_tick.damage;
}

/* Decode the two set-bit positions of a grey mask into out-a/out-b
 * (0xFF when absent).  Pools are <= 5 positions, so only bits 0-4 are
 * ever set. */
void status_grey_mask_indices(uint8_t mask, uint8_t *a, uint8_t *b)
{
    uint8_t i, n = 0;
    *a = 0xFF;
    *b = 0xFF;
    for (i = 0; i < 8; i++) {
        if ((mask & (uint8_t)(1u << i)) != 0) {
            if (n == 0) *a = i;
            else { *b = i; break; }
            n++;
        }
    }
}

void status_grey_apply(uint8_t actor_slot, uint8_t pool_size)
{
#ifdef DEBUG_BUILD
    uint8_t a, b;
#endif
    if (actor_slot >= STATUS_ROUND_SLOTS) return;

    g_bk_call_bank = 3;
    g_bk_call_target = (uint16_t)&status_grey_apply_banked;
    g_bk_byte_a = actor_slot;
    g_bk_byte_b = pool_size;
    banked_call_run();

#ifdef DEBUG_BUILD
    /* An empty pool or a bad slot leaves the mask untouched (no event). */
    if (s_grey_mask[actor_slot] == 0) return;

    status_grey_mask_indices(s_grey_mask[actor_slot], &a, &b);
    telemetry_emit(EVENT_CARDS_GREYED, actor_slot, a, b,
                   POISON_GREY_TURNS);
#endif
}

void status_grey_tick(uint8_t actor_slot)
{
    if (actor_slot >= STATUS_ROUND_SLOTS) return;
    if (s_grey_dur[actor_slot] == 0) return;

    g_bk_call_bank = 3;
    g_bk_call_target = (uint16_t)&status_grey_tick_banked;
    g_bk_byte_a = actor_slot;
    banked_call_run();

#ifdef DEBUG_BUILD
    if (g_bk_byte_a != 0) {
        telemetry_emit(EVENT_CARDS_UNGREYED, actor_slot, g_bk_byte_b, g_bk_byte_c, 0);
    }
#endif
}
