#pragma bank 10

#include "status.h"
#include "banked.h"
#include "rng.h"
#include "rpg/effects.h"

/* ── Status definitions + banked bodies (Phase C) ───────────────────
 * Runs from ROM bank 10 via the WRAM banked-call trampoline; reads only
 * its own bank-local definition table and the staged pointers/bytes, and
 * writes through staged pointers / shared WRAM result structs
 * (g_status_applied in status.c).  Never calls fixed-bank code.  Lives
 * in bank 10 (event bank): banks 3/5 are render-full in release, and
 * every caller dispatches through the trampoline, so no bank-local call
 * constrains placement (AGENTS.md §52.11.1, §55.5).  Bank 10 already
 * hosts dispatched bodies (dialogue nav, Carl offer) next to the event
 * table; the status bodies touch disjoint WRAM and never read the
 * table.
 *
 * Stacking rule (plan §16): POISON/BURN/ICE STACK up to max_stacks and
 * refresh duration.  Balance values are deliberately provisional (§26).
 * POISON is control-only (tick 0): it never deals damage, it confuses
 * the victim (enemies attack their allies, the hero plays random hands
 * -- see battle.c).  ICE carries the grey-out (ICE_GREY_CARDS cards for
 * ICE_GREY_TURNS rounds).  BURN is the
 * damage-over-time status (10% of the inflicting hit per round, min 1,
 * scaled per instance at apply). */

static const StatusDefinition s_status_defs[STATUS_DEF_COUNT] = {
    /* id            tick  max                        dur */
    { STATUS_NONE,     0,   0,                         0 },
    { STATUS_POISON,   0,   STATUS_POISON_MAX_STACKS,  STATUS_POISON_DURATION },
    /* BURN tick is per-instance scaled at apply (10% of the inflicting
     * hit, min 1); the table slot stays 0 so no flat fallback exists. */
    { STATUS_BURN,     0,   3,                         3 },
    { STATUS_FREEZE,   0,   5,                         2 }
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

/* Burn tick from the inflicting hit's damage: 10% per round, min 1.
 * Subtract loop instead of / (which would lower to the fixed-bank div
 * library, unreachable while bank 10 is mapped -- AGENTS.md §52.11.1). */
static uint8_t burn_tick(uint8_t basis)
{
    uint8_t t = 0;
    while (basis >= 10) {
        basis = (uint8_t)(basis - 10);
        t++;
    }
    if (t == 0) t = 1;
    return t;
}

void status_apply_banked(void)
{
    StatusSlots *slots = (StatusSlots *)g_bk_ptr_a;
    uint8_t id = g_bk_byte_a;
    uint8_t stacks = (uint8_t)(g_bk_byte_b >> 4);
    uint8_t duration = (uint8_t)(g_bk_byte_b & 0x0F);
    const StatusDefinition *def;
    uint8_t i;
    /* Burn basis (inflicting hit's damage): g_effect_last.amount, left
     * by the combo dispatch every hit runs.  Gameplay rider paths run
     * immediately after their hit's dispatch, so this is always their
     * own hit (status.h).  Consumed AND cleared on entry, so a spent
     * basis can never leak into a later debug/organic apply. */
    uint8_t basis = g_effect_last.amount;
    g_effect_last.amount = 0;

    if (!slots || id == STATUS_NONE) return;
    def = status_def(id);
    if (!def) return;

    if (stacks == 0) stacks = 1;
    if (duration == 0) duration = def->duration;
    if (stacks > def->max_stacks) stacks = def->max_stacks;

    /* STACK rule: existing instance gains stacks (capped), duration
     * refreshed to the request.  A re-applied burn keeps the stronger
     * tick (a weak re-hit never cools an existing blaze). */
    for (i = 0; i < slots->count; i++) {
        if (slots->slot[i].id == id) {
            uint16_t total = (uint16_t)(slots->slot[i].stacks + stacks);
            uint8_t nt;
            if (total > def->max_stacks) total = def->max_stacks;
            slots->slot[i].stacks = (uint8_t)total;
            slots->slot[i].duration = duration;
            if (id == STATUS_BURN) {
                nt = burn_tick(basis);
                if (nt > slots->slot[i].tick) slots->slot[i].tick = nt;
            }
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
    slots->slot[slots->count].tick =
        (id == STATUS_BURN) ? burn_tick(basis) : 0;
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
    /* Clear last round's frozen flag for this actor (retained for
     * layout stability -- nothing reads the mask since the freeze-skip
     * removal). */
    g_status_frozen_mask &= (uint8_t)~(1 << actor_slot);

    for (i = 0; i < slots->count; ) {
        StatusInstance *inst = &slots->slot[i];
        const StatusDefinition *def = status_def(inst->id);
        if (def != (const StatusDefinition *)0) {
            /* Per-instance tick: burn carries its scaled round damage
             * (10% of the inflicting hit, min 1 -- staged at apply);
             * poison/ice instances hold 0.  Extra applications only
             * refresh duration and deepen stacks for telemetry. */
            damage += inst->tick;
        }
        if (inst->id == STATUS_POISON &&
            (g_poison_grace_mask & (uint8_t)(1u << actor_slot))) {
            i++;
            continue;
        }
        inst->duration--;
        if (inst->duration == 0) {
            if (expired < MAX_STATUSES_PER_COMBATANT) {
                g_status_tick.expired_ids[expired++] = inst->id;
            }
            /* Remove by shifting the tail down, field-wise: struct
             * assignment lowers to __memcpy, which lives in the fixed
             * bank and is unreachable while bank 10 is mapped. */
            slots->slot[i].id = slots->slot[slots->count - 1].id;
            slots->slot[i].stacks = slots->slot[slots->count - 1].stacks;
            slots->slot[i].duration = slots->slot[slots->count - 1].duration;
            slots->slot[i].tick = slots->slot[slots->count - 1].tick;
            slots->count--;
        } else {
            i++;
        }
    }

    g_poison_grace_mask &= (uint8_t)~(1u << actor_slot);
    g_status_tick.damage = damage;
    g_status_tick.expired_count = expired;
}

/* Ice grey-out pick body (status.h): selects ICE_GREY_CARDS distinct
 * pool positions at random and arms the battle-scoped mask/duration.
 * Reads the actor slot (g_bk_byte_a) and the victim's card-pool size
 * (g_bk_byte_b) from the staged globals; writes s_grey_mask/s_grey_dur
 * (WRAM) directly.  A victim already greyed is only refreshed (same cards,
 * same duration) -- the STACK rule for grey, matching ice itself.
 *
 * Banked self-containment: randomness uses the SAME inlined xorshift step
 * on g_rng_state as rng_next() (src/debug/rng.c), so banked code draws from
 * the deterministic stream without calling the fixed-bank function; the
 * modulo is a subtraction loop instead of % (which would lower to the
 * fixed-bank div library, unreachable while bank 10 is mapped). */
#ifdef DEBUG_BUILD
static void decode_grey_mask(uint8_t mask)
{
    uint8_t a = 0xFF, b = 0xFF, i;
    for (i = 0; i < 8; i++) {
        if ((mask & (uint8_t)(1u << i)) != 0) {
            if (a == 0xFF) a = i;
            else { b = i; break; }
        }
    }
    g_bk_byte_b = a;
    g_bk_byte_c = b;
}
#endif

void status_grey_apply_banked(void)
{
    uint8_t slot = g_bk_byte_a;
    uint8_t pool = g_bk_byte_b;
    uint8_t i, idx, mask = 0;

    /* Defensive: a pool smaller than ICE_GREY_CARDS cannot host that
     * many distinct greyed positions -- the second draw's linear probe
     * would loop forever.  Unreachable today (player hand = BATTLE_HAND_SIZE,
     * enemy decks are 4/5 cards), but a future smaller deck must not hang
     * bank 10. */
    if (slot >= STATUS_ROUND_SLOTS || pool < ICE_GREY_CARDS) return;

    if (s_grey_mask[slot] != 0) {
        s_grey_dur[slot] = ICE_GREY_TURNS;
#ifdef DEBUG_BUILD
        decode_grey_mask(s_grey_mask[slot]);
#endif
        return;
    }

    for (i = 0; i < ICE_GREY_CARDS; i++) {
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
    /* The grey drains at the enter-select round boundary in lockstep
     * with the STATUS_FREEZE instance itself (both tick down once per
     * round in battle_tick_statuses), so the tint and the
     * grey-out expire together after ICE_GREY_TURNS rounds. */
    s_grey_dur[slot] = ICE_GREY_TURNS;
#ifdef DEBUG_BUILD
    decode_grey_mask(mask);
#endif
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

    if (slot >= STATUS_ROUND_SLOTS || s_grey_dur[slot] == 0 ||
        --s_grey_dur[slot] != 0) {
        g_bk_byte_a = 0;
        return;
    }
    /* Expired: decode indices and clear mask. */
#ifdef DEBUG_BUILD
    decode_grey_mask(s_grey_mask[slot]);
#endif
    s_grey_mask[slot] = 0;
    g_bk_byte_a = 1;
}