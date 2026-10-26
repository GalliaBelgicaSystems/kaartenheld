#include "audio.h"
#include "telemetry.h"
#include "huge_music.h"
#include "huge_music_data.h"
#include "sfx_tables.h"
#include "scene.h"
#include "banked.h"

MusicTrack g_audio_current_track;  /* BSS zero == MUSIC_NONE (52.27) */
/* BSS (no _INITIALIZER cost): game_init() re-asserts the sound-on default
 * before any audio_play_music() call (the harness skips CRT0's .data copy,
 * so the initializer never reached it anyway). */
uint8_t g_sound_enabled;

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
 * (src/audio/sfx_step.c, generated/sfx/sfx_tables.c).  Banks 6/7 hold
 * drivers + tracker songs and are full, so SFX live in bank 8. */
#define SFX_STEP_BANK 8
/* Cursor state shared with the bank-8 stepper (src/audio/sfx_step.c):
 * plain WRAM globals, readable from any bank. */
/* Harness-visible trigger log (AGENTS.md 53.7: semantic, not transport):
 * per-trigger SFX telemetry would flood the 32-entry gameplay ring and
 * evict gameplay events scenarios assert on, so triggers land here
 * instead: total count + last id, read by name from host tools.
 * Debug-only: the release ROM never reads them. */
#ifdef DEBUG_BUILD
uint16_t g_sfx_played_count;  /* BSS zero (AGENTS.md 52.27) */
/* BSS zero, then audio_init() sets SFX_NONE: a non-zero initializer is
 * never copied by the custom CRT0 (AGENTS.md 52.27), and the stale 0
 * would look like SFX_CURSOR -- the ISR stepped a phantom cursor blip at
 * every boot. */
uint8_t g_sfx_last_id;
#endif
uint8_t sfx_id;
uint8_t sfx_tick;  /* BSS zero (AGENTS.md 52.27) */
uint8_t sfx_tone_idx;  /* BSS zero (AGENTS.md 52.27) */
uint8_t sfx_noise_idx;  /* BSS zero (AGENTS.md 52.27) */
static uint8_t sfx_div;  /* BSS (52.27) */
static uint8_t sfx_muted;  /* BSS (52.27) */

extern uint8_t sfx_step_tick(void);
extern void mimic_chain_tick(void);
extern uint16_t g_mimic_intro_left;

void audio_play_sfx(uint8_t s)
{
    uint8_t voices;

    if (!g_sound_enabled) return;
    if (s > SFX_HEAL) return;  /* SFX_HEAL is the last id (audio.h) */
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
#ifdef DEBUG_BUILD
    g_sfx_played_count++;
    g_sfx_last_id = s;
#endif
}

#ifdef DEBUG_BUILD
volatile uint16_t g_audio_ticks;  /* BSS zero (AGENTS.md 52.27) */
#endif

/* All music is tracked (hUGEDriver) now: the old hardcoded chiptune
 * note-table engine (note freqs, per-track note arrays, play_note) was
 * removed per docs/uge.md Phase 6.  MUSIC_OVERWORLD plays the opening
 * theme (assets/music/opening.uge, bank 7); MUSIC_TITLE plays song_title
 * (assets/music/title short.uge). */

void audio_init(void)
{
    NR52_REG = 0x80;
    NR50_REG = 0x77;
    NR51_REG = 0xFF;
    g_audio_current_track = MUSIC_NONE;
    huge_music_init();
    /* Explicit init (AGENTS.md 52.27): CRT0 does not copy _INITIALIZER. */
    sfx_id = SFX_NONE;
#ifdef DEBUG_BUILD
    g_sfx_last_id = SFX_NONE;
    g_sfx_played_count = 0;
#endif

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
    const hUGESong_t *song;
    uint8_t bank;
    extern void music_play_banked(void);
    /* Loop tracks are idempotent, but one-shot tracks (chest jingle,
     * title fanfare) restart when re-requested: the repeatable Heal
     * Mage replays MUSIC_CHEST on every visit, even mid-jingle, and
     * music_play_banked resets that jingle's restore clock. */
    if (g_audio_current_track == track && track != MUSIC_CHEST && track != MUSIC_TITLE) return;
    if (!g_sound_enabled) return;
    /* Track -> (song, bank) resolves in the bank-8 body (fixed bank is
     * full); the MBC select + driver call stay fixed (a banked body
     * must never switch away its own bank).  The body stops the old
     * song first with the track already switched, so the ISR never
     * steps a half-switched song. */
    g_audio_current_track = MUSIC_NONE;
    g_bk_call_bank = 8;
    g_bk_call_target = (uint16_t)&music_play_banked;
    g_bk_byte_a = track;
    banked_call_run();
    song = (const hUGESong_t *)g_bk_ptr_a;
    bank = g_bk_byte_b;
    if (song) {
        huge_music_play_banked(song, bank);
    }

    /* Centralized MUSIC_CHANGED telemetry (AGENTS.md 8): emitted only when
     * the track actually changes, so callers never forget it. */
    telemetry_emit(EVENT_MUSIC_CHANGED, track, 0, 0, 0);
}

void audio_update(void)
{
#ifdef DEBUG_BUILD
    g_audio_ticks++;
#endif
    /* ISR bank discipline: this runs inside the 256 Hz timer ISR, which
     * can now interrupt a banked body (the trampolines run bodies with
     * IME enabled so the music clock never stalls).  The SFX-branch
     * restore and the tail restore read the live bank via
     * g_rom_bank_live (banked.h) instead of home bank 1: restoring home
     * would resume an interrupted body on the wrong bank.  The sub-calls
     * (stepper/mute/driver/update) restore nothing; the tail covers them
     * (this function and its fixed callees live in ROM0 <0x4000, always
     * mapped -- enforced by the memmap ISR-ROM0 check; only WRAM/I-O is
     * touched between the selects and the tail). */
    /* Step the transcribed SFX at the 64 Hz tracker rate through the
     * bank-8 stepper body (inline select-8/call/restore-live; the music path
     * below selects bank 6 separately in the same ISR). At the table
     * end the used voices are silenced and their music channels unmuted. */
    if (sfx_id <= SFX_HEAL) {  /* last id; see audio_play_sfx's guard */
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
            *(volatile uint8_t *)0x2000 = g_rom_bank_live;
        }
    }

    if (g_audio_current_track == MUSIC_NONE) return;

    /* Mimic intro -> loop service (bank-7 body, same select-7/call
     * pattern as the SFX stepper above; stays ISR-safe: no di/ei
     * inside).  No restore here either: the tail below covers it.
     * Guarded: the 256 Hz ISR skips the two bank switches + call on
     * every non-mimic tick (the common case).  The counter arm covers
     * the transient -- a mid-intro track switch leaves it nonzero,
     * which re-arms exactly one body run that zeroes it. */
    if (g_audio_current_track == MUSIC_MIMIC || g_mimic_intro_left != 0) {
        *(volatile uint8_t *)0x2000 = HUGE_MUSIC_BANK_B7;
        mimic_chain_tick();
    }

    /* All playback is tracked now; tracks without a song (OVERWORLD,
     * VICTORY) simply stay silent.  No is_playing gate here: the divider
     * body re-checks it, so gating twice costs a call on every 256 Hz
     * tick for no reason. */
    huge_music_update();

    /* ISR exit bank discipline: the inline selects above (SFX stepper,
     * mimic chain, tracker update, ISR mutes) leave transient mappings;
     * re-establish the interrupted live bank here, once, so a preempted
     * banked body resumes on its own bank.  No 0x4000-window fetch can
     * land between those selects and this restore (this function and its
     * fixed callees live in ROM0 <0x4000 -- enforced by the memmap
     * ISR-ROM0 check; only WRAM/I-O touched in between). */
    *(volatile uint8_t *)0x2000 = g_rom_bank_live;
}
