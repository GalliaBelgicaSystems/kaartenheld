#pragma bank 3

#include "status.h"
#include "banked.h"
#include "rng.h"

/* ── Status definitions + banked bodies (Phase C) ───────────────────
 * Runs from ROM bank 3 via the WRAM banked-call trampoline; reads only
 * its own bank-local definition table and the staged pointers/bytes, and
 * writes through staged pointers / shared WRAM result structs
 * (g_status_applied in status.c).  Never calls fixed-bank code.
 *
 * Stacking rule (plan §16): POISON/BURN STACK up to max_stacks and
 * refresh duration; FREEZE does not stack (control status -- a frozen
 * combatant can neither attack nor defend for the whole duration,
 * battle-side).  Balance values are deliberately provisional (§26).
 * POISON is control-only (tick 0): it never deals damage, it only greys
 * out the victim's cards for POISON_GREY_TURNS rounds.  BURN is the
 * damage-over-time status (flat 1 HP/round). */

static const StatusDefinition s_status_defs[STATUS_DEF_COUNT] = {
    /* id            tick  max  dur */
    { STATUS_NONE,     0,   0,   0 },
    { STATUS_POISON,   0,   5,   3 },
    { STATUS_BURN,     1,   3,   3 },
    { STATUS_FREEZE,   0,   1,   3 }
};

static const StatusDefinition *status_def(uint8_t id)
{
    uint8_t i;
    for (i = 0; i < STATUS_DEF_COUNT; i++) {
        if (s_status_defs[i].id == id) {
            return &s_status_defs[i];
        }
    }
    return (const StatusDefinition *)0;
}

void status_apply_banked(void)
{
    StatusSlots *slots = (StatusSlots *)g_bk_ptr_a;
    uint8_t id = g_bk_byte_a;
    uint8_t stacks = (uint8_t)(g_bk_byte_b >> 4);
    uint8_t duration = (uint8_t)(g_bk_byte_b & 0x0F);
    const StatusDefinition *def;
    uint8_t i;

    if (!slots || id == STATUS_NONE) return;
    def = status_def(id);
    if (!def) return;

    if (stacks == 0) stacks = 1;
    if (duration == 0) duration = def->duration;
    if (stacks > def->max_stacks) stacks = def->max_stacks;

    /* STACK rule: existing instance gains stacks (capped), duration
     * refreshed to the request. */
    for (i = 0; i < slots->count; i++) {
        if (slots->slot[i].id == id) {
            uint16_t total = (uint16_t)(slots->slot[i].stacks + stacks);
            if (total > def->max_stacks) total = def->max_stacks;
            slots->slot[i].stacks = (uint8_t)total;
            slots->slot[i].duration = duration;
            g_status_applied.id = id;
            g_status_applied.stacks = slots->slot[i].stacks;
            g_status_applied.duration = duration;
            return;
        }
    }

    if (slots->count >= MAX_STATUSES_PER_COMBATANT) return;
    slots->slot[slots->count].id = id;
    slots->slot[slots->count].stacks = stacks;
    slots->slot[slots->count].duration = duration;
    slots->count++;
    g_status_applied.id = id;
    g_status_applied.stacks = stacks;
    g_status_applied.duration = duration;
    /* The frozen-mask bit is armed by the fixed wrapper (status_apply,
     * src/rpg/status.c), not here -- always go through the wrapper. */
}

void status_tick_banked(void)
{
    StatusSlots *slots = (StatusSlots *)g_bk_ptr_a;
    uint8_t actor_slot = g_bk_byte_a;
    uint8_t i;
    uint8_t damage = 0;
    uint8_t expired = 0;

    if (!slots) return;

    g_status_tick.damage = 0;
    g_status_tick.expired_count = 0;
    /* Consume last round's frozen flag for this actor; re-set below if
     * the status is still active (battle tests the bit once per attack).
     * Slots beyond STATUS_ROUND_SLOTS can't occur (wrapper guards). */
    g_status_frozen_mask &= (uint8_t)~(1 << actor_slot);

    for (i = 0; i < slots->count; ) {
        StatusInstance *inst = &slots->slot[i];
        const StatusDefinition *def = status_def(inst->id);
        if (def != (const StatusDefinition *)0) {
            /* FLAT tick (docs/combo-system.md Phase C decision): burn
             * deals def->tick HP per round regardless of stacks; extra
             * applications only refresh duration and deepen stacks for
             * telemetry.  Poison's tick is 0 (control-only: the grey-out
             * is its whole effect), so it contributes nothing here. */
            damage += def->tick;
        }
        inst->duration--;
        if (inst->duration == 0) {
            if (expired < MAX_STATUSES_PER_COMBATANT) {
                g_status_tick.expired_ids[expired++] = inst->id;
            }
            /* Remove by shifting the tail down, field-wise: struct
             * assignment lowers to __memcpy, which lives in the fixed
             * bank and is unreachable while bank 3 is mapped.  An
             * expiring FREEZE must NOT re-arm the frozen-mask bit: it
             * armed at the top of the round, and the expiry consumes the
             * status, so the combatant resumes acting next round. */
            slots->slot[i].id = slots->slot[slots->count - 1].id;
            slots->slot[i].stacks = slots->slot[slots->count - 1].stacks;
            slots->slot[i].duration = slots->slot[slots->count - 1].duration;
            slots->count--;
        } else {
            if (def != (const StatusDefinition *)0 && inst->id == STATUS_FREEZE) {
                /* Still frozen after this round: the combatant skips its
                 * attack again; the mask bit is tested (never consumed)
                 * by the battle gates. */
                g_status_frozen_mask |= (uint8_t)(1 << actor_slot);
            }
            i++;
        }
    }

    g_status_tick.damage = damage;
    g_status_tick.expired_count = expired;
}

/* Poison grey-out pick body (status.h): selects POISON_GREY_CARDS distinct
 * pool positions at random and arms the battle-scoped mask/duration.
 * Reads the actor slot (g_bk_byte_a) and the victim's card-pool size
 * (g_bk_byte_b) from the staged globals; writes s_grey_mask/s_grey_dur
 * (WRAM) directly.  A victim already greyed is only refreshed (same cards,
 * same duration) -- the STACK rule for grey, matching poison itself.
 *
 * Banked self-containment: randomness uses the SAME inlined xorshift step
 * on g_rng_state as rng_next() (src/debug/rng.c), so banked code draws from
 * the deterministic stream without calling the fixed-bank function; the
 * modulo is a subtraction loop instead of % (which would lower to the
 * fixed-bank div library, unreachable while bank 3 is mapped). */
void status_grey_apply_banked(void)
{
    uint8_t slot = g_bk_byte_a;
    uint8_t pool = g_bk_byte_b;
    uint8_t i, idx, mask = 0;

    /* Defensive: a pool smaller than POISON_GREY_CARDS cannot host that
     * many distinct greyed positions -- the second draw's linear probe
     * would loop forever.  Unreachable today (player hand = BATTLE_HAND_SIZE,
     * enemy decks are 4/5 cards), but a future smaller deck must not hang
     * bank 3. */
    if (slot >= STATUS_ROUND_SLOTS || pool < POISON_GREY_CARDS) return;

    if (s_grey_mask[slot] != 0) {
        s_grey_dur[slot] = (uint8_t)(POISON_GREY_TURNS + 1);
        return;
    }

    for (i = 0; i < POISON_GREY_CARDS; i++) {
        g_rng_state ^= (uint16_t)(g_rng_state << 7);
        g_rng_state ^= (uint16_t)(g_rng_state >> 9);
        g_rng_state ^= (uint16_t)(g_rng_state << 8);
        idx = (uint8_t)g_rng_state;
        while (idx >= pool) idx = (uint8_t)(idx - pool);
        while ((mask & (uint8_t)(1u << idx)) != 0) {
            idx++;
            if (idx >= pool) idx = 0;
        }
        mask |= (uint8_t)(1u << idx);
    }
    s_grey_mask[slot] = mask;
    /* The grey drains at the enter-select round boundary; the poison
     * usually lands mid-round, so POISON_GREY_TURNS+1 boundaries leave
     * exactly POISON_GREY_TURNS future decision rounds greyed. */
    s_grey_dur[slot] = (uint8_t)(POISON_GREY_TURNS + 1);
}

/* One round boundary for a greyed combatant: decrement the duration and,
 * when it hits zero, clear the mask and return the indices.  Banked body
 * dispatched by status_grey_tick().  Stages: g_bk_byte_a = actor_slot.
 * On return: g_bk_byte_a = 0 if no expiry, 1 if expired; g_bk_byte_b = idxA,
 * g_bk_byte_c = idxB (0xFF if only one).  The fixed wrapper emits
 * EVENT_CARDS_UNGREYED. */
void status_grey_tick_banked(void)
{
    uint8_t slot = g_bk_byte_a;
    uint8_t a, b, i;

    if (slot >= STATUS_ROUND_SLOTS) {
        g_bk_byte_a = 0;
        return;
    }
    if (s_grey_dur[slot] == 0) {
        g_bk_byte_a = 0;
        return;
    }
    if (--s_grey_dur[slot] != 0) {
        g_bk_byte_a = 0;
        return;
    }
    /* Expired: decode indices and clear mask. */
    a = 0xFF; b = 0xFF;
    for (i = 0; i < 8; i++) {
        if ((s_grey_mask[slot] & (uint8_t)(1u << i)) != 0) {
            if (a == 0xFF) a = i;
            else { b = i; break; }
        }
    }
    s_grey_mask[slot] = 0;
    g_bk_byte_a = 1;
    g_bk_byte_b = a;
    g_bk_byte_c = b;
}