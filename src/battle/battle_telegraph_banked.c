#pragma bank 12
#pragma disable_warning 110

#include "battle.h"
#include "banked.h"
#include "rpg/status.h"
#include "telemetry.h"
#include "telemetry_ring.h"

/* Same predicate as battle_defend_content.c's confuse_attacker_poisoned
 * (bank 10), duplicated here because this body now lives in its own bank
 * (12) and a banked body must never call across banks (AGENTS.md
 * 52.11.1).  Both read the same WRAM status slots, so they cannot
 * disagree about what "confused" means. */
static uint8_t teleg_attacker_confused(const Battle *b, uint8_t atk)
{
    const StatusSlots *slots;
    uint8_t i;
    if (atk >= b->enemy_count) return 0;
    slots = &s_battle_status[(uint8_t)(atk + 1)];
    for (i = 0; i < slots->count; i++) {
        if (slots->slot[i].id == STATUS_POISON) return 1;
    }
    return 0;
}

/* Same shape for ice: a banked body must never call across banks, so
 * the predicate is duplicated here (see above).  An iced attacker
 * deals halved damage (rounded up). */
static uint8_t teleg_attacker_iced(const Battle *b, uint8_t atk)
{
    const StatusSlots *slots;
    uint8_t i;
    if (atk >= b->enemy_count) return 0;
    slots = &s_battle_status[(uint8_t)(atk + 1)];
    for (i = 0; i < slots->count; i++) {
        if (slots->slot[i].id == STATUS_FREEZE) return 1;
    }
    return 0;
}

/* ── Enemy telegraph card draw (ROM bank 12) ─────────────────────────
 * Hosts battle_update()'s ANIM/SHUFFLE-tail telegraph block out of the
 * fixed bank (which sits hard against 0x8000, AGENTS.md §52.18).  The
 * fixed wrapper in battle.c stages the Battle* through the WRAM
 * trampoline; the SFX tail (SFX_HIT) and the delay stay in the fixed
 * wrapper.
 *
 * Self-contained (AGENTS.md §52.11.1): reads/writes only the staged
 * Battle* plus battle-scoped WRAM (frozen mask, shake
 * timers), never calls fixed-bank functions.  Telemetry appends run
 * inline on the ring buffer (debug builds only) -- a banked body must
 * not dispatch another banked body (banked.h non-reentrancy), so the
 * bank-2 append body is replicated field-for-field here instead of
 * called; keep in sync with telemetry_emit_banked.c.  Field-wise
 * EnemyCard copies only: SDCC lowers struct assignment to the
 * RAM-resident __memcpy helper, unreachable while a bank is mapped
 * (see battle_turn_draw_banked.c).
 *
 * Ice (Phase D, halved-only): an iced attacker deals halved damage
 * (rounded up, min 1).  There is no grey-out and no fizzle -- every
 * telegraphed swing lands, just soft.  Poison confuses: a poisoned
 * attacker swings normally, and the defend resolve retargets its
 * swing at its allies (see battle.c). */

/* Ring telemetry runs through telemetry_ring.h (one shared inline
 * writer; the release macro there compiles the calls out).  Keep the
 * call sites' field order in sync with telemetry_emit_banked.c. */

void battle_telegraph_banked(void)
{
    /* Volatile access throughout (same SDCC pointer-cache workaround as
     * battle_update in battle.c): the optimizer reuses cached
     * &b->field stack slots across the branches below. */
    volatile Battle *b = (volatile Battle *)g_bk_ptr_a;
    uint8_t gi;
    uint8_t atk;
    uint8_t di;

    if (!b) return;
    /* Consume any stale staged message at the next telegraph.  The
     * unconditional clear is safe for the other staged messages:
     * Carl's msg 9 is consumed by the dialogue
     * screen on close before battle resumes, and despair's msg 10 lives in
     * SLIME_DESPAIR, a phase that never dispatches a telegraph. */
    b->msg_id = 0;
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
    if (b->enemy_deck.count > 0) {
        di = b->enemy_deck.draw_idx;
        b->enemy_played_card.type = b->enemy_deck.cards[di].type;
        b->enemy_played_card.value = b->enemy_deck.cards[di].value;
        b->enemy_played_card.status_id = b->enemy_deck.cards[di].status_id;
        b->enemy_played_card.status_chance = b->enemy_deck.cards[di].status_chance;
        b->enemy_incoming_dmg = b->enemy_played_card.value;
        /* Iced attacker (ice bow): the swing lands halved (rounded up,
         * min 1).  The card announcement above keeps the full value;
         * only the damage softens. */
        if (teleg_attacker_iced(b, atk)) {
            b->enemy_incoming_dmg = (uint8_t)((b->enemy_incoming_dmg + 1) >> 1);
        }
        telemetry_ring_emit(EVENT_ENEMY_CARD_PLAYED,
                       di,
                       b->enemy_played_card.type,
                       b->enemy_played_card.value, 0);
        b->enemy_deck.draw_idx++;
        if (b->enemy_deck.draw_idx >= b->enemy_deck.count) {
            b->enemy_deck.draw_idx = 0;
        }
    } else {
        /* No enemy card: the fallback flat 6-damage swipe still
         * marks an inbound strike.  Only deckless battlers (the
         * Lord of Slimes) reach this: three SH2 shields (3 x 2 = 6)
         * or one steel shield (power 6, stocked by the merchant)
         * blocks it exactly (docs/enemies_ranking.md §5.2). */
        b->enemy_incoming_dmg = 6;
        /* Iced Lord (ice bow): the fallback softens to 3 like any
         * deck swing (rounded-up halving, same as above). */
        if (teleg_attacker_iced(b, atk)) {
            b->enemy_incoming_dmg = (uint8_t)((b->enemy_incoming_dmg + 1) >> 1);
        }
    }
    /* Confused attacker (poison): the swing will turn on its own side.
     * Stage the first acknowledgement beat (msg 6, "FOE IS CONFUSED!",
     * waits for A) plus the outcome flag the A press reveals.  The
     * ally-vs-self verdict uses the same rule as confuse_pick_victim
     * (first live enemy that is not the attacker, else self), decided
     * here so the render needs only a flag read (bank 3 is full in
     * release). */
    if (b->msg_id == 0 && teleg_attacker_confused(b, atk)) {
        b->msg_id = BATTLE_MSG_CONFUSED_FOE;
        b->msg_ttl = 0;
        g_battle_confuse_self = 1;
        for (gi = 0; gi < b->enemy_count; gi++) {
            if (gi != atk && b->enemies[gi].hp != 0) {
                g_battle_confuse_self = 0;
                break;
            }
        }
    }
}
