#include "audio.h"
#include "telemetry.h"
#include "huge_music.h"
#include "huge_music_data.h"
#include "sfx_tables.h"

MusicTrack g_audio_current_track = MUSIC_NONE;
uint8_t g_sound_enabled = 1;

/* ── SFX layer (transcribed tracker SFX) ────────────────────────────
 * Music runs through hUGEDriver.  Effect sounds use channels 2 and 4 so
 * they never collide with the CH1 music voice.  Each SFX id voices the
 * step tables in generated/sfx/sfx_tables.c (transcribed from the
 * assets-sfx tracker files by tools/transcribe_sfx.py): score CH1 renders
 * to the CH2 voice, score CH4 renders verbatim.  When hUGEDriver music is
 * active, the used music channels are muted during SFX playback and
 * unmuted at the table end. */
#define SFX_NONE 0xFF
/* ROM bank holding the transcribed-SFX stepper body + tables
 * (src/audio/sfx_step.c, generated/sfx/sfx_tables.c).  Bank 6 holds the
 * driver + six tracker songs and is full, so SFX live in bank 7. */
#define SFX_STEP_BANK 7
/* Cursor state shared with the bank-7 stepper (src/audio/sfx_step.c):
 * plain WRAM globals, readable from any bank. */
/* Harness-visible trigger log (AGENTS.md 53.7: semantic, not transport):
 * per-trigger SFX telemetry would flood the 32-entry gameplay ring and
 * evict gameplay events scenarios assert on, so triggers land here
 * instead: total count + last id, read by name from host tools. */
uint16_t g_sfx_played_count = 0;
uint8_t g_sfx_last_id = SFX_NONE;
uint8_t sfx_id = SFX_NONE;
uint8_t sfx_tick = 0;
uint8_t sfx_tone_idx = 0;
uint8_t sfx_noise_idx = 0;
static uint8_t sfx_div = 0;
static uint8_t sfx_muted = 0;

extern uint8_t sfx_step_tick(void);
extern void mimic_chain_tick(void);

void audio_play_sfx(uint8_t s)
{
    uint8_t voices;

    if (!g_sound_enabled) return;
    if (s > SFX_BLOCK) return;
    sfx_id = s;
    sfx_tick = 0;
    sfx_tone_idx = 0;
    sfx_noise_idx = 0;
    sfx_div = 0;
    sfx_muted = 0;
    voices = s_sfx_voices[s];
    if (voices & 0x01) {
        huge_music_mute_channel(HT_CH2, HT_CH_MUTE);
        sfx_muted |= 0x01;
    }
    if (voices & 0x02) {
        huge_music_mute_channel(HT_CH4, HT_CH_MUTE);
        sfx_muted |= 0x02;
    }
    g_sfx_played_count++;
    g_sfx_last_id = s;
}

#ifdef DEBUG_BUILD
volatile uint16_t g_audio_ticks = 0;
#endif

/* All music is tracked (hUGEDriver) now: the old hardcoded chiptune
 * note-table engine (note freqs, per-track note arrays, play_note) was
 * removed per docs/uge.md Phase 6.  MUSIC_OVERWORLD and MUSIC_VICTORY
 * have no authored .uge yet, so they play nothing; MUSIC_TITLE plays
 * song_title (assets/music/title short.uge). */

void audio_init(void)
{
    NR52_REG = 0x80;
    NR50_REG = 0x77;
    NR51_REG = 0xFF;
    g_audio_current_track = MUSIC_NONE;
    huge_music_init();

    TAC_REG = 0x00;
    TMA_REG = 0x00;
    TIMA_REG = 0x00;
    /* TACF_START | TACF_65KHZ = 0x06: 65536 Hz clock -> TIMA overflows at
     * 256 Hz (TMA=0), one audio_update() per overflow (crt0.s timer ISR).
     * 0x05 (TACF_16KHZ) would select the 262144 Hz clock -> a 1024 Hz music
     * clock, 4x too fast. */
    TAC_REG = 0x06;
}

void audio_play_music(MusicTrack track)
{
    if (g_audio_current_track == track) return;
    if (!g_sound_enabled) return;
    /* Suppress the ISR while switching: set MUSIC_NONE first so the timer
     * interrupt never steps the new track mid-switch. */
    g_audio_current_track = MUSIC_NONE;
    huge_music_stop();

    g_audio_current_track = track;
    if (track == MUSIC_BATTLE) {
        huge_music_play(&song_battle);
    } else if (track == MUSIC_DESOLATE) {
        huge_music_play(&song_desolate_landscape);
    } else if (track == MUSIC_FOREST) {
        huge_music_play(&song_forest);
    } else if (track == MUSIC_BOSS) {
        huge_music_play(&song_boss_fight);
    } else if (track == MUSIC_MIMIC) {
        /* Intro one-shot first; the bank-7 mimic_chain_tick() body swaps
         * in the looping song when the intro ends (self-arming, so no
         * fixed-bank countdown is needed). */
        huge_music_play_banked(&song_mimic_intro, HUGE_MUSIC_BANK_B7);
    } else if (track == MUSIC_TOWN) {
        huge_music_play(&song_village);
    } else if (track == MUSIC_DUNGEON) {
        huge_music_play(&song_castle);
    } else if (track == MUSIC_TITLE) {
        huge_music_play(&song_title);
    } else if (track == MUSIC_VICTORY) {
        huge_music_play_banked(&song_victory, HUGE_MUSIC_BANK_B7);
    }
    /* MUSIC_OVERWORLD has no authored .uge yet: it stays silent (the
     * track still reports correctly via telemetry). */

    /* Centralized MUSIC_CHANGED telemetry (AGENTS.md 8): emitted only when
     * the track actually changes, so callers never forget it. */
    telemetry_emit(EVENT_MUSIC_CHANGED, track, 0, 0, 0);
}

MusicTrack audio_get_current_track(void)
{
    return g_audio_current_track;
}

void audio_update(void)
{
#ifdef DEBUG_BUILD
    g_audio_ticks++;
#endif

    /* Step the transcribed SFX at the 64 Hz tracker rate through the
     * bank-7 stepper body (inline select-7/call/restore-1; the music path
     * below selects bank 6 separately in the same ISR). At the table
     * end the used voices are silenced and their music channels unmuted. */
    if (sfx_id <= SFX_BLOCK) {
        if (++sfx_div >= 4) {
            sfx_div = 0;
            *(volatile uint8_t *)0x2000 = SFX_STEP_BANK;
            if (sfx_step_tick()) {
                if (sfx_muted & 0x01) {
                    NR22_REG = 0x00;
                    /* ISR context: the __critical wrapper's ei() would
                     * nest timer interrupts (WRAM smash, ghost input). */
                    huge_music_mute_channel_isr(HT_CH2, HT_CH_PLAY);
                }
                if (sfx_muted & 0x02) {
                    NR42_REG = 0x00;
                    huge_music_mute_channel_isr(HT_CH4, HT_CH_PLAY);
                }
                sfx_muted = 0;
                sfx_id = SFX_NONE;
            }
            *(volatile uint8_t *)0x2000 = 1;
        }
    }

    if (g_audio_current_track == MUSIC_NONE) return;

    /* Mimic intro -> loop service (bank-7 body, same select-7/call/
     * restore-1 dispatch as the SFX stepper above; stays ISR-safe: no
     * di/ei inside). */
    *(volatile uint8_t *)0x2000 = HUGE_MUSIC_BANK_B7;
    mimic_chain_tick();
    *(volatile uint8_t *)0x2000 = 1;

    /* All playback is tracked now; tracks without a song (OVERWORLD,
     * VICTORY) simply stay silent. */
    if (huge_music_is_playing()) {
        huge_music_update();
    }
}
