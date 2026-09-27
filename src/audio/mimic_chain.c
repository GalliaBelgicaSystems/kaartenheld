#pragma bank 7

#include <gb/gb.h>
#include <stdint.h>
#include "audio.h"
#include "huge_music.h"
#include "huge_music_data.h"

/* ── Mimic intro -> loop service (bank-7 body) ──────────────────────
 * Dispatched inline from audio_update() with bank 7 selected (select-7/
 * call/restore-1), the same pattern as the SFX stepper (sfx_step.c): the
 * fixed bank cannot afford the swap code (§52.18), and the WRAM
 * banked-call trampoline is unusable here (its trailing ei would nest
 * the timer ISR).  Calls only the in-bank hUGE_init_b7.
 *
 * The tracker driver loops whole songs unconditionally (the ROM build
 * ignores loop_order), so the intro cannot live in the looping song.
 * MUSIC_MIMIC starts the one-shot intro (Mimic_intro.uge: 1 order); the
 * intro is a short descending sting on rows 0-7 with an explicit note
 * cut on row 8 (last audible tick ~50), so this body swaps in the
 * looping Mimic.uge at 56 tracker ticks (224 timer ticks, ~0.9 s) --
 * right after the sting dies, skipping the order's remaining silence.  The body is self-arming: any non-mimic track (victory, flee,
 * overworld return) or the already-looping song holds the counter at
 * zero, so a quick victory/flee preempts a mid-intro battle cleanly with
 * no fixed-bank cancel logic.  The debug harness skips the timer ISR,
 * so scenarios only ever observe MUSIC_MIMIC. */

/* Intro length in 256 Hz timer-ISR ticks (56 tracker ticks * 4).  The
 * body is self-arming: any non-mimic track (victory, flee, overworld
 * return) or the already-looping song holds the counter at zero, so a
 * quick victory/flee preempts a mid-intro battle cleanly with no
 * fixed-bank cancel logic.  The debug harness skips the timer ISR, so
 * scenarios only ever observe MUSIC_MIMIC. */
#define MIMIC_INTRO_ISR_TICKS 224

uint16_t g_mimic_intro_left = 0;

void mimic_chain_tick(void)
{
    /* Not our battle, paused/quiet, or already looping: hold at zero. */
    if (g_audio_current_track != MUSIC_MIMIC || !g_huge_playing ||
        g_huge_current_song == &song_mimic) {
        g_mimic_intro_left = 0;
        return;
    }
    /* The loop song is only ever started by the swap below, so reaching
     * here with the intro current means the intro is running: count its
     * ticks, then swap to the loop. */
    if (g_huge_current_song == &song_mimic_intro) {
        if (++g_mimic_intro_left >= MIMIC_INTRO_ISR_TICKS) {
            g_huge_playing = 0;
            g_huge_current_song = &song_mimic;
            g_huge_music_bank = HUGE_MUSIC_BANK_B7;
            hUGE_init_b7(&song_mimic);
            g_huge_tick_divider = 0;
            g_huge_playing = 1;
            g_mimic_intro_left = 0;
        }
    }
}
