#pragma bank 7

#include <gb/gb.h>
#include <stdint.h>
#include "banked.h"
#include "audio.h"
#include "hUGEDriver.h"
#include "bird_data.h"
#include "game.h"

/* The streaming window must end well before the gap wraps: at
 * w == BIRD_MAX_FRAMES the body releases CH3, and frames past that
 * must hit the release branch (never re-enter the window).  If a chirp
 * ever reached BIRD_GAP frames, w < BIRD_MAX_FRAMES would hold for the
 * whole gap and CH3 would stay muted for good.  A size-0 array is a
 * hard compile error (same pattern as g_tiletype_fits_u8). */
static const int g_bird_win_fits_gap[(BIRD_MAX_FRAMES < BIRD_GAP) ? 1 : 0];

/* Tracker CH3 mute masks, one per driver copy (hUGEDriver.asm
 * hUGE_mute_channel: channel B sets bit B; CH3 = 0x04; the bank-6 mask
 * comes from hUGEDriver.h).  The body pokes both directly instead of
 * calling huge_music_mute_channel: a fixed-bank call per overworld frame
 * costs fixed bytes the budget does not have, and the poke is exactly
 * what the driver call does (mask bit; the note-cut is redundant -- the
 * stepper owns CH3 outright from the next wave write).  Poking the
 * inactive copy's mask too is harmless (its driver never runs while
 * unselected) and keeps this body branch-free.  No di/ei here: the
 * pokes are single WRAM/IO stores (atomic against the timer ISR by
 * construction), and the release path orders hardware-off before
 * mask-clear so no note can start in the window. */
extern volatile uint8_t hUGE_mute_mask_b7;

/* ── Ambient bird-chirp body (bank-7) ─────────────────────────────────
 * Forest ambience on the CH3 wave channel (see audio.h for the full
 * contract).  Driven once per overworld frame; streams one 16-byte
 * wave frame per call.  Pure: WRAM reads + sound regs + same-bank song
 * data only, no MBC switches, no fixed-bank calls (a banked body must
 * never switch away its own bank mid-function).
 *
 * ZERO-WRAM BY DESIGN (see the +23B WRAM-shift ghost-input post-mortem
 * in the roadmap): timing derives from g_game.frame (window = low 7
 * bits, species = bit 7), so no countdown/cursor state exists.
 * Observation is a single EVENT_BIRD_CHIRP telemetry event per chirp
 * (one entry in the 32-entry ring per ~128 frames).  The bank-7
 * placement keeps the layout-pinned bank 5 byte-identical.
 *
 * Fidelity note: one wave frame per 60 Hz overworld frame is ~60 wave
 * frames/s, not the transcribed ~8 kHz -- each 32-sample frame loops
 * about four times before the next replaces it, so the chirp plays
 * roughly four times slower and lower than the PCM.  That is the price
 * of zero WRAM and no ISR work (the retired ISR design paced this
 * correctly); it still reads as birdsong against the tracker.
 *
 * CH3 coexistence: the tracker bass also uses CH3.  While a chirp plays
 * the tracker channel stays muted (skipped by the driver's dosound --
 * see update_ch3_waveform's retMute), so the two never fight over wave
 * RAM.  No SFX table touches CH3 (transcribe_sfx.py rejects scored
 * CH2/CH3 outright; SFX mute/unmute only CH2/CH4), so this body owns
 * the whole mute edge -- including the release below -- with no
 * conflicting writer.  No song-wave save buffer exists (that was 16 of
 * the 23 WRAM bytes): at the window end the body silences CH3 and
 * unmutes, and the tracker's next CH3 note reloads the song wave
 * itself.  The bass drops out for a few frames -- inaudible against
 * the chirp. */

/* Staged input: g_bk_byte_a = nonzero when a chirp may start this frame
 * (overworld on a birdsong map: game_scene_has_birds).  Results return through g_bk_byte_b
 * (0 = nothing, else the window-start species for one EVENT_BIRD_CHIRP,
 * emitted by the fixed wrapper).  Species alternates per window
 * (deterministic, no shared-RNG impact). */
void bird_ambient_banked(void)
{
    uint8_t f8;
    uint8_t w;
    uint8_t species;
    const uint8_t *src;
    const uint8_t *p;
    uint8_t frames;
    uint8_t i;
    g_bk_byte_b = 0;
    if (g_bk_byte_a && g_sound_enabled &&
        g_audio_current_track != MUSIC_CHEST) {
        /* Chirp window: one wave frame per overworld frame.  Only the
         * low frame-counter byte matters (gap is a power of two), so no
         * 32-bit ops reach fixed code. */
        f8 = (uint8_t)g_game.frame;
        w = (uint8_t)(f8 & (BIRD_GAP - 1));
        if (w < BIRD_MAX_FRAMES) {
            /* Species flips every window (bit 7 of the frame counter
             * for BIRD_GAP 128 -- deterministic, no shared-RNG impact). */
            species = (uint8_t)((f8 >> 7) & 1);
            if (species == 0) {
                src = g_bird_bird1;
                frames = g_bird_bird1_frames;
            } else {
                src = g_bird_bird2;
                frames = g_bird_bird2_frames;
            }
            hUGE_mute_mask |= 0x04;
            hUGE_mute_mask_b7 |= 0x04;
            if (w < frames) {
                p = src + ((uint16_t)w << 4);
                /* Driver wave-load protocol (hUGEDriver
                 * update_ch3_waveform): kill the channel while rewriting
                 * wave RAM (a trigger mid-read corrupts it), then
                 * re-enable.  NR33/NR34 = 1792: the 32-sample wave loops
                 * at 65536/(2048-1792) = 256 Hz, so each held frame
                 * plays almost exactly once per 256 Hz-tick equivalent
                 * (at 60 fps it instead loops ~4x -- see the fidelity
                 * note above).  0xFC here would be x=2044 = 16.4 kHz
                 * loop rate, a near-ultrasonic buzz, not birdsong. */
                NR30_REG = 0x00;
                for (i = 0; i < 16; i++) {
                    ((volatile uint8_t *)0xFF30)[i] = p[i];
                }
                NR30_REG = 0x80;
                NR32_REG = 0x20;
                NR33_REG = 0x00;
                NR34_REG = 0x87;
            }
            /* One telemetry event per chirp, fired on the window-start
             * frame (the fixed wrapper emits it; the stream itself is
             * silent). */
            if (w == 0) {
                g_bk_byte_b = (uint8_t)(species + 1);
            }
            return;
        }
    }
    /* Past the window, or no chirp frame (outside the grove/field, muted, or the
     * chest jingle owns the channels): hand CH3 back if a chirp left it
     * muted (gate crossed or battle entered mid-chirp, jingle started
     * mid-chirp, sound toggled mid-chirp).  Idempotent -- the mute-bit
     * read keeps this a no-op otherwise -- so any frame past the window
     * releases, never just the exact end frame. */
    if ((hUGE_mute_mask | hUGE_mute_mask_b7) & 0x04) {
        /* Silence the hardware first, then clear the mute bits: if the
         * timer ISR fires between the two and the tracker starts a CH3
         * note, it would play one tick before the release below kills
         * it.  Mask-still-set order keeps the handoff clean (the ISR
         * skips CH3 while muted, so no note can start in the window). */
        NR30_REG = 0x00;
        hUGE_mute_mask &= (uint8_t)~0x04;
        hUGE_mute_mask_b7 &= (uint8_t)~0x04;
    }
}
