#pragma bank 7

#include <gb/gb.h>
#include <stdint.h>
#include "banked.h"
#include "audio.h"
#include "hUGEDriver.h"
#include "bird_data.h"
#include "game.h"

/* Tracker CH3 mute masks, one per driver copy (hUGEDriver.asm
 * hUGE_mute_channel: channel B sets bit B; CH3 = 0x04; the bank-6 mask
 * comes from hUGEDriver.h).  The body pokes both directly instead of
 * calling huge_music_mute_channel: a fixed-bank call per overworld frame
 * costs fixed bytes the budget does not have, and the poke is exactly
 * what the driver call does (mask bit; the note-cut is redundant -- the
 * stepper owns CH3 outright from the next wave write).  Poking the
 * inactive copy's mask too is harmless (its driver never runs while
 * unselected) and keeps this body branch-free.  Muting runs under the
 * trampoline di...ei, so no teardown races. */
extern volatile uint8_t hUGE_mute_mask_b7;

/* ── Ambient bird-chirp body (bank-7) ─────────────────────────────────
 * Forest ambience on the CH3 wave channel (see audio.h for the full
 * contract).  Driven once per overworld frame; streams up to
 * BIRD_CHUNK wave frames per call (~240 wave frames/s at 60 fps, the
 * same ~8 kHz playback the 256 Hz ISR design had).  Pure: WRAM reads +
 * sound regs + same-bank song data only, no MBC switches, no
 * fixed-bank calls (a banked body must never switch away its own bank
 * mid-function).
 *
 * ZERO-WRAM BY DESIGN (see the +23B WRAM-shift ghost-inputKill in the
 * roadmap): timing derives from g_game.frame (window = low 7 bits,
 * species = bit 7), so no countdown/cursor state exists.  Observation
 * is a single EVENT_BIRD_CHIRP telemetry event per chirp (one entry in
 * the 32-entry ring per ~256 frames).  The bank-7 placement keeps the
 * layout-pinned bank 5 byte-identical.
 *
 * CH3 coexistence: the tracker bass also uses CH3.  While a chirp plays
 * the tracker channel stays muted (skipped by the driver's dosound --
 * see update_ch3_waveform's retMute), so the two never fight over wave
 * RAM.  No song-wave save buffer exists (that was 16 of the 23 WRAM
 * bytes): at the window end the body silences CH3 and unmutes, and the
 * tracker's next CH3 note reloads the song wave itself.  The bass drops
 * out for at most a few frames -- inaudible against the chirp. */

/* Staged input: g_bk_byte_a = nonzero when a chirp may start this frame
 * (overworld in a forest scene).  Results return through g_bk_byte_b
 * (0 = nothing, else the window-start species for one EVENT_BIRD_CHIRP,
 * emitted by the fixed wrapper).  Species alternates per window
 * (deterministic, no shared-RNG impact). */
void bird_ambient_banked(void)
{
    uint32_t f;
    uint8_t w;
    uint8_t species;
    const uint8_t *src;
    const uint8_t *p;
    uint8_t frames;
    uint8_t base;
    uint8_t k;
    uint8_t idx;
    uint8_t i;
    g_bk_byte_b = 0;
    if (!g_bk_byte_a || !g_sound_enabled ||
        g_audio_current_track == MUSIC_CHEST) {
        /* Not a chirp frame (outside forests, muted, or the one-shot
         * chest jingle owns the channels): release CH3 if a chirp left
         * it muted (gate crossed mid-chirp, jingle started mid-chirp),
         * else touch nothing. */
        if ((hUGE_mute_mask | hUGE_mute_mask_b7) & 0x04) {
            hUGE_mute_mask &= (uint8_t)~0x04;
            hUGE_mute_mask_b7 &= (uint8_t)~0x04;
            NR30_REG = 0x00;
        }
        g_bk_byte_b = 0;
        return;
    }
    f = g_game.frame;
    w = (uint8_t)(f & (BIRD_GAP - 1));
    if (w >= (uint8_t)(BIRD_WIN + 1)) {
        g_bk_byte_b = 0;
        return;
    }
    if (w == BIRD_WIN) {
        /* First frame past the window: hand CH3 back (silence until the
         * tracker's next CH3 note re-enables with the song wave).  The
         * mute-bit read keeps this a no-op when no chirp played. */
        if ((hUGE_mute_mask | hUGE_mute_mask_b7) & 0x04) {
            hUGE_mute_mask &= (uint8_t)~0x04;
            hUGE_mute_mask_b7 &= (uint8_t)~0x04;
            NR30_REG = 0x00;
        }
        g_bk_byte_b = 0;
        return;
    }
    /* Streaming window: species flips every window (bit 7 of the frame
     * counter for BIRD_GAP 128 -- deterministic, no shared-RNG impact). */
    species = (uint8_t)((f >> 7) & 1);
    if (species == 0) {
        src = g_bird_bird1;
        frames = g_bird_bird1_frames;
    } else {
        src = g_bird_bird2;
        frames = g_bird_bird2_frames;
    }
    hUGE_mute_mask |= 0x04;
    hUGE_mute_mask_b7 |= 0x04;
    base = (uint8_t)(w << 2);
    for (k = 0; k < BIRD_CHUNK; k++) {
        idx = (uint8_t)(base + k);
        if (idx >= frames) {
            break;
        }
        p = src + ((uint16_t)idx << 4);
        /* Driver wave-load protocol (hUGEDriver update_ch3_waveform):
         * kill the channel while rewriting wave RAM (a trigger mid-read
         * corrupts it), then re-enable at the 8 kHz chirp rate (the
         * bird PCM was transcribed for NR33/NR34 = 2044). */
        NR30_REG = 0x00;
        for (i = 0; i < 16; i++) {
            ((volatile uint8_t *)0xFF30)[i] = p[i];
        }
        NR30_REG = 0x80;
        NR32_REG = 0x20;
        NR33_REG = 0xFC;
        NR34_REG = 0x87;
    }
    /* One telemetry event per chirp, fired on the window-start frame
     * (the fixed wrapper emits it; the stream itself is silent). */
    if (w == 0) {
        g_bk_byte_b = (uint8_t)(species + 1);
    }
}
