#pragma bank 7

#include <gb/gb.h>
#include <stdint.h>
#include "sfx_tables.h"

/* ── Transcribed-SFX stepper (bank-7 body) ───────────────────────────
 * Dispatched inline from audio_update() with bank 7 selected (select-7/
 * call/restore-1; the music path uses bank 6 separately in the same ISR).
 * Reads the generated tables in-bank plus the WRAM cursor state owned by
 * audio.c, so fixed code pays no generic-pointer or struct-multiply costs
 * (AGENTS.md §52.18).
 * Returns nonzero when the table end is reached; the fixed-bank caller
 * then silences the used voices and unmutes their music channels. */

extern uint8_t sfx_id;
extern uint8_t sfx_tick;
extern uint8_t sfx_tone_idx;
extern uint8_t sfx_noise_idx;

uint8_t sfx_step_tick(void)
{
    const SfxEntry *e = &s_sfx_index[sfx_id];

    while (sfx_tone_idx < e->tone_len &&
           e->tone[sfx_tone_idx].tick == sfx_tick) {
        const SfxToneStep *st = &e->tone[sfx_tone_idx];
        if (st->mask & 0x01) NR21_REG = st->nr21;
        if (st->mask & 0x02) NR22_REG = st->nr22;
        if (st->mask & 0x04) NR23_REG = st->freq_lo;
        if (st->mask & 0x08) NR24_REG = st->freq_hi;
        sfx_tone_idx++;
    }
    while (sfx_noise_idx < e->noise_len &&
           e->noise[sfx_noise_idx].tick == sfx_tick) {
        const SfxNoiseStep *st = &e->noise[sfx_noise_idx];
        if (st->mask & 0x01) NR41_REG = st->nr41;
        if (st->mask & 0x02) NR42_REG = st->nr42;
        if (st->mask & 0x04) NR43_REG = st->nr43;
        if (st->mask & 0x08) NR44_REG = st->nr44;
        sfx_noise_idx++;
    }
    sfx_tick++;
    return (uint8_t)(sfx_tick > e->total_ticks);
}
