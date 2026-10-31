#pragma bank 8
#pragma disable_warning 110

#include "game.h"
#include "battle.h"
#include "banked.h"

/* Slime-lord desperation check+apply (banked body, AGENTS.md 52.11.1):
 * the BATTLE_NONE boss never dies.  Once he reaches 20 HP or less -- even
 * on a hit that would reduce him to 0 -- he survives (clamped to 1) and
 * answers with a 99-damage reprisal.  Reads the live battle straight from
 * g_game (WRAM, always mapped -- same pattern as chest_is_open_banked, so
 * no staging is needed and the fixed bank pays only bank+target+run).
 * Reports via g_bk_byte_c (1 = triggered).  Sets g_slime_despair (WRAM).
 * Self-contained: Battle + WRAM globals only, no fixed-bank calls.  The
 * fixed wrapper owns the telemetry + result transition (banked code
 * cannot reach fixed telemetry).  Only the BATTLE_NONE boss reaches here
 * (deckless fallback, solo screen), so testing the engine battle id keeps
 * game content out of the engine (AGENTS.md 55.1).  One-shot per battle
 * (battle_start clears the flag).  Counter/tick paths can never kill the
 * boss either: the ALL_DEAD nav op (bank 4) clamps any lethal blow to 1,
 * so the only exit from this fight is this reprisal into the demo outro.
 * (A counter or tick that drops him into range merely sets up the next
 * attack to trigger it: single-step counter/tick damage can never bridge
 * >20 straight to 0 with real decks.) */
void battle_despair_banked(void)
{
    Battle *b = &g_game.battle;
    uint8_t hp;

    g_bk_byte_c = 0;
    if (b->enemy_battle_id != 0) return;
    if (g_slime_despair != 0) return;
    if (b->target_idx >= b->enemy_count) return;
    if (b->enemies[b->target_idx].hp > 20) return;
    if (b->enemies[b->target_idx].hp == 0) {
        b->enemies[b->target_idx].hp = 1;
    }
    hp = b->player.hp;
    b->player.hp = (hp > 99) ? (uint8_t)(hp - 99) : 0;
    g_slime_despair = 1;
    g_bk_byte_c = 1;
}
