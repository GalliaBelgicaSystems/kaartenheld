#pragma bank 8
#pragma disable_warning 110

#include <stdint.h>
#include "banked.h"
#include "audio.h"
#include "game.h"
#include "game_ids.h"

extern volatile uint8_t g_harness_mode;

/* ── Chest-jingle full body (MUSIC_CHEST, one-shot) ────────────────────
 * Game-layer banked body: reads the loot mask from game story state
 * directly (field-cache and amulet-chest loot flags), so the fixed-bank engine never branches on game ids (§55.1) -- it only owns
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
 * Restore clock: the tracker's ÷4 divider counts into g_jingle_frames
 * while the jingle owns the track (see huge_music_update), so the
 * restore lands at an exact tracker-tick count no matter how loop
 * iterations drift against frames (LCD-off full redraws consume whole
 * frames of PPU time without running game_update, while the timer keeps
 * ticking: a frame-counted restore lands late and the driver's
 * unconditional song wrap replays the opening attack as a double-hit).
 * The ISR never runs under the harness, so there the main thread counts
 * exact STEP frames instead (backstop: later than the tick clock can
 * ever be, early enough that the restore scenarios still observe it). */

/* Restore points for the one-shot: the song is a 3-note arp over by
 * tracker tick ~15 and then silence, wrapping unconditionally at tick
 * 192 (3.0 s at 64 Hz).  The tick clock restores at 80 tracker ticks
 * (1.25 s, counted by the tracker's own ÷4 divider in
 * huge_music_update, so it cannot drift against the song): ~1 s of
 * ring-out silence after the arp, deep in the silent tail where no
 * musical content can be clipped and the wrap can never replay audibly.
 * The harness has no timer ISR, so there the frame backstop restores
 * instead (later than the tick clock, well inside every restore
 * scenario's final wait). */
#define CHEST_JINGLE_TICKS 80
#define CHEST_JINGLE_BACKSTOP 240

/* Title-theme play-once clock (see audio.h MUSIC_TITLE): the ROM driver
 * loops whole songs, so the 6.0 s title song (384 tracker ticks @64 Hz
 * per `make music-size SONG=title`) would loop forever without an explicit stop.
 * The active musical notes in song_title occupy only rows 0..26 (162 tracker
 * ticks = ~2.53 s, ending around frame 135) followed by 37 rows of silence
 * (rows 27..63) before hUGEDriver wraps back to row 0 at 384 tracker ticks
 * (~331 game_update frames due to LCD-off splash/title redraws).
 * Stopping at 200 frames halts playback cleanly in the silent tail after the
 * final chord's decay has completed, leaving a wide safety margin (~130 frames)
 * so the song never wraps or replays. Counted per game_update frame in this
 * bank-8 body. Main-thread only: no volatile needed.
 * Reset on every MUSIC_TITLE dispatch (music_play_banked). */
#define TITLE_SONG_FRAMES 200u
uint16_t g_title_frames = 0;

/* Tick/frame counter for the restore clock.  Shared with the timer ISR
 * (huge_music_update's divider branch increments while the jingle owns
 * the track): volatile
 * because it changes outside the main thread, though the race is
 * harmless either way (worst case a ±1-tick restore against 20+ ticks
 * of margin).  Same BSS slot as the old frame-only counter, so the WRAM
 * layout does not move a byte. */
volatile uint8_t g_jingle_frames = 0;
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
    if (g_game.state.flags.bytes[(STORY_FLAG_ID_AMULET_CHEST - 1) >> 3] &
        (uint8_t)(1u << ((STORY_FLAG_ID_AMULET_CHEST - 1) & 7))) {
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
    /* Title-theme play-once (see TITLE_SONG_FRAMES): runs after the
     * chest logic so a loot edge on the same frame still wins the
     * dispatch (a title screen never holds one, but the ordering costs
     * nothing and keeps the jingle path untouched). */
    if (g_bk_byte_b == 0 && g_audio_current_track == MUSIC_TITLE &&
        ++g_title_frames >= TITLE_SONG_FRAMES) {
        g_title_frames = 0;
        g_bk_byte_b = 3;
    }
    if (g_audio_current_track != MUSIC_CHEST) {
        g_jingle_frames = 0;
        return;
    }
    if (g_harness_mode) {
        /* No timer ISR under the harness, so the tick clock above never
         * advances there: count exact STEP frames instead. */
        if (++g_jingle_frames > CHEST_JINGLE_BACKSTOP) {
            g_jingle_frames = 0;
            g_bk_byte_b = 2;
        }
    } else if (g_jingle_frames > CHEST_JINGLE_TICKS) {
        g_jingle_frames = 0;
        g_bk_byte_b = 2;
    }
}
