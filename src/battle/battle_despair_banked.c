#pragma bank 8
#pragma disable_warning 110

#include "game.h"
#include "battle.h"
#include "banked.h"

/* Slime-lord desperation check (banked body, AGENTS.md 52.11.1): the
 * BATTLE_NONE boss never dies.  Once he reaches 20 HP or less -- even on
 * a hit that would reduce him to 0 -- he is clamped to 1 HP and the fixed
 * side opens the telegraph hold (DESPAIR phase, flashing art, rows 9+10
 * message, A to continue); the 99-damage reprisal lands on the A press,
 * not here.  Reads the live battle straight from g_game (WRAM, always
 * mapped -- same pattern as chest_is_open_banked, so no staging is needed
 * and the fixed bank pays only bank+target+run).  Reports via g_bk_byte_c
 * (1 = triggered).  Sets g_slime_despair (WRAM, read by the banked render
 * and the dialogue close).  Self-contained: Battle + WRAM globals only,
 * no fixed-bank calls.  The fixed execute tail stages the hold; the
 * A-press handler owns the 99 + telemetry + result (banked code cannot
 * reach fixed telemetry).  Only the BATTLE_NONE boss reaches here
 * (deckless fallback, solo screen), so testing the engine battle id keeps
 * game content out of the engine (AGENTS.md 55.1).  One-shot per battle
 * (battle_start clears the flag).  Counter/tick paths can never kill the
 * boss either: the ALL_DEAD nav op (bank 4) clamps any lethal blow to 1,
 * so the only exit from this fight is the A-gated reprisal into the demo
 * outro. */
void battle_despair_banked(void)
{
    Battle *b = &g_game.battle;

    g_bk_byte_c = 0;
    if (b->enemy_battle_id != 0) return;
    if (g_slime_despair != 0) return;
    if (b->target_idx >= b->enemy_count) return;
    if (b->enemies[b->target_idx].hp > 20) return;
    if (b->enemies[b->target_idx].hp == 0) {
        b->enemies[b->target_idx].hp = 1;
    }
    g_slime_despair = 1;
    g_bk_byte_c = 1;
}
