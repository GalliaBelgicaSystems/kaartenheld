#pragma bank 8

#include <stdint.h>
#include "banked.h"
#include "audio.h"
#include "game.h"
#include "game_ids.h"

/* ── Chest-jingle full body (MUSIC_CHEST, one-shot) ────────────────────
 * Game-layer banked body: reads the loot mask from game story state
 * directly (field-cache flag, merchant-quest amulet variable), so the
 * fixed-bank engine never branches on game ids (§55.1) -- it only owns
 * the trampoline dispatch in game_update().  Ticks every non-battle
 * frame (menus, dialogue, shops included) so they can't freeze the
 * restore timer while the driver loops the one-shot song.  Returns
 * through g_bk_byte_b (0 = nothing, 1 = play the jingle, 2 = restore
 * the scene track); the fixed side plays/restores with engine-generic
 * calls only.  Never calls fixed-bank code: the bank is switched away
 * while it runs.
 *
 * Consolidates the old split (fixed mask hook in content.c + fixed
 * audio_jingle_frame wrapper + this edge/tick core): one trampoline
 * round-trip per frame instead of two nested dispatches, which the
 * fixed-bank budget could no longer afford.
 *
 * Edge: loot mask bit 0 = field cache open, bit 1 = amulet found.
 * The first frame a chest reads open starts the jingle once; the very
 * first call seeds the edge from the live mask (bit 7 flag), so a save
 * loaded with chests already open stays silent (re-armed by
 * load_game_slot, which clears the seeded flag).
 * Tick: the play ends after CHEST_JINGLE_FRAMES frames. */

/* Overworld frames per jingle play: the song is one 64-row order (the
 * audible arp is over by row 4; the rest is silence) looping every 192
 * tracker ticks (3.0 s at 64 Hz), and the ROM driver wraps
 * unconditionally -- so a restore at ~180 replays the opening attack as
 * an audible flam/double-hit whenever frame and tracker rates drift
 * apart (LCD-off redraws stall frames but never the timer ISR).  168
 * restores ~0.2 s early, deep in the silent tail where no musical
 * content can be clipped and no wrap can replay.  Must stay below ~179
 * (tick 192 = song end). */
#define CHEST_JINGLE_FRAMES 168

static uint8_t s_jingle_frames = 0;
/* Per-chest done bits (bit 7 = seeded flag).  Global (not file-static)
 * so load_game_slot() can re-arm the edge when a save replaces the
 * whole canonical state mid-session.  Written only here and in save.c
 * (re-arm); read only here. */
uint8_t g_jingle_done = 0;

void jingle_full_banked(void)
{
    uint8_t mask;
    uint8_t b;
    g_bk_byte_b = 0;
    if (g_game.screen == SCREEN_BATTLE || g_game.screen == SCREEN_GAME_OVER) {
        return;
    }
    mask = 0;
    if (g_game.state.flags.bytes[(STORY_FLAG_ID_FIELD_CHEST - 1) >> 3] &
        (uint8_t)(1u << ((STORY_FLAG_ID_FIELD_CHEST - 1) & 7))) {
        mask |= 1u;
    }
    if (g_game.state.variables.values[VARIABLE_ID_MERCHANT_QUEST - 1] != 0) {
        mask |= 2u;
    }
    if (!(g_jingle_done & 0x80)) {
        /* First call (boot, new game, fresh scenario, loaded save):
         * seed the edge from the live loot state, so an already-looted
         * chest does not replay its jingle on the first overworld
         * frame.  Bit 7 is the seeded flag; bits 0-1 stay the per-chest
         * done bits, so no extra WRAM is spent. */
        g_jingle_done = (uint8_t)(0x80 | (mask & 0x03));
    }
    for (b = 0; b < 2; b++) {
        if (mask & (uint8_t)(1u << b)) {
            if (!(g_jingle_done & (uint8_t)(1u << b)) &&
                g_audio_current_track != MUSIC_CHEST) {
                g_jingle_done |= (uint8_t)(1u << b);
                g_bk_byte_b = 1;
                return;
            }
        } else {
            g_jingle_done &= (uint8_t)~(1u << b);
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
