#ifndef AUDIO_H
#define AUDIO_H

#include <gb/gb.h>
#include <stdint.h>

typedef enum {
    MUSIC_NONE,
    MUSIC_OVERWORLD, /* opening theme (assets/music/opening.uge, loops) */
    MUSIC_BATTLE,
    MUSIC_VICTORY,   /* one-shot 4-note fanfare; ends in silence */
    MUSIC_TITLE,     /* title screen theme (title short.uge, bank 7):
                         plays ONCE per title visit (~6 s), then stops
                         in silence (TITLE_SONG_FRAMES game frames, timed
                         in the bank-8 jingle body; replay on revisit) */
    MUSIC_TOWN,      /* town / hub theme (loops) */
    MUSIC_DUNGEON,   /* castle / dungeon theme (loops) */
    MUSIC_BOSS,      /* final-boss theme (loops) */
    MUSIC_MIMIC,     /* mimic battle: short intro sting (Mimic_intro.uge,
                        ~0.9 s) then looping Mimic.uge; both bank 7 */
    MUSIC_DESOLATE,  /* desolate landscape theme (loops) */
    MUSIC_FOREST,    /* forest / plain theme (Forest.uge, loops) */
    MUSIC_CHEST,     /* chest-loot jingle (chest opening.uge, one-shot,
                        bank 7); the overworld restores the scene track
                        when it ends (audio_jingle_frame) */
    MUSIC_GAMEOVER,  /* game-over theme (game over.uge, loops, bank 7):
                        starts the instant the hero falls
                        (battle_set_result DEFEAT) and plays over the
                        game-over choice menu until a map load restores
                        the scene track */
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

/* Ambient bird chirps (bird1.pcm = BIRD_1, bird2.pcm = BIRD_2 -- the
 * EVENT_BIRD_CHIRP d0 values).  PCM transcribed to 4-bit wave frames
 * (tools/bird_compile.py, bank 7) and streamed on CH3 by the bank-7
 * body, one wave frame per overworld frame without touching the timer
 * ISR.  One chirp window (BIRD_MAX_FRAMES streaming frames from
 * generated/birds/bird_data.h, then release) opens every BIRD_GAP
 * overworld frames on birdsong maps (Wake Grove + Carl's field, via
 * game_scene_has_birds); species alternates per window.  At ~60
 * wave frames/s each 32-sample frame loops about four times, so the
 * chirp plays roughly four times slower and lower than the transcribed
 * ~8 kHz -- the price of zero WRAM and no ISR work.  The tracker CH3
 * is muted for the ~220 ms play; no song wave is saved (that buffer
 * was the WRAM-shift that broke battles), so the bass drops out for a
 * few frames until its next note reloads the wave -- inaudible against
 * the chirp.
 *
 * ZERO-WRAM BY DESIGN: window/species derive from g_game.frame (low
 * byte / bit 7), so no countdown/cursor state exists; observation is
 * one EVENT_BIRD_CHIRP telemetry event per chirp (fixed-bank emit in
 * the overworld wrapper, one ring entry per ~128 frames).  A +23B WRAM
 * layout shift was proven to break battle_boot (ghost START input),
 * so birds must not move a single WRAM byte. */
enum {
    BIRD_1 = 1,
    BIRD_2 = 2
};
/* Chirp window every 128 overworld frames (~2.1 s at 60 fps).  The
 * streaming window itself is BIRD_MAX_FRAMES (generated header: longest
 * chirp in wave frames, one per overworld frame, then release).
 * Power-of-two gap on purpose: the banked body uses masking/shifts,
 * never % or /, which would pull SDCC div/mod support routines into
 * fixed _CODE. */
#define BIRD_GAP 128

/* Chest-loot jingle (MUSIC_CHEST, one-shot): edge-trigger + timed
 * restore, driven by game_update() every non-battle frame through the
 * game-layer bank-8 body (src/game/jingle_full_banked.c, which reads
 * the loot mask from game story state).  Mask bit 0 = field cache
 * open, bit 1 = amulet found. */

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
