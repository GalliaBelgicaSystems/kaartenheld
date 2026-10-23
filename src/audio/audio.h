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
                         plays ONCE per title visit (~2.5 s notes + decay),
                         then stops in silence (TITLE_SONG_FRAMES game frames,
                         timed in the bank-8 jingle body; replay on revisit) */
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
    MUSIC_ENDING,    /* end-credits theme (demo complete.uge, loops,
                        bank 7): starts when a final-boss victory exits
                        to the ending screen (THE END) */
    MUSIC_CASINO,    /* chuck-a-luck table theme (casino.uge, loops,
                        bank 9 with its own driver copy): plays on
                        Nis's table; the gamble body restores the scene
                        track on exit */
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
 * CH2 thump for a *fully blocked* defend (no net damage); SFX_HURT is the
 * player-takes-damage cue (any net damage, even behind staged shields);
 * SFX_HEAL is the ring-heal cue; SFX_ALERT is the pursuer-entry sting
 * (stepped by the same 64 Hz ISR path -- the sfx_id range check in
 * audio_update must admit it, or the trigger mutes music CH2 forever
 * with no sound). */
enum {
    SFX_CURSOR = 0,
    SFX_CONFIRM = 1,
    SFX_SELECT = 2,
    SFX_BACK = 3,
    SFX_ATTACK = 4,
    SFX_HIT = 5,
    SFX_BLOCK = 6,
    /* Alert sting for pursuer levels (kobolds, slime knights): plays once
     * on entry, then the level track resumes (screens/sfx.json ALERT). */
    SFX_ALERT = 7,
    /* Chuck-a-luck win cue: the chest-opening arpeggio as an SFX (its .uge
     * transcribes to 3 CH2 tone steps), so the table theme keeps playing
     * instead of being replaced by the MUSIC_CHEST track. */
    SFX_WIN = 8,
    /* Player-takes-damage cue (assets/sfx/sfx hurt.uge): played whenever
     * net damage lands on the hero -- including behind staged shields.
     * Replaces the old always-on defend thump for those cases. */
    SFX_HURT = 9,
    /* Ring-heal cue (assets/sfx/sfx heal.uge): played when a ring
     * converts an attack hand to healing.  Must stay the LAST id:
     * audio.c range-checks against it. */
    SFX_HEAL = 10
};
void audio_play_sfx(uint8_t sfx);
void audio_update(void);

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
