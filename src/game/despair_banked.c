#pragma bank 8
#pragma disable_warning 110

#include "game.h"
#include "battle.h"
#include "banked.h"
#include "actor.h"

/* ── Unwinnable-boss desperation, game-owned data + check (ROM bank 8) ──
 * THIS game ("Kaartenheld") ends its demo with an unwinnable Lord of
 * Slimes: once the boss drops to its threshold HP it survives (clamped
 * to 1), flashes, announces its focus, and answers the hero's A press
 * with a scripted lethal reprisal into the wake-grove outro.  The
 * engine (battle.c, battle_nav_banked.c, battle_screen.c,
 * ui_battle_content.c, dialogue_screen.c) owns only the GENERIC
 * mechanism -- a cached unkillable slot, the DESPAIR hold phase, the
 * blink, the two-line message layout, WRAM-staged text -- and never
 * names a battle id, a threshold, or an English string (AGENTS.md
 * §55.1).  Everything game-specific lives here.  Because the demo has a
 * single unwinnable boss (BATTLE_NONE, slot 0, threshold 20 HP), the
 * parameters are inlined directly to save bank-8 ROM; reintroduce a
 * per-boss table here if additional despair bosses are added in the future.
 *
 * Self-contained body (AGENTS.md §52.11.1): reads the live battle
 * straight from g_game (WRAM, always mapped) and writes only WRAM +
 * g_bk_byte_c.  No fixed-bank calls.
 * Runs every frame from battle_update (plus the attack tail), so it
 * re-derives the slot each call: no battle-start handshake needed, and
 * non-boss battles simply report 0. */

/* Stage a table string into its fixed-size WRAM buffer (NUL-terminated,
 * never overruns: the table widths match the buffers in battle.c). */
static void despair_stage(char *dst, const char *src, uint8_t n)
{
    uint8_t i;
    for (i = 0; i < n; i++) {
        dst[i] = src[i];
        if (src[i] == '\0') return;
    }
    dst[(uint8_t)(n - 1)] = '\0';
}

void game_despair_check_banked(void)
{
    Battle *b = &g_game.battle;
    Combatant *victim;

    g_bk_byte_c = 0;
    if (b->enemy_battle_id != BATTLE_NONE) {
        g_despair_slot = 0xFF;
        return;
    }
    if (b->enemy_count == 0) return;
    g_despair_slot = 0;
    victim = &b->enemies[0];
    /* HP floor where damage lands (shared by the attack, counter and
     * tick paths through this body): the boss never reads 0 HP, so no
     * defeat telemetry or victory check can ever observe a corpse. */
    if (victim->hp == 0) {
        victim->hp = 1;
    }
    /* One-shot per battle, and never when the hero is already dead (a
     * mutual KO takes the normal defeat exit instead of the outro). */
    if (g_slime_despair != 0 || b->player.hp == 0 || victim->hp > 20) return;
    despair_stage(g_despair_banner, "SLIMES FOCUSING", 16);
    despair_stage(g_despair_row9, "THE LORD OF SLIMES", 19);
    despair_stage(g_despair_row10, "IS CONCENTRATING...", 20);
    g_slime_despair = 1;
    g_bk_byte_c = 1;
}

/* Reprisal damage + message (scripted 99-damage answer to the hero's A press):
 * restages the row buffers the telegraph used, so the DEFEATED! result
 * screen keeps its banner and rows 9+10 read "L. OF SLIMES CASTS /
 * POWERFUL SPELL!" instead of stranding the concentrating text or
 * clearing to bare rows.  Buffer sizes bound the split: row9 19,
 * row10 20 (NUL-included).
 *
 * The 99 damage lands here too, as pure WRAM arithmetic with the same
 * clamp as combatant_take_damage (99 >= hp -> 0, else hp - 99): the
 * fixed-bank A-press path in battle_screen.c spends no second engine
 * call for it (fixed-bank budget, AGENTS.md 52.18).  Equivalent in all
 * cases, including a dead hero (stays 0, same as take_damage). */
void game_despair_reprisal_banked(void)
{
    Battle *b = &g_game.battle;

    despair_stage(g_despair_row9, "L. OF SLIMES CASTS", 19);
    despair_stage(g_despair_row10, "POWERFUL SPELL!", 20);
    if (b->player.hp <= 99) {
        b->player.hp = 0;
    } else {
        b->player.hp = (uint8_t)(b->player.hp - 99);
    }
}
