#pragma bank 8

#include <gb/gb.h>
#include <stdint.h>
#include "banked.h"
#include "audio.h"
#include "huge_music.h"
#include "huge_music_data.h"

/* Title-theme play-once counter (src/game/jingle_full_banked.c, bank 8
 * BSS): reset here on every MUSIC_TITLE dispatch so replays start the
 * 6 s window from the top.  Same bank: plain extern, no trampoline. */
extern uint16_t g_title_frames;

/* Chest-jingle edge clock (same bank-8 BSS): reset here on every
 * MUSIC_CHEST dispatch so a re-triggered jingle (the repeatable Heal
 * Mage replays it mid-tail) gets its full restore window instead of
 * inheriting a stale count. */
extern volatile uint8_t g_jingle_frames;

/* ── Track mapping body (bank-8) ────────────────────────────────────
 * Dispatched by audio_play_music() through the WRAM trampoline (the
 * fixed bank is full, §52.18).  PURE mapping: stop-inline (sound regs
 * + WRAM only, bank-safe), track -> (song, bank) lookup, set track.
 * Returns song in g_bk_ptr_a (NULL = stop silently), bank in
 * g_bk_byte_b.  The fixed wrapper performs the MBC select + driver
 * call + restore.
 *
 * This split is structural, not stylistic: a banked body must NEVER
 * switch away its own bank mid-function.  After `*(0x2000) = other`
 * the CPU fetches the body's own continuation bytes through the NEW
 * bank's window (song data executes as code and the guest spins).
 * Fixed-bank code is immune (bank 0 never remaps); banked bodies stay
 * pure (WRAM + regs + same-bank calls only).  The jingle body obeys
 * the same rule (no selects, no cross-bank calls). */

static void huge_stop_inline(void)
{
    /* Sole stop implementation (the fixed-bank twin was removed to fund
     * the farewell dialogue): identical register writes, no __critical.  Banked bodies must not emit
     * di/ei (lint gate; a held di would stall the music clock), and none
     * is needed: the banked mapping body stops playback first
     * (g_huge_playing = 0) before resolving the new track, so a
     * preempting timer ISR always sees either silence or a fully-switched
     * song.  This body is the only stop path left, so every channel it
     * silences here is the whole contract. */
    g_huge_playing = 0;
    g_huge_current_song = 0;
    /* Silence active sound channels */
    NR12_REG = 0x00;
    NR14_REG = 0x80;
    NR22_REG = 0x00;
    NR24_REG = 0x80;
    NR32_REG = 0x00;
    NR42_REG = 0x00;
    NR44_REG = 0x80;
}

/* Track -> (song, bank) + stop + set, one round-trip.  Reads the track
 * from g_bk_byte_a (MusicTrack, audio.h).  MUSIC_NONE stops silently.
 * Stopping first (with the track already switched, like the old fixed
 * interlock) means the ISR never steps a half-switched song.  Returns
 * song in g_bk_ptr_a, bank in g_bk_byte_b; the fixed wrapper selects
 * the bank and calls the matching driver copy. */
void music_play_banked(void)
{
    const hUGESong_t *song = 0;
    uint8_t bank = HUGE_MUSIC_BANK;
    uint8_t track = g_bk_byte_a;
    huge_stop_inline();
    if (track == MUSIC_TITLE) g_title_frames = 0;
    if (track == MUSIC_BATTLE) {
        song = &song_battle;
    } else if (track == MUSIC_OVERWORLD) {
        song = &song_opening;
        bank = HUGE_MUSIC_BANK_B7;
    } else if (track == MUSIC_CHEST) {
        song = &song_chest_opening;
        bank = HUGE_MUSIC_BANK_B7;
        g_jingle_frames = 0;
    } else if (track == MUSIC_DESOLATE) {
        song = &song_desolate_landscape;
    } else if (track == MUSIC_FOREST) {
        song = &song_forest;
    } else if (track == MUSIC_BOSS) {
        song = &song_boss_fight;
    } else if (track == MUSIC_MIMIC) {
        /* Intro one-shot first; the bank-7 mimic_chain_tick() body swaps
         * in the looping song when the intro ends (self-arming). */
        song = &song_mimic_intro;
        bank = HUGE_MUSIC_BANK_B7;
    } else if (track == MUSIC_TOWN) {
        song = &song_village;
    } else if (track == MUSIC_DUNGEON) {
        song = &song_castle;
    } else if (track == MUSIC_TITLE) {
        song = &song_title;
    } else if (track == MUSIC_VICTORY) {
        song = &song_victory;
        bank = HUGE_MUSIC_BANK_B7;
    } else if (track == MUSIC_GAMEOVER) {
        song = &song_game_over;
        bank = HUGE_MUSIC_BANK_B7;
    } else if (track == MUSIC_ENDING) {
        song = &song_demo_complete;
        bank = HUGE_MUSIC_BANK_B7;
    } else if (track == MUSIC_CASINO) {
        song = &song_casino;
        bank = HUGE_MUSIC_BANK_B9;
    } else if (track == MUSIC_UNDERGROUND) {
        song = &song_underground;
        bank = HUGE_MUSIC_BANK_B9;
    }
    g_audio_current_track = track;
    g_bk_ptr_a = (void *)song;
    g_bk_byte_b = bank;
}
