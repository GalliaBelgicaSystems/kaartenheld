## Implementation plan: `.uge` / hUGEDriver integration

### 1. Establish the target architecture

**Goal:** Keep the game's existing API unchanged.

Existing game code should continue to do:

```c
audio_play_music(MUSIC_TOWN);
audio_play_music(MUSIC_BATTLE);
audio_stop_music();
```

The `.uge` implementation should remain entirely inside `src/audio/`.

Target structure:

```text
src/
  audio/
    audio.c
    audio.h
    huge_music.c
    huge_music.h
    huge_music_data.c
    huge_music_data.h

assets/
  music/
    title.uge
    town.uge
    battle.uge
    dungeon.uge
    boss.uge
```

The `.uge` files are **build-time assets**, not runtime files.

---

### 2. Add hUGEDriver as a project dependency

Bring the required hUGEDriver source/library into the repository.

Prefer pinning a specific version/commit rather than depending on whatever happens to be installed on the developer's machine.

For example:

```text
lib/
  hUGEDriver/
    include/
    src/
```

The exact files should be determined from the version of hUGEDriver chosen.

**Deliverable:** hUGEDriver can be compiled by the project's existing GBDK/SDCC build.

---

### 3. Define the `.uge` asset pipeline

Establish a deterministic conversion:

```text
.uge
  │
  ▼
hUGETracker export/converter
  │
  ▼
C source/header
  │
  ▼
SDCC
  │
  ▼
Game Boy ROM
```

For each song:

```text
assets/music/town.uge
        ↓
generated/music/town.c
generated/music/town.h
```

Generated files should ideally **not be hand-edited**.

Add a Makefile target such as:

```text
make music
```

or have the normal build automatically regenerate the music data when the `.uge` changes.

---

### 4. Create a music-data abstraction

Introduce:

```c
// huge_music.h

void huge_music_init(void);
void huge_music_play(const hUGESong_t *song);
void huge_music_stop(void);
void huge_music_update(void);
```

This gives the rest of the project no dependency on hUGEDriver's implementation details.

Conceptually:

```text
audio.c
   │
   └── huge_music.c
          │
          └── hUGEDriver
```

This separation will make future changes much easier.

---

### 5. Integrate with the existing 256-Hz audio tick

This is the most important architectural step.

The repository already has a timer-driven audio update mechanism. **Do not move music timing into the main game loop.**

Instead:

```text
TIMA interrupt
      │
      ▼
 audio_update()
      │
      ├── huge_music_update()
      │
      └── SFX update
```

`huge_music_update()` should ultimately invoke the appropriate hUGEDriver tick.

This preserves music timing even if rendering/gameplay takes variable amounts of time.

---

### 6. Resolve the Game Boy APU channel allocation

Before converting the songs, decide exactly which channels hUGEDriver owns.

The project currently has SFX requirements, so this needs to be designed rather than left to chance.

Recommended initial allocation:

| APU channel | Owner      |
| ----------- | ---------- |
| CH1         | hUGE music |
| CH2         | hUGE music |
| CH3         | hUGE music |
| CH4         | SFX        |

This means the tracker compositions should be authored with the first three channels in mind.

If the songs require all four channels, implement a proper **music/SFX arbitration policy** instead of allowing both systems to write to the same registers unpredictably.

---

### 7. Adapt `audio_play_music()`

Keep the existing `MusicTrack` enum:

```c
typedef enum {
    MUSIC_NONE,
    MUSIC_OVERWORLD,
    MUSIC_BATTLE,
    MUSIC_VICTORY,
    MUSIC_TITLE,
    MUSIC_TOWN,
    MUSIC_DUNGEON,
    MUSIC_BOSS
} MusicTrack;
```

Create a lookup table:

```c
static const hUGESong_t *const music_songs[] = {
    [MUSIC_NONE]     = NULL,
    [MUSIC_OVERWORLD] = &overworld_song,
    [MUSIC_BATTLE]   = &battle_song,
    [MUSIC_VICTORY]  = &victory_song,
    [MUSIC_TITLE]    = &title_song,
    [MUSIC_TOWN]     = &town_song,
    [MUSIC_DUNGEON]  = &dungeon_song,
    [MUSIC_BOSS]     = &boss_song
};
```

Then:

```c
void audio_play_music(MusicTrack track)
{
    if (track == g_audio_current_track)
        return;

    huge_music_play(music_songs[track]);

    g_audio_current_track = track;
}
```

This means the rest of the game doesn't change.

---

### 8. Decide what happens to the existing note-array music

Don't delete the existing implementation immediately.

First create a build option:

```text
USE_HUGE_MUSIC=1
```

or equivalent.

Then initially allow:

```text
USE_HUGE_MUSIC=0
    ↓
existing music

USE_HUGE_MUSIC=1
    ↓
hUGEDriver music
```

This provides a fallback if something breaks during integration.

Once hUGE playback is stable, remove the old music implementation.

---

### 9. Preserve SFX behavior

Test all existing SFX independently:

* card selection
* menu movement
* attacks
* hits
* victory sounds
* other gameplay effects

The critical question is:

> Does starting/stopping/advancing hUGE music accidentally break an SFX?

If CH4 remains dedicated to SFX, this should be relatively straightforward.

If hUGEDriver touches CH4, modify the driver integration so that its register writes don't overwrite active SFX.

---

### 10. Implement music transitions

Define behavior for:

```text
MUSIC_NONE → MUSIC_TOWN
MUSIC_TOWN → MUSIC_BATTLE
MUSIC_BATTLE → MUSIC_VICTORY
MUSIC_VICTORY → MUSIC_TOWN
```

Initially use **hard transitions**:

```text
stop current song
start new song from pattern 0
```

Do not implement fades/crossfades yet.

That keeps the first implementation small and deterministic.

---

### 11. Handle pause/resume

Determine how the project currently behaves when the game is paused or the Game Boy enters a non-gameplay state.

The desired behavior should be explicitly defined:

```text
pause:
    music stops advancing

resume:
    music continues from current position
```

rather than accidentally restarting the song.

If hUGEDriver's state needs special handling, encapsulate that in `huge_music.c`.

---

### 12. Handle sound enable/disable

Preserve the existing sound setting.

Expected behavior:

```text
sound ON
    ↓
music + SFX

sound OFF
    ↓
no audible output
```

Decide whether disabling sound should:

**A.** stop playback completely, or
**B.** pause the music state while muting output.

I'd choose **B** if practical, because re-enabling sound then resumes naturally.

---

### 13. Memory/ROM-size audit

`.uge` songs can be substantially larger than the current hand-written note arrays.

Measure:

```text
ROM size before
ROM size after

RAM usage before
RAM usage after
```

Also check where song data ends up.

Ideally the large immutable song data remains in ROM rather than consuming scarce Game Boy RAM.

---

### 14. Build-system integration

Add explicit targets:

```text
make music
make clean
make
```

The normal build should produce:

```text
generated/music/*.c
```

before compiling the game.

Also make sure changing:

```text
assets/music/town.uge
```

causes `town.c` to regenerate.

---

### 15. Add automated validation

Extend the existing audio/music validation rather than relying only on listening tests.

At minimum test:

```text
✓ ROM builds
✓ MUSIC_NONE works
✓ every MusicTrack has a song
✓ changing tracks works
✓ same track isn't unnecessarily restarted
✓ timer continues updating music
✓ SFX still works
✓ sound-off works
✓ sound-on works
```

The existing `verify_music.py` should remain part of CI.

---

## Suggested implementation phases

### Phase 1 — Driver proof of concept

Use **one `.uge` song only**.

```text
town.uge
   ↓
hUGEDriver
   ↓
Game Boy
```

Don't touch all the existing tracks yet.

**Success criterion:** the song plays correctly on hardware/emulator.

---

### Phase 2 — Timer integration

Move the hUGE update into the project's existing audio timer path.

Test:

* music while rendering
* music during gameplay
* music while menus are active
* varying game-loop workload

**Success criterion:** music timing is independent of the main loop.

---

### Phase 3 — SFX coexistence

Integrate the existing SFX system.

Test all sound effects while music is playing.

**Success criterion:** music and SFX don't corrupt each other's channels.

---

### Phase 4 — Game API integration

Connect:

```c
MusicTrack
    ↓
audio_play_music()
    ↓
huge_music_play()
    ↓
hUGEDriver
```

At this point the game scenes shouldn't need any modifications.

---

### Phase 5 — Convert all music

Replace the old tracks one at a time:

```text
MUSIC_TITLE
MUSIC_TOWN
MUSIC_DUNGEON
MUSIC_BATTLE
MUSIC_BOSS
MUSIC_VICTORY
MUSIC_OVERWORLD
```

After each replacement:

```text
build
emulator test
SFX test
transition test
```

---

### Phase 6 — Remove legacy music

> **Done (Sep 2026).**  All hardcoded chiptune playback is gone:
> `s_note_freqs`, `play_note`, the `lacrimosa` (overworld), `victory`, and
> `title` note arrays and the `s_track_*` tables were deleted from
> `src/audio/audio.c`; `audio_update` only steps hUGEDriver now.  The title
> theme plays `title short.uge` (bank 6) and the battle-victory jingle plays
> `victory.uge` (bank 7); MUSIC_OVERWORLD is silent until an asset exists.
> `tools/compile_music.py` also patches the empty `waves[]` table some
> wave-less songs emit (SDCC error 286).
>
> Bank headroom is now the constraint (bank 6: 1032 B, bank 7: 1432 B).
> `title long.uge` (3005 B) does not fit either bank alongside the existing
> songs and victory; see the roadmap note on adding a third driver bank.

Once every track has been converted:

```text
remove old note arrays
remove old playback code
remove compatibility code
remove USE_HUGE_MUSIC switch
```

Keep the public `audio.h` API unchanged.

---

## Final architecture

The finished system should look roughly like this:

```text
                    GAME
                     │
                     ▼
              audio_play_music()
                     │
                     ▼
               ┌───────────┐
               │ audio.c   │
               └─────┬─────┘
                     │
             ┌───────▼────────┐
             │ huge_music.c   │
             └───────┬────────┘
                     │
              hUGEDriver API
                     │
                     ▼
             Game Boy APU
             ┌──┬──┬──┬──┐
             │1 │2 │3 │4 │
             └──┴──┴──┴──┘
              │  │  │  │
              │  │  │  └── SFX
              └──┴──┴───── hUGE music
```

And the asset pipeline:

```text
                 hUGETracker
                      │
                   song.uge
                      │
                      ▼
              music conversion
                      │
                      ▼
             generated/song.c
                      │
                      ▼
                   SDCC
                      │
                      ▼
                   ROM.gb
```

### Recommended order of work

If I were implementing this repository, I'd do it in exactly this order:

**1. Add hUGEDriver → 2. Convert one `.uge` → 3. Prove playback → 4. Hook it into the 256-Hz timer → 5. Solve CH1/2/3/4 ownership → 6. Integrate `MusicTrack` → 7. Convert remaining tracks → 8. Add automated tests → 9. Remove legacy music.**

The **channel ownership + timer integration are the two parts I'd prototype first**. Those are the places where simply following a generic hUGEDriver tutorial is most likely to conflict with this particular project's existing architecture.
