#pragma bank 9
#pragma disable_warning 110

#include "battle.h"
#include "game.h"

/* Battle hit feedback via HP deltas (self-contained body, AGENTS.md
 * 52.11.1: reads the live battle straight from g_game (WRAM, always
 * mapped), plus the WRAM caches below, never calls fixed-bank
 * functions).  An enemy HP drop judders that slot (12 frames), a
 * player HP drop judders the whole hand (12 frames), a player HP rise
 * blinks the hero HP block green twice (16 frames).  Status ticks,
 * counters, overblock heals and debug HP pokes all funnel through HP,
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
        g_battle_fx_php = b->player.hp;
        for (k = 0; k < MAX_BATTLE_ENEMIES; k++) {
            g_battle_fx_ehp[k] = b->enemies[k].hp;
        }
        return;
    }
    if (b->player.hp < g_battle_fx_php) {
        g_battle_deck_timer = 12;
    } else if (b->player.hp > g_battle_fx_php) {
        g_battle_heal_flash = 16;
    }
    g_battle_fx_php = b->player.hp;
    for (k = 0; k < b->enemy_count; k++) {
        if (b->enemies[k].hp < g_battle_fx_ehp[k]) {
            g_battle_shake_slot = k;
            g_battle_shake_timer = 12;
        }
        g_battle_fx_ehp[k] = b->enemies[k].hp;
    }
    if (g_battle_shake_timer != 0) {
        g_battle_shake_timer--;
        b->dirty |= (BATTLE_DIRTY_BLINK | BATTLE_DIRTY_ENEMIES);
    }
    if (g_battle_heal_flash != 0) {
        g_battle_heal_flash--;
        b->dirty |= BATTLE_DIRTY_HERO;
    }
    if (g_battle_deck_timer != 0) {
        g_battle_deck_timer--;
        b->dirty |= BATTLE_DIRTY_HAND;
    }
}
