#ifndef AUDIO_H
#define AUDIO_H

#include <gb/gb.h>
#include <stdint.h>

typedef enum {
    MUSIC_NONE,
    MUSIC_OVERWORLD, /* opening theme (assets/music/opening.uge, loops) */
    MUSIC_BATTLE,
    MUSIC_VICTORY,   /* one-shot 4-note fanfare; ends in silence */
    MUSIC_TITLE,     /* title screen theme (loops) */
    MUSIC_TOWN,      /* town / hub theme (loops) */
    MUSIC_DUNGEON,   /* castle / dungeon theme (loops) */
    MUSIC_BOSS,      /* final-boss theme (loops) */
    MUSIC_MIMIC,     /* mimic battle: short intro sting (Mimic_intro.uge,
                        ~0.9 s) then looping Mimic.uge; both bank 7 */
    MUSIC_DESOLATE,  /* desolate landscape theme (loops) */
    MUSIC_FOREST,    /* forest / plain theme (Forest.uge, loops) */
    MUSIC_CHEST      /* chest-loot jingle (chest opening.uge, one-shot,
                        bank 8); the overworld restores the scene track
                        when it ends (audio_jingle_tick) */
} MusicTrack;

#define MUSIC_DESOLATE_LANDSCAPE MUSIC_DESOLATE

/* Master sound enable (title menu SOUND ON/OFF).  Gates both music and
 * SFX: audio_play_music() no-ops (leaving any currently playing track
 * ringing) and audio_play_sfx() drops the note.  When set back ON the next
 * audio_play_music() call restores the requested track. */
extern uint8_t g_sound_enabled;

void audio_init(void);
void audio_play_music(MusicTrack track);

/* One-shot SFX ids.  CH2 tones route through audio_play_sfx to channel 2
 * (NR21-NR24); attack/hit sounds use the channel-4 noise generator (NR41-
 * NR44), which sits on a separate voice from the CH1 music and CH2 blips.
 * SFX_CURSOR is the short navigation blip (menu open, cursor move, sound
 * toggle); SFX_CONFIRM is a lower-pitch variant for selections; SFX_SELECT
 * is a distinct blip for battle hand / card management selects; SFX_BACK is
 * a low downward "bloup" for cancel / going back; SFX_ATTACK / SFX_HIT are
 * channel-4 noise bursts (player slash / enemy strike); SFX_BLOCK is a low
 * CH2 thump for a successful defend. */
enum {
    SFX_CURSOR = 0,
    SFX_CONFIRM = 1,
    SFX_SELECT = 2,
    SFX_BACK = 3,
    SFX_ATTACK = 4,
    SFX_HIT = 5,
    SFX_BLOCK = 6
};
void audio_play_sfx(uint8_t sfx);
void audio_update(void);

/* Chest-loot jingle (MUSIC_CHEST, one-shot): edge-trigger + timed
 * restore, driven by the overworld screen every frame (see audio.c).
 * open_mask bit 0 = field cache open, bit 1 = amulet found. */
void audio_jingle_frame(uint8_t map_id, uint8_t open_mask);

/* Current track (WRAM): readable directly from bank-2 code, which cannot
 * call this fixed-bank getter. */
extern MusicTrack g_audio_current_track;

#ifdef DEBUG_BUILD
/* Per-music-clock-tick counter, incremented once per audio_update() call
 * (i.e. once per ISR tick).  Host-side tools (tools/verify_music.py) sample
 * it every frame to assert the music clock never stalls during LCD-off
 * screen/map transitions (see AGENTS.md Music contract).  Not present in
 * release builds. */
extern volatile uint16_t g_audio_ticks;
#endif

#endif /* AUDIO_H */
