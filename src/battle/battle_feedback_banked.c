#pragma bank 9
#pragma disable_warning 110

#include "battle.h"
#include "game.h"

/* Battle hit feedback via HP deltas (self-contained body, AGENTS.md
 * 52.11.1: reads the live battle straight from g_game (WRAM, always
 * mapped), plus the WRAM caches below, never calls fixed-bank
 * functions).  An enemy HP drop judders those slots (mask bits, 12
 * frames), a player HP drop judders the whole hand with one shared
 * offset (5 frames), a player HP rise flashes green twice on a white
 * base over the 50-frame heal jingle.  Status ticks, counters,
 * overblock heals and debug HP pokes all funnel through HP,
 * so every present and future source feeds the same feedback with no
 * per-source wiring and no fixed-bank bytes in the resolve paths.  The
 * epoch (bumped by battle_start) re-arms the caches, so a new fight
 * never inherits stale deltas; off-battle ticks see a static Battle
 * and stay silent.  Timer countdown re-arms the matching dirty bits
 * so the bank-3/bank-5 painters re-run each frame (battle_update's
 * delay branch sets no dirty flags of its own).  Lives in bank 9
 * (release bank 5 cannot hold it); dispatched every frame from
 * game_update under the same non-terminal gate as the frame tick. */
void battle_feedback_tick_banked(void)
{
    Battle *b = &g_game.battle;
    uint8_t k;

    if (g_battle_fx_seen != g_battle_fx_epoch) {
        g_battle_fx_seen = g_battle_fx_epoch;
        g_battle_shake_mask = 0;
        g_battle_shake_timer = 0;
        g_battle_shake_wipe = 0;
        g_battle_heal_flash = 0;
        g_battle_heal_phase = 0;
        g_battle_deck_timer = 0;
        g_battle_deck_wipe = 0;
        g_battle_fx_php = b->player.hp;
        for (k = 0; k < MAX_BATTLE_ENEMIES; k++) {
            g_battle_fx_ehp[k] = b->enemies[k].hp;
        }
        return;
    }
    if (b->player.hp < g_battle_fx_php) {
        /* Player hit: the whole hand judders, unless the fight is
         * already decided (a killing counter lands damage and victory
         * on the same frame -- shaking then would clobber the loot
         * reveal rows the hand painter wipes). */
        if (b->result == BATTLE_RESULT_NONE) {
            g_battle_deck_timer = 5;
        }
    } else if (b->player.hp > g_battle_fx_php) {
        /* 50-frame flash, white base with two green pulses (10 frames
         * each), spanning the 47-tick heal jingle: green appears
         * twice instead of vanishing twice, so the flash reads green.
         * Ends white so the return to steady green snaps visibly. */
        g_battle_heal_flash = 50;
    }
    g_battle_fx_php = b->player.hp;
    /* Fresh judder replaces the old mask (a previous shake always
     * clears it on expiry below, so a set mask here means genuinely
     * simultaneous damage -- e.g. one poison tick felling two slots). */
    if (g_battle_shake_timer == 0) {
        g_battle_shake_mask = 0;
    }
    for (k = 0; k < b->enemy_count; k++) {
        if (b->enemies[k].hp < g_battle_fx_ehp[k]) {
            g_battle_shake_mask |= (uint8_t)(1u << k);
            g_battle_shake_timer = 12;
        }
        g_battle_fx_ehp[k] = b->enemies[k].hp;
    }
    if (g_battle_shake_timer != 0) {
        g_battle_shake_timer--;
        b->dirty |= (BATTLE_DIRTY_BLINK | BATTLE_DIRTY_ENEMIES);
        if (g_battle_shake_timer == 0) {
            /* Judder over: drop the mask (otherwise the next hit
             * shakes every previously hit slot too) and wipe the
             * band once so no shifted stamp strands. */
            g_battle_shake_mask = 0;
            g_battle_shake_wipe = 1;
        }
    }
    if (g_battle_heal_flash != 0) {
        g_battle_heal_flash--;
        /* Pulse phase for the 50-frame flash: white 50-41, green 40-31,
         * white 30-21, green 20-11, white 10-1 (two green pulses over
         * the heal jingle, snapping back to steady green at the end).
         * Thresholds live here, not in the painter: release bank 3
         * has no room for the math. */
        g_battle_heal_phase = (uint8_t)(((g_battle_heal_flash <= 40 &&
            g_battle_heal_flash > 30) || (g_battle_heal_flash <= 20 &&
            g_battle_heal_flash > 10)) ? 1 : 0);
        b->dirty |= BATTLE_DIRTY_HERO;
    }
    /* The deck timer only runs while undecided (see the arming gate):
     * freezing it on a result screen leaves nothing to strand, and
     * the epoch re-arm clears it for the next fight. */
    if (g_battle_deck_timer != 0 && b->result == BATTLE_RESULT_NONE) {
        g_battle_deck_timer--;
        b->dirty |= BATTLE_DIRTY_HAND;
        if (g_battle_deck_timer == 0) {
            /* One-shot wipe (see the hand painter): the next hand
             * redraw clears the zone once, so no shifted frame,
             * marker or tint strands after the judder. */
            g_battle_deck_wipe = 1;
        }
    }
}
