# Game Boy Tracker Music (`assets/music/`)

Music by **jonas bryan**.

This directory holds the source `.uge` (hUGETracker) files for the Game Boy RPG soundtrack.

---

## 1. Tracker & Tooling

* **Tracker**: [hUGETracker](https://nickfa.ro/huge-tracker/) (v1.0.11+).
* **CLI Converter**: `uge2source` (packaged in the Nix environment).
* **Driver**: [hUGEDriver](https://github.com/SuperDisk/hUGEDriver) (v1.0+).
* **Object Bridge**: `tools/rgb2sdas.py` (converts RGBDS objects, rev 6–13 / RGBDS 0.5.x–1.0.x, to SDAS format for GBDK-4 linking).

All tools are provided deterministically via `nix develop`.

---

## 2. Audio Pipeline Architecture

```
assets/music/*.uge
       │
       ▼ (uge2source -b 6 <symbol>)
generated/music/*.c  [#pragma bank 6]
       │
       ▼ (lcc)
build/debug/music/*.o  [ROM Bank 6]
       ▲
       │ (rgbasm-huge + rgb2sdas -b 6)
lib/hUGEDriver/src/hUGEDriver.asm ────────┘
```

### Key Components

1. **ROM Bank 6 Placement**:
   * The hUGEDriver engine code and all converted `.uge` songs reside in **ROM Bank 6** (`_CODE_6`). The driver reads song bytes through the mapped ROM window, so songs cannot live anywhere else.
   * This strictly preserves the fixed 32 KB Bank 0/1 memory budget (`_CODE`/`_HOME`).
   * Bank 6 is full: the transcribed-SFX step tables + stepper live in **ROM Bank 7** (`_CODE_7`), selected by the timer ISR around `sfx_step_tick()`. `make memmap` fails on any bank over 16 KB.
2. **Timer-Driven Tick Rate (64 Hz)**:
   * Game audio runs on the hardware **Timer interrupt** (TIMA overflow at 256 Hz, see `AGENTS.md` §35).
   * `audio_update()` in `src/audio/audio.c` divides the 256 Hz timer clock by 4 to tick `hUGE_dosound()` at a steady **64 Hz** (`huge_music_update()`), regardless of visual rendering, screen wipes, or CPU load.
3. **Interrupt & Bank Safety**:
   * API calls (`huge_music_play`, `huge_music_mute_channel`, `huge_music_stop`) are critical sections (`__critical`) that switch to Bank 6, perform driver operations, and immediately restore the home bank (`HOME_BANK 1`).
4. **SFX Coexistence (Channel Muting)**:
   * When tone sound effects (CH2) or noise bursts (CH4) play, the game calls `huge_music_mute_channel(HT_CH2 / HT_CH4, HT_CH_MUTE)`.
   * When the SFX envelope finishes, the channel is unmuted (`HT_CH_PLAY`) so the tracker music resumes cleanly without channel interference or clicks.

---

## 3. Adding or Updating Tracks

1. **Authoring**:
   * Create or modify `.uge` tracks using `hUGETracker` (or launch via `nix develop --command hUGETracker`).
   * Respect standard Game Boy hardware channels:
     * **CH1**: Pulse with frequency sweep (lead melody)
     * **CH2**: Pulse (harmony / chords; shared with tone SFX)
     * **CH3**: Custom 4-bit Waveforms (basslines)
     * **CH4**: Noise channel (percussion / drums; shared with noise SFX)
2. **Build Rule**:
   * Save the file under `assets/music/<track_name>.uge`.
   * In the `Makefile`, add a rule to generate the C source into `generated/music/`:
      ```makefile
      $(GENERATED_MUSIC_DIR)/<track_name>.c: assets/music/<track_name>.uge | $(GENERATED_MUSIC_DIR)
      	$(UGE2SOURCE) "$<" -b 6 song_<track_name> "$@"
      ```
   (Bank 6 holds driver + all songs; SFX tables/stepper live in bank 7.
   Bank 6 is full, so the mimic loop + intro and victory use `-b 7` with
   `huge_music_play_banked(..., HUGE_MUSIC_BANK_B7)` and the `_b7` driver
   copy instead.)
3. **C Code Binding**:
   * Declare the song in `src/audio/huge_music_data.h`:
     ```c
     extern const hUGESong_t song_<track_name>;
     ```
   * Hook into `audio_play_music()` in `src/audio/audio.c` using `huge_music_play(&song_<track_name>)`.
4. **Editor preview**:
   * Re-run `make music-preview` to re-render `tools/level_editor/public/audio/<track_name>.wav`
     (`tools/render_music_preview.py`: driver-faithful approximation from the generated song C).
   * The level editor's Inspector BGM row plays it via a ▶ toggle. The ROM mix stays authoritative.

---

## 4. Sound Effects (`assets/sfx/`, transcribed — Path C)

One-shot SFX are **not** played as tracker songs (song takeover would
interrupt BGM and the 2-order jingles would loop). They are transcribed
into synth step tables that the existing CH2-tone / CH4-noise voices
render with zero BGM interruption:

```
assets/sfx/*.uge
       │  (uge2source, scratch)
       ▼
tools/transcribe_sfx.py  (driver-faithful emulation at 64 Hz ticks:
note rows, instrument loads, subpattern jumps/transposes/portamento,
arpeggio, CH4 polys, tempo, length timer)
       ▼
 generated/sfx/sfx_tables.c  [#pragma bank 7: per-voice step arrays]
generated/sfx/sfx_index.c   [fixed bank: 7 voice-presence bytes]
```

| File | SFX id(s) | Duration |
|---|---|---|
| `sfx cursor.uge` | CURSOR | 9 ticks (0.14 s) |
| `sfx accept.uge` | CONFIRM, SELECT | 8 ticks (0.12 s) |
| `sfx back.uge` | BACK | 10 ticks (0.16 s) |
| `sfx hit2.uge` | ATTACK | 22 ticks (0.34 s) |
| `sfx hit.uge` | HIT | 12 ticks (0.19 s) |
| `sfx block.uge` | BLOCK | 77 ticks (1.20 s) |

* `make sfx` regenerates the tables deterministically (byte-identical
  reruns). The transcriber fails loudly on any encoding outside the
  observed subset (other effects, CH2/CH3 notes, wave instruments,
  routines, tempo changes).
* `BLOCK`'s 1.2 s decay ducks music CH2 for its full duration by design
  (scored envelope). If that proves annoying in play, shorten the
  envelope in the `.uge` and regenerate — do not hand-edit the tables.
* Runtime: `audio_play_sfx()` (ids unchanged, ~20 existing call sites)
  mutes exactly the voices each SFX uses, steps the tables from the
  timer ISR through the bank-7 body `sfx_step_tick()`, then silences and
  unmutes. Triggers do NOT emit gameplay telemetry: per-trigger events
  would flood the 32-entry ring and evict gameplay events scenarios
  assert on. Instead each trigger bumps `g_sfx_played_count` and sets
  `g_sfx_last_id` (WRAM globals read by name from host tools), asserted
  via the `sfx_count` / `sfx_last` scenario assertions;
  `tools/verify_music.py` checks 5a–5d (trigger, completion, content
  hold, no stall).
* Previews: `make sfx-preview` renders `tools/level_editor/public/audio/sfx/*.wav`
  (`tools/render_sfx_preview.py`, emulated register state) for the editor's
  toolbar sound-test panel. The ROM synth mix stays authoritative.
* Fixed-bank cost is kept under `0x8000` by structure: the voice tables
  and stepper body live in bank 7; fixed bank holds only the 7 presence
  bytes plus trigger/dispatch (`make memmap` gates this).

