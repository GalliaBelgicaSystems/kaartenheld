/* Bank split (memory budget): the defend body lives in bank 10 (event/
 * status bank) in both builds -- release bank 4 filled up (rider OAM
 * pass + nav cluster + confuse branch) and debug bank 4 is
 * test-content-full.  Bank 10 already hosts battle-flow bodies (the
 * telegraph) and has room.  Keep in sync with the dispatch bank in
 * battle_defend_resolve() below (battle.c). */
#pragma bank 10

#include "battle.h"
#include "rpg/effects.h"
#include "rpg/status.h"
#include "rng.h"
#include "audio.h"
#include "banked.h"
#include "telemetry.h"
#include "telemetry_ring.h"

/* Confused-attacker helpers (poison, docs/loot.md §34.6): the fixed
 * caller (battle_execute_combo) already confirmed the attacker is
 * poisoned, but this body re-derives everything it needs from the
 * staged Battle* + WRAM (no fixed calls while banked).  Status slots
 * are plain WRAM (s_battle_status), readable directly. */
static uint8_t confuse_attacker_poisoned(Battle *b, uint8_t atk)
{
    StatusSlots *slots;
    uint8_t i;
    if (atk >= b->enemy_count) return 0;
    slots = &s_battle_status[(uint8_t)(atk + 1)];
    for (i = 0; i < slots->count; i++) {
        if (slots->slot[i].id == STATUS_POISON) return 1;
    }
    return 0;
}

/* Friendly-fire victim: the first live enemy that is not the attacker,
 * or the attacker itself when alone (self-harm).  Deterministic slot
 * order (no RNG step): the confusion is in WHO the blow lands on, not
 * which ally -- and a fixed victim keeps the harness deterministic. */
static uint8_t confuse_pick_victim(Battle *b, uint8_t atk)
{
    uint8_t i;
    for (i = 0; i < b->enemy_count; i++) {
        if (i != atk && b->enemies[i].hp != 0) return i;
    }
    return atk;
}

/* Banked body of battle_defend_resolve (src/battle/battle.c).  Computes
 * and applies the DEFENSE net resolution entirely in bank 10 (was bank
 * 4 release / bank 5 debug; both filled up -- see header note) so the
 * fixed bank (which is completely full, see make memmap) only runs a thin
 * staging wrapper + telemetry.  Lives in bank 10 (not bank 3) to leave
 * room for the rider OAM pass in the release layout.
 *
 * Self-contained (AGENTS.md §52.11.1): reads/writes only WRAM structs
 * (the staged Battle* and the stashed block sum) and its own bank-local
 * code; it must NOT call any fixed-bank function (the bank is switched
 * away).  The block sum rides in g_defend_block_save (stashed by the
 * counter prep, which runs first -- the counter eval would otherwise
 * overwrite g_effect_last.amount), the ring flag is scanned from
 * last_combo.cards[] here.
 *
 * Defense over-block rule (docs/loot.md §34.4): a defense net
 * <= 0 is CLAMPED to zero (no damage); HP is restored only when a ring
 * (healing shield) is in the defense combo, capped at max HP.
 *
 * Damage path replicates combatant_take_damage() inline (the banked body
 * may not call the fixed-bank helper per §52.11.1): the helper is only
 * `hp = (damage >= hp) ? 0 : hp - damage` -- a pure HP clamp with no death
 * flag / status / invuln side effects, so the inline copy is equivalent.
 * The hero's HP==0 defeat check that follows in battle_execute_combo still
 * relies on this writing hp to 0 on an overkill hit.
 *
 * Telemetry: g_bk_byte_a reports the HP ACTUALLY lost (clamped to current
 * HP), not the nominal `net` -- on an overkill hit it is the remaining HP
 * (possibly 0), a deliberate change from the old nominal-net reporting
 * (the two agree whenever the hit does not overkill; the clamped value is
 * the observable damage).
 *
 * On return it stages the two possible magnitudes into g_bk_byte_a
 * (net damage taken) and g_bk_byte_b (net HP healed); the fixed wrapper
 * emits the corresponding telemetry. */
void battle_defend_resolve_banked(void)
{
    Battle *b = (Battle *)g_bk_ptr_a;
    int16_t net;
    uint8_t di;
    uint8_t combo_has_ring = 0;
    uint8_t atk;

    g_bk_byte_a = 0;
    g_bk_byte_b = 0;
    g_bk_byte_c = 0;
    if (!b) return;

    /* Counter-first cancel: the dagger counter (when staged) already ran
     * and may have dropped the telegraphed attacker -- nothing else in
     * the defend window deals damage, and the telegraph always advances
     * past the dead, so a dead attacker here means the counter killed it
     * and its swing never lands.  Resolve as an exact block (no damage,
     * block cue, no rider -- and crucially no over-block heal even with
     * a ring staged, which a plain incoming-zero would have granted). */
    atk = (b->attacking_enemy_idx < b->enemy_count) ? b->attacking_enemy_idx : 0;
    if (b->enemies[atk].hp == 0) {
        /* Unkillable desperation boss: the counter floored it to 0, but
         * the floor gives 1 HP back and its swing still lands -- falling
         * through to the normal resolve below.  A killing swing then
         * takes the ordinary defeat exit (never the scripted outro);
         * skipping it here would ghost-cancel the attack and strand the
         * hero alive into the hold. */
        if (atk == g_despair_slot) {
            b->enemies[atk].hp = 1;
        } else {
            g_bk_byte_d = SFX_BLOCK;
            return;
        }
    }

    /* Confused attacker (poison): the single place confusion is decided.
     * The swing turns on the attacker's own side instead of the hero --
     * the hero takes nothing (bytes a/b stay 0, so the wrapper emits
     * RECEIVED 0) and the victim takes the full telegraphed swing.  Lone
     * confused attackers harm themselves.  The despair floor holds
     * exactly like the counter path above.  The organic rider stages
     * for the fixed wrapper like any other swing (victim-slot routing
     * via g_battle_confuse_victim); telemetry here is DAMAGE only,
     * victim slot in d1.  Only poison already present when the defend
     * began counts here: a counter rider lands after the swing (see
     * the caller), so the swing it rode in on is never redirected. */
    if (confuse_attacker_poisoned(b, atk)) {
        uint8_t vic = confuse_pick_victim(b, atk);
        uint8_t dmg = b->enemy_incoming_dmg;
        g_battle_confuse_victim = vic;
        if (dmg != 0 && b->enemies[vic].hp != 0) {
            if (vic == g_despair_slot) {
                b->enemies[vic].hp = 1;
            } else if (dmg >= b->enemies[vic].hp) {
                b->enemies[vic].hp = 0;
            } else {
                b->enemies[vic].hp = (uint8_t)(b->enemies[vic].hp - dmg);
            }
            g_bk_byte_d = SFX_HURT;
            telemetry_ring_emit(EVENT_DAMAGE_DEALT, dmg, (uint8_t)(vic + 1), 0, 0);
            if (b->enemy_played_card.status_id != STATUS_NONE &&
                b->enemy_played_card.status_chance != 0) {
                g_rng_state ^= (uint16_t)(g_rng_state << 7);
                g_rng_state ^= (uint16_t)(g_rng_state >> 9);
                g_rng_state ^= (uint16_t)(g_rng_state << 8);
                if ((uint8_t)g_rng_state < b->enemy_played_card.status_chance) {
                    g_bk_byte_c = b->enemy_played_card.status_id;
                }
            }
        } else {
            g_bk_byte_d = SFX_BLOCK;
        }
        return;
    }

    for (di = 0; di < b->combo_count; di++) {
        if (b->last_combo.cards[di].ring) combo_has_ring = 1;
    }
    /* The block sum rides in g_defend_block_save (stashed by the counter
     * prep -- the counter eval overwrote g_effect_last), the ring flag
     * is scanned from last_combo.cards[] here. */
    net = (int16_t)b->enemy_incoming_dmg - (int16_t)g_defend_block_save;
    if (net > 0) {
        uint8_t dmg = (uint8_t)net;
        if (dmg >= b->player.hp) {
            g_bk_byte_a = b->player.hp;
            b->player.hp = 0;
        } else {
            g_bk_byte_a = dmg;
            b->player.hp = (uint8_t)(b->player.hp - dmg);
        }
    } else if (net < 0 && combo_has_ring) {
        /* net < 0: the unsigned trick is safe -- subtracting a negative
         * `net` ADDS its magnitude; capped at max_hp. */
        uint16_t nh = (uint16_t)b->player.hp - net;
        if (nh > b->player.max_hp) nh = b->player.max_hp;
        g_bk_byte_b = (uint8_t)(nh - (uint16_t)b->player.hp);
        b->player.hp = (uint8_t)nh;
    }

    /* Block vs hurt cue (player feedback contract): an exact block plays
     * the block thump; ANY net damage -- even behind staged shields --
     * plays the hurt cue instead.  An over-block that heals (ring-gated)
     * plays the heal cue.  Staged as a plain SFX id in g_bk_byte_d so the
     * fixed wrapper below spends a single unconditional call (fixed-bank
     * budget, AGENTS.md 52.18); the tick's one-shot SFX slot never sees
     * it (marks_refresh clears the staging bytes every tick before the
     * bank-8 bodies run, and the wrapper consumes it in the same frame).
     */
    if (g_bk_byte_a != 0) {
        g_bk_byte_d = SFX_HURT;
    } else if (g_bk_byte_b != 0) {
        g_bk_byte_d = SFX_HEAL;
    } else {
        g_bk_byte_d = SFX_BLOCK;
    }

    /* Organic enemy status rider (Phase D): the enemy's telegraphed card
     * may carry a status (e.g. the bat's poison).  The rider only lands
     * if the swing actually deals damage (net > 0) AND the deterministic roll
     * succeeds; g_bk_byte_c = status id to apply through the fixed wrapper
     * (0 = none).  The roll inlines the xorshift step of rng_next() -- a
     * banked body must not call fixed-bank functions (AGENTS.md §52.11.1);
     * the wrapper applies via the real status_apply + status_grey_apply,
     * so ice also greys the victim's pool. */
    if (net > 0 && b->enemy_played_card.status_id != STATUS_NONE &&
        b->enemy_played_card.status_chance != 0) {
        g_rng_state ^= (uint16_t)(g_rng_state << 7);
        g_rng_state ^= (uint16_t)(g_rng_state >> 9);
        g_rng_state ^= (uint16_t)(g_rng_state << 8);
        if ((uint8_t)g_rng_state < b->enemy_played_card.status_chance) {
            g_bk_byte_c = b->enemy_played_card.status_id;
        }
    }
}
