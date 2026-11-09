#pragma bank 9
#pragma disable_warning 110

#include "battle.h"
#include "game.h"
#include "banked.h"
#include "ui/ui.h"

/* Slime-light heal-flash ramp for BG slot GOLD (6): font ink is palette
 * entry 3 (proved by the steady HUD: banner tan = slot-0 entry 3,
 * heart red = slot-1 entry 3, HP brown = slot-3 entry 3 = numbers
 * ink), so the green MUST live in entry 3 -- entries 0-2 stay
 * fightspider.  #77E331 -> RGB555 (14,28,6) -> 0x1B8E, little-endian
 * 8E 1B.  Programmed ONLY during green pulse frames (see the tick):
 * greyed cards (DIM) and the boss stamp (DIM/fightboss) keep their own
 * ramps the whole time, so only the hero HP block -- the flash's one
 * GOLD user -- ever flashes green.  The restore twin below is a
 * hardcoded copy because bank 9 cannot read the bank-5 ROM table --
 * keep in sync with generated/tiles/base.json slot 6 (fightspider)
 * if the artist ramp moves. */
static const uint8_t s_heal_flash_ramp[8] = { 0xFF, 0x7F, 0x31, 0x46, 0x2B, 0x1D, 0x8E, 0x1B };
/* fightspider slot-6 restore ramp: white; (143,143,143) -> 0x4631; grey
 * (90,74,61) -> 0x1D2B; black. */
static const uint8_t s_heal_flash_restore[8] = { 0xFF, 0x7F, 0x31, 0x46, 0x2B, 0x1D, 0x00, 0x00 };

/* Program (or restore) BG slot GOLD for the heal flash.  Same LCD-safe
 * per-store pattern as ui_load_cram_banked (BANKED_VRAM_WRITE); a
 * no-op on DMG, where BCPS/BCPD are unmapped.  GOLD (fightspider) is
 * otherwise unused by battle BG spans, so the flash never tints enemy
 * art, greyed cards, or the boss stamp. */
static void battle_heal_cram(uint8_t flash_on)
{
    const uint8_t *ramp;
    uint8_t c;
    if (!g_is_cgb) return;
    ramp = flash_on ? s_heal_flash_ramp : s_heal_flash_restore;
    VBK_REG = 0;
    BCPS_REG = (uint8_t)(0x80 | (UI_COLOR_GOLD << 3));
    for (c = 0; c < 8; c++) {
        BANKED_VRAM_WRITE(&BCPD_REG, ramp[c]);
    }
}

/* Battle hit feedback via HP deltas (self-contained body, AGENTS.md
 * 52.11.1: reads the live battle straight from g_game (WRAM, always
 * mapped), plus the WRAM caches below, never calls fixed-bank
 * functions).  An enemy HP drop judders those slots (mask bits, 12
 * frames), a player HP drop judders the whole hand with one shared
 * offset (5 frames), a player HP rise flashes green twice on a white
 * base over the 50-frame heal jingle (the GOLD slot holds the flash
 * ramp only during the two green pulses; every other screen and
 * phase reads fightspider).  Status ticks, counters,
 * overblock heals and debug HP pokes all funnel through HP,
 * so every present and future source feeds the same feedback with no
 * per-source wiring and no fixed-bank bytes in the resolve paths.  The
 * epoch (bumped by battle_start) re-arms the caches, so a new fight
 * never inherits stale deltas; off-battle ticks cancel the flash
 * without touching CRAM (every transition reloads its own set) and
 * otherwise see a static Battle.  Timer countdown re-arms the
 * matching dirty bits so the bank-3/bank-5 painters re-run each frame
 * (battle_update's delay branch sets no dirty flags of its own).
 * Lives in bank 9 (release bank 5 cannot hold it); dispatched every
 * frame from game_update under the same non-terminal gate as the
 * frame tick. */
void battle_feedback_tick_banked(void)
{
    Battle *b = &g_game.battle;
    uint8_t k;
    uint8_t np;

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
    if (g_game.screen != SCREEN_BATTLE) {
        /* Off-battle: cancel an in-flight flash WITHOUT touching CRAM.
         * Every transition reloads its own set (overworld/item/shop/
         * dialogue/battle all program CRAM on entry), and terminal
         * screens never read DIM -- so the programmed slot is already
         * correct wherever we landed.  Shake/deck timers freeze
         * harmlessly (no painter runs off-battle; the epoch re-arm
         * clears them). */
        g_battle_heal_flash = 0;
        g_battle_heal_phase = 0;
        return;
    }
    if (b->player.hp < g_battle_fx_php) {
        /* Player hit: the whole hand judders, unless the fight ended
         * in victory on the same frame (a killing counter lands
         * damage and victory together -- shaking then would clobber
         * the loot reveal rows the hand painter wipes).  Defeat --
         * including the scripted desperation reprisal -- keeps the
         * shake: nothing overwrites the hand zone there. */
        if (b->result != BATTLE_RESULT_VICTORY) {
            g_battle_deck_timer = 5;
        }
    } else if (b->player.hp > g_battle_fx_php) {
        /* 50-frame flash, white base with two slime-light green pulses
         * (10 frames each), spanning the 47-tick heal jingle: green
         * appears twice instead of vanishing twice.  The GOLD slot is
         * programmed only on green-pulse frames below (never for the
         * whole flash), so greyed cards and the boss stamp keep their
         * own ramps except during the pulses themselves. */
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
        if (b->enemies[k].hp < g_battle_fx_ehp[k] && b->enemies[k].hp != 0) {
            /* A killing blow never judders: the slot blanks instead,
             * and the result screen needs no shake timers running. */
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
         * The CRAM program follows the phase edges, not the flash:
         * GOLD holds the flash ramp only while a green pulse is on
         * screen (greyed cards and the boss stamp read their own ramps
         * the whole time).  Thresholds live here, not in the painter:
         * release bank 3 has no room for the math. */
        np = (uint8_t)(((g_battle_heal_flash <= 40 &&
            g_battle_heal_flash > 30) || (g_battle_heal_flash <= 20 &&
            g_battle_heal_flash > 10)) ? 1 : 0);
        if (np != g_battle_heal_phase) {
            battle_heal_cram(np);
            g_battle_heal_phase = np;
        }
        b->dirty |= BATTLE_DIRTY_HERO;
    }
    /* The deck timer only runs outside a victory result (see the arming
     * gate): freezing it on the loot reveal leaves nothing to strand,
     * and the epoch re-arm clears it for the next fight. */
    if (g_battle_deck_timer != 0 && b->result != BATTLE_RESULT_VICTORY) {
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
