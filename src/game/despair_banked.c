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
 * §55.1).  Everything game-specific lives in this table.
 *
 * Self-contained body (AGENTS.md §52.11.1): reads the live battle
 * straight from g_game (WRAM, always mapped), the table below (same
 * bank), and writes only WRAM + g_bk_byte_c.  No fixed-bank calls.
 * Runs every frame from battle_update (plus the attack tail), so it
 * re-derives the slot each call: no battle-start handshake needed, and
 * non-boss battles simply report 0. */

typedef struct {
    uint8_t battle_id;      /* engine battle id (BATTLE_NONE = lord) */
    uint8_t threshold;      /* trigger at this HP or less */
    uint8_t boss_slot;      /* enemy slot that cannot die */
    const char *banner;     /* row-0 label (<= 18 chars, screen margin) */
    const char *row9;       /* message first half (18 cols) */
    const char *row10;      /* message second half (19 cols) */
} DespairConfig;

static const DespairConfig s_despair[] = {
    { BATTLE_NONE, 20, 0,
      "SLIMES FOCUSING", "THE LORD OF SLIMES", "IS CONCENTRATING..." },
};

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
    const DespairConfig *cfg = 0;
    Combatant *victim;
    uint8_t i;

    g_bk_byte_c = 0;
    for (i = 0; i < (uint8_t)(sizeof(s_despair) / sizeof(s_despair[0])); i++) {
        if (b->enemy_battle_id == s_despair[i].battle_id) {
            cfg = &s_despair[i];
            break;
        }
    }
    if (!cfg) {
        g_despair_slot = 0xFF;
        return;
    }
    if (cfg->boss_slot >= b->enemy_count) return;
    g_despair_slot = cfg->boss_slot;
    victim = &b->enemies[cfg->boss_slot];
    /* HP floor where damage lands (shared by the attack, counter and
     * tick paths through this body): the boss never reads 0 HP, so no
     * defeat telemetry or victory check can ever observe a corpse. */
    if (victim->hp == 0) {
        victim->hp = 1;
    }
    /* One-shot per battle, and never when the hero is already dead (a
     * mutual KO takes the normal defeat exit instead of the outro). */
    if (g_slime_despair != 0) return;
    if (b->player.hp == 0) return;
    if (victim->hp > cfg->threshold) return;
    despair_stage(g_despair_banner, cfg->banner, 16);
    despair_stage(g_despair_row9, cfg->row9, 19);
    despair_stage(g_despair_row10, cfg->row10, 20);
    g_slime_despair = 1;
    g_bk_byte_c = 1;
}
