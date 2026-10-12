#pragma bank 8

#include <stdint.h>
#include "banked.h"
#include "audio.h"

/* ── Chest-jingle edge + tick body (MUSIC_CHEST, one-shot) ─────────────
 * Dispatched by the thin fixed-bank entry in audio.c through the WRAM
 * trampoline.  Self-contained: the loot mask arrives in g_bk_byte_a,
 * state lives in file WRAM, and the action returns through g_bk_byte_b
 * (0 = nothing, 1 = play the jingle, 2 = restore the scene track).
 * Never calls fixed-bank code: the bank is switched away while it runs.
 *
 * Protocol (audio.c documents the full jingle contract):
 * - edge: loot mask bit 0 = field cache open, bit 1 = amulet found.
 *   The first frame a chest reads open starts the jingle once.
 * - tick: the play ends after CHEST_JINGLE_FRAMES overworld frames. */

#include <stdint.h>
#include "banked.h"
#include "audio.h"

/* Overworld frames per jingle play: chest opening.uge is 192 tracker
 * ticks at 64 Hz = 3.0 s (see `make music-size SONG=chest_opening`). */
#define CHEST_JINGLE_FRAMES 180

static uint8_t s_jingle_frames = 0;
static uint8_t s_chest_jingle_done = 0;

void jingle_frame_banked(void)
{
    uint8_t mask = g_bk_byte_a;
    uint8_t b;
    g_bk_byte_b = 0;
    for (b = 0; b < 2; b++) {
        if (mask & (uint8_t)(1u << b)) {
            if (!(s_chest_jingle_done & (uint8_t)(1u << b)) &&
                g_audio_current_track != MUSIC_CHEST) {
                s_chest_jingle_done |= (uint8_t)(1u << b);
                g_bk_byte_b = 1;
                return;
            }
        } else {
            s_chest_jingle_done &= (uint8_t)~(1u << b);
        }
    }
    if (g_audio_current_track != MUSIC_CHEST) {
        s_jingle_frames = 0;
        return;
    }
    if (++s_jingle_frames > CHEST_JINGLE_FRAMES) {
        s_jingle_frames = 0;
        g_bk_byte_b = 2;
    }
}
