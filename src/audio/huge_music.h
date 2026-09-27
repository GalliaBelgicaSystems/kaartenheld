#ifndef HUGE_MUSIC_H
#define HUGE_MUSIC_H

#include <gb/gb.h>
#include <stdint.h>
#include "hUGEDriver.h"

#define HUGE_MUSIC_BANK 6
/* Second driver copy for bank-7 songs (bank 6 is full).  Renamed
 * exports (hUGE_*_b7) coexist with the bank-6 originals. */
#define HUGE_MUSIC_BANK_B7 7
extern void hUGE_init_b7(const hUGESong_t *song);
extern void hUGE_dosound_b7(void);
extern void hUGE_mute_channel_b7(enum hUGE_channel_t ch, enum hUGE_mute_t mute);

void huge_music_init(void);
void huge_music_play(const hUGESong_t *song);
/* Play a song from an explicit ROM bank (HUGE_MUSIC_BANK or
 * HUGE_MUSIC_BANK_B7).  huge_music_play() is the bank-6 shorthand. */
void huge_music_play_banked(const hUGESong_t *song, uint8_t bank);
void huge_music_stop(void);
void huge_music_update(void);
void huge_music_pause(void);
void huge_music_resume(void);
void huge_music_mute_channel(uint8_t ch, uint8_t mute);
void huge_music_mute_channel_isr(uint8_t ch, uint8_t mute); /* timer-ISR context: no di/ei */

/* Driver state (WRAM): exposed so the bank-7 mimic intro -> loop body
 * (mimic_chain.c) can switch songs without a fixed-bank helper call
 * (§52.18 memory budget).  Written under __critical from user context
 * (huge_music_play_banked) and directly from the timer ISR (chain body,
 * huge_music_update); same shared-ISR pattern as the SFX cursor state
 * in audio.c. */
extern const hUGESong_t *g_huge_current_song;
extern uint8_t g_huge_playing;
extern uint8_t g_huge_tick_divider;
extern uint8_t g_huge_music_bank;

#endif /* HUGE_MUSIC_H */
