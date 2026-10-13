#pragma bank 8

#include <gb/gb.h>
#include <stdint.h>
#include "banked.h"
#include "audio.h"
#include "huge_music.h"
#include "huge_music_data.h"

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
    /* Bank-8 twin of the fixed-bank huge_music_stop() below: identical
     * register writes, minus the __critical (the WRAM trampoline already
     * runs with interrupts disabled).  The banked mapping body calls this
     * because it cannot call fixed-bank code while its own bank is
     * unmapped.  Keep the two in sync -- if one gains a channel, so
     * must the other. */
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
    if (track == MUSIC_BATTLE) {
        song = &song_battle;
    } else if (track == MUSIC_OVERWORLD) {
        song = &song_opening;
        bank = HUGE_MUSIC_BANK_B7;
    } else if (track == MUSIC_CHEST) {
        song = &song_chest_opening;
        bank = HUGE_MUSIC_BANK_B7;
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
    }
    g_audio_current_track = track;
    g_bk_ptr_a = (void *)song;
    g_bk_byte_b = bank;
}
