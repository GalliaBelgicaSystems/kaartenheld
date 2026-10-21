#include "huge_music.h"
#include "banked.h"
#include "audio.h"

/* Chest-jingle restore clock (bank-8 BSS in jingle_full_banked.c):
 * counted in huge_music_update's divider branch while the jingle owns
 * the track (see below).  WRAM-only byte: no bank switch, no di/ei,
 * atomic against the main-thread reader (worst case a ±1-tick
 * restore). */
extern volatile uint8_t g_jingle_frames;

/* Driver state (WRAM globals, not static: the bank-7 mimic intro -> loop
 * body (mimic_chain.c) runs in the timer ISR and must touch these
 * directly.
 * A helper call would cost fixed-bank ROM the budget does not have
 * (§52.18); the swap site uses the same select-7/call/restore-1 pattern
 * huge_music_update() already uses.  Same precedent as
 * g_audio_current_track in audio.h. */
const hUGESong_t *g_huge_current_song = 0;
uint8_t g_huge_playing = 0;
uint8_t g_huge_tick_divider = 0;
/* ROM bank holding the active song + its driver copy.  Bank 6 holds the
 * driver and the six original songs; bank 7 holds the second driver copy
 * and the mimic/victory/opening/chest songs (bank 6 is full -- the SFX
 * moved to bank 8 to make room, so no third driver copy was needed). */
/* BSS (no _INITIALIZER cost): huge_music_init() (via audio_init() at
 * every boot) sets the home bank before any play/mute/update call. */
uint8_t g_huge_music_bank;

void huge_music_init(void)
{
    g_huge_current_song = 0;
    g_huge_playing = 0;
    g_huge_tick_divider = 0;
    g_huge_music_bank = HUGE_MUSIC_BANK;
}

void huge_music_play_banked(const hUGESong_t *song, uint8_t bank)
{
    /* Fixed-bank driver switch: fixed code is immune to MBC window
     * remaps (bank 0 never remaps); a banked body must never switch
     * away its own bank mid-function. */
    if (!song) {
        huge_music_stop();
        return;
    }

    __critical {
        g_huge_playing = 0;
        g_huge_current_song = song;
        g_huge_music_bank = bank;
        *(volatile uint8_t *)0x2000 = bank;
        if (bank == HUGE_MUSIC_BANK_B9) {
            hUGE_init_b9(song);
        } else if (bank == HUGE_MUSIC_BANK_B7) {
            hUGE_init_b7(song);
        } else {
            hUGE_init(song);
        }
        *(volatile uint8_t *)0x2000 = 1;
        g_huge_tick_divider = 0;
        g_huge_playing = 1;
    }
}

void huge_music_stop(void)
{
    /* Fixed-bank twin of huge_stop_inline() in huge_music_banked.c
     * (bank 8): identical register writes.  The banked copy exists
     * because a banked body cannot call fixed-bank code while its own
     * bank is unmapped -- keep the two in sync. */
    __critical {
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
}

/* Bank -> driver-copy mute dispatch.  One shared chain keeps the fixed
 * bank cost to a single compare ladder instead of duplicating it in the
 * user-context and ISR mute entry points (AGENTS.md 52.18).  The caller
 * has already mapped g_huge_music_bank. */
static void huge_mute_dispatch(uint8_t ch, uint8_t mute)
{
    if (g_huge_music_bank == HUGE_MUSIC_BANK_B9) {
        hUGE_mute_channel_b9((enum hUGE_channel_t)ch, (enum hUGE_mute_t)mute);
    } else if (g_huge_music_bank == HUGE_MUSIC_BANK_B7) {
        hUGE_mute_channel_b7((enum hUGE_channel_t)ch, (enum hUGE_mute_t)mute);
    } else {
        hUGE_mute_channel((enum hUGE_channel_t)ch, (enum hUGE_mute_t)mute);
    }
}

void huge_music_mute_channel(uint8_t ch, uint8_t mute)
{
    __critical {
        *(volatile uint8_t *)0x2000 = g_huge_music_bank;
        huge_mute_dispatch(ch, mute);
        *(volatile uint8_t *)0x2000 = 1;
    }
}

void huge_music_mute_channel_isr(uint8_t ch, uint8_t mute)
{
    /* ISR-context variant of huge_music_mute_channel: NO __critical.
     * SDCC's __critical emits di/ei -- the ei() would re-enable nested
     * timer interrupts while this already runs inside the timer ISR
     * (256 Hz), stacking ISR frames until the WRAM below the stack is
     * smashed (observed as ghost joypad input: g_inp_pad/g_inp_prev
     * accumulate pressed bits and releases never register).  Inside the
     * ISR no user code can run concurrently, so no di is needed either.
     * No restore here: the caller (audio_update's tail) re-establishes
     * the live bank once for the whole ISR (fixed-bank budget). */
    *(volatile uint8_t *)0x2000 = g_huge_music_bank;
    huge_mute_dispatch(ch, mute);
}

void huge_music_update(void)
{
    if (!g_huge_playing) return;

    /* 256 Hz TIMA timer -> 64 Hz tracker tick (every 4 timer interrupts) */
    if (++g_huge_tick_divider >= 4) {
        g_huge_tick_divider = 0;
        /* Chest-jingle restore clock (see jingle_full_banked): count the
         * tracker's own ticks while the jingle owns the track, so the
         * restore cannot drift against the song it times. */
        if (g_audio_current_track == MUSIC_CHEST) {
            ++g_jingle_frames;
        }
        /* No restore here: audio_update's tail re-establishes the live
         * bank once for the whole ISR (same contract as the ISR mute). */
        *(volatile uint8_t *)0x2000 = g_huge_music_bank;
        if (g_huge_music_bank == HUGE_MUSIC_BANK_B9) {
            hUGE_dosound_b9();
        } else if (g_huge_music_bank == HUGE_MUSIC_BANK_B7) {
            hUGE_dosound_b7();
        } else {
            hUGE_dosound();
        }
    }
}
